// MIT License - FijkPlayer macOS Player Implementation
//
// macOS player wrapping FJKNativePlayer (shared with iOS).
// Uses FlutterTexture for video rendering via CVPixelBuffer.

#import <stdatomic.h>
#import "FijkPlayer.h"
#import "FJKNativePlayer.h"
#import "FJKPixelBufferRenderer.h"
#import "FijkHostOption.h"
#import "FijkPlugin.h"
#import "FijkQueuingEventSink.h"
#import "FFmpegRecorder.h"

#import <FlutterMacOS/FlutterMacOS.h>
#import <Foundation/Foundation.h>
#import <CoreImage/CoreImage.h>
#import <ImageIO/ImageIO.h>
#import <libkern/OSAtomic.h>
#import <stdatomic.h>

@interface FijkPlugin ()
- (void)onPlayingChange:(int)delta;
- (void)onPlayableChange:(int)delta;
@end

static atomic_int atomicId = 0;

@implementation FijkPlayer {
    // Native FFmpeg player (shared with iOS)
    FJKNativePlayer *_nativePlayer;
    FJKPixelBufferRenderer *_renderer;
    
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
    
    NSString *_dataSource;
}

static const int idle = 0;
static const int initialized = 1;
static const int asyncPreparing = 2;
static const int prepared = 3;
static const int started = 4;
static const int paused = 5;
static const int completed = 6;
static const int stopped = 7;
static const int error = 8;
static const int end = 9;

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
        _textureRegistry = [registrar textures];
        int pid = atomic_fetch_add(&atomicId, 1);
        _playerId = @(pid);
        _pid = pid;
        _eventSink = [[FijkQueuingEventSink alloc] init];
        _latestPixelBuffer = nil;
        _vid = -1;
        _rotate = -1;
        _state = idle;

        _hostOption = [[FijkHostOption alloc] init];
        _lastBuffer = nil;
        
        // Create native player (same as iOS)
        _nativePlayer = [[FJKNativePlayer alloc] init];
        
        // Set up event callback
        __weak typeof(self) weakSelf = self;
        _nativePlayer.eventCallback = ^(FJKPlayerEvent event, NSInteger arg1, NSInteger arg2) {
            [weakSelf handleNativePlayerEvent:event arg1:arg1 arg2:arg2];
        };
        
        // Set up frame callback to notify texture when new frames are available
        _nativePlayer.frameCallback = ^{
            typeof(self) strongSelf = weakSelf;
            if (strongSelf && strongSelf->_vid >= 0) {
                [strongSelf->_textureRegistry textureFrameAvailable:strongSelf->_vid];
            }
        };

        // Setup method channel
        _methodChannel = [FlutterMethodChannel
            methodChannelWithName:[@"befovy.com/fijkplayer/"
                                      stringByAppendingString:[_playerId stringValue]]
                  binaryMessenger:[registrar messenger]];

        __block typeof(self) blockSelf = self;
        [_methodChannel setMethodCallHandler:^(FlutterMethodCall *call,
                                               FlutterResult result) {
          [blockSelf handleMethodCall:call result:result];
        }];

        // Setup event channel
        _eventChannel = [FlutterEventChannel
            eventChannelWithName:[@"befovy.com/fijkplayer/event/"
                                     stringByAppendingString:[_playerId stringValue]]
                 binaryMessenger:[registrar messenger]];
        [_eventChannel setStreamHandler:self];
    }
    return self;
}

- (void)shutdown {
    [self notifyState:end];
    
    if (_nativePlayer) {
        [_nativePlayer cleanup];
        _nativePlayer = nil;
    }
    
    if (_renderer) {
        [_renderer cleanup];
        _renderer = nil;
    }
    
    if (_vid >= 0) {
        [_textureRegistry unregisterTexture:_vid];
        _vid = -1;
        _textureRegistry = nil;
    }

    CVPixelBufferRef old = _latestPixelBuffer;
    while (!atomic_compare_exchange_weak((_Atomic(void *)*)&_latestPixelBuffer, (void **)&old, nil)) {
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

// MARK: - FlutterTexture Protocol

- (CVPixelBufferRef _Nullable)copyPixelBuffer {
    if (!_nativePlayer) return nil;
    
    CVPixelBufferRef pixelBuffer = [_nativePlayer copyPixelBuffer];
    return pixelBuffer;
}

// MARK: - Event Handling

- (void)handleNativePlayerEvent:(FJKPlayerEvent)event arg1:(NSInteger)arg1 arg2:(NSInteger)arg2 {
    switch (event) {
        case FJKPlayerEventPrepared:
            [self notifyState:prepared];
            _state = prepared;
            if (_nativePlayer.videoSize.width > 0) {
                _width = (int)_nativePlayer.videoSize.width;
                _height = (int)_nativePlayer.videoSize.height;
            }
            // Send 'prepared' event with duration so Dart side gets the duration value
            {
                NSInteger durationMs = (NSInteger)(_nativePlayer.duration);
                NSDictionary *preparedEvent = @{
                    @"event": @"prepared",
                    @"duration": @(durationMs)
                };
                [_eventSink success:preparedEvent];
            }
            break;
            
        case FJKPlayerEventStarted:
            [self notifyState:started];
            _state = started;
            [[FijkPlugin singleInstance] onPlayingChange:1];
            break;
            
        case FJKPlayerEventPaused:
            [self notifyState:paused];
            _state = paused;
            [[FijkPlugin singleInstance] onPlayingChange:-1];
            break;
            
        case FJKPlayerEventCompleted:
            [self notifyState:completed];
            _state = completed;
            [[FijkPlugin singleInstance] onPlayingChange:-1];
            break;
            
        case FJKPlayerEventError:
            [self notifyState:error];  // Notify Flutter of state change BEFORE updating _state
            _state = error;
            [self notifyError:(int)arg1 extra:@(arg2)];
            break;
            
        case FJKPlayerEventVideoSizeChanged:
            _width = (int)arg1;
            _height = (int)arg2;
            [self notifyVideoSize];
            break;
            
        case FJKPlayerEventSeekComplete:
            [self notifySeekComplete];
            break;
            
        default:
            break;
    }
}

- (void)notifyState:(int)newState {
    if (_state == newState) {
        return;
    }
    
    int oldState = _state;
    NSDictionary *event = @{
        @"event": @"state_change",
        @"new": @(newState),
        @"old": @(oldState)
    };
    [_eventSink success:event];
}

- (void)notifyError:(int)code extra:(id)extra {
    NSDictionary *event = @{
        @"event": @"error",
        @"code": @(code),
        @"message": extra ?: @"Unknown error"
    };
    [_eventSink success:event];
}

- (void)notifyVideoSize {
    NSDictionary *event = @{
        @"event": @"size_changed",
        @"width": @(_width),
        @"height": @(_height)
    };
    [_eventSink success:event];
}

- (void)notifySeekComplete {
    NSDictionary *event = @{
        @"event": @"seek_complete"
    };
    [_eventSink success:event];
}

// MARK: - FlutterStreamHandler

- (FlutterError *_Nullable)onCancelWithArguments:(id _Nullable)arguments {
    [_eventSink setDelegate:nil];
    return nil;
}

- (FlutterError *_Nullable)onListenWithArguments:(id _Nullable)arguments
                                       eventSink:(nonnull FlutterEventSink)events {
    [_eventSink setDelegate:events];
    
    // Send cached video size immediately if available
    if (_width > 0 && _height > 0) {
        NSDictionary *sizeEvent = @{
            @"event": @"size_changed",
            @"width": @(_width),
            @"height": @(_height)
        };
        [_eventSink success:sizeEvent];
    }
    
    return nil;
}

// MARK: - Method Channel Handler

- (void)handleMethodCall:(FlutterMethodCall *)call result:(FlutterResult)result {
    if ([@"setDataSource" isEqualToString:call.method]) {
        NSString *url = call.arguments[@"url"];
        _dataSource = [url copy];
        int ret = [_nativePlayer setDataSource:url];
        if (ret == 0) {
            [self notifyState:initialized];
            _state = initialized;
        }
        result(@(ret));
        
    } else if ([@"prepareAsync" isEqualToString:call.method]) {
        if (_vid < 0) {
            _vid = [_textureRegistry registerTexture:self];
        }
        [self notifyState:asyncPreparing];
        _state = asyncPreparing;
        [_nativePlayer prepareAsync];
        result(@{@"id": @(_vid)});
        
    } else if ([@"start" isEqualToString:call.method]) {
        int ret = [_nativePlayer start];
        result(@(ret));
        
    } else if ([@"pause" isEqualToString:call.method]) {
        [_nativePlayer pause];
        result(@(0));
        
    } else if ([@"stop" isEqualToString:call.method]) {
        [_nativePlayer stop];
        [self notifyState:stopped];
        _state = stopped;
        result(@(0));
        
    } else if ([@"reset" isEqualToString:call.method]) {
        [_nativePlayer cleanup];
        [self notifyState:idle];
        _state = idle;
        result(@(0));
        
    } else if ([@"getCurrentPosition" isEqualToString:call.method]) {
        result(@(_nativePlayer.currentPosition));
        
    } else if ([@"getDuration" isEqualToString:call.method]) {
        result(@(_nativePlayer.duration));
        
    } else if ([@"seekTo" isEqualToString:call.method]) {
        NSNumber *msec = call.arguments[@"msec"];
        [_nativePlayer seekTo:[msec longLongValue]];
        result(@(0));
        
    } else if ([@"setVolume" isEqualToString:call.method]) {
        NSNumber *volume = call.arguments[@"volume"];
        if (volume && _nativePlayer) {
            [_nativePlayer setVolume:[volume floatValue]];
        }
        result(@(0));
        
    } else if ([@"setSpeed" isEqualToString:call.method]) {
        result(@(0));
        
    } else if ([@"setLoop" isEqualToString:call.method]) {
        result(@(0));
        
    } else if ([@"setOption" isEqualToString:call.method]) {
        result(@(0));
        
    } else if ([@"applyOptions" isEqualToString:call.method]) {
        result(@(0));
        
    } else if ([@"setPlaybackMode" isEqualToString:call.method]) {
        NSNumber *mode = call.arguments[@"mode"];
        NSNumber *bufferMs = call.arguments[@"bufferMs"];
        NSNumber *enableAudio = call.arguments[@"enableAudio"];
        NSNumber *maxLatencyMs = call.arguments[@"maxLatencyMs"];
        NSNumber *enableFrameDrop = call.arguments[@"enableFrameDrop"];
        
        int ret = [_nativePlayer setPlaybackMode:[mode integerValue]
                                        bufferMs:[bufferMs integerValue]
                                     enableAudio:[enableAudio integerValue]
                                   maxLatencyMs:[maxLatencyMs integerValue]
                                 enableFrameDrop:[enableFrameDrop integerValue]];
        result(@(ret));
        
    } else if ([@"setupSurface" isEqualToString:call.method]) {
        result(@(_vid));
        
    } else if ([@"startFFmpegRecording" isEqualToString:call.method]) {
        NSString *path = call.arguments[@"path"];
        if (!_dataSource || _dataSource.length == 0) {
            result([FlutterError errorWithCode:@"NO_DATA_SOURCE"
                                       message:@"No data source set for recording"
                                       details:nil]);
            return;
        }
        NSError *err = nil;
        BOOL ok = [[FFmpegRecorder sharedInstance] startRecordingWithRtspUrl:_dataSource
                                                                 outputPath:path
                                                                      error:&err];
        if (ok) {
            [_methodChannel invokeMethod:@"_onRecordingStarted" arguments:nil];
            result(@(0));
        } else {
            NSString *msg = err ? err.localizedDescription : @"Failed to start recording";
            [_methodChannel invokeMethod:@"_onRecordingError" arguments:msg];
            result([FlutterError errorWithCode:@"RECORDING_FAILED" message:msg details:nil]);
        }
        
    } else if ([@"stopFFmpegRecording" isEqualToString:call.method]) {
        NSError *err = nil;
        [[FFmpegRecorder sharedInstance] stopRecordingWithError:&err];
        if (err) {
            result([FlutterError errorWithCode:@"STOP_RECORDING_FAILED"
                                       message:err.localizedDescription
                                       details:nil]);
        } else {
            result(@(0));
        }
        
    } else if ([@"isFFmpegRecording" isEqualToString:call.method]) {
        BOOL recording = [[FFmpegRecorder sharedInstance] isRecording];
        result(@(recording));
        
    } else if ([@"startRecording" isEqualToString:call.method]) {
        NSString *path = call.arguments[@"path"];
        if (!_dataSource || _dataSource.length == 0) {
            result([FlutterError errorWithCode:@"NO_DATA_SOURCE"
                                       message:@"No data source set for recording"
                                       details:nil]);
            return;
        }
        NSError *err = nil;
        BOOL ok = [[FFmpegRecorder sharedInstance] startRecordingWithRtspUrl:_dataSource
                                                                 outputPath:path
                                                                      error:&err];
        if (ok) {
            [_methodChannel invokeMethod:@"_onRecordingStarted" arguments:nil];
            result(@(0));
        } else {
            NSString *msg = err ? err.localizedDescription : @"Failed to start recording";
            [_methodChannel invokeMethod:@"_onRecordingError" arguments:msg];
            result([FlutterError errorWithCode:@"RECORDING_FAILED" message:msg details:nil]);
        }
        
    } else if ([@"stopRecording" isEqualToString:call.method]) {
        NSError *err = nil;
        [[FFmpegRecorder sharedInstance] stopRecordingWithError:&err];
        if (err) {
            result([FlutterError errorWithCode:@"STOP_RECORDING_FAILED"
                                       message:err.localizedDescription
                                       details:nil]);
        } else {
            result(@(0));
        }
        
    } else if ([@"release" isEqualToString:call.method]) {
        [self shutdown];
        result(@(0));
        
    } else if ([@"snapshot" isEqualToString:call.method]) {
        CVPixelBufferRef pixelBuffer = [_nativePlayer copyPixelBuffer];
        if (!pixelBuffer) {
            [_methodChannel invokeMethod:@"_onSnapshot" arguments:@{@"data": [NSNull null]}];
            result(nil);
            return;
        }
        
        CIImage *ciImage = [CIImage imageWithCVPixelBuffer:pixelBuffer];
        CIContext *context = [CIContext context];
        CGImageRef cgImage = [context createCGImage:ciImage fromRect:ciImage.extent];
        CVPixelBufferRelease(pixelBuffer);
        
        if (!cgImage) {
            [_methodChannel invokeMethod:@"_onSnapshot" arguments:@{@"data": [NSNull null]}];
            result(nil);
            return;
        }
        
        NSMutableData *pngData = [NSMutableData data];
        CGImageDestinationRef dest = CGImageDestinationCreateWithData(
            (__bridge CFMutableDataRef)pngData, kUTTypePNG, 1, NULL);
        if (dest) {
            CGImageDestinationAddImage(dest, cgImage, nil);
            CGImageDestinationFinalize(dest);
            CFRelease(dest);
        }
        CGImageRelease(cgImage);
        
        if (pngData.length > 0) {
            FlutterStandardTypedData *typedData =
                [FlutterStandardTypedData typedDataWithBytes:pngData];
            [_methodChannel invokeMethod:@"_onSnapshot" arguments:@{@"data": typedData}];
        } else {
            [_methodChannel invokeMethod:@"_onSnapshot" arguments:@{@"data": [NSNull null]}];
        }
        result(nil);
        
    } else {
        result(FlutterMethodNotImplemented);
    }
}

@end
