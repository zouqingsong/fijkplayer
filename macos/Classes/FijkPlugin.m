// MIT License - FijkPlayer macOS Plugin Implementation
//
// macOS desktop platform support for fijkplayer.
// Shares NativePlayer code with iOS (FFmpeg demuxer, VideoToolbox decoder,
// CVPixelBuffer renderer). No UIKit dependencies.

#import "FijkPlugin.h"
#import "FijkPlayer.h"
#import "FijkQueuingEventSink.h"
#import "FijkRecorderHandler.h"
#import "FijkFFmpegKitHandler.h"

#import <FlutterMacOS/FlutterMacOS.h>
#import <Foundation/Foundation.h>

@implementation FijkPlugin {
    NSObject<FlutterPluginRegistrar> *_registrar;
    NSMutableDictionary<NSNumber *, FijkPlayer *> *_fijkPlayers;

    FijkQueuingEventSink *_eventSink;
    FlutterEventChannel *_eventChannel;

    int _playingCnt;
    int _playableCnt;
}

static FijkPlugin *_instance = nil;

+ (void)registerWithRegistrar:(NSObject<FlutterPluginRegistrar> *)registrar {
    // Register main production channel
    FlutterMethodChannel *channel =
        [FlutterMethodChannel methodChannelWithName:@"befovy.com/fijk"
                                    binaryMessenger:[registrar messenger]];
    FijkPlugin *instance = [[FijkPlugin alloc] initWithRegistrar:registrar];
    _instance = instance;
    [registrar addMethodCallDelegate:instance channel:channel];

    // Register texture system
    FijkPlayer *player = [[FijkPlayer alloc] initJustTexture];
    int64_t vid = [[registrar textures] registerTexture:player];
    [player shutdown];
    [[registrar textures] unregisterTexture:vid];
    
    // Register recorder channel
    FlutterMethodChannel *recorderChannel =
        [FlutterMethodChannel methodChannelWithName:@"befovy.com/fijk/recorder"
                                    binaryMessenger:[registrar messenger]];
    FijkRecorderHandler *recorderHandler = [[FijkRecorderHandler alloc] init];
    [recorderChannel setMethodCallHandler:^(FlutterMethodCall * _Nonnull call, FlutterResult  _Nonnull result) {
        [recorderHandler handleMethodCall:call result:result];
    }];
    
    // Register FFmpegKit channel
    FlutterMethodChannel *ffmpegKitChannel =
        [FlutterMethodChannel methodChannelWithName:@"befovy.com/fijk/ffmpeg_kit"
                                    binaryMessenger:[registrar messenger]];
    FijkFFmpegKitHandler *ffmpegKitHandler = [[FijkFFmpegKitHandler alloc] init];
    [ffmpegKitChannel setMethodCallHandler:^(FlutterMethodCall * _Nonnull call, FlutterResult  _Nonnull result) {
        [ffmpegKitHandler handleMethodCall:call result:result];
    }];
    
    NSLog(@"✅ FijkPlugin (macOS) registered channels:");
    NSLog(@"  - Production: befovy.com/fijk");
    NSLog(@"  - Recorder: befovy.com/fijk/recorder");
    NSLog(@"  - FFmpegKit: befovy.com/fijk/ffmpeg_kit");
}

+ (FijkPlugin *)singleInstance {
    return _instance;
}

- (instancetype)initWithRegistrar:
    (NSObject<FlutterPluginRegistrar> *)registrar {
    self = [super init];
    if (self) {
        _registrar = registrar;
        _fijkPlayers = [[NSMutableDictionary alloc] init];
        _playingCnt = 0;
        _playableCnt = 0;
        _eventSink = [[FijkQueuingEventSink alloc] init];

        _eventChannel =
            [FlutterEventChannel eventChannelWithName:@"befovy.com/fijk/event"
                                      binaryMessenger:[registrar messenger]];
        [_eventChannel setStreamHandler:self];
    }
    return self;
}

- (void)handleMethodCall:(FlutterMethodCall *)call
                  result:(FlutterResult)result {

    NSDictionary *argsMap = call.arguments;
    if ([@"getPlatformVersion" isEqualToString:call.method]) {
        NSProcessInfo *pInfo = [NSProcessInfo processInfo];
        result([@"macOS " stringByAppendingString:[pInfo operatingSystemVersionString]]);
    } else if ([@"init" isEqualToString:call.method]) {
        result(NULL);
    } else if ([@"createPlayer" isEqualToString:call.method]) {
        FijkPlayer *fijkplayer =
            [[FijkPlayer alloc] initWithRegistrar:_registrar];
        NSNumber *playerId = fijkplayer.playerId;
        _fijkPlayers[playerId] = fijkplayer;
        result(playerId);
    } else if ([@"releasePlayer" isEqualToString:call.method]) {
        NSNumber *pid = argsMap[@"pid"];
        FijkPlayer *fijkPlayer = [_fijkPlayers objectForKey:pid];
        [fijkPlayer shutdown];
        if (fijkPlayer != nil) {
            [_fijkPlayers removeObjectForKey:pid];
        }
        result(nil);
    } else if ([@"logLevel" isEqualToString:call.method]) {
        // Log level control
        result(nil);
    } else if ([@"setOrientationPortrait" isEqualToString:call.method]) {
        // Not applicable on macOS
        result(@(NO));
    } else if ([@"setOrientationLandscape" isEqualToString:call.method]) {
        // Not applicable on macOS
        result(@(NO));
    } else if ([@"setOrientationAuto" isEqualToString:call.method]) {
        // Not applicable on macOS
        result(@(NO));
    } else if ([@"isScreenKeptOn" isEqualToString:call.method]) {
        result(@(NO));
    } else if ([@"setScreenOn" isEqualToString:call.method]) {
        // TODO: Implement NSProcessInfo activity for preventing sleep
        result(nil);
    } else if ([@"volUp" isEqualToString:call.method]) {
        // Not implemented on macOS (system volume)
        result(nil);
    } else if ([@"volDown" isEqualToString:call.method]) {
        // Not implemented on macOS (system volume)
        result(nil);
    } else if ([@"volMute" isEqualToString:call.method]) {
        // Not implemented on macOS (system volume)
        result(nil);
    } else {
        result(FlutterMethodNotImplemented);
    }
}

#pragma mark - FlutterStreamHandler

- (FlutterError *_Nullable)onCancelWithArguments:(id _Nullable)arguments {
    [_eventSink setDelegate:nil];
    return nil;
}

- (FlutterError *_Nullable)onListenWithArguments:(id _Nullable)arguments
                                       eventSink:(nonnull FlutterEventSink)events {
    [_eventSink setDelegate:events];
    return nil;
}

#pragma mark - State Management

- (void)onPlayingChange:(int)delta {
    _playingCnt += delta;
}

- (void)onPlayableChange:(int)delta {
    _playableCnt += delta;
}

@end
