// MIT License - FijkPlayer Native iOS Implementation

#import "FJKNativePlayer.h"
#import "FJKPixelBufferRenderer.h"
#import "FJKAudioRenderer.h"
#import <VideoToolbox/VideoToolbox.h>
#include "ffmpeg_demuxer.h"
#include "FJKAudioDecoder.h"
#include "FJKAudioQueue.h"
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

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
    
    // Playback mode configuration
    NSInteger _playbackMode;      // 0=LIVE_LOW_LATENCY, 1=LIVE_WITH_AUDIO, 2=VOD_OPTIMIZED
    NSInteger _bufferSize;        // Number of frames to buffer
    BOOL _enableAudio;            // Whether to enable audio
    NSInteger _maxLatencyMs;      // Maximum acceptable latency
    BOOL _enableFrameDrop;        // Whether to drop frames when behind
    
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
    
    // Software decoder fallback (when VideoToolbox is unavailable, e.g. simulator)
    AVCodecContext *_swDecoderCtx;
    struct SwsContext *_swsCtx;
    AVBSFContext *_bsfCtx;
    BOOL _useSoftwareDecoder;
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
        
        // Software decoder not used by default
        _useSoftwareDecoder = NO;
        _swDecoderCtx = NULL;
        _swsCtx = NULL;
        _bsfCtx = NULL;
        
        // Default playback configuration (VOD optimized)
        _playbackMode = 2;        // VOD_OPTIMIZED
        _bufferSize = 10;         // 10 frames
        _enableAudio = YES;       // Audio enabled
        _maxLatencyMs = 0;        // No latency constraint
        _enableFrameDrop = NO;    // No frame dropping
        
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

- (int)setPlaybackMode:(NSInteger)mode
              bufferMs:(NSInteger)bufferMs
           enableAudio:(NSInteger)enableAudio
         maxLatencyMs:(NSInteger)maxLatencyMs
       enableFrameDrop:(NSInteger)enableFrameDrop {
    
    [_stateLock lock];
    
    NSLog(@"[FJKNativePlayer] ⚙️ Setting playback mode: mode=%ld, buffer=%ldms, audio=%ld, maxLatency=%ldms, frameDrop=%ld",
          (long)mode, (long)bufferMs, (long)enableAudio, (long)maxLatencyMs, (long)enableFrameDrop);
    
    _playbackMode = mode;
    
    // Mode 0: LIVE_LOW_LATENCY (no audio, minimum latency, aggressive frame drop)
    // Mode 1: LIVE_WITH_AUDIO (audio sync, low latency, moderate frame drop)
    // Mode 2: VOD_OPTIMIZED (smooth playback, larger buffer, no frame drop)
    
    switch (mode) {
        case 0: // LIVE_LOW_LATENCY
            _bufferSize = (bufferMs > 0) ? (bufferMs / 100) : 1;  // ~1-2 frames
            _enableFrameDrop = (enableFrameDrop >= 0) ? (enableFrameDrop != 0) : YES;
            _maxLatencyMs = (maxLatencyMs > 0) ? maxLatencyMs : 200;
            _enableAudio = NO;  // No audio for low latency
            NSLog(@"[FJKNativePlayer] 📹 LIVE_LOW_LATENCY: buffer=%ld frames, max_latency=%ldms, frame_drop=ON, audio=OFF",
                  (long)_bufferSize, (long)_maxLatencyMs);
            break;
            
        case 1: // LIVE_WITH_AUDIO
            _bufferSize = (bufferMs > 0) ? (bufferMs / 100) : 5;  // ~5 frames
            _enableFrameDrop = (enableFrameDrop >= 0) ? (enableFrameDrop != 0) : YES;
            _maxLatencyMs = (maxLatencyMs > 0) ? maxLatencyMs : 1000;
            _enableAudio = (enableAudio >= 0) ? (enableAudio != 0) : YES;
            NSLog(@"[FJKNativePlayer] 🎬 LIVE_WITH_AUDIO: buffer=%ld frames, max_latency=%ldms, frame_drop=MODERATE, audio=ON",
                  (long)_bufferSize, (long)_maxLatencyMs);
            break;
            
        case 2: // VOD_OPTIMIZED (default)
        default:
            _bufferSize = (bufferMs > 0) ? (bufferMs / 1000) : 10;  // ~10 frames
            _enableFrameDrop = (enableFrameDrop >= 0) ? (enableFrameDrop != 0) : NO;
            _maxLatencyMs = 0;  // No latency constraint
            _enableAudio = (enableAudio >= 0) ? (enableAudio != 0) : YES;
            NSLog(@"[FJKNativePlayer] 🎞️ VOD_OPTIMIZED: buffer=%ld frames, frame_drop=OFF, audio=ON",
                  (long)_bufferSize);
            break;
    }
    
    [_stateLock unlock];
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

- (void)setVolume:(float)volume {
    if (volume < 0.0) volume = 0.0;
    if (volume > 1.0) volume = 1.0;
    
    if (_audioRenderer) {
        [_audioRenderer setVolume:volume];
        NSLog(@"[FJKNativePlayer] Volume set to: %.2f", volume);
    }
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
    
    // Set state to idle first to signal doPrepare to abort
    [_stateLock lock];
    _isPlaying = NO;
    _state = FJKPlayerStateIdle;
    [_stateLock unlock];
    
    // CRITICAL: Interrupt the demuxer to unblock any pending
    // avformat_open_input or avformat_find_stream_info calls
    if (_demuxer) {
        ff_demuxer_interrupt(_demuxer);
    }
    
    // Wait for any pending doPrepare to exit on the demuxer queue.
    // The interrupt was set above, so FFmpeg should return quickly.
    // Use dispatch_group_wait with timeout to avoid deadlock.
    dispatch_group_t group = dispatch_group_create();
    dispatch_group_enter(group);
    dispatch_async(_demuxerQueue, ^{
        NSLog(@"[FJKNativePlayer] Demuxer queue drained, safe to cleanup");
        dispatch_group_leave(group);
    });
    // Wait up to 5 seconds for doPrepare to exit
    dispatch_group_wait(group, dispatch_time(DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC));
    
    // Also wait for the decoder queue to stop (runDecoderLoop checks _isPlaying)
    dispatch_group_t decoderGroup = dispatch_group_create();
    dispatch_group_enter(decoderGroup);
    dispatch_async(_decoderQueue, ^{
        NSLog(@"[FJKNativePlayer] Decoder queue drained");
        dispatch_group_leave(decoderGroup);
    });
    // Wait indefinitely — demuxer interrupt ensures av_read_frame returns quickly
    dispatch_group_wait(decoderGroup, DISPATCH_TIME_FOREVER);
    
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
    
    // Cleanup software decoder
    if (_bsfCtx) {
        av_bsf_free(&_bsfCtx);
    }
    if (_swsCtx) {
        sws_freeContext(_swsCtx);
        _swsCtx = NULL;
    }
    if (_swDecoderCtx) {
        avcodec_free_context(&_swDecoderCtx);
    }
    _useSoftwareDecoder = NO;
    
    [_stateLock lock];
    _state = FJKPlayerStateIdle;
    [_stateLock unlock];
}

#pragma mark - Private Implementation

- (void)doPrepare {
    // Check if we were already cleaned up before starting
    if (_state == FJKPlayerStateIdle || _state == FJKPlayerStateStopped) {
        NSLog(@"[FJKNativePlayer] doPrepare aborted - player already reset");
        return;
    }
    
    NSLog(@"[FJKNativePlayer] Preparing with URL: %@", _dataSource);
    
    // Create FFmpeg demuxer
    _demuxer = ff_demuxer_create();
    if (!_demuxer) {
        [self notifyError:@"Failed to create demuxer"];
        return;
    }
    
    // Open media file (this can block for RTSP connection)
    NSLog(@"[FJKNativePlayer] Opening URL...");
    const char *url = [_dataSource UTF8String];
    int ret = ff_demuxer_open(_demuxer, url, NULL);
    NSLog(@"[FJKNativePlayer] ff_demuxer_open returned: %d", ret);
    if (ret < 0) {
        [self notifyError:[NSString stringWithFormat:@"Failed to open: %@ (error %d)", _dataSource, ret]];
        return;
    }
    
    // Check if we were interrupted during open
    if (_state == FJKPlayerStateIdle || _state == FJKPlayerStateStopped) {
        NSLog(@"[FJKNativePlayer] Prepare interrupted after open");
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
    
    // Create VideoToolbox decoder (try hardware first, fall back to software)
    ret = [self createVideoToolboxDecoder:videoStream];
    if (ret < 0) {
        NSLog(@"[FJKNativePlayer] VideoToolbox failed, trying FFmpeg software decoder...");
        ret = [self createSoftwareDecoder:videoStream];
        if (ret < 0) {
            [self notifyError:@"Failed to create video decoder (both HW and SW failed)"];
            return;
        }
        _useSoftwareDecoder = YES;
        
        // Disable the internal parser in the format context
        // to prevent av_read_frame from crashing in the codec parser.
        // We handle parsing ourselves in the software decoder.
        struct AVFormatContext *fmtCtx = ff_demuxer_get_format_context(_demuxer);
        if (fmtCtx) {
            fmtCtx->flags |= AVFMT_FLAG_NOFILLIN | AVFMT_FLAG_NOPARSE;
            NSLog(@"[FJKNativePlayer] Disabled internal parser (NOPARSE|NOFILLIN)");
        }
        
        NSLog(@"[FJKNativePlayer] ✅ Using FFmpeg software decoder");
    } else {
        _useSoftwareDecoder = NO;
    }
    
    // Create renderer
    _renderer = [[FJKPixelBufferRenderer alloc] initWithWidth:videoStream->width height:videoStream->height];
    if (!_renderer) {
        [self notifyError:@"Failed to create renderer"];
        return;
    }
    
    // Phase 3: Initialize audio (find audio stream) - Only if audio is enabled
    if (_enableAudio) {
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
    } else {
        NSLog(@"[FJKNativePlayer] 🔇 Audio disabled by playback mode");
        _audioStreamIndex = -1;
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

#pragma mark - Software Decoder (FFmpeg fallback)

- (int)createSoftwareDecoder:(FFStream *)stream {
    // Find the H.264 decoder
    const AVCodec *codec = NULL;
    if (stream->video_codec == FF_VIDEO_CODEC_H265) {
        codec = avcodec_find_decoder(AV_CODEC_ID_HEVC);
    } else {
        codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    }
    
    if (!codec) {
        NSLog(@"[FJKNativePlayer] Software codec not found");
        return -1;
    }
    
    _swDecoderCtx = avcodec_alloc_context3(codec);
    if (!_swDecoderCtx) {
        NSLog(@"[FJKNativePlayer] Failed to allocate software decoder context");
        return -1;
    }
    
    // Copy codec parameters from the demuxer stream
    struct AVFormatContext *fmtCtx = ff_demuxer_get_format_context(_demuxer);
    int videoIdx = ff_demuxer_find_video_stream(_demuxer);
    if (fmtCtx && videoIdx >= 0) {
        avcodec_parameters_to_context(_swDecoderCtx, fmtCtx->streams[videoIdx]->codecpar);
    }
    
    // Set decoder options - single-threaded for stability on simulator
    _swDecoderCtx->thread_count = 1;
    _swDecoderCtx->flags |= AV_CODEC_FLAG_LOW_DELAY;
    _swDecoderCtx->flags2 |= AV_CODEC_FLAG2_FAST;
    
    int ret = avcodec_open2(_swDecoderCtx, codec, NULL);
    if (ret < 0) {
        NSLog(@"[FJKNativePlayer] Failed to open software decoder: %d", ret);
        avcodec_free_context(&_swDecoderCtx);
        return -1;
    }
    
    // Create h264_mp4toannexb bitstream filter for AVCC -> Annex B conversion
    const AVBitStreamFilter *bsf = av_bsf_get_by_name(
        stream->video_codec == FF_VIDEO_CODEC_H265 ? "hevc_mp4toannexb" : "h264_mp4toannexb");
    if (bsf) {
        ret = av_bsf_alloc(bsf, &_bsfCtx);
        if (ret >= 0 && fmtCtx && videoIdx >= 0) {
            avcodec_parameters_copy(_bsfCtx->par_in, fmtCtx->streams[videoIdx]->codecpar);
            ret = av_bsf_init(_bsfCtx);
            if (ret < 0) {
                NSLog(@"[FJKNativePlayer] BSF init failed: %d (non-fatal)", ret);
                av_bsf_free(&_bsfCtx);
            } else {
                NSLog(@"[FJKNativePlayer] BSF filter initialized for Annex B conversion");
            }
        }
    }
    
    NSLog(@"[FJKNativePlayer] Software decoder created: %dx%d, codec=%s",
          _swDecoderCtx->width, _swDecoderCtx->height, codec->name);
    return 0;
}

- (void)decodeSoftwarePacket:(FFPacket *)packet {
    if (!_swDecoderCtx || !packet || !packet->data || packet->size <= 0) return;
    
    // Create AVPacket with proper buffer management
    AVPacket *avpkt = av_packet_alloc();
    if (!avpkt) return;
    
    // Use av_new_packet to properly allocate buffer (sets buf, data, size)
    if (av_new_packet(avpkt, packet->size) < 0) {
        av_packet_free(&avpkt);
        return;
    }
    memcpy(avpkt->data, packet->data, packet->size);
    avpkt->pts = packet->pts;
    avpkt->dts = packet->dts;
    
    // Apply BSF filter if available (AVCC -> Annex B)
    if (_bsfCtx) {
        int bsfRet = av_bsf_send_packet(_bsfCtx, avpkt);
        if (bsfRet < 0) {
            av_packet_free(&avpkt);
            return;
        }
        // Receive filtered packet (reuse avpkt)
        bsfRet = av_bsf_receive_packet(_bsfCtx, avpkt);
        if (bsfRet < 0) {
            av_packet_free(&avpkt);
            return;
        }
    }
    
    int ret = avcodec_send_packet(_swDecoderCtx, avpkt);
    av_packet_free(&avpkt);
    
    if (ret < 0) {
        static int errCount = 0;
        if (errCount++ < 5) {
            NSLog(@"[FJKNativePlayer] SW decode send error: %d", ret);
        }
        return;
    }
    
    AVFrame *frame = av_frame_alloc();
    if (!frame) return;
    
    while (avcodec_receive_frame(_swDecoderCtx, frame) == 0) {
        // Convert AVFrame to CVPixelBuffer
        CVPixelBufferRef pixelBuffer = [self pixelBufferFromAVFrame:frame];
        if (pixelBuffer) {
            // Track decoded frame position (for stall detection)
            _currentPosition++;
            
            // Deliver frame to renderer
            [_renderer renderFrame:pixelBuffer];
            
            // Notify texture update
            if (self.frameCallback) {
                dispatch_async(dispatch_get_main_queue(), ^{
                    self.frameCallback();
                });
            }
            
            CVPixelBufferRelease(pixelBuffer);
        }
    }
    
    av_frame_free(&frame);
}

- (CVPixelBufferRef)pixelBufferFromAVFrame:(AVFrame *)frame {
    int width = frame->width;
    int height = frame->height;
    
    // Create CVPixelBuffer
    CVPixelBufferRef pixelBuffer = NULL;
    NSDictionary *attrs = @{
        (id)kCVPixelBufferIOSurfacePropertiesKey: @{}
    };
    
    CVReturn cvRet = CVPixelBufferCreate(
        kCFAllocatorDefault,
        width, height,
        kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange, // NV12
        (__bridge CFDictionaryRef)attrs,
        &pixelBuffer
    );
    
    if (cvRet != kCVReturnSuccess || !pixelBuffer) {
        return NULL;
    }
    
    CVPixelBufferLockBaseAddress(pixelBuffer, 0);
    
    // Frame is YUV420P (3 planes) -> convert to NV12 (2 planes: Y + interleaved UV)
    // Y plane - direct copy
    uint8_t *yDst = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 0);
    size_t yDstStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 0);
    
    for (int row = 0; row < height; row++) {
        memcpy(yDst + row * yDstStride,
               frame->data[0] + row * frame->linesize[0],
               width);
    }
    
    // UV plane - interleave U and V
    uint8_t *uvDst = CVPixelBufferGetBaseAddressOfPlane(pixelBuffer, 1);
    size_t uvDstStride = CVPixelBufferGetBytesPerRowOfPlane(pixelBuffer, 1);
    int uvHeight = height / 2;
    int uvWidth = width / 2;
    
    for (int row = 0; row < uvHeight; row++) {
        uint8_t *uSrc = frame->data[1] + row * frame->linesize[1];
        uint8_t *vSrc = frame->data[2] + row * frame->linesize[2];
        uint8_t *dst = uvDst + row * uvDstStride;
        
        for (int col = 0; col < uvWidth; col++) {
            dst[col * 2]     = uSrc[col];
            dst[col * 2 + 1] = vSrc[col];
        }
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, 0);
    
    return pixelBuffer;
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
    int consecutiveErrors = 0;
    
    while (_isPlaying && _demuxer) {
        @autoreleasepool {
            // Safety: re-check after autoreleasepool setup
            if (!_isPlaying || !_demuxer) break;
            
            // Read packet
            FFPacket *packet = NULL;
            int ret = ff_demuxer_read_packet(_demuxer, &packet); // Returns 0 on success
            
            if (ret == 0 && packet) {
                consecutiveErrors = 0; // Reset on success
                int video_stream_index = ff_demuxer_find_video_stream(_demuxer);
                
                // Route packet to appropriate decoder
                if (packet->stream_index == video_stream_index) {
                    // Video packet - decode with appropriate decoder
                    if (_useSoftwareDecoder) {
                        [self decodeSoftwarePacket:packet];
                    } else {
                        [self decodePacket:packet];
                    }
                    
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
                consecutiveErrors++;
                NSLog(@"[FJKNativePlayer] Read error: %d (consecutive: %d)", ret, consecutiveErrors);
                
                // For live streams, retry on transient errors instead of exiting
                if (_isLiveStream && _isPlaying && consecutiveErrors < 50) {
                    usleep(100000); // 100ms backoff before retry
                    continue;
                }
                break;
            }
        }
    }
    
    NSLog(@"[FJKNativePlayer] Decoder loop exited (isPlaying=%d)", _isPlaying);
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
    
    // Forward error message to Flutter via callback
    if (self.errorMessageCallback) {
        dispatch_async(dispatch_get_main_queue(), ^{
            self.errorMessageCallback(message);
        });
    }
    
    [self notifyEvent:FJKPlayerEventError arg1:-1 arg2:0];
}

@end
