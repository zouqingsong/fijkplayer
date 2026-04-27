package com.befovy.fijkplayer;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import androidx.annotation.NonNull;

import java.util.ArrayList;
import java.util.List;

import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;

/**
 * FFmpeg Kit Handler - Executes FFmpeg commands via native FFmpeg libraries
 * 
 * Channel: befovy.com/fijk/ffmpeg_kit
 */
public class FijkFFmpegKitHandler implements MethodChannel.MethodCallHandler {
    
    private static final String TAG = "FijkFFmpegKitHandler";
    private final Context context;
    private final FijkFFmpegKit ffmpegKit;
    private final Handler mainHandler;
    
    public FijkFFmpegKitHandler(Context context) {
        this.context = context;
        this.ffmpegKit = new FijkFFmpegKit();
        this.mainHandler = new Handler(Looper.getMainLooper());
        Log.i(TAG, "FijkFFmpegKitHandler initialized");
    }
    
    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
        switch (call.method) {
            case "getFFmpegVersion":
                result.success(ffmpegKit.getFFmpegVersion());
                break;
            case "execute": {
                String command = call.argument("command");
                List<String> arguments = call.argument("arguments");
                
                if (command == null && arguments == null) {
                    result.error("INVALID_ARGS", "Either 'command' or 'arguments' must be provided", null);
                    return;
                }
                
                final String[] args;
                if (arguments != null) {
                    args = arguments.toArray(new String[0]);
                } else {
                    args = parseCommand(command);
                }
                
                // Execute on background thread
                new Thread(() -> {
                    int ret = ffmpegKit.executeFFmpegCommand(args);
                    mainHandler.post(() -> result.success(ret));
                }).start();
                break;
            }
            case "cancel":
                ffmpegKit.cancelExecution(0);
                result.success(null);
                break;
            default:
                result.notImplemented();
                break;
        }
    }
    
    private String[] parseCommand(String command) {
        List<String> args = new ArrayList<>();
        StringBuilder current = new StringBuilder();
        boolean inQuote = false;
        char quoteChar = 0;
        
        for (int i = 0; i < command.length(); i++) {
            char c = command.charAt(i);
            if (inQuote) {
                if (c == quoteChar) {
                    inQuote = false;
                } else {
                    current.append(c);
                }
            } else if (c == '\'' || c == '"') {
                inQuote = true;
                quoteChar = c;
            } else if (c == ' ' || c == '\t') {
                if (current.length() > 0) {
                    args.add(current.toString());
                    current.setLength(0);
                }
            } else {
                current.append(c);
            }
        }
        if (current.length() > 0) {
            args.add(current.toString());
        }
        return args.toArray(new String[0]);
    }
}
