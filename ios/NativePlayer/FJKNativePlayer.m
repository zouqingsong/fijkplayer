// MIT License - FijkPlayer Native iOS Implementation

#import "FJKNativePlayer.h"
#import "FJKPixelBufferRenderer.h"
#import <VideoToolbox/VideoToolbox.h>
#include "ffmpeg_demuxer.h"

@interface FJKNativePlayer () {
    // Native C components
    FFDemuxer *_demuxer;
    VTDecompressionSessionRef _decompressionSession;
    FJKPixelBufferRenderer *_renderer;
    
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
        _demuxerQueue = dispatch_queue_create("com.befovy.fijk.demuxer", DISPATCH_QUEUE_SERIAL);
        _decoderQueue = dispatch_queue_create("com.befovy.fijk.decoder", DISPATCH_QUEUE_SERIAL);
        
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
    [_stateLock unlock];
    
    // Start decoder thread
    dispatch_async(_decoderQueue, ^{
        [self runDecoderLoop];
    });
    
    [self notifyEvent:FJKPlayerEventStarted arg1:_videoSize.width arg2:_videoSize.height];
    NSLog(@"[FJKNativePlayer] Playback started");
    return 0;
}

- (void)pause {
    [_stateLock lock];
    if (_state == FJKPlayerStatePlaying) {
        _state = FJKPlayerStatePaused;
        _isPlaying = NO;
        [self notifyEvent:FJKPlayerEventPaused arg1:0 arg2:0];
    }
    [_stateLock unlock];
}

- (void)resume {
    [_stateLock lock];
    if (_state == FJKPlayerStatePaused) {
        _state = FJKPlayerStatePlaying;
        _isPlaying = YES;
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

- (void)cleanup {
    NSLog(@"[FJKNativePlayer] Releasing resources");
    
    [self stop];
    
    if (_decompressionSession) {
        VTDecompressionSessionInvalidate(_decompressionSession);
        CFRelease(_decompressionSession);
        _decompressionSession = NULL;
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
    
    [_stateLock lock];
    _state = FJKPlayerStatePrepared;
    [_stateLock unlock];
    
    [self notifyEvent:FJKPlayerEventPrepared arg1:0 arg2:0];
    [self notifyEvent:FJKPlayerEventVideoSizeChanged arg1:_videoSize.width arg2:_videoSize.height];
    
    NSLog(@"[FJKNativePlayer] Prepared: %dx%d, duration=%lld ms", 
          (int)_videoSize.width, (int)_videoSize.height, _duration);
}

- (int)createVideoToolboxDecoder:(FFStream *)stream {
    // Create format description from extradata (SPS/PPS)
    CMFormatDescriptionRef formatDesc = NULL;
    
    if (stream->extradata && stream->extradata_size > 0) {
        // Parse H.264 extradata (avcC format)
        OSStatus status = CMVideoFormatDescriptionCreateFromH264ParameterSets(
            kCFAllocatorDefault,
            0, NULL, NULL, // Will parse from extradata
            4, // NAL unit length size
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
    
    CFRelease(formatDesc);
    
    if (status != noErr) {
        NSLog(@"[FJKNativePlayer] Failed to create decompression session: %d", (int)status);
        return -1;
    }
    
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
    
    // Render frame to output buffer
    BOOL success = [_renderer renderFrame:imageBuffer];
    if (success) {
        _currentPosition = CMTimeGetSeconds(pts) * 1000;
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
                // Decode packet with VideoToolbox
                [self decodePacket:packet];
                ff_packet_free(packet);
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
        NULL, // format description - need to set
        1, 1, &timing,
        0, NULL,
        &sampleBuffer
    );
    
    if (status == noErr && sampleBuffer) {
        // Decode with VideoToolbox
        VTDecodeFrameFlags flags = kVTDecodeFrame_EnableAsynchronousDecompression;
        VTDecodeInfoFlags infoFlags;
        
        status = VTDecompressionSessionDecodeFrame(
            _decompressionSession,
            sampleBuffer,
            flags,
            NULL, // source frame ref con
            &infoFlags
        );
        
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
