//
//  FijkRecorderHandler.m
//  fijkplayer
//
//  Handler for befovy.com/fijk/recorder channel
//  Delegates to FFmpegRecorder for actual recording
//

#import "FijkRecorderHandler.h"
#import "FFmpegRecorder.h"

@implementation FijkRecorderHandler

- (void)handleMethodCall:(FlutterMethodCall*)call result:(FlutterResult)result {
    NSLog(@"[FijkRecorderHandler] Method call: %@", call.method);
    
    if ([@"startRecording" isEqualToString:call.method]) {
        NSString *rtspUrl = call.arguments[@"rtspUrl"];
        NSString *outputPath = call.arguments[@"outputPath"];
        
        if (!rtspUrl || !outputPath) {
            result([FlutterError errorWithCode:@"INVALID_ARGS"
                                       message:@"Missing rtspUrl or outputPath"
                                       details:nil]);
            return;
        }
        
        NSError *error = nil;
        BOOL started = [[FFmpegRecorder sharedInstance] startRecordingWithRtspUrl:rtspUrl
                                                                      outputPath:outputPath
                                                                           error:&error];
        if (started) {
            result(@YES);
        } else {
            result([FlutterError errorWithCode:@"RECORDING_FAILED"
                                       message:error.localizedDescription ?: @"Failed to start recording"
                                       details:nil]);
        }
    } else if ([@"stopRecording" isEqualToString:call.method]) {
        NSError *error = nil;
        BOOL stopped = [[FFmpegRecorder sharedInstance] stopRecordingWithError:&error];
        if (stopped) {
            result(@YES);
        } else {
            result([FlutterError errorWithCode:@"STOP_FAILED"
                                       message:error.localizedDescription ?: @"Failed to stop recording"
                                       details:nil]);
        }
    } else if ([@"isRecording" isEqualToString:call.method]) {
        result(@([[FFmpegRecorder sharedInstance] isRecording]));
    } else {
        result(FlutterMethodNotImplemented);
    }
}

@end
