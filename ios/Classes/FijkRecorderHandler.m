//
//  FijkRecorderHandler.m
//  fijkplayer
//
//  Stub handler for befovy.com/fijk/recorder channel
//

#import "FijkRecorderHandler.h"

@implementation FijkRecorderHandler

- (void)handleMethodCall:(FlutterMethodCall*)call result:(FlutterResult)result {
    NSLog(@"[FijkRecorderHandler] Method call: %@", call.method);
    
    if ([@"startRecording" isEqualToString:call.method]) {
        // TODO: Implement recording start
        NSLog(@"[FijkRecorderHandler] startRecording not yet implemented");
        result([FlutterError errorWithCode:@"NOT_IMPLEMENTED"
                                   message:@"Recording not yet implemented on iOS"
                                   details:nil]);
    } else if ([@"stopRecording" isEqualToString:call.method]) {
        // TODO: Implement recording stop
        NSLog(@"[FijkRecorderHandler] stopRecording not yet implemented");
        result([FlutterError errorWithCode:@"NOT_IMPLEMENTED"
                                   message:@"Recording not yet implemented on iOS"
                                   details:nil]);
    } else if ([@"isRecording" isEqualToString:call.method]) {
        result(@NO);
    } else {
        result(FlutterMethodNotImplemented);
    }
}

@end
