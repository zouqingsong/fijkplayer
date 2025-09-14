// MIT License
//
// Copyright (c) [2019] [Befovy]
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#import "FijkPlayer.h"
#import "FijkHostOption.h"
#import "FijkPlugin.h"
#import "FijkQueuingEventSink.h"

#import <Flutter/Flutter.h>
#import <Foundation/Foundation.h>
#import <IJKMediaPlayer/IJKMediaPlayer.h>
#import <libkern/OSAtomic.h>
#import <stdatomic.h>
#import <TargetConditionals.h>

@interface FijkPlugin ()

- (void)onPlayingChange:(int)delta;
- (void)onPlayableChange:(int)delta;
- (void)setScreenOn:(BOOL)on;

@end

static atomic_int atomicId = 0;

@implementation FijkPlayer {
    IJKFFMediaPlayer *_ijkMediaPlayer;

    FijkQueuingEventSink *_eventSink;
    FlutterMethodChannel *_methodChannel;
    FlutterEventChannel *_eventChannel;

    id<FlutterPluginRegistrar> _registrar;
    id<FlutterTextureRegistry> _textureRegistry;

    CVPixelBufferRef volatile _latestPixelBuffer;
    CVPixelBufferRef _lastBuffer;

    int _width;
    int _height;
    int _rotate;

    FijkHostOption *_hostOption;
    int _state;
    int _pid;
    int64_t _vid;
    
    // Recording related properties
    AVAssetWriter *_assetWriter;
    AVAssetWriterInput *_writerInput;
    AVAssetWriterInputPixelBufferAdaptor *_pixelBufferAdaptor;
    BOOL _isRecording;
    NSString *_recordingPath;
    CMTime _firstFrameTime;
    BOOL _hasFirstFrame;
}

static const int idle = 0;
static const int initialized = 1;
static const int asyncPreparing = 2;
static const int __attribute__((unused)) prepared = 3;
static const int __attribute__((unused)) started = 4;
static const int paused = 5;
static const int completed = 6;
static const int stopped = 7;
static const int __attribute__((unused)) error = 8;
static const int end = 9;

static int renderType = 0;

// static int debugLeak = 0;

- (instancetype)initJustTexture {
    self = [super init];
    if (self) {
        int pid = atomic_fetch_add(&atomicId, 1);
        _playerId = @(pid);
        _pid = pid;
        _vid = -1;
    }
    return self;
}

- (instancetype)initWithRegistrar:(id<FlutterPluginRegistrar>)registrar {
    self = [super init];
    if (self) {
        _registrar = registrar;
        int pid = atomic_fetch_add(&atomicId, 1);
        _playerId = @(pid);
        _pid = pid;
        _eventSink = [[FijkQueuingEventSink alloc] init];
        _latestPixelBuffer = nil;
        _vid = -1;
        _rotate = -1;
        _state = 0;

        _hostOption = [[FijkHostOption alloc] init];
        _lastBuffer = nil;
        
        // Initialize recording timing variables
        _hasFirstFrame = NO;
        _firstFrameTime = kCMTimeInvalid;
        _isRecording = NO;
        if (renderType == 0) {
            _ijkMediaPlayer = [[IJKFFMediaPlayer alloc] init];
            [_ijkMediaPlayer setOptionValue:@"fcc-bgra"
                                     forKey:@"overlay-format"
                                 ofCategory:kIJKFFOptionCategoryPlayer];
        } else {
            // _ijkMediaPlayer = [[IJKFFMediaPlayer alloc] initWithFbo];
        }
        // if (debugLeak) {
        //    [_ijkMediaPlayer setLoop:0];
        //    [_ijkMediaPlayer setSpeed:4.0];
        //}

        [_ijkMediaPlayer setOptionIntValue:0
                                    forKey:@"start-on-prepared"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        [_ijkMediaPlayer setOptionIntValue:1
                                    forKey:@"enable-position-notify"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        
        // Configure video decoding based on device type
#if TARGET_OS_SIMULATOR
        // iOS Simulator - disable hardware acceleration completely
        [_ijkMediaPlayer setOptionIntValue:0
                                    forKey:@"videotoolbox"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        [_ijkMediaPlayer setOptionIntValue:0
                                    forKey:@"videotoolbox-hevc"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        [_ijkMediaPlayer setOptionIntValue:0
                                    forKey:@"videotoolbox-handle-resolution-change"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        
        // Force software decoding path
        [_ijkMediaPlayer setOptionValue:@"avdec_h264"
                                 forKey:@"vcodec"
                             ofCategory:kIJKFFOptionCategoryCodec];
        
        // Use BGRA format for iOS Simulator (reverted from NV12)
        [_ijkMediaPlayer setOptionValue:@"fcc-bgra"
                                 forKey:@"overlay-format"
                             ofCategory:kIJKFFOptionCategoryPlayer];
        
        // Disable frame dropping to ensure consistent stride
        [_ijkMediaPlayer setOptionIntValue:0
                                    forKey:@"framedrop"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        
        // Alternative: Force specific software decoder options
        [_ijkMediaPlayer setOptionIntValue:0
                                    forKey:@"hwaccel"
                                ofCategory:kIJKFFOptionCategoryCodec];
        [_ijkMediaPlayer setOptionIntValue:1
                                    forKey:@"threads"
                                ofCategory:kIJKFFOptionCategoryCodec];
        
        NSLog(@"FijkPlayer: iOS Simulator configured with BGRA format and software decoding");
        
        // Disable problematic optimizations for simulator
        [_ijkMediaPlayer setOptionIntValue:0
                                    forKey:@"fast"
                                ofCategory:kIJKFFOptionCategoryCodec];
        [_ijkMediaPlayer setOptionIntValue:1
                                    forKey:@"enable-accurate-seek"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        
        NSLog(@"FijkPlayer: Configured for iOS Simulator with software decoding");
#else
        // Real iOS devices - use hardware acceleration
        [_ijkMediaPlayer setOptionIntValue:1
                                    forKey:@"videotoolbox"
                                ofCategory:kIJKFFOptionCategoryPlayer];
        NSLog(@"FijkPlayer: Configured for iOS device with hardware acceleration");
#endif

        [IJKFFMoviePlayerController setLogLevel:k_IJK_LOG_INFO];

        [_ijkMediaPlayer addIJKMPEventHandler:self];

        _methodChannel = [FlutterMethodChannel
            methodChannelWithName:[@"befovy.com/fijkplayer/"
                                      stringByAppendingString:[_playerId
                                                                  stringValue]]
                  binaryMessenger:[registrar messenger]];

        __block typeof(self) weakSelf = self;
        [_methodChannel setMethodCallHandler:^(FlutterMethodCall *call,
                                               FlutterResult result) {
          [weakSelf handleMethodCall:call result:result];
        }];

        _eventChannel = [FlutterEventChannel
            eventChannelWithName:[@"befovy.com/fijkplayer/event/"
                                     stringByAppendingString:[_playerId
                                                                 stringValue]]
                 binaryMessenger:[registrar messenger]];

        [_eventChannel setStreamHandler:self];
    }

    return self;
}

- (void)setup {
    _ijkMediaPlayer.cacheSnapshot = ([_hostOption getIntValue:FIJK_HOST_OPTION_ENABLE_SNAPSHOT defalt:@(0)] > 0);
}

- (void)shutdown {
    // Stop recording if in progress
    if (_isRecording && _assetWriter) {
        _isRecording = NO;
        [_writerInput markAsFinished];
        [_assetWriter finishWritingWithCompletionHandler:^{
            // Recording cleanup handled in completion
        }];
        _assetWriter = nil;
        _writerInput = nil;
        _pixelBufferAdaptor = nil;
        _recordingPath = nil;
    }
    
    [self handleEvent:IJKMPET_PLAYBACK_STATE_CHANGED
              andArg1:end
              andArg2:_state
             andExtra:nil];
    if (_ijkMediaPlayer) {
        [_ijkMediaPlayer shutdown];
        _ijkMediaPlayer = nil;
    }
    if (_vid >= 0) {
        [_textureRegistry unregisterTexture:_vid];
        _vid = -1;
        _textureRegistry = nil;
    }

    CVPixelBufferRef old = _latestPixelBuffer;
    while (!OSAtomicCompareAndSwapPtrBarrier(old, nil,
                                             (void **)&_latestPixelBuffer)) {
        old = _latestPixelBuffer;
    }
    if (old) {
        CFRelease(old);
    }

    if (_lastBuffer) {
        CVPixelBufferRelease(_lastBuffer);
        _lastBuffer = nil;
    }
    [_methodChannel setMethodCallHandler:nil];
    _methodChannel = nil;

    [_eventSink setDelegate:nil];
    _eventSink = nil;
    [_eventChannel setStreamHandler:nil];
    _eventChannel = nil;
}

- (FlutterError *_Nullable)onCancelWithArguments:(id _Nullable)arguments {
    [_eventSink setDelegate:nil];
    return nil;
}

- (FlutterError *_Nullable)onListenWithArguments:(id _Nullable)arguments
                                       eventSink:
                                           (nonnull FlutterEventSink)events {
    [_eventSink setDelegate:events];
    return nil;
}

// Create a Flutter-compatible pixel buffer with proper stride alignment
- (CVPixelBufferRef _Nullable)createFlutterCompatiblePixelBuffer:(CVPixelBufferRef)sourceBuffer {
    if (sourceBuffer == nil) return nil;
    
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(sourceBuffer);
    size_t width = CVPixelBufferGetWidth(sourceBuffer);
    size_t height = CVPixelBufferGetHeight(sourceBuffer);
    
    // Only process BGRA format for iOS Simulator
#if TARGET_OS_SIMULATOR
    if (pixelFormat != kCVPixelFormatType_32BGRA) {
        return nil; // Let IJKPlayer handle format conversion
    }
    
    // Check if the source buffer already has proper alignment
    CVPixelBufferLockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
    size_t sourceBytesPerRow = CVPixelBufferGetBytesPerRow(sourceBuffer);
    size_t expectedBytesPerRow = width * 4; // 4 bytes per pixel for BGRA
    
    // If stride is already correct, use original buffer
    if (sourceBytesPerRow == expectedBytesPerRow) {
        CVPixelBufferUnlockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
        return nil; // Use original buffer
    }
    
    // Create a new buffer with proper alignment
    NSDictionary *attributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (NSString *)kCVPixelBufferWidthKey: @(width),
        (NSString *)kCVPixelBufferHeightKey: @(height),
        (NSString *)kCVPixelBufferBytesPerRowAlignmentKey: @(1), // Ensure minimal alignment
    };
    
    CVPixelBufferRef newBuffer = nil;
    CVReturn result = CVPixelBufferCreate(kCFAllocatorDefault, width, height, 
                                         kCVPixelFormatType_32BGRA, 
                                         (__bridge CFDictionaryRef)attributes, 
                                         &newBuffer);
    
    if (result != kCVReturnSuccess || newBuffer == nil) {
        CVPixelBufferUnlockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
        return nil;
    }
    
    // Copy pixel data with proper stride
    CVPixelBufferLockBaseAddress(newBuffer, 0);
    
    uint8_t *sourceData = (uint8_t *)CVPixelBufferGetBaseAddress(sourceBuffer);
    uint8_t *destData = (uint8_t *)CVPixelBufferGetBaseAddress(newBuffer);
    size_t destBytesPerRow = CVPixelBufferGetBytesPerRow(newBuffer);
    
    for (size_t row = 0; row < height; row++) {
        memcpy(destData + row * destBytesPerRow, 
               sourceData + row * sourceBytesPerRow, 
               expectedBytesPerRow);
    }
    
    CVPixelBufferUnlockBaseAddress(sourceBuffer, kCVPixelBufferLock_ReadOnly);
    CVPixelBufferUnlockBaseAddress(newBuffer, 0);
    
    return newBuffer; // Caller should release this
#else
    return nil; // Use original buffer on real devices
#endif
}

// IJKCVPBViewProtocol delegate
// IJKFFMediaPlayer will incoke this method whem new frame should be displayed
- (void)display_pixelbuffer:(CVPixelBufferRef)pixelbuffer {

    if (_lastBuffer == nil) {
        _lastBuffer = CVPixelBufferRetain(pixelbuffer);
        CFRetain(pixelbuffer);
    } else if (_lastBuffer != pixelbuffer) {
        CVPixelBufferRelease(_lastBuffer);
        _lastBuffer = CVPixelBufferRetain(pixelbuffer);
        CFRetain(pixelbuffer);
    }

    CVPixelBufferRef newBuffer = pixelbuffer;

    CVPixelBufferRef old = _latestPixelBuffer;
    while (!OSAtomicCompareAndSwapPtrBarrier(old, newBuffer,
                                             (void **)&_latestPixelBuffer)) {
        old = _latestPixelBuffer;
    }

    if (old && old != pixelbuffer) {
        CFRelease(old);
    }
    if (_vid >= 0) {
        [_textureRegistry textureFrameAvailable:_vid];
    }
    
    // Record frame if recording is active
    if (_isRecording && _writerInput && _pixelBufferAdaptor && _assetWriter) {
        if (_writerInput.readyForMoreMediaData) {
            CMTime frameTime;
            
            // Use the player's current position directly as presentation time
            long currentPositionMs = [_ijkMediaPlayer getCurrentPosition];
            frameTime = CMTimeMake(currentPositionMs, 1000); // Convert ms to CMTime
            
            // For the first frame during recording, start the session
            if (!_hasFirstFrame) {
                _hasFirstFrame = YES;
                // Start the recording session with the first frame's timestamp
                [_assetWriter startSessionAtSourceTime:frameTime];
                NSLog(@"Recording session started at time: %f seconds", CMTimeGetSeconds(frameTime));
            }
            
            BOOL success = [_pixelBufferAdaptor appendPixelBuffer:pixelbuffer withPresentationTime:frameTime];
            if (!success) {
                NSLog(@"Failed to append pixel buffer for recording at time: %f", CMTimeGetSeconds(frameTime));
            }
        }
    }
}

/**
 * Copy pixel buffer for Flutter texture rendering
 * Handles iOS Simulator specific pixel format processing for proper video display
 */
- (CVPixelBufferRef _Nullable)copyPixelBuffer {
    CVPixelBufferRef pixelBuffer = _latestPixelBuffer;
    while (!OSAtomicCompareAndSwapPtrBarrier(pixelBuffer, nil,
                                             (void **)&_latestPixelBuffer)) {
        pixelBuffer = _latestPixelBuffer;
    }

#if TARGET_OS_SIMULATOR
    if (!pixelBuffer) {
        return NULL;
    }

    // Get pixel format and handle both BGRA and NV12
    OSType pixelFormat = CVPixelBufferGetPixelFormatType(pixelBuffer);
    
    // For NV12 format, return directly 
    if (pixelFormat == kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange) {
        return pixelBuffer;
    }
    
    // Only process BGRA format for iOS Simulator stride issues
    size_t width = CVPixelBufferGetWidth(pixelBuffer);
    if (pixelFormat != kCVPixelFormatType_32BGRA) {
        return pixelBuffer; // Return as-is for other formats
    }

    CVPixelBufferLockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
    
    size_t height = CVPixelBufferGetHeight(pixelBuffer);
    size_t bytesPerRow = CVPixelBufferGetBytesPerRow(pixelBuffer);
    size_t expectedBytesPerRow = width * 4; // 4 bytes per pixel for BGRA
    
    // If stride is already correct, return original
    if (bytesPerRow == expectedBytesPerRow) {
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        return pixelBuffer;
    }
    
    // Create new buffer with correct stride
    NSDictionary *attributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (NSString *)kCVPixelBufferWidthKey: @(width),
        (NSString *)kCVPixelBufferHeightKey: @(height),
        (NSString *)kCVPixelBufferBytesPerRowAlignmentKey: @(1)
    };
    
    CVPixelBufferRef newBuffer = NULL;
    CVReturn result = CVPixelBufferCreate(kCFAllocatorDefault, width, height,
                                         kCVPixelFormatType_32BGRA, 
                                         (__bridge CFDictionaryRef)attributes, 
                                         &newBuffer);
    
    if (result == kCVReturnSuccess && newBuffer != NULL) {
        CVPixelBufferLockBaseAddress(newBuffer, 0);
        
        uint8_t *sourceData = (uint8_t *)CVPixelBufferGetBaseAddress(pixelBuffer);
        uint8_t *destData = (uint8_t *)CVPixelBufferGetBaseAddress(newBuffer);
        size_t newBytesPerRow = CVPixelBufferGetBytesPerRow(newBuffer);
        
        // Copy data row by row
        for (size_t row = 0; row < height; row++) {
            memcpy(destData + row * newBytesPerRow,
                   sourceData + row * bytesPerRow,
                   expectedBytesPerRow);
        }
        
        CVPixelBufferUnlockBaseAddress(newBuffer, 0);
        CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
        CVPixelBufferRelease(pixelBuffer);
        
        return newBuffer;
    }
    
    CVPixelBufferUnlockBaseAddress(pixelBuffer, kCVPixelBufferLock_ReadOnly);
#endif
    
    return pixelBuffer;
}

- (NSNumber *)setupSurface {
    [self setup];
    if (_vid < 0) {
        _textureRegistry = [_registrar textures];
        int64_t vid = [_textureRegistry registerTexture:self];
        _vid = vid;
        [_ijkMediaPlayer setupCVPixelBufferView:self];
    }
    return [NSNumber numberWithLongLong:_vid];
}

- (BOOL)isPlayable:(int)state {
    return state == started || state == paused || state == completed ||
           state == prepared;
}

- (void)onStateChangedWithNew:(int)newState andOld:(int)oldState {
    FijkPlugin *plugin = [FijkPlugin singleInstance];
    if (plugin == nil)
        return;
    if (newState == started && oldState != started) {
        [plugin onPlayingChange:1];
        if ([[_hostOption getIntValue:FIJK_HOST_OPTION_REQUEST_SCREENON
                               defalt:@(0)] intValue] == 1) {
            [plugin setScreenOn:YES];
        }
    } else if (newState != started && oldState == started) {
        [plugin onPlayingChange:-1];
        if ([[_hostOption getIntValue:FIJK_HOST_OPTION_REQUEST_SCREENON
                               defalt:@(0)] intValue] == 1) {
            [plugin setScreenOn:NO];
        }
    }

    if ([self isPlayable:newState] && ![self isPlayable:oldState]) {
        [plugin onPlayableChange:1];
    } else if (![self isPlayable:newState] && [self isPlayable:oldState]) {
        [plugin onPlayableChange:-1];
    }
}

- (void)handleEvent:(int)what
            andArg1:(int)arg1
            andArg2:(int)arg2
           andExtra:(void *)extra {
    switch (what) {
    case IJKMPET_PREPARED:
        [_eventSink success:@{
            @"event" : @"prepared",
            @"duration" : @([_ijkMediaPlayer getDuration]),
        }];
        break;
    case IJKMPET_PLAYBACK_STATE_CHANGED:
        _state = arg1;
        [_eventSink success:@{
            @"event" : @"state_change",
            @"new" : @(arg1),
            @"old" : @(arg2),
        }];
        [self onStateChangedWithNew:arg1 andOld:arg2];
        // Auto-stop recording if playback reaches completed or end state
        if (_isRecording && (arg1 == completed || arg1 == end)) {
            [self stopRecordingWithResult:^(id result) {
                NSLog(@"Auto-stopped recording due to playback end. Result: %@", result);
            }];
        }
        break;
    case IJKMPET_VIDEO_RENDERING_START:
    case IJKMPET_AUDIO_RENDERING_START:
        [_eventSink success:@{
            @"event" : @"rendering_start",
            @"type" : what == IJKMPET_VIDEO_RENDERING_START ? @"video"
                                                            : @"audio",
        }];
        break;
    case IJKMPET_BUFFERING_START:
    case IJKMPET_BUFFERING_END:
        [_eventSink success:@{
            @"event" : @"freeze",
            @"value" :
                [NSNumber numberWithBool:what == IJKMPET_BUFFERING_START],
        }];
        break;
    case IJKMPET_BUFFERING_UPDATE:
        [_eventSink success:@{
            @"event" : @"buffering",
            @"head" : @(arg1),
            @"percent" : @(arg2),
        }];
        break;
    case IJKMPET_CURRENT_POSITION_UPDATE:
        [_eventSink success:@{
            @"event" : @"pos",
            @"pos" : @(arg1),
        }];
        break;
    case IJKMPET_VIDEO_ROTATION_CHANGED:
        [_eventSink success:@{@"event" : @"rotate", @"degree" : @(arg1)}];
        _rotate = arg1;
        if (_height > 0 && _width > 0) {
            [self handleEvent:IJKMPET_VIDEO_SIZE_CHANGED
                      andArg1:_width
                      andArg2:_height
                     andExtra:nil];
        }
        break;
    case IJKMPET_VIDEO_SIZE_CHANGED:
        if (_rotate == 0 || _rotate == 180) {
            [_eventSink success:@{
                @"event" : @"size_changed",
                @"width" : @(arg1),
                @"height" : @(arg2),
            }];
        } else if (_rotate == 90 || _rotate == 270) {
            [_eventSink success:@{
                @"event" : @"size_changed",
                @"width" : @(arg2),
                @"height" : @(arg1),
            }];
        }
        _width = arg1;
        _height = arg2;
        break;
    case 600:
        [_eventSink success:@{
            @"event" : @"seek_complete",
            @"pos" : @(arg1),
            @"err" : @(arg2),
        }];
        break;
    case IJKMPET_ERROR:
        [_eventSink error:[NSString stringWithFormat:@"%d", arg1]
                  message:extra ? [NSString stringWithUTF8String:extra] : nil
                  details:@(arg2)];
        break;
    default:
        break;
    }
}

- (void)onEvent4Player:(IJKFFMediaPlayer *)player
              withType:(int)what
               andArg1:(int)arg1
               andArg2:(int)arg2
              andExtra:(void *)extra {
    switch (what) {
    case IJKMPET_PREPARED:
    case IJKMPET_PLAYBACK_STATE_CHANGED:
    case IJKMPET_BUFFERING_START:
    case IJKMPET_BUFFERING_END:
    case IJKMPET_BUFFERING_UPDATE:
    case IJKMPET_VIDEO_SIZE_CHANGED:
    case IJKMPET_VIDEO_RENDERING_START:
    case IJKMPET_AUDIO_RENDERING_START:
    case IJKMPET_ERROR:
    case IJKMPET_CURRENT_POSITION_UPDATE:
    case 600:
    case IJKMPET_VIDEO_ROTATION_CHANGED:
        [self handleEvent:what andArg1:arg1 andArg2:arg2 andExtra:extra];
        break;
    default:
        break;
    }
}

- (void)setOptions:(NSDictionary *)options {
    for (id cat in options) {
        NSDictionary *option = [options objectForKey:cat];
        for (NSString *key in option) {
            id optValue = [option objectForKey:key];
            if ([optValue isKindOfClass:[NSNumber class]]) {
                if ([cat intValue] == 0) {
                    [_hostOption setIntValue:optValue forKey:key];
                } else {
                    [_ijkMediaPlayer
                        setOptionIntValue:[optValue longLongValue]
                                   forKey:key
                               ofCategory:(IJKFFOptionCategory)[cat intValue]];
                }
            } else if ([optValue isKindOfClass:[NSString class]]) {
                if ([cat intValue] == 0) {
                    [_hostOption setStrValue:optValue forKey:key];
                } else {
                    [_ijkMediaPlayer
                        setOptionValue:optValue
                                forKey:key
                            ofCategory:(IJKFFOptionCategory)[cat intValue]];
                }
            }
        }
    }
}

- (void) takeSnapshot{
    
    [_ijkMediaPlayer takeSnapshot:^(UIImage * _Nullable image, NSError * _Nullable error) {
        if (image != nil) {
            NSDictionary *args = @{@"data":UIImageJPEGRepresentation(image, 1.0), @"w": @(image.size.width), @"h": @(image.size.height)};
            [self->_methodChannel invokeMethod:@"_onSnapshot" arguments:args];
        } else {
            [self->_methodChannel invokeMethod:@"_onSnapshot" arguments:@"snapshot error"];
        }
    }];
}

- (void)startRecordingWithPath:(NSString *)path result:(FlutterResult)result {
    if (_isRecording) {
        result([FlutterError errorWithCode:@"RECORDING_IN_PROGRESS"
                                   message:@"Recording is already in progress"
                                   details:nil]);
        return;
    }
    
    if (!path || [path length] == 0) {
        result([FlutterError errorWithCode:@"INVALID_PATH"
                                   message:@"Recording path cannot be null or empty"
                                   details:nil]);
        return;
    }
    
    // Remove existing file if it exists
    NSFileManager *fileManager = [NSFileManager defaultManager];
    if ([fileManager fileExistsAtPath:path]) {
        NSError *error;
        [fileManager removeItemAtPath:path error:&error];
        if (error) {
            result([FlutterError errorWithCode:@"FILE_ERROR"
                                       message:[NSString stringWithFormat:@"Failed to remove existing file: %@", error.localizedDescription]
                                       details:nil]);
            return;
        }
    }
    
    NSURL *outputURL = [NSURL fileURLWithPath:path];
    NSError *error;
    
    // Create asset writer
    _assetWriter = [[AVAssetWriter alloc] initWithURL:outputURL
                                             fileType:AVFileTypeMPEG4
                                                error:&error];
    
    if (error || !_assetWriter) {
        result([FlutterError errorWithCode:@"WRITER_CREATION_FAILED"
                                   message:[NSString stringWithFormat:@"Failed to create asset writer: %@", error.localizedDescription]
                                   details:nil]);
        return;
    }
    
    // Configure video settings with higher quality
    NSDictionary *videoSettings = @{
        AVVideoCodecKey: AVVideoCodecTypeH264,
        AVVideoWidthKey: @(_width > 0 ? _width : 1280),
        AVVideoHeightKey: @(_height > 0 ? _height : 720),
        AVVideoCompressionPropertiesKey: @{
            AVVideoAverageBitRateKey: @(5000000), // 5Mbps for much better quality
            AVVideoMaxKeyFrameIntervalKey: @(30), // Keyframe every 30 frames
            AVVideoProfileLevelKey: AVVideoProfileLevelH264HighAutoLevel, // High profile for better compression
            AVVideoH264EntropyModeKey: AVVideoH264EntropyModeCABAC, // Better entropy coding
            AVVideoExpectedSourceFrameRateKey: @(30), // 30 fps
        }
    };
    
    // Create writer input
    _writerInput = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo
                                                      outputSettings:videoSettings];
    _writerInput.expectsMediaDataInRealTime = YES;
    
    // Create pixel buffer adaptor
    NSDictionary *pixelBufferAttributes = @{
        (NSString *)kCVPixelBufferPixelFormatTypeKey: @(kCVPixelFormatType_32BGRA),
        (NSString *)kCVPixelBufferWidthKey: @(_width > 0 ? _width : 1280),
        (NSString *)kCVPixelBufferHeightKey: @(_height > 0 ? _height : 720),
    };
    
    _pixelBufferAdaptor = [AVAssetWriterInputPixelBufferAdaptor
                          assetWriterInputPixelBufferAdaptorWithAssetWriterInput:_writerInput
                                                          sourcePixelBufferAttributes:pixelBufferAttributes];
    
    if ([_assetWriter canAddInput:_writerInput]) {
        [_assetWriter addInput:_writerInput];
    } else {
        result([FlutterError errorWithCode:@"INPUT_NOT_SUPPORTED"
                                   message:@"Cannot add input to asset writer"
                                   details:nil]);
        _assetWriter = nil;
        _writerInput = nil;
        _pixelBufferAdaptor = nil;
        return;
    }
    
    // Start writing
    if ([_assetWriter startWriting]) {
        // Reset timing variables for each new recording
        _hasFirstFrame = NO;
        _firstFrameTime = kCMTimeInvalid;
        _isRecording = YES;
        _recordingPath = path;
        
        NSLog(@"Recording started to path: %@", path);
        
        // Notify Dart that recording started
        [_methodChannel invokeMethod:@"_onRecordingStarted" arguments:nil];
        result(nil);
    } else {
        NSLog(@"Failed to start asset writer: %@", _assetWriter.error.localizedDescription);
        result([FlutterError errorWithCode:@"START_WRITING_FAILED"
                                   message:[NSString stringWithFormat:@"Failed to start writing: %@", _assetWriter.error.localizedDescription]
                                   details:nil]);
        
        // Clean up on failure
        _assetWriter = nil;
        _writerInput = nil;
        _pixelBufferAdaptor = nil;
        _isRecording = NO;
    }
}

- (void)stopRecordingWithResult:(FlutterResult)result {
    if (!_isRecording || !_assetWriter) {
        result([FlutterError errorWithCode:@"NO_RECORDING"
                                   message:@"No recording in progress"
                                   details:nil]);
        return;
    }
    
    NSLog(@"Stopping recording...");
    _isRecording = NO;
    
    [_writerInput markAsFinished];
    [_assetWriter finishWritingWithCompletionHandler:^{
        dispatch_async(dispatch_get_main_queue(), ^{
            if (self->_assetWriter.status == AVAssetWriterStatusCompleted) {
                NSLog(@"Recording completed successfully to: %@", self->_recordingPath);
                
                // Log file info for debugging
                NSError *error;
                NSDictionary *fileAttributes = [[NSFileManager defaultManager] 
                    attributesOfItemAtPath:self->_recordingPath error:&error];
                if (fileAttributes) {
                    NSNumber *fileSize = [fileAttributes objectForKey:NSFileSize];
                    NSLog(@"Recorded file size: %@ bytes", fileSize);
                }
                
                // Notify Dart that recording stopped
                [self->_methodChannel invokeMethod:@"_onRecordingStopped" arguments:self->_recordingPath];
                result(nil);
            } else {
                NSString *errorMessage = self->_assetWriter.error ? 
                    self->_assetWriter.error.localizedDescription : @"Unknown error";
                NSLog(@"Recording failed with error: %@", errorMessage);
                [self->_methodChannel invokeMethod:@"_onRecordingError" arguments:errorMessage];
                result([FlutterError errorWithCode:@"FINISH_WRITING_FAILED"
                                           message:[NSString stringWithFormat:@"Failed to finish writing: %@", errorMessage]
                                           details:nil]);
            }
            
            // Clean up - ensure all variables are reset
            self->_assetWriter = nil;
            self->_writerInput = nil;
            self->_pixelBufferAdaptor = nil;
            self->_recordingPath = nil;
            self->_hasFirstFrame = NO;
            self->_firstFrameTime = kCMTimeInvalid;
        });
    }];
}

- (void)handleMethodCall:(FlutterMethodCall *)call
                  result:(FlutterResult)result {

    NSDictionary *argsMap = call.arguments;
    if ([@"setupSurface" isEqualToString:call.method]) {
        result([self setupSurface]);
    } else if ([@"setOption" isEqualToString:call.method]) {
        int category = [argsMap[@"cat"] intValue];
        NSString *key = argsMap[@"key"];
        if (argsMap[@"long"] != nil) {
            int64_t value = [argsMap[@"long"] longLongValue];
            if (category == 0) {
                [_hostOption setIntValue:argsMap[@"long"] forKey:key];
            } else {
                [_ijkMediaPlayer
                    setOptionIntValue:value
                               forKey:key
                           ofCategory:(IJKFFOptionCategory)category];
            }
        } else if (argsMap[@"str"] != nil) {
            NSString *value = argsMap[@"str"];
            if (category == 0) {
                [_hostOption setStrValue:value forKey:key];
            } else {
                [_ijkMediaPlayer setOptionValue:value
                                         forKey:key
                                     ofCategory:(IJKFFOptionCategory)category];
            }
        } else {
            NSLog(@"FIJKPLAYER: error arguments for setOptions");
        }
        result(nil);
    } else if ([@"applyOptions" isEqualToString:call.method]) {
        [self setOptions:argsMap];
        result(nil);
    } else if ([@"setDataSource" isEqualToString:call.method]) {
        NSString *url = argsMap[@"url"];
        NSURL *aUrl =
            [NSURL URLWithString:[url stringByAddingPercentEscapesUsingEncoding:
                                          NSUTF8StringEncoding]];
        bool file404 = false;
        if ([@"asset" isEqualToString:aUrl.scheme]) {
            NSString *host = aUrl.host;
            NSString *asset = [host length] == 0
                                  ? [_registrar lookupKeyForAsset:aUrl.path]
                                  : [_registrar lookupKeyForAsset:aUrl.path
                                                      fromPackage:host];
            if ([asset length] > 0) {
                NSString *path = [[NSBundle mainBundle] pathForResource:asset
                                                                 ofType:nil];
                if ([path length] > 0)
                    url = path;
            }
            if ([url isEqualToString:argsMap[@"url"]]) {
                file404 = true;
            }
        } else if ([@"file" isEqualToString:aUrl.scheme] ||
                   [aUrl.scheme length] == 0) {
            NSFileManager *fileManager = [[NSFileManager alloc] init];
            if (![fileManager fileExistsAtPath:aUrl.path]) {
                file404 = true;
            }
        }
        if (file404) {
            result([FlutterError errorWithCode:@"-875574348"
                                       message:[@"Local File not found:"
                                                   stringByAppendingString:url]
                                       details:nil]);
        } else {
            [_ijkMediaPlayer setDataSource:url];
            [self handleEvent:IJKMPET_PLAYBACK_STATE_CHANGED
                      andArg1:initialized
                      andArg2:-1
                     andExtra:nil];
            result(nil);
        }
    } else if ([@"prepareAsync" isEqualToString:call.method]) {
        [self setup];
        [_ijkMediaPlayer prepareAsync];
        [self handleEvent:IJKMPET_PLAYBACK_STATE_CHANGED
                  andArg1:asyncPreparing
                  andArg2:-1
                 andExtra:nil];
        result(nil);
    } else if ([@"start" isEqualToString:call.method]) {
        int ret = [_ijkMediaPlayer start];
        NSLog(@"start start %d", ret);
        result(nil);
    } else if ([@"pause" isEqualToString:call.method]) {
        [_ijkMediaPlayer pause];
        result(nil);
    } else if ([@"stop" isEqualToString:call.method]) {
        [_ijkMediaPlayer stop];
        [self handleEvent:IJKMPET_PLAYBACK_STATE_CHANGED
                  andArg1:stopped
                  andArg2:-1
                 andExtra:nil];
        result(nil);
    } else if ([@"reset" isEqualToString:call.method]) {
        [_ijkMediaPlayer reset];
        [self handleEvent:IJKMPET_PLAYBACK_STATE_CHANGED
                  andArg1:idle
                  andArg2:-1
                 andExtra:nil];
        result(nil);
    } else if ([@"getCurrentPosition" isEqualToString:call.method]) {
        long pos = [_ijkMediaPlayer getCurrentPosition];
        result(@(pos));
    } else if ([@"setVolume" isEqualToString:call.method]) {
        double volume = [argsMap[@"volume"] doubleValue];
        [_ijkMediaPlayer setPlaybackVolume:(float)volume];
        result(nil);
    } else if ([@"seekTo" isEqualToString:call.method]) {
        long pos = [argsMap[@"msec"] longValue];
        if (_state == completed)
            [self handleEvent:IJKMPET_PLAYBACK_STATE_CHANGED
                      andArg1:paused
                      andArg2:-1
                     andExtra:nil];
        [_ijkMediaPlayer seekTo:pos];
        result(nil);
    } else if ([@"setLoop" isEqualToString:call.method]) {
        int loopCount = [argsMap[@"loop"] intValue];
        [_ijkMediaPlayer setLoop:loopCount];
        result(nil);
    } else if ([@"setSpeed" isEqualToString:call.method]) {
        float speed = [argsMap[@"speed"] doubleValue];
        [_ijkMediaPlayer setSpeed:speed];
        result(nil);
    } else if ([@"snapshot" isEqualToString:call.method]) {
        [self takeSnapshot];
        result(nil);
    } else if ([@"startRecording" isEqualToString:call.method]) {
        NSString *path = argsMap[@"path"];
        [self startRecordingWithPath:path result:result];
    } else if ([@"stopRecording" isEqualToString:call.method]) {
        [self stopRecordingWithResult:result];
    } else {
        result(FlutterMethodNotImplemented);
    }
}

@end
