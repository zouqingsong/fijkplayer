// FijkFFmpegKitHandler.m - macOS
// Handler for befovy.com/fijk/ffmpeg_kit channel
// Executes FFmpeg commands using embedded fftools

#import "FijkFFmpegKitHandler.h"
#import "ffmpeg_kit_execute.h"

@implementation FijkFFmpegKitHandler

- (void)handleMethodCall:(FlutterMethodCall*)call result:(FlutterResult)result {
    if ([@"execute" isEqualToString:call.method]) {
        NSString *command = call.arguments[@"command"];
        NSArray<NSString *> *arguments = call.arguments[@"arguments"];
        
        if (!command && !arguments) {
            result([FlutterError errorWithCode:@"INVALID_ARGS"
                                       message:@"Either 'command' or 'arguments' must be provided"
                                       details:nil]);
            return;
        }
        
        NSArray<NSString *> *args;
        if (arguments) {
            args = arguments;
        } else {
            args = [self parseCommand:command];
        }
        
        dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0), ^{
            int argc = (int)args.count + 1;
            char **argv = (char **)malloc(sizeof(char *) * (argc + 1));
            argv[0] = strdup("ffmpeg");
            for (int i = 0; i < (int)args.count; i++) {
                argv[i + 1] = strdup([args[i] UTF8String]);
            }
            argv[argc] = NULL;
            
            NSLog(@"[FijkFFmpegKit] Executing with %d args", argc);
            int ret = ffmpeg_kit_execute(argc, argv);
            NSLog(@"[FijkFFmpegKit] Execution returned: %d", ret);
            
            for (int i = 0; i < argc; i++) {
                free(argv[i]);
            }
            free(argv);
            
            dispatch_async(dispatch_get_main_queue(), ^{
                result(@(ret));
            });
        });
    } else if ([@"cancel" isEqualToString:call.method]) {
        ffmpeg_kit_cancel();
        result(nil);
    } else if ([@"getFFmpegVersion" isEqualToString:call.method]) {
        const char *version = ffmpeg_kit_get_version();
        result([NSString stringWithUTF8String:version]);
    } else {
        result(FlutterMethodNotImplemented);
    }
}

- (NSArray<NSString *> *)parseCommand:(NSString *)command {
    NSMutableArray<NSString *> *args = [NSMutableArray array];
    NSMutableString *current = [NSMutableString string];
    BOOL inQuote = NO;
    unichar quoteChar = 0;
    
    for (NSUInteger i = 0; i < command.length; i++) {
        unichar c = [command characterAtIndex:i];
        
        if (inQuote) {
            if (c == quoteChar) {
                inQuote = NO;
            } else {
                [current appendFormat:@"%C", c];
            }
        } else if (c == '\'' || c == '"') {
            inQuote = YES;
            quoteChar = c;
        } else if (c == ' ' || c == '\t') {
            if (current.length > 0) {
                [args addObject:[current copy]];
                [current setString:@""];
            }
        } else {
            [current appendFormat:@"%C", c];
        }
    }
    
    if (current.length > 0) {
        [args addObject:[current copy]];
    }
    
    return args;
}

@end
