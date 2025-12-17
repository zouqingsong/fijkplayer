// MIT License - FijkPlayer iOS Plugin Integration

#import <Flutter/Flutter.h>
#import "FJKNativePlayer.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * Flutter texture wrapper for native player
 * Provides CVPixelBuffer to Flutter's texture registry
 */
@interface FJKPlayerTexture : NSObject <FlutterTexture>

@property (nonatomic, strong) FJKNativePlayer *player;
@property (nonatomic, readonly) int64_t textureId;

- (instancetype)initWithPlayer:(FJKNativePlayer *)player 
                  textureRegistry:(id<FlutterTextureRegistry>)textureRegistry;

- (void)dispose;

@end

/**
 * Native player handler for Flutter method channel
 * Mirrors Android's NativePlayerTestHandler
 */
@interface FJKNativePlayerHandler : NSObject

- (instancetype)initWithTextureRegistry:(id<FlutterTextureRegistry>)textureRegistry;

- (void)handleMethodCall:(FlutterMethodCall *)call result:(FlutterResult)result;

- (void)dispose;

@end

NS_ASSUME_NONNULL_END
