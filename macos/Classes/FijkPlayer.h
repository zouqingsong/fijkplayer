// MIT License - FijkPlayer macOS Player Header

#import <Foundation/Foundation.h>
#import <AVFoundation/AVFoundation.h>
#import <FlutterMacOS/FlutterMacOS.h>

NS_ASSUME_NONNULL_BEGIN

@interface FijkPlayer : NSObject <FlutterStreamHandler, FlutterTexture>

@property(atomic, readonly) NSNumber *playerId;

- (instancetype)initWithRegistrar:(id<FlutterPluginRegistrar>)registrar;

- (instancetype)initJustTexture;

- (void)shutdown;

@end

NS_ASSUME_NONNULL_END
