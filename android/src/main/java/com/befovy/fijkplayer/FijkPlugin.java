package com.befovy.fijkplayer;

import android.content.Context;
import androidx.annotation.NonNull;
import io.flutter.embedding.engine.plugins.FlutterPlugin;
import io.flutter.plugin.common.MethodChannel;

/**
 * Minimal FijkPlugin for Native Player Testing
 * 
 * This is a simplified version that only registers the NativePlayerTestHandler
 * for testing the new native player implementation.
 */
public class FijkPlugin implements FlutterPlugin {
    private static final String TAG = "FijkPlugin";
    private Context appContext;
    private NativePlayerTestHandler testHandler;

    @Override
    public void onAttachedToEngine(@NonNull FlutterPluginBinding binding) {
        appContext = binding.getApplicationContext();
        
        // Register NativePlayerTestHandler for testing
        final MethodChannel testChannel = new MethodChannel(
            binding.getBinaryMessenger(),
            "befovy.com/fijk/native_player_test"
        );
        testHandler = new NativePlayerTestHandler(
            appContext,
            binding.getTextureRegistry()
        );
        testChannel.setMethodCallHandler(testHandler);
        
        android.util.Log.i(TAG, "FijkPlugin attached - Native Player Test Handler registered");
    }

    @Override
    public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
        testHandler = null;
        appContext = null;
        android.util.Log.i(TAG, "FijkPlugin detached");
    }
}
