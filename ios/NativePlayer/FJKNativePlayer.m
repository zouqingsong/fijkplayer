// MIT License - FijkPlayer Native iOS Implementation

#import "FJKNativePlayer.h"
#import "FJKPixelBufferRenderer.h"
#import "FJKAudioRenderer.h"
#import <VideoToolbox/VideoToolbox.h>
#include "ffmpeg_demuxer.h"
#include "FJKAudioDecoder.h"
#include "FJKAudioQueue.h"

@interface FJKNativePlayer () {
    // Native C components
    FFDemuxer *_demuxer;
    VTDecompressionSessionRef _decompressionSession;
    CMFormatDescriptionRef _formatDescription;
    FJKPixelBufferRenderer *_renderer;
    
    // Audio components (Phase 3)
    FJKAudioDecoder *_audioDecoder;
    FJKAudioQueue *_audioQueue;
    FJKAudioRenderer *_audioRenderer;
    dispatch_queue_t _audioDecoderQueue;
    dispatch_queue_t _audioPlaybackQueue;
    int _audioStreamIndex;
    double _audioClock;
    NSLock *_audioClockLock;
    
    // State
    FJKPlayerState _state;
    NSString *_dataSource;
    CVPixelBufferRef _pixelBuffer;
    
    // Threading
    dispatch_queue_t _demuxerQueue;
    dispatch_queue_t _decoderQueue;
    
    // Video info
    CGSize _videoSize;
    int64_t _duration;
    int64_t _currentPosition;
    BOOL _isPlaying;
    
    // Frame timing
    CFTimeInterval _lastFrameTime;
    CFTimeInterval _targetFrameInterval; // 1/framerate
    BOOL _isLiveStream; // Live streams have no frame timing
    double _videoFrameRate; // Actual frame rate from stream
    int64_t _lastVideoPts;
    
    // Synchronization
    NSLock *_stateLock;
}

@end

@implementation FJKNativePlayer

#pragma mark - Lifecycle

- (instancetype)init {
    self = [super init];
    if (self) {
        _state = FJKPlayerStateIdle;
        _stateLock = [[NSLock alloc] init];
        _audioClockLock = [[NSLock alloc] init];
        _demuxerQueue = dispatch_queue_create("com.befovy.fijk.demuxer", DISPATCH_QUEUE_SERIAL);
        _decoderQueue = dispatch_queue_create("com.befovy.fijk.decoder", DISPATCH_QUEUE_SERIAL);
        _audioDecoderQueue = dispatch_queue_create("com.befovy.fijk.audio.decoder", DISPATCH_QUEUE_SERIAL);
        _audioPlaybackQueue = dispatch_queue_create("com.befovy.fijk.audio.playback", DISPATCH_QUEUE_SERIAL);
        _isLiveStream = NO;
        _videoFrameRate = 24.0; // Default fallback
        _targetFrameInterval = 1.0/24.0;
        _audioStreamIndex = -1;
        _audioClock = 0.0;
        _lastVideoPts = -1;
        
        NSLog(@"[FJKNativePlayer] Initialized");
    }
    return self;
}

- (void)dealloc {
    [self cleanup];
}

#pragma mark - Properties

- (FJKPlayerState)state {
    [_stateLock lock];
    FJKPlayerState state = _state;
    [_stateLock unlock];
    return state;
}

- (CGSize)videoSize {
    return _videoSize;
}

- (int64_t)duration {
    return _duration;
}

- (int64_t)currentPosition {
    return _currentPosition;
}

- (BOOL)isPlaying {
    return _isPlaying;
}

#pragma mark - Public API

- (int)setDataSource:(NSString *)url {
    if (!url || url.length == 0) {
        NSLog(@"[FJKNativePlayer] Invalid URL");
        return -1;
    }
    
    [_stateLock lock];
    if (_state != FJKPlayerStateIdle) {
        NSLog(@"[FJKNativePlayer] Cannot set data source in state %ld", (long)_state);
        [_stateLock unlock];
        return -1;
    }
    
    _dataSource = [url copy];
    _state = FJKPlayerStateInitialized;
    [_stateLock unlock];
    
    NSLog(@"[FJKNativePlayer] Data source set: %@", url);
    return 0;
}

- (void)setPixelBuffer:(CVPixelBufferRef)pixelBuffer {
    if (_pixelBuffer) {
        CVPixelBufferRelease(_pixelBuffer);
    }
    _pixelBuffer = CVPixelBufferRetain(pixelBuffer);
}

- (void)prepareAsync {
    dispatch_async(_demuxerQueue, ^{
        [self doPrepare];
    });
}

- (int)start {
    [_stateLock lock];
    if (_state != FJKPlayerStatePrepared && _state != FJKPlayerStatePaused) {
        NSLog(@"[FJKNativePlayer] Cannot start in state %ld", (long)_state);
        [_stateLock unlock];
        return -1;
    }
    
    _state = FJKPlayerStatePlaying;
    _isPlaying = YES;
    
    // Reset frame timing
    _lastFrameTime = 0;
    _targetFrameInterval = 1.0 / 24.0; // Assume 24fps for now
    
    [_stateLock unlock];
    
    // Start decoder thread
    dispatch_async(_decoderQueue, ^{
        [self runDecoderLoop];
    });
    
    // Phase 3: Start audio playback if audio is available
    if (_audioStreamIndex >= 0 && _audioDecoder && _audioQueue && _audioRenderer) {
        // Start audio renderer
        [_audioRenderer start];
        
        // Start audio playback thread (decoder is inline in main demuxer loop)
        dispatch_async(_audioPlaybackQueue, ^{
            [self runAudioPlaybackLoop];
        });
        
        NSLog(@"[FJKNativePlayer] 🔊 Audio playback started");
    }
    
    [self notifyEvent:FJKPlayerEventStarted arg1:_videoSize.width arg2:_videoSize.height];
    NSLog(@"[FJKNativePlayer] Playback started");
    return 0;
}

- (void)pause {
    [_stateLock lock];
    if (_state == FJKPlayerStatePlaying) {
        _state = FJKPlayerStatePaused;
        _isPlaying = NO;
        
        // Pause audio
        if (_audioRenderer) {
            [_audioRenderer pause];
        }
        
        [self notifyEvent:FJKPlayerEventPaused arg1:0 arg2:0];
    }
    [_stateLock unlock];
}

- (void)resume {
    [_stateLock lock];
    if (_state == FJKPlayerStatePaused) {
        _state = FJKPlayerStatePlaying;
        _isPlaying = YES;
        
        // Resume audio
        if (_audioRenderer) {
            [_audioRenderer resume];
        }
        
        [self notifyEvent:FJKPlayerEventStarted arg1:0 arg2:0];
    }
    [_stateLock unlock];
}

- (void)stop {
    [_stateLock lock];
    _state = FJKPlayerStateStopped;
    _isPlaying = NO;
    [_stateLock unlock];
}

- (void)seekTo:(int64_t)positionMs {
    // TODO: Implement seeking
    NSLog(@"[FJKNativePlayer] Seek to %lld ms (not implemented)", positionMs);
}

- (BOOL)isLiveStream:(NSString *)url {
    if (!url) return NO;
    
    // Check for live streaming protocols
    if ([url hasPrefix:@"rtsp://"] || 
        [url hasPrefix:@"rtmp://"] ||
        [url hasPrefix:@"rtp://"]) {
        return YES;
    }
    
    // Check for live streaming formats in HTTP URLs
    if ([url hasPrefix:@"http://"] || [url hasPrefix:@"https://"]) {
        if ([url containsString:@".m3u8"] || // HLS
            [url containsString:@"/live/"] ||
            [url containsString:@"live="]) {
            return YES;
        }
    }
    
    return NO;
}

- (void)cleanup {
    NSLog(@"[FJKNativePlayer] Releasing resources");
    
    [self stop];
    
    if (_decompressionSession) {
        VTDecompressionSessionInvalidate(_decompressionSession);
        CFRelease(_decompressionSession);
        _decompressionSession = NULL;
    }
    
    if (_formatDescription) {
        CFRelease(_formatDescription);
        _formatDescription = NULL;
    }
    
    if (_renderer) {
        [_renderer cleanup];
        _renderer = nil;
    }
    
    if (_demuxer) {
        ff_demuxer_close(_demuxer);
        // Note: demuxer is freed in ff_demuxer_close
        _demuxer = NULL;
    }
    
    if (_pixelBuffer) {
        CVPixelBufferRelease(_pixelBuffer);
        _pixelBuffer = NULL;
    }
    
    // Phase 3: Cleanup audio resources
    if (_audioRenderer) {
        [_audioRenderer stop];
        _audioRenderer = nil;
    }
    
    if (_audioQueue) {
        fjk_audio_queue_destroy(_audioQueue);
        _audioQueue = NULL;
    }
    
    if (_audioDecoder) {
        fjk_audio_decoder_destroy(_audioDecoder);
        _audioDecoder = NULL;
    }
    
    _audioStreamIndex = -1;
    
    [_stateLock lock];
    _state = FJKPlayerStateIdle;
    [_stateLock unlock];
}

#pragma mark - Private Implementation

- (void)doPrepare {
    NSLog(@"[FJKNativePlayer] Preparing...");
    
    // Create FFmpeg demuxer
    _demuxer = ff_demuxer_create();
    if (!_demuxer) {
        [self notifyError:@"Failed to create demuxer"];
        return;
    }
    
    // Open media file
    const char *url = [_dataSource UTF8String];
    int ret = ff_demuxer_open(_demuxer, url, NULL);
    if (ret < 0) {
        [self notifyError:[NSString stringWithFormat:@"Failed to open: %@", _dataSource]];
        return;
    }
    
    // Find video stream
    int videoStreamIndex = ff_demuxer_find_video_stream(_demuxer);
    if (videoStreamIndex < 0) {
        [self notifyError:@"No video stream found"];
        return;
    }
    
    // Get video stream
    FFStream *videoStream = ff_demuxer_get_stream(_demuxer, videoStreamIndex);
    if (!videoStream) {
        [self notifyError:@"Failed to get video stream"];
        return;
    }
    
    _videoSize = CGSizeMake(videoStream->width, videoStream->height);
    _duration = ff_demuxer_get_duration(_demuxer) / 1000; // Convert µs to ms
    
    // Detect live stream and frame rate
    _isLiveStream = [self isLiveStream:_dataSource] || (_duration == 0);
    
    // Get actual frame rate from stream
    if (videoStream->fps_den > 0 && videoStream->fps_num > 0) {
        _videoFrameRate = (double)videoStream->fps_num / (double)videoStream->fps_den;
        _targetFrameInterval = 1.0 / _videoFrameRate;
    } else {
        _videoFrameRate = 25.0; // Default for unknown frame rate
        _targetFrameInterval = 1.0 / 25.0;
    }
    
    NSLog(@"[FJKNativePlayer] Stream info: %dx%d, fps=%.2f, live=%@, duration=%lld ms", 
          (int)_videoSize.width, (int)_videoSize.height, _videoFrameRate, 
          _isLiveStream ? @"YES" : @"NO", _duration);
    
    // Create VideoToolbox decoder
    ret = [self createVideoToolboxDecoder:videoStream];
    if (ret < 0) {
        [self notifyError:@"Failed to create VideoToolbox decoder"];
        return;
    }
    
    // Create renderer
    _renderer = [[FJKPixelBufferRenderer alloc] initWithWidth:videoStream->width height:videoStream->height];
    if (!_renderer) {
        [self notifyError:@"Failed to create renderer"];
        return;
    }
    
    // Phase 3: Initialize audio (find audio stream)
    _audioStreamIndex = ff_demuxer_find_audio_stream(_demuxer);
    if (_audioStreamIndex >= 0) {
        FFStream *audioStream = ff_demuxer_get_stream(_demuxer, _audioStreamIndex);
        if (audioStream && audioStream->type == FF_STREAM_TYPE_AUDIO) {
            NSLog(@"[FJKNativePlayer] 🔊 Found audio stream: %d Hz, %d channels, codec=%d",
                  audioStream->sample_rate, audioStream->channels, audioStream->audio_codec);
            
            // Create audio decoder
            _audioDecoder = fjk_audio_decoder_create(audioStream);
            if (!_audioDecoder) {
                NSLog(@"[FJKNativePlayer] ⚠️ Failed to create audio decoder");
                _audioStreamIndex = -1;
            } else {
                // Create audio queue (200ms buffer for fast audio start)
                FJKAudioQueueConfig queueConfig = {
                    .sample_rate = audioStream->sample_rate,
                    .channels = audioStream->channels,
                    .format = FJK_AUDIO_QUEUE_FORMAT_S16,
                    .capacity_ms = 200
                };
                _audioQueue = fjk_audio_queue_create(&queueConfig);
                
                if (!_audioQueue) {
                    NSLog(@"[FJKNativePlayer] ⚠️ Failed to create audio queue");
                    fjk_audio_decoder_destroy(_audioDecoder);
                    _audioDecoder = NULL;
                    _audioStreamIndex = -1;
                } else {
                    // Create audio renderer
                    _audioRenderer = [[FJKAudioRenderer alloc] initWithSampleRate:audioStream->sample_rate
                                                                          channels:audioStream->channels];
                    if (!_audioRenderer) {
                        NSLog(@"[FJKNativePlayer] ⚠️ Failed to create audio renderer");
                        fjk_audio_queue_destroy(_audioQueue);
                        fjk_audio_decoder_destroy(_audioDecoder);
                        _audioQueue = NULL;
                        _audioDecoder = NULL;
                        _audioStreamIndex = -1;
                    } else {
                        [_audioRenderer setVolume:1.0];  // Maximum volume
                        NSLog(@"[FJKNativePlayer] ✅ Audio pipeline ready");
                    }
                }
            }
        }
    } else {
        NSLog(@"[FJKNativePlayer] ℹ️ No audio stream found");
    }
    
    [_stateLock lock];
    _state = FJKPlayerStatePrepared;
    [_stateLock unlock];
    
    [self notifyEvent:FJKPlayerEventPrepared arg1:0 arg2:0];
    [self notifyEvent:FJKPlayerEventVideoSizeChanged arg1:_videoSize.width arg2:_videoSize.height];
    
    NSLog(@"[FJKNativePlayer] Prepared: %dx%d, duration=%lld ms, audio=%@",
          (int)_videoSize.width, (int)_videoSize.height, _duration,
          _audioStreamIndex >= 0 ? @"YES" : @"NO");
}

- (int)createVideoToolboxDecoder:(FFStream *)stream {
    // Create format description from extradata (SPS/PPS)
    CMFormatDescriptionRef formatDesc = NULL;
    
    if (stream->extradata && stream->extradata_size > 0) {
        // Parse H.264 extradata (avcC format)
        // avcC format: [configurationVersion(1)][AVCProfileIndication(1)][profile_compatibility(1)][AVCLevelIndication(1)]
        //              [lengthSizeMinusOne(1)][numOfSequenceParameterSets(1)][SPS...]
        //              [numOfPictureParameterSets(1)][PPS...]
        
        const uint8_t *extradata = stream->extradata;
        int extradata_size = stream->extradata_size;
        
        if (extradata_size < 7) {
            NSLog(@"[FJKNativePlayer] Extradata too small: %d bytes", extradata_size);
            return -1;
        }
        
        // Get NAL unit length size from avcC header
        int lengthSizeMinusOne = extradata[4] & 0x03;
        int nalUnitLengthSize = lengthSizeMinusOne + 1;
        
        NSLog(@"[FJKNativePlayer] avcC NAL unit length size: %d bytes", nalUnitLengthSize);
        
        // Skip first 5 bytes (version, profile, compatibility, level, length_size)
        int offset = 5;
        
        // Number of SPS
        int num_sps = extradata[offset++] & 0x1F;
        NSMutableArray *spsArray = [NSMutableArray array];
        NSMutableArray *spsSizes = [NSMutableArray array];
        
        for (int i = 0; i < num_sps; i++) {
            if (offset + 2 > extradata_size) {
                NSLog(@"[FJKNativePlayer] Invalid SPS size offset");
                return -1;
            }
            
            uint16_t sps_size = (extradata[offset] << 8) | extradata[offset + 1];
            offset += 2;
            
            if (offset + sps_size > extradata_size) {
                NSLog(@"[FJKNativePlayer] Invalid SPS data");
                return -1;
            }
            
            [spsArray addObject:[NSData dataWithBytes:&extradata[offset] length:sps_size]];
            [spsSizes addObject:@(sps_size)];
            offset += sps_size;
        }
        
        // Number of PPS
        if (offset >= extradata_size) {
            NSLog(@"[FJKNativePlayer] No PPS in extradata");
            return -1;
        }
        
        int num_pps = extradata[offset++];
        NSMutableArray *ppsArray = [NSMutableArray array];
        NSMutableArray *ppsSizes = [NSMutableArray array];
        
        for (int i = 0; i < num_pps; i++) {
            if (offset + 2 > extradata_size) {
                NSLog(@"[FJKNativePlayer] Invalid PPS size offset");
                return -1;
            }
            
            uint16_t pps_size = (extradata[offset] << 8) | extradata[offset + 1];
            offset += 2;
            
            if (offset + pps_size > extradata_size) {
                NSLog(@"[FJKNativePlayer] Invalid PPS data");
                return -1;
            }
            
            [ppsArray addObject:[NSData dataWithBytes:&extradata[offset] length:pps_size]];
            [ppsSizes addObject:@(pps_size)];
            offset += pps_size;
        }
        
        if (spsArray.count == 0 || ppsArray.count == 0) {
            NSLog(@"[FJKNativePlayer] No SPS or PPS found in extradata");
            return -1;
        }
        
        NSLog(@"[FJKNativePlayer] Parsed avcC: %d SPS, %d PPS", (int)spsArray.count, (int)ppsArray.count);
        
        // Create parameter set pointers arrays
        const uint8_t *parameterSetPointers[spsArray.count + ppsArray.count];
        size_t parameterSetSizes[spsArray.count + ppsArray.count];
        
        // Add SPS
        for (int i = 0; i < spsArray.count; i++) {
            NSData *sps = spsArray[i];
            parameterSetPointers[i] = sps.bytes;
            parameterSetSizes[i] = sps.length;
        }
        
        // Add PPS
        for (int i = 0; i < ppsArray.count; i++) {
            NSData *pps = ppsArray[i];
            parameterSetPointers[spsArray.count + i] = pps.bytes;
            parameterSetSizes[spsArray.count + i] = pps.length;
        }
        
        OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
            kCFAllocatorDefault,
            (size_t)(spsArray.count + ppsArray.count),
            parameterSetPointers,
            parameterSetSizes,
            nalUnitLengthSize, // Use actual NAL unit length from avcC
            &formatDesc
        );
        
        if (status != noErr) {
            NSLog(@"[FJKNativePlayer] Failed to create format description: %d", (int)status);
            return -1;
        }
    } else {
        NSLog(@"[FJKNativePlayer] No extradata for VideoToolbox decoder");
        return -1;
    }
    
    // Create decompression session
    NSDictionary *destinationPixelBufferAttributes = @{
        (id)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange),
        (id)kCVPixelBufferWidthKey: @(stream->width),
        (id)kCVPixelBufferHeightKey: @(stream->height),
        (id)kCVPixelBufferOpenGLCompatibilityKey: @YES,
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
    
    VTDecompressionOutputCallbackRecord callback = {
        .decompressionOutputCallback = decompressionOutputCallback,
        .decompressionOutputRefCon = (__bridge void *)self
    };
    
    OSStatus status = VTDecompressionSessionCreate(
        kCFAllocatorDefault,
        formatDesc,
        NULL, // decoder specification
        (__bridge CFDictionaryRef)destinationPixelBufferAttributes,
        &callback,
        &_decompressionSession
    );
    
    if (status != noErr) {
        CFRelease(formatDesc);
        NSLog(@"[FJKNativePlayer] Failed to create decompression session: %d", (int)status);
        return -1;
    }
    
    // Store format description for CMSampleBuffer creation
    _formatDescription = formatDesc;
    
    NSLog(@"[FJKNativePlayer] VideoToolbox decoder created: %dx%d", stream->width, stream->height);
    return 0;
}

// VideoToolbox callback
static void decompressionOutputCallback(
    void *decompressionOutputRefCon,
    void *sourceFrameRefCon,
    OSStatus status,
    VTDecodeInfoFlags infoFlags,
    CVImageBufferRef imageBuffer,
    CMTime presentationTimeStamp,
    CMTime presentationDuration)
{
    static int frameCount = 0;
    if (frameCount++ < 3) {
        NSLog(@"[FJKNativePlayer] 🎬 Decode callback #%d: status=%d, imageBuffer=%p", frameCount, (int)status, imageBuffer);
    }
    
    if (status != noErr) {
        NSLog(@"[FJKNativePlayer] Decode error: %d", (int)status);
        return;
    }
    
    FJKNativePlayer *player = (__bridge FJKNativePlayer *)decompressionOutputRefCon;
    [player handleDecodedFrame:imageBuffer pts:presentationTimeStamp];
}

- (void)handleDecodedFrame:(CVImageBufferRef)imageBuffer pts:(CMTime)pts {
    if (!imageBuffer || !_renderer) {
        return;
    }
    
    // Simple frame rate limiting
    CFTimeInterval currentTime = CACurrentMediaTime();
    if (_lastFrameTime > 0) {
        CFTimeInterval elapsed = currentTime - _lastFrameTime;
        if (elapsed < _targetFrameInterval) {
            // Skip this frame - we're going too fast
            return;
        }
    }
    
    _lastFrameTime = currentTime;
    [self displayFrame:imageBuffer pts:pts];
}

- (void)displayFrame:(CVImageBufferRef)imageBuffer pts:(CMTime)pts {
    // Render frame to output buffer
    BOOL success = [_renderer renderFrame:imageBuffer];
    if (success) {
        _currentPosition = CMTimeGetSeconds(pts) * 1000;
        
        // Notify that a new frame is available
        if (self.frameCallback) {
            self.frameCallback();
        }
    }
}

- (CVPixelBufferRef)copyPixelBuffer {
    if (!_renderer) {
        return NULL;
    }
    
    // Get the renderer's output buffer
    CVPixelBufferRef buffer = [_renderer getOutputBuffer];
    if (buffer) {
        // Retain for caller - they must release
        CVPixelBufferRetain(buffer);
    }
    return buffer;
}

- (void)runDecoderLoop {
    NSLog(@"[FJKNativePlayer] Decoder loop started");
    
    while (_isPlaying) {
        @autoreleasepool {
            // Read packet
            FFPacket *packet = NULL;
            int ret = ff_demuxer_read_packet(_demuxer, &packet); // Returns 0 on success
            
            if (ret == 0 && packet) {
                int video_stream_index = ff_demuxer_find_video_stream(_demuxer);
                
                // Route packet to appropriate decoder
                if (packet->stream_index == video_stream_index) {
                    // Video packet - decode with VideoToolbox
                    [self decodePacket:packet];
                    
                    // Adaptive timing based on stream type
                    if (_isLiveStream) {
                        // Live streams: minimal delay for lowest latency
                        usleep(1000); // 1ms only
                    } else {
                        // File playback: respect actual frame rate
                        usleep((useconds_t)(_targetFrameInterval * 1000000)); // Convert to microseconds
                    }
                } else if (_audioStreamIndex >= 0 && packet->stream_index == _audioStreamIndex) {
                    // Audio packet - send to audio decoder asynchronously
                    dispatch_async(_audioDecoderQueue, ^{
                        [self processAudioPacket:packet];
                    });
                    // Don't free packet here - audio decoder will free it
                    packet = NULL;
                }
                
                if (packet) {
                    ff_packet_free(packet);
                }
            } else if (ret == -EAGAIN) {
                // No packet available, wait a bit
                usleep(1000);
            } else if (ret < 0) {
                NSLog(@"[FJKNativePlayer] Read error: %d", ret);
                break;
            }
        }
    }
    
    NSLog(@"[FJKNativePlayer] Decoder loop exited");
}

#pragma mark - Audio Decoder & Playback (Phase 3)

- (void)processAudioPacket:(FFPacket *)packet {
    if (!packet || !_audioDecoder || !_audioQueue) {
        if (packet) ff_packet_free(packet);
        return;
    }
    
    static int packet_count = 0;
    packet_count++;
    
    // Send packet to audio decoder
    int ret = fjk_audio_decoder_send_packet(_audioDecoder, packet);
    if (ret < 0) {
        NSLog(@"[AudioDecoder] Failed to send packet: %d", ret);
        ff_packet_free(packet);
        return;
    }
    
    // Decode to PCM samples
    int16_t pcm_buffer[4096 * 2];  // 4096 samples stereo
    int64_t pts;
    int samples = fjk_audio_decoder_receive_samples(_audioDecoder, pcm_buffer, 4096, &pts);
    
    if (samples > 0) {
        // Log first few successful decodes with amplitude check
        if (packet_count <= 5) {
            int16_t max_amp = 0;
            for (int i = 0; i < samples * 2; i++) {
                int16_t abs_val = abs(pcm_buffer[i]);
                if (abs_val > max_amp) max_amp = abs_val;
            }
            NSLog(@"[AudioDecoder] ✅ Packet #%d decoded: %d samples, max_amp=%d", 
                  packet_count, samples, max_amp);
        }
        
        // Push to audio queue (blocks if full)
        fjk_audio_queue_push(_audioQueue, pcm_buffer, samples, pts);
    } else if (samples < 0) {
        if (packet_count <= 10) {
            NSLog(@"[AudioDecoder] ❌ Packet #%d failed to decode: %d", packet_count, samples);
        }
    }
    
    ff_packet_free(packet);
}

- (void)runAudioPlaybackLoop {
    NSLog(@"[FJKNativePlayer] 🔊 Audio playback loop starting");
    
    int playback_count = 0;
    int16_t pcm_buffer[2048 * 2];  // 2048 samples stereo (~46ms at 44.1kHz)
    
    while (_isPlaying && _audioStreamIndex >= 0) {
        // Pop samples from queue (non-blocking)
        int64_t pts;
        int samples = fjk_audio_queue_pop(_audioQueue, pcm_buffer, 2048, &pts);
        
        if (samples > 0) {
            playback_count++;
            
            // Update audio clock for A/V sync
            if (pts > 0) {
                double audio_pts = (double)pts / 1000000.0;  // Convert to seconds
                [_audioClockLock lock];
                _audioClock = audio_pts;
                [_audioClockLock unlock];
            }
            
            // Write to audio renderer
            [_audioRenderer writeSamples:pcm_buffer count:samples];
            
            // Log first few chunks
            if (playback_count <= 3) {
                int16_t max_amplitude = 0;
                for (int i = 0; i < samples * 2; i++) {
                    int16_t abs_sample = abs(pcm_buffer[i]);
                    if (abs_sample > max_amplitude) max_amplitude = abs_sample;
                }
                NSLog(@"[FJKNativePlayer] 🔊 Played chunk #%d: samples=%d, max_amplitude=%d",
                      playback_count, samples, max_amplitude);
            }
        } else {
            // Queue empty, wait a bit
            usleep(5000);  // 5ms
        }
    }
    
    NSLog(@"[FJKNativePlayer] 🔊 Audio playback loop exited");
}

- (void)decodePacket:(FFPacket *)packet {
    // Create CMBlockBuffer from packet data
    CMBlockBufferRef blockBuffer = NULL;
    OSStatus status = CMBlockBufferCreateWithMemoryBlock(
        kCFAllocatorDefault,
        packet->data,
        packet->size,
        kCFAllocatorNull,
        NULL, 0, packet->size, 0,
        &blockBuffer
    );
    
    if (status != noErr) {
        return;
    }
    
    // Create CMSampleBuffer
    CMSampleBufferRef sampleBuffer = NULL;
    CMSampleTimingInfo timing = {
        .duration = kCMTimeInvalid,
        .presentationTimeStamp = CMTimeMake(packet->pts, 1000000), // microseconds
        .decodeTimeStamp = CMTimeMake(packet->dts, 1000000)
    };
    
    status = CMSampleBufferCreate(
        kCFAllocatorDefault,
        blockBuffer,
        TRUE,
        NULL, NULL,
        _formatDescription,
        1, 1, &timing,
        0, NULL,
        &sampleBuffer
    );
    
    if (status == noErr && sampleBuffer) {
        // Decode with VideoToolbox
        VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
        VTDecodeInfoFlags infoFlags;
        
        static int decodeCount = 0;
        if (decodeCount++ < 3) {
            NSLog(@"[FJKNativePlayer] 📤 Submitting frame #%d to VideoToolbox", decodeCount);
        }
        
        status = VTDecompressionSessionDecodeFrame(
            _decompressionSession,
            sampleBuffer,
            flags,
            NULL, // source frame ref con
            &infoFlags
        );
        
        if (decodeCount <= 3) {
            NSLog(@"[FJKNativePlayer] 📥 Decode status: %d, infoFlags: 0x%x", (int)status, (unsigned int)infoFlags);
        }
        
        CFRelease(sampleBuffer);
    }
    
    if (blockBuffer) {
        CFRelease(blockBuffer);
    }
}

- (void)notifyEvent:(FJKPlayerEvent)event arg1:(NSInteger)arg1 arg2:(NSInteger)arg2 {
    if (self.eventCallback) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.eventCallback(event, arg1, arg2);
        });
    }
}

- (void)notifyError:(NSString *)message {
    NSLog(@"[FJKNativePlayer] Error: %@", message);
    
    [_stateLock lock];
    _state = FJKPlayerStateError;
    [_stateLock unlock];
    
    [self notifyEvent:FJKPlayerEventError arg1:-1 arg2:0];
}

@end
