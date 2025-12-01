//
//  FijkFFmpegKitHandler.m
//  fijkplayer
//
//  Stub handler for befovy.com/fijk/ffmpeg_kit channel
//

#import "FijkFFmpegKitHandler.h"

@implementation FijkFFmpegKitHandler

- (void)handleMethodCall:(FlutterMethodCall*)call result:(FlutterResult)result {
    NSLog(@"[FijkFFmpegKitHandler] Method call: %@", call.method);
    
    if ([@"execute" isEqualToString:call.method]) {
        // TODO: Implement FFmpeg command execution
        NSLog(@"[FijkFFmpegKitHandler] execute not yet implemented");
        result([FlutterError errorWithCode:@"NOT_IMPLEMENTED"
                                   message:@"FFmpeg kit not yet implemented on iOS"
                                   details:nil]);
    } else if ([@"cancel" isEqualToString:call.method]) {
        // TODO: Implement cancellation
        NSLog(@"[FijkFFmpegKitHandler] cancel not yet implemented");
        result([FlutterError errorWithCode:@"NOT_IMPLEMENTED"
                                   message:@"FFmpeg kit not yet implemented on iOS"
                                   details:nil]);
    } else if ([@"getFFmpegVersion" isEqualToString:call.method]) {
        result(@"FFmpeg (iOS - not yet implemented)");
    } else {
        result(FlutterMethodNotImplemented);
    }
}

@end
