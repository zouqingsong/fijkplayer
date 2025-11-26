// MIT License - FijkPlayer with Native FFmpeg Backend
//
// This replaces IJKMediaPlayer (BIJKPlayer) with FJKNativePlayer
// maintaining the same API for backward compatibility

#import "FijkPlayer.h"
#import "FJKNativePlayer.h"
#import "FJKPixelBufferRenderer.h"
#import "FijkHostOption.h"
#import "FijkPlugin.h"
#import "FijkQueuingEventSink.h"

#import <Flutter/Flutter.h>
#import <Foundation/Foundation.h>
#import <libkern/OSAtomic.h>
#import <stdatomic.h>

@interface FijkPlugin ()
- (void)onPlayingChange:(int)delta;
- (void)onPlayableChange:(int)delta;
- (void)setScreenOn:(BOOL)on;
@end

static atomic_int atomicId = 0;

@implementation FijkPlayer {
    // Native FFmpeg player (replaces IJKFFMediaPlayer)
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
        
        // Create native player
        _nativePlayer = [[FJKNativePlayer alloc] init];
        
        // Set up event callback
        __weak typeof(self) weakSelf = self;
        _nativePlayer.eventCallback = ^(FJKPlayerEvent event, NSInteger arg1, NSInteger arg2) {
            [weakSelf handleNativePlayerEvent:event arg1:arg1 arg2:arg2];
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
        
        NSLog(@"[FijkPlayer] Initialized with native FFmpeg backend (player %d)", _pid);
    }
    return self;
}

- (void)setup {
    // Native player doesn't need special setup
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
    
    NSLog(@"[FijkPlayer] Shutdown complete (player %d)", _pid);
}

// MARK: - FlutterTexture Protocol

- (CVPixelBufferRef _Nullable)copyPixelBuffer {
    if (!_nativePlayer) return nil;
    
    CVPixelBufferRef pixelBuffer = [_nativePlayer copyPixelBuffer];
    if (pixelBuffer) {
        [_textureRegistry textureFrameAvailable:_vid];
    }
    return pixelBuffer;
}

// MARK: - Event Handling

- (void)handleNativePlayerEvent:(FJKPlayerEvent)event arg1:(NSInteger)arg1 arg2:(NSInteger)arg2 {
    switch (event) {
        case FJKPlayerEventPrepared:
            _state = prepared;
            if (_nativePlayer.videoSize.width > 0) {
                _width = (int)_nativePlayer.videoSize.width;
                _height = (int)_nativePlayer.videoSize.height;
            }
            [self notifyState:prepared];
            break;
            
        case FJKPlayerEventStarted:
            _state = started;
            [self notifyState:started];
            [[FijkPlugin singleInstance] onPlayingChange:1];
            break;
            
        case FJKPlayerEventPaused:
            _state = paused;
            [self notifyState:paused];
            [[FijkPlugin singleInstance] onPlayingChange:-1];
            break;
            
        case FJKPlayerEventCompleted:
            _state = completed;
            [self notifyState:completed];
            [[FijkPlugin singleInstance] onPlayingChange:-1];
            break;
            
        case FJKPlayerEventError:
            _state = error;
            [self notifyError:arg1 extra:@(arg2)];
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
    NSDictionary *event = @{
        @"event": @"state_change",
        @"new": @(newState),
        @"old": @(_state)
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
        @"event": @"video_size",
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
    return nil;
}

// MARK: - Method Channel Handler

- (void)handleMethodCall:(FlutterMethodCall *)call result:(FlutterResult)result {
    if ([@"setDataSource" isEqualToString:call.method]) {
        NSString *url = call.arguments[@"url"];
        int ret = [_nativePlayer setDataSource:url];
        result(@(ret));
        
    } else if ([@"prepareAsync" isEqualToString:call.method]) {
        // Register texture
        if (_vid < 0) {
            _vid = [_textureRegistry registerTexture:self];
        }
        [_nativePlayer prepareAsync];
        _state = asyncPreparing;
        result(@{@"id": @(_vid)});
        
    } else if ([@"start" isEqualToString:call.method]) {
        int ret = [_nativePlayer start];
        result(@(ret));
        
    } else if ([@"pause" isEqualToString:call.method]) {
        [_nativePlayer pause];
        result(@(0));
        
    } else if ([@"stop" isEqualToString:call.method]) {
        [_nativePlayer stop];
        result(@(0));
        
    } else if ([@"reset" isEqualToString:call.method]) {
        [_nativePlayer stop];
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
        // TODO: Implement volume control
        result(@(0));
        
    } else if ([@"setSpeed" isEqualToString:call.method]) {
        // TODO: Implement speed control
        result(@(0));
        
    } else if ([@"setLoop" isEqualToString:call.method]) {
        // TODO: Implement loop
        result(@(0));
        
    } else if ([@"setOption" isEqualToString:call.method]) {
        // Options not applicable to native player
        result(@(0));
        
    } else if ([@"release" isEqualToString:call.method]) {
        [self shutdown];
        result(@(0));
        
    } else {
        result(FlutterMethodNotImplemented);
    }
}

@end
