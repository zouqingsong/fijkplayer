// MIT License - FijkPlayer iOS Audio Renderer Implementation

#import "FJKAudioRenderer.h"
#import <AudioToolbox/AudioToolbox.h>

#define BUFFER_SIZE 8192  // Samples per channel

@interface FJKAudioRenderer () {
    AVAudioEngine *_audioEngine;
    AVAudioPlayerNode *_playerNode;
    AVAudioFormat *_audioFormat;
    
    int _sampleRate;
    int _channels;
    BOOL _isPlaying;
    
    // Ring buffer for PCM data
    int16_t *_ringBuffer;
    int _ringBufferCapacity;  // In samples per channel
    int _ringBufferReadPos;
    int _ringBufferWritePos;
    int _ringBufferAvailable;
    
    dispatch_queue_t _audioQueue;
    dispatch_semaphore_t _bufferSemaphore;
}

@end

@implementation FJKAudioRenderer

- (nullable instancetype)initWithSampleRate:(int)sampleRate channels:(int)channels {
    self = [super init];
    if (self) {
        _sampleRate = sampleRate;
        _channels = channels;
        _isPlaying = NO;
        
        // Ring buffer: 500ms capacity
        _ringBufferCapacity = (sampleRate / 2);  // 500ms
        _ringBuffer = (int16_t *)calloc(_ringBufferCapacity * channels, sizeof(int16_t));
        _ringBufferReadPos = 0;
        _ringBufferWritePos = 0;
        _ringBufferAvailable = 0;
        
        _audioQueue = dispatch_queue_create("com.befovy.fijk.audio", DISPATCH_QUEUE_SERIAL);
        _bufferSemaphore = dispatch_semaphore_create(0);
        
        // Create AVAudioEngine
        _audioEngine = [[AVAudioEngine alloc] init];
        _playerNode = [[AVAudioPlayerNode alloc] init];
        
        // Create audio format (PCM Float32, non-interleaved)
        _audioFormat = [[AVAudioFormat alloc] initStandardFormatWithSampleRate:sampleRate
                                                                       channels:channels];
        
        if (!_audioFormat) {
            NSLog(@"[AudioRenderer] Failed to create audio format");
            return nil;
        }
        
        // Attach player node
        [_audioEngine attachNode:_playerNode];
        
        // Connect player node to main mixer
        [_audioEngine connect:_playerNode
                           to:_audioEngine.mainMixerNode
                       format:_audioFormat];
        
        NSLog(@"[AudioRenderer] Initialized: %d Hz, %d channels", sampleRate, channels);
    }
    return self;
}

- (void)dealloc {
    [self stop];
    free(_ringBuffer);
}

- (BOOL)start {
    if (_isPlaying) {
        return YES;
    }
    
    NSError *error = nil;
    
    // Start audio engine
    if (![_audioEngine startAndReturnError:&error]) {
        NSLog(@"[AudioRenderer] Failed to start audio engine: %@", error);
        return NO;
    }
    
    // Start player node
    [_playerNode play];
    _isPlaying = YES;
    
    // Start feeding audio in background
    [self scheduleAudioBuffers];
    
    NSLog(@"[AudioRenderer] Started playback");
    return YES;
}

- (void)stop {
    if (!_isPlaying) {
        return;
    }
    
    _isPlaying = NO;
    [_playerNode stop];
    [_audioEngine stop];
    
    // Clear ring buffer
    _ringBufferReadPos = 0;
    _ringBufferWritePos = 0;
    _ringBufferAvailable = 0;
    
    NSLog(@"[AudioRenderer] Stopped playback");
}

- (void)pause {
    if (_isPlaying) {
        [_playerNode pause];
        NSLog(@"[AudioRenderer] Paused");
    }
}

- (void)resume {
    if (_isPlaying) {
        [_playerNode play];
        NSLog(@"[AudioRenderer] Resumed");
    }
}

- (int)writeSamples:(const int16_t *)samples count:(int)sampleCount {
    if (!samples || sampleCount <= 0) {
        return -1;
    }
    
    @synchronized(self) {
        // Check if ring buffer has space
        int space = _ringBufferCapacity - _ringBufferAvailable;
        if (space < sampleCount) {
            // Ring buffer full - drop samples or wait
            return 0;
        }
        
        // Write interleaved S16 samples to ring buffer
        for (int i = 0; i < sampleCount * _channels; i++) {
            _ringBuffer[_ringBufferWritePos * _channels + (i % _channels)] = samples[i];
            if (i % _channels == _channels - 1) {
                _ringBufferWritePos = (_ringBufferWritePos + 1) % _ringBufferCapacity;
            }
        }
        
        _ringBufferAvailable += sampleCount;
        
        // Signal that data is available
        dispatch_semaphore_signal(_bufferSemaphore);
    }
    
    return sampleCount;
}

- (void)scheduleAudioBuffers {
    dispatch_async(_audioQueue, ^{
        while (self->_isPlaying) {
            // Wait for data or timeout
            dispatch_time_t timeout = dispatch_time(DISPATCH_TIME_NOW, 50 * NSEC_PER_MSEC);
            dispatch_semaphore_wait(self->_bufferSemaphore, timeout);
            
            @synchronized(self) {
                if (self->_ringBufferAvailable == 0) {
                    continue;
                }
                
                // Read from ring buffer
                int samplesToRead = MIN(BUFFER_SIZE, self->_ringBufferAvailable);
                int16_t *s16Buffer = (int16_t *)malloc(samplesToRead * self->_channels * sizeof(int16_t));
                
                for (int i = 0; i < samplesToRead * self->_channels; i++) {
                    s16Buffer[i] = self->_ringBuffer[self->_ringBufferReadPos * self->_channels + (i % self->_channels)];
                    if (i % self->_channels == self->_channels - 1) {
                        self->_ringBufferReadPos = (self->_ringBufferReadPos + 1) % self->_ringBufferCapacity;
                    }
                }
                
                self->_ringBufferAvailable -= samplesToRead;
                
                // Convert S16 to Float32 for AVAudioPCMBuffer
                AVAudioPCMBuffer *pcmBuffer = [[AVAudioPCMBuffer alloc] initWithPCMFormat:self->_audioFormat
                                                                            frameCapacity:samplesToRead];
                pcmBuffer.frameLength = samplesToRead;
                
                static int buffer_count = 0;
                buffer_count++;
                
                float *const *channelData = pcmBuffer.floatChannelData;
                int16_t max_input = 0, max_boosted = 0;
                float max_output = 0;
                
                for (int ch = 0; ch < self->_channels; ch++) {
                    for (int i = 0; i < samplesToRead; i++) {
                        int16_t input = s16Buffer[i * self->_channels + ch];
                        
                        // Track input amplitude (first 3 buffers only)
                        if (buffer_count <= 3) {
                            int16_t abs_input = abs(input);
                            if (abs_input > max_input) max_input = abs_input;
                        }
                        
                        // Convert S16 (-32768 to 32767) to Float32 (-1.0 to 1.0)
                        // Normal conversion without boost to match Android natural sound
                        float output = (float)input / 32768.0f;
                        channelData[ch][i] = output;
                        
                        // Track output amplitude
                        if (buffer_count <= 3) {
                            float abs_output = fabsf(output);
                            if (abs_output > max_output) max_output = abs_output;
                        }
                    }
                }
                
                if (buffer_count <= 3) {
                    NSLog(@"[AudioRenderer] 🔊 Buffer #%d: samples=%d, input_max=%d, output_max=%.3f",
                          buffer_count, (int)samplesToRead, max_input, max_output);
                }
                
                free(s16Buffer);
                
                // Schedule buffer for playback
                [self->_playerNode scheduleBuffer:pcmBuffer completionHandler:nil];
            }
        }
    });
}

- (void)setVolume:(float)volume {
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    
    _playerNode.volume = volume;
    
#if TARGET_OS_IOS
    // Manage audio session based on volume (iOS only)
    if (volume == 0.0f) {
        // When muted, deactivate audio session to allow other apps to play
        [[AVAudioSession sharedInstance] setActive:NO withOptions:AVAudioSessionSetActiveOptionNotifyOthersOnDeactivation error:nil];
        NSLog(@"[AudioRenderer] Volume muted, audio session deactivated");
    } else if (_isPlaying) {
        // When unmuted and playing, reactivate audio session
        NSError *error = nil;
        [[AVAudioSession sharedInstance] setCategory:AVAudioSessionCategoryPlayback error:&error];
        [[AVAudioSession sharedInstance] setActive:YES error:&error];
        if (error) {
            NSLog(@"[AudioRenderer] Failed to activate audio session: %@", error);
        } else {
            NSLog(@"[AudioRenderer] Volume set to: %.2f, audio session activated", volume);
        }
    }
#else
    NSLog(@"[AudioRenderer] Volume set to: %.2f", volume);
#endif
}

- (BOOL)isPlaying {
    return _isPlaying && _playerNode.isPlaying;
}

@end
