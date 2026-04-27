// FijkRecorderHandler.h - macOS

#import <Foundation/Foundation.h>
#import <FlutterMacOS/FlutterMacOS.h>

NS_ASSUME_NONNULL_BEGIN

@interface FijkRecorderHandler : NSObject

- (void)handleMethodCall:(FlutterMethodCall*)call result:(FlutterResult)result;

@end

NS_ASSUME_NONNULL_END
