// MIT License - FijkPlayer macOS Plugin Header

#import <Foundation/Foundation.h>
#import <FlutterMacOS/FlutterMacOS.h>

NS_ASSUME_NONNULL_BEGIN

@interface FijkPlugin : NSObject<FlutterPlugin, FlutterStreamHandler>

+ (FijkPlugin *)singleInstance;

- (void)onPlayingChange:(int)delta;
- (void)onPlayableChange:(int)delta;

@end

NS_ASSUME_NONNULL_END
