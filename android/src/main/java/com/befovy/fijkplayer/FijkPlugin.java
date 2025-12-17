package com.befovy.fijkplayer;

import android.content.Context;
import androidx.annotation.NonNull;
import io.flutter.embedding.engine.plugins.FlutterPlugin;
import io.flutter.plugin.common.EventChannel;
import io.flutter.plugin.common.MethodChannel;

/**
 * FijkPlugin - Flutter plugin for FFmpeg-based video player
 *
 * Registers four method channels:
 * - befovy.com/fijk: Production API for mobile_fijk_player (backward
 * compatible)
 * - befovy.com/fijk/native_player: Direct NativePlayer API
 * - befovy.com/fijk/recorder: Recording API (stub)
 * - befovy.com/fijk/ffmpeg_kit: FFmpeg utility functions (stub)
 */
public class FijkPlugin implements FlutterPlugin {
    private static final String TAG = "FijkPlugin";
    private Context appContext;

    // Production channel for mobile_fijk_player compatibility
    private MethodChannel productionChannel;
    private FijkPlayerHandler productionHandler;

    // Native player channel for direct NativePlayer access
    private MethodChannel nativePlayerChannel;
    private EventChannel nativePlayerEventChannel;
    private FJKNativePlayerHandler nativePlayerHandler;

    // Recorder channel for recording functionality
    private MethodChannel recorderChannel;
    private FijkRecorderHandler recorderHandler;

    // FFmpeg Kit channel for utility functions
    private MethodChannel ffmpegKitChannel;
    private FijkFFmpegKitHandler ffmpegKitHandler;

    @Override
    public void onAttachedToEngine(@NonNull FlutterPluginBinding binding) {
        appContext = binding.getApplicationContext();

        // Register production channel (befovy.com/fijk)
        productionChannel =
            new MethodChannel(binding.getBinaryMessenger(), "befovy.com/fijk");
        productionHandler =
            new FijkPlayerHandler(appContext, binding.getTextureRegistry(),
                                  binding.getBinaryMessenger());
        productionChannel.setMethodCallHandler(productionHandler);

        // Register native player channel (befovy.com/fijk/native_player)
        nativePlayerChannel = new MethodChannel(
            binding.getBinaryMessenger(), "befovy.com/fijk/native_player");
        nativePlayerHandler = new FJKNativePlayerHandler(
            appContext, binding.getTextureRegistry());
        nativePlayerChannel.setMethodCallHandler(nativePlayerHandler);

        // Register native player event channel
        nativePlayerEventChannel =
            new EventChannel(binding.getBinaryMessenger(),
                             "befovy.com/fijk/native_player/event");
        nativePlayerEventChannel.setStreamHandler(nativePlayerHandler);

        // Register recorder channel (befovy.com/fijk/recorder)
        recorderChannel = new MethodChannel(binding.getBinaryMessenger(),
                                            "befovy.com/fijk/recorder");
        recorderHandler = new FijkRecorderHandler(appContext);
        recorderChannel.setMethodCallHandler(recorderHandler);

        // Register FFmpeg Kit channel (befovy.com/fijk/ffmpeg_kit)
        ffmpegKitChannel = new MethodChannel(binding.getBinaryMessenger(),
                                             "befovy.com/fijk/ffmpeg_kit");
        ffmpegKitHandler = new FijkFFmpegKitHandler(appContext);
        ffmpegKitChannel.setMethodCallHandler(ffmpegKitHandler);

        android.util.Log.i(
            TAG, "✅ FijkPlugin attached - All 4 channels registered:");
        android.util.Log.i(TAG, "  1. befovy.com/fijk (production)");
        android.util.Log.i(TAG, "  2. befovy.com/fijk/native_player");
        android.util.Log.i(TAG, "  3. befovy.com/fijk/recorder");
        android.util.Log.i(TAG, "  4. befovy.com/fijk/ffmpeg_kit");
    }

    @Override
    public void onDetachedFromEngine(@NonNull FlutterPluginBinding binding) {
        if (productionChannel != null) {
            productionChannel.setMethodCallHandler(null);
            productionChannel = null;
        }
        productionHandler = null;

        if (nativePlayerChannel != null) {
            nativePlayerChannel.setMethodCallHandler(null);
            nativePlayerChannel = null;
        }
        nativePlayerHandler = null;

        if (recorderChannel != null) {
            recorderChannel.setMethodCallHandler(null);
            recorderChannel = null;
        }
        recorderHandler = null;

        if (ffmpegKitChannel != null) {
            ffmpegKitChannel.setMethodCallHandler(null);
            ffmpegKitChannel = null;
        }
        ffmpegKitHandler = null;

        appContext = null;
        android.util.Log.i(TAG, "FijkPlugin detached");
    }
}
