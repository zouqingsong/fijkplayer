package com.befovy.fijkplayer;

import android.content.Context;
import android.util.Log;

import androidx.annotation.NonNull;

import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;

/**
 * Recorder Handler - delegates to FijkFFmpegRecorder for actual recording
 * 
 * Channel: befovy.com/fijk/recorder
 */
public class FijkRecorderHandler implements MethodChannel.MethodCallHandler {
    
    private static final String TAG = "FijkRecorderHandler";
    private final Context context;
    private FijkFFmpegRecorder recorder;
    
    public FijkRecorderHandler(Context context) {
        this.context = context;
        this.recorder = new FijkFFmpegRecorder();
        Log.i(TAG, "FijkRecorderHandler initialized");
    }
    
    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
        Log.d(TAG, "Method call: " + call.method);
        
        switch (call.method) {
            case "isRecording":
                result.success(recorder.isRecording());
                break;
            case "startRecording": {
                String rtspUrl = call.argument("rtspUrl");
                String outputPath = call.argument("outputPath");
                if (rtspUrl == null || outputPath == null) {
                    result.error("INVALID_ARGS", "Missing rtspUrl or outputPath", null);
                    return;
                }
                try {
                    boolean started = recorder.startRecording(rtspUrl, outputPath);
                    result.success(started);
                } catch (Exception e) {
                    Log.e(TAG, "Failed to start recording", e);
                    result.error("RECORDING_FAILED", e.getMessage(), null);
                }
                break;
            }
            case "stopRecording": {
                try {
                    boolean stopped = recorder.stopRecording();
                    result.success(stopped);
                } catch (Exception e) {
                    Log.e(TAG, "Failed to stop recording", e);
                    result.error("STOP_FAILED", e.getMessage(), null);
                }
                break;
            }
            default:
                Log.w(TAG, "Unimplemented recorder method: " + call.method);
                result.notImplemented();
                break;
        }
    }
}
