// MIT License - FijkPlayer iOS Plugin Integration

#import "FJKNativePlayerHandler.h"

@implementation FJKPlayerTexture {
    FJKNativePlayer *_player;
    int64_t _textureId;
    id<FlutterTextureRegistry> _textureRegistry;
    CVPixelBufferRef _latestPixelBuffer;
    NSLock *_bufferLock;
}

- (instancetype)initWithPlayer:(FJKNativePlayer *)player 
                  textureRegistry:(id<FlutterTextureRegistry>)textureRegistry {
    self = [super init];
    if (self) {
        _player = player;
        _textureRegistry = textureRegistry;
        _bufferLock = [[NSLock alloc] init];
        
        // Register texture with Flutter
        _textureId = [textureRegistry registerTexture:self];
        
        // Set frame callback to notify Flutter when new frames are available
        FJKPlayerTexture *texture = self;  // Capture self without retain cycle in non-ARC
        [player setFrameCallback:^{
            if (texture && texture->_textureId >= 0) {
                [texture->_textureRegistry textureFrameAvailable:texture->_textureId];
            }
        }];
        
        NSLog(@"[FJKPlayerTexture] Texture registered: ID=%lld", _textureId);
    }
    return self;
}

- (void)dealloc {
    [self dispose];
}

- (void)dispose {
    if (_textureId >= 0) {
        [_textureRegistry unregisterTexture:_textureId];
        _textureId = -1;
    }
    
    [_bufferLock lock];
    if (_latestPixelBuffer) {
        CVPixelBufferRelease(_latestPixelBuffer);
        _latestPixelBuffer = NULL;
    }
    [_bufferLock unlock];
    
    NSLog(@"[FJKPlayerTexture] Disposed");
}

- (int64_t)textureId {
    return _textureId;
}

// FlutterTexture protocol
- (CVPixelBufferRef _Nullable)copyPixelBuffer {
    if (!_player) {
        return NULL;
    }
    
    // Get latest frame from player
    CVPixelBufferRef newBuffer = [_player copyPixelBuffer];
    
    if (newBuffer) {
        [_bufferLock lock];
        
        // Release old buffer
        if (_latestPixelBuffer) {
            CVPixelBufferRelease(_latestPixelBuffer);
        }
        
        // Store new buffer
        _latestPixelBuffer = newBuffer;
        
        // Return retained copy for Flutter
        CVPixelBufferRetain(_latestPixelBuffer);
        
        [_bufferLock unlock];
        
        return _latestPixelBuffer;
    }
    
    // Return previous buffer if no new frame
    [_bufferLock lock];
    if (_latestPixelBuffer) {
        CVPixelBufferRetain(_latestPixelBuffer);
    }
    CVPixelBufferRef buffer = _latestPixelBuffer;
    [_bufferLock unlock];
    
    return buffer;
}

- (void)onTextureUnregistered:(NSObject<FlutterTexture> *)texture {
    // Cleanup when Flutter unregisters texture
    [self dispose];
}

@end

@implementation FJKNativePlayerHandler {
    id<FlutterTextureRegistry> _textureRegistry;
    FJKNativePlayer *_player;
    FJKPlayerTexture *_texture;
    FlutterResult _prepareResult;
}

- (instancetype)initWithTextureRegistry:(id<FlutterTextureRegistry>)textureRegistry {
    self = [super init];
    if (self) {
        _textureRegistry = textureRegistry;
    }
    return self;
}

- (void)handleMethodCall:(FlutterMethodCall *)call result:(FlutterResult)result {
    NSString *method = call.method;
    NSLog(@"[FJKNativePlayerHandler] Method call: %@", method);
    
    if ([method isEqualToString:@"create"]) {
        [self handleCreate:result];
    } else if ([method isEqualToString:@"setDataSource"]) {
        [self handleSetDataSource:call result:result];
    } else if ([method isEqualToString:@"setPlaybackMode"]) {
        [self handleSetPlaybackMode:call result:result];
    } else if ([method isEqualToString:@"prepare"]) {
        [self handlePrepare:result];
    } else if ([method isEqualToString:@"start"]) {
        [self handleStart:result];
    } else if ([method isEqualToString:@"pause"]) {
        [self handlePause:result];
    } else if ([method isEqualToString:@"resume"]) {
        [self handleResume:result];
    } else if ([method isEqualToString:@"stop"]) {
        [self handleStop:result];
    } else if ([method isEqualToString:@"seekTo"]) {
        [self handleSeekTo:call result:result];
    } else if ([method isEqualToString:@"getPosition"]) {
        [self handleGetPosition:result];
    } else if ([method isEqualToString:@"getDuration"]) {
        [self handleGetDuration:result];
    } else if ([method isEqualToString:@"getVideoSize"]) {
        [self handleGetVideoSize:result];
    } else if ([method isEqualToString:@"release"]) {
        [self handleRelease:result];
    } else {
        result(FlutterMethodNotImplemented);
    }
}

- (void)handleCreate:(FlutterResult)result {
    if (_player) {
        result(@{@"error": @"Player already exists"});
        return;
    }
    
    _player = [[FJKNativePlayer alloc] init];
    _texture = [[FJKPlayerTexture alloc] initWithPlayer:_player textureRegistry:_textureRegistry];
    
    // Setup event callback
    __weak typeof(self) weakSelf = self;
    _player.eventCallback = ^(FJKPlayerEvent event, NSInteger arg1, NSInteger arg2) {
        [weakSelf handlePlayerEvent:event arg1:arg1 arg2:arg2];
    };
    
    result(@{
        @"textureId": @(_texture.textureId),
        @"success": @YES
    });
    
    NSLog(@"[FJKNativePlayerHandler] Player created, textureId=%lld", _texture.textureId);
}

- (void)handleSetDataSource:(FlutterMethodCall *)call result:(FlutterResult)result {
    if (!_player) {
        result(@{@"error": @"Player not created"});
        return;
    }
    
    NSString *url = call.arguments[@"url"];
    int ret = [_player setDataSource:url];
    
    result(@{
        @"success": @(ret == 0),
        @"error": ret != 0 ? @"Failed to set data source" : [NSNull null]
    });
}

- (void)handleSetPlaybackMode:(FlutterMethodCall *)call result:(FlutterResult)result {
    if (!_player) {
        result(@{@"error": @"Player not created"});
        return;
    }
    
    NSNumber *mode = call.arguments[@"mode"];
    NSNumber *bufferMs = call.arguments[@"customBufferMs"];
    NSNumber *enableAudio = call.arguments[@"enableAudio"];
    NSNumber *maxLatencyMs = call.arguments[@"maxLatencyMs"];
    NSNumber *enableFrameDrop = call.arguments[@"enableFrameDrop"];
    
    // Apply defaults if nil
    NSInteger modeValue = mode ? [mode integerValue] : 2; // VOD_OPTIMIZED
    NSInteger bufferMsValue = bufferMs ? [bufferMs integerValue] : -1;
    NSInteger enableAudioValue = enableAudio ? ([enableAudio boolValue] ? 1 : 0) : -1;
    NSInteger maxLatencyMsValue = maxLatencyMs ? [maxLatencyMs integerValue] : -1;
    NSInteger enableFrameDropValue = enableFrameDrop ? ([enableFrameDrop boolValue] ? 1 : 0) : -1;
    
    int ret = [_player setPlaybackMode:modeValue
                              bufferMs:bufferMsValue
                           enableAudio:enableAudioValue
                         maxLatencyMs:maxLatencyMsValue
                       enableFrameDrop:enableFrameDropValue];
    
    result(@{
        @"success": @(ret == 0),
        @"error": ret != 0 ? @"Failed to set playback mode" : [NSNull null]
    });
    
    if (ret == 0) {
        NSLog(@"[FJKNativePlayerHandler] Playback mode set: mode=%ld, buffer=%ldms, audio=%ld, maxLatency=%ldms, frameDrop=%ld",
              (long)modeValue, (long)bufferMsValue, (long)enableAudioValue, (long)maxLatencyMsValue, (long)enableFrameDropValue);
    }
}

- (void)handlePrepare:(FlutterResult)result {
    if (!_player) {
        result(@{@"error": @"Player not created"});
        return;
    }
    
    _prepareResult = result;
    [_player prepareAsync];
    // Result will be returned in event callback
}

- (void)handleStart:(FlutterResult)result {
    if (!_player) {
        result(@{@"error": @"Player not created"});
        return;
    }
    
    int ret = [_player start];
    result(@{@"success": @(ret == 0)});
}

- (void)handlePause:(FlutterResult)result {
    if (_player) {
        [_player pause];
    }
    result(@{@"success": @YES});
}

- (void)handleResume:(FlutterResult)result {
    if (_player) {
        [_player resume];
    }
    result(@{@"success": @YES});
}

- (void)handleStop:(FlutterResult)result {
    if (_player) {
        [_player stop];
    }
    result(@{@"success": @YES});
}

- (void)handleSeekTo:(FlutterMethodCall *)call result:(FlutterResult)result {
    if (!_player) {
        result(@{@"error": @"Player not created"});
        return;
    }
    
    NSNumber *position = call.arguments[@"position"];
    [_player seekTo:[position longLongValue]];
    result(@{@"success": @YES});
}

- (void)handleGetPosition:(FlutterResult)result {
    if (!_player) {
        result(@0);
        return;
    }
    
    result(@(_player.currentPosition));
}

- (void)handleGetDuration:(FlutterResult)result {
    if (!_player) {
        result(@0);
        return;
    }
    
    result(@(_player.duration));
}

- (void)handleGetVideoSize:(FlutterResult)result {
    if (!_player) {
        result(@{@"width": @0, @"height": @0});
        return;
    }
    
    CGSize size = _player.videoSize;
    result(@{
        @"width": @((int)size.width),
        @"height": @((int)size.height)
    });
}

- (void)handleRelease:(FlutterResult)result {
    [self dispose];
    result(@{@"success": @YES});
}

- (void)handlePlayerEvent:(FJKPlayerEvent)event arg1:(NSInteger)arg1 arg2:(NSInteger)arg2 {
    NSLog(@"[FJKNativePlayerHandler] Player event: %ld, arg1=%ld, arg2=%ld", (long)event, (long)arg1, (long)arg2);
    
    if (event == FJKPlayerEventPrepared && _prepareResult) {
        CGSize size = _player.videoSize;
        _prepareResult(@{
            @"success": @YES,
            @"duration": @(_player.duration),
            @"width": @((int)size.width),
            @"height": @((int)size.height)
        });
        _prepareResult = nil;
    }
    
    // TODO: Send event to Flutter via event channel
}

- (void)dispose {
    if (_texture) {
        [_texture dispose];
        _texture = nil;
    }
    
    if (_player) {
        // ARC handles memory management, just nil out the reference
        _player = nil;
    }
    
    NSLog(@"[FJKNativePlayerHandler] Disposed");
}

@end
