package com.befovy.fijkplayer;

import android.content.Context;
import android.graphics.Bitmap;
import android.util.Log;
import android.view.PixelCopy;
import android.view.Surface;
import android.os.Handler;
import android.os.Looper;
import androidx.annotation.NonNull;
import io.flutter.plugin.common.BinaryMessenger;
import io.flutter.plugin.common.EventChannel;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.view.TextureRegistry;
import java.io.ByteArrayOutputStream;
import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Production handler for FijkPlayer - Backward compatible with
 * mobile_fijk_player
 *
 * This handler provides the same API as the old ijkplayer-based implementation,
 * but uses the new FJKNativePlayer (FFmpeg-based) implementation under the
 * hood.
 *
 * Channel: befovy.com/fijk
 */
public class FijkPlayerHandler implements MethodChannel.MethodCallHandler {

    private static final String TAG = "FijkPlayerHandler";

    private final Context context;
    private final TextureRegistry textureRegistry;
    private final BinaryMessenger binaryMessenger;
    private final AtomicInteger playerIdCounter = new AtomicInteger(0);

    // Map of player ID to player instance
    private final ConcurrentHashMap<Integer, PlayerInstance> players =
        new ConcurrentHashMap<>();

    public FijkPlayerHandler(Context context, TextureRegistry textureRegistry,
                             BinaryMessenger binaryMessenger) {
        this.context = context;
        this.textureRegistry = textureRegistry;
        this.binaryMessenger = binaryMessenger;
        // Log.i(TAG,
        //       "FijkPlayerHandler initialized with per-player channel support");
    }

    @Override
    public void onMethodCall(@NonNull MethodCall call,
                             @NonNull MethodChannel.Result result) {
        // Log. "Method call: " + call.method);

        switch (call.method) {
        case "init":
        case "createPlayer": // Alias for backward compatibility
            handleInit(result);
            break;
        case "setDataSource":
            handleSetDataSource(call, result);
            break;
        case "prepareAsync":
            handlePrepareAsync(call, result);
            break;
        case "start":
            handleStart(call, result);
            break;
        case "pause":
            handlePause(call, result);
            break;
        case "stop":
            handleStop(call, result);
            break;
        case "reset":
            handleReset(call, result);
            break;
        case "release":
        case "releasePlayer": // Alias used by the Dart side
            handleRelease(call, result);
            break;
        case "seekTo":
            handleSeekTo(call, result);
            break;
        case "getPosition":
            handleGetPosition(call, result);
            break;
        case "getDuration":
            handleGetDuration(call, result);
            break;
        case "setVolume":
            handleSetVolume(call, result);
            break;
        case "setSpeed":
            handleSetSpeed(call, result);
            break;
        case "setupSurface":
            handleSetupSurface(call, result);
            break;
        case "setLoop":
            handleSetLoop(call, result);
            break;
        case "getVideoWidth":
            handleGetVideoWidth(call, result);
            break;
        case "getVideoHeight":
            handleGetVideoHeight(call, result);
            break;
        case "logLevel":
            handleLogLevel(call, result);
            break;
        default:
            Log.w(TAG, "Unimplemented method: " + call.method);
            result.notImplemented();
            break;
        }
    }

    private void handleInit(MethodChannel.Result result) {
        try {
            int playerId = playerIdCounter.incrementAndGet();

            // Create texture for video rendering
            TextureRegistry.SurfaceTextureEntry textureEntry =
                textureRegistry.createSurfaceTexture();
            long textureId = textureEntry.id();

            // Create native player
            FJKNativePlayer player = new FJKNativePlayer();

            // Create surface texture manager
            SurfaceTextureManager surfaceManager =
                new SurfaceTextureManager(textureEntry);

            // Set surface on player
            Surface surface = surfaceManager.getSurface();
            player.setSurface(surface);

            // Create per-player MethodChannel (befovy.com/fijkplayer/{id})
            String methodChannelName = "befovy.com/fijkplayer/" + playerId;
            MethodChannel methodChannel =
                new MethodChannel(binaryMessenger, methodChannelName);

            // Create per-player EventChannel (befovy.com/fijkplayer/event/{id})
            String eventChannelName = "befovy.com/fijkplayer/event/" + playerId;
            EventChannel eventChannel =
                new EventChannel(binaryMessenger, eventChannelName);

            // Store player instance
            PlayerInstance instance =
                new PlayerInstance(playerId, player, textureEntry,
                                   surfaceManager, methodChannel, eventChannel);
            players.put(playerId, instance);

            // Set up per-player method channel handler
            methodChannel.setMethodCallHandler(
                new MethodChannel.MethodCallHandler() {
                    @Override
                    public void onMethodCall(
                        @NonNull MethodCall call,
                        @NonNull MethodChannel.Result result) {
                        handlePerPlayerMethodCall(playerId, call, result);
                    }
                });

            // Set up per-player event channel
            eventChannel.setStreamHandler(instance.eventStreamHandler);

            // Wire native player events to event channel and state transitions
            player.setEventCallback(new FJKNativePlayer.EventCallback() {
                @Override
                public void onNativeEvent(int eventType, int arg1, int arg2) {
                    instance.handleNativeEvent(eventType, arg1, arg2);
                }
            });

            Log.i(TAG, "Player created: id=" + playerId +
                           ", texture=" + textureId + ", channels=[" +
                           methodChannelName + ", " + eventChannelName + "]");

            // Return just the playerId for backward compatibility
            result.success(playerId);

        } catch (Exception e) {
            Log.e(TAG, "Failed to create player", e);
            result.error("CREATE_FAILED", e.getMessage(), null);
        }
    }

    /**
     * Handle method calls on per-player channels
     */
    private void handlePerPlayerMethodCall(int playerId, MethodCall call,
                                           MethodChannel.Result result) {
        // Log.d(TAG, "Per-player method call: player=" + playerId +
        //            ", method=" + call.method);

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        // Handle methods without requiring 'pid' argument since we know the
        // player from the channel
        switch (call.method) {
        case "setDataSource": {
            String url = call.argument("url");
            if (url == null) {
                result.error("INVALID_ARGS", "Missing url", null);
                return;
            }
            try {
                instance.player.setDataSource(url);
                instance.dataSource = url;

                // Send state change: idle -> initialized
                instance.sendStateChange(PlayerInstance.STATE_INITIALIZED);

                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to set data source", e);
                result.error("SET_DATA_SOURCE_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "prepareAsync": {
            try {
                // Send state change: initialized -> asyncPreparing
                instance.sendStateChange(PlayerInstance.STATE_ASYNC_PREPARING);

                instance.player.prepareAsync();
                Log.i(TAG, "Prepare async: player=" + playerId);

                // Native callback will send prepared state when ready
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to prepare", e);
                result.error("PREPARE_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "start": {
            try {
                instance.player.start();
                Log.i(TAG, "Start: player=" + playerId);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to start", e);
                result.error("START_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "pause": {
            try {
                instance.player.pause();
                Log.i(TAG, "Pause: player=" + playerId);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to pause", e);
                result.error("PAUSE_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "stop": {
            try {
                instance.player.stop();
                Log.i(TAG, "Stop: player=" + playerId);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to stop", e);
                result.error("STOP_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "reset": {
            try {
                // Full reset back to IDLE so the player can be reused (setDataSource
                // again). stop() alone leaves native in STOPPED, which rejects a
                // subsequent setDataSource/prepare.
                instance.player.reset();
                Log.i(TAG, "Reset: player=" + playerId);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to reset", e);
                result.error("RESET_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "release": {
            try {
                instance.release();
                players.remove(playerId);
                Log.i(TAG, "Release: player=" + playerId);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to release", e);
                result.error("RELEASE_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "seekTo": {
            Number posMs = call.argument("msec");
            if (posMs == null) {
                result.error("INVALID_ARGS", "Missing msec", null);
                return;
            }
            try {
                instance.player.seekTo(posMs.longValue());
                Log.i(TAG, "Seek to: player=" + playerId + ", pos=" + posMs);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to seek", e);
                result.error("SEEK_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "getCurrentPosition":  // Dart channel name
        case "getPosition": {
            try {
                long pos = instance.player.getCurrentPosition();
                result.success(pos);
            } catch (Exception e) {
                Log.e(TAG, "Failed to get position", e);
                result.error("GET_POSITION_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "getDuration": {
            try {
                long duration = instance.player.getDuration();
                result.success(duration);
            } catch (Exception e) {
                Log.e(TAG, "Failed to get duration", e);
                result.error("GET_DURATION_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "setVolume": {
            Number volume = call.argument("volume");
            if (volume == null) {
                result.error("INVALID_ARGS", "Missing volume", null);
                return;
            }
            try {
                instance.player.setVolume(volume.floatValue());
                Log.i(TAG,
                      "Set volume: player=" + playerId + ", volume=" + volume);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to set volume", e);
                result.error("SET_VOLUME_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "setSpeed": {
            Number speed = call.argument("speed");
            if (speed == null) {
                result.error("INVALID_ARGS", "Missing speed", null);
                return;
            }
            try {
                // NativePlayer doesn't support setSpeed yet - return success
                // for compatibility
                Log.w(TAG,
                      "setSpeed not yet implemented in NativePlayer: player=" +
                          playerId + ", speed=" + speed);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to set speed", e);
                result.error("SET_SPEED_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "setupSurface": {
            try {
                long textureId = instance.textureEntry.id();
                Log.i(TAG, "Setup surface: player=" + playerId +
                               ", texture=" + textureId);
                result.success(textureId);
            } catch (Exception e) {
                Log.e(TAG, "Failed to setup surface", e);
                result.error("SETUP_SURFACE_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "setLoop": {
            Number loopCount = call.argument("loop");
            if (loopCount == null) {
                result.error("INVALID_ARGS", "Missing loop", null);
                return;
            }
            try {
                // NativePlayer doesn't support setLooping yet - return success
                // for compatibility
                Log.w(
                    TAG,
                    "setLooping not yet implemented in NativePlayer: player=" +
                        playerId + ", loop=" + loopCount);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to set loop", e);
                result.error("SET_LOOP_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "getVideoWidth": {
            try {
                int width = instance.player.getVideoWidth();
                result.success(width);
            } catch (Exception e) {
                Log.e(TAG, "Failed to get video width", e);
                result.error("GET_WIDTH_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "getVideoHeight": {
            try {
                int height = instance.player.getVideoHeight();
                result.success(height);
            } catch (Exception e) {
                Log.e(TAG, "Failed to get video height", e);
                result.error("GET_HEIGHT_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "setPlaybackMode": {
            try {
                Integer mode = call.argument("mode");
                Integer bufferMs = call.argument("customBufferMs");
                Boolean enableAudio = call.argument("enableAudio");
                Integer maxLatencyMs = call.argument("maxLatencyMs");
                Boolean enableFrameDrop = call.argument("enableFrameDrop");
                if (mode == null) mode = 0;               // LIVE_LOW_LATENCY
                if (bufferMs == null) bufferMs = -1;
                if (enableAudio == null) enableAudio = false;
                if (maxLatencyMs == null) maxLatencyMs = -1;
                if (enableFrameDrop == null) enableFrameDrop = true;
                instance.player.setPlaybackMode(mode, bufferMs, enableAudio,
                                                maxLatencyMs, enableFrameDrop);
                Log.i(TAG, "Set playback mode: player=" + playerId + ", mode=" +
                               mode + ", buffer=" + bufferMs + "ms, audio=" +
                               enableAudio + ", maxLatency=" + maxLatencyMs +
                               "ms, frameDrop=" + enableFrameDrop);
                result.success(null);
            } catch (Exception e) {
                Log.e(TAG, "Failed to set playback mode", e);
                result.error("SET_PLAYBACK_MODE_FAILED", e.getMessage(), null);
            }
            break;
        }
        case "setOption":
        case "applyOptions": {
            // NativePlayer is configured via setPlaybackMode; individual ijk-style
            // options are not applicable. Accept for API compatibility.
            result.success(null);
            break;
        }
        case "snapshot": {
            try {
                byte[] pngData = instance.player.snapshot();
                if (pngData != null) {
                    java.util.Map<String, Object> args = new java.util.HashMap<>();
                    args.put("data", pngData);
                    instance.methodChannel.invokeMethod("_onSnapshot", args);
                    result.success(null);
                } else {
                    // Fallback for Surface-rendering pipeline where native YUV planes
                    // may not be available yet: capture the latest Surface frame.
                    captureSnapshotWithPixelCopy(instance, result, 3);
                }
            } catch (Exception e) {
                Log.e(TAG, "Snapshot failed", e);
                result.error("SNAPSHOT_FAILED", e.getMessage(), null);
            }
            break;
        }
        default:
            // Check for recording methods before returning notImplemented
            if (handleRecordingMethod(instance, call, result)) {
                return;
            }
            Log.w(TAG, "Unimplemented per-player method: " + call.method);
            result.notImplemented();
            break;
        }
    }

    private void captureSnapshotWithPixelCopy(PlayerInstance instance,
                                              MethodChannel.Result result,
                                              int remainingAttempts) {
        final Surface surface = instance.surfaceManager.getSurface();
        if (surface == null) {
            result.error("SNAPSHOT_FAILED", "Surface unavailable", null);
            return;
        }

        int width = instance.player.getVideoWidth();
        int height = instance.player.getVideoHeight();
        if (width <= 0 || height <= 0) {
            width = 1280;
            height = 720;
        }

        final Bitmap bitmap = Bitmap.createBitmap(width, height,
                                                  Bitmap.Config.ARGB_8888);
        final Handler handler = new Handler(Looper.getMainLooper());

        PixelCopy.request(surface, bitmap, copyResult -> {
            if (copyResult == PixelCopy.SUCCESS) {
                try {
                    ByteArrayOutputStream stream = new ByteArrayOutputStream();
                    bitmap.compress(Bitmap.CompressFormat.PNG, 100, stream);
                    byte[] pngData = stream.toByteArray();
                    stream.close();

                    java.util.Map<String, Object> args = new java.util.HashMap<>();
                    args.put("data", pngData);
                    instance.methodChannel.invokeMethod("_onSnapshot", args);
                    result.success(null);
                } catch (Exception e) {
                    Log.e(TAG, "PixelCopy encode failed", e);
                    result.error("SNAPSHOT_FAILED", e.getMessage(), null);
                } finally {
                    bitmap.recycle();
                }
                return;
            }

            bitmap.recycle();
            if (remainingAttempts > 1) {
                handler.postDelayed(
                    () -> captureSnapshotWithPixelCopy(instance, result,
                                                       remainingAttempts - 1),
                    120);
            } else {
                result.error("SNAPSHOT_FAILED",
                             "No frame available (PixelCopy=" + copyResult + ")",
                             null);
            }
        }, handler);
    }

    /**
     * Handle FFmpeg recording method calls for a player instance.
     * Returns true if the method was handled, false otherwise.
     */
    private boolean handleRecordingMethod(PlayerInstance instance, MethodCall call, MethodChannel.Result result) {
        switch (call.method) {
            case "startFFmpegRecording":
            case "startRecording": {
                String path = call.argument("path");
                if (path == null) {
                    result.error("INVALID_ARGS", "Missing path", null);
                    return true;
                }
                if (instance.dataSource == null || instance.dataSource.isEmpty()) {
                    result.error("NO_DATA_SOURCE", "No data source set for recording", null);
                    return true;
                }
                try {
                    if (instance.ffmpegRecorder == null) {
                        instance.ffmpegRecorder = new FijkFFmpegRecorder();
                    }
                    boolean started = instance.ffmpegRecorder.startRecording(instance.dataSource, path);
                    if (started) {
                        // Notify Dart that recording started
                        instance.methodChannel.invokeMethod("_onRecordingStarted", null);
                        result.success(null);
                    } else {
                        instance.methodChannel.invokeMethod("_onRecordingError", "Failed to start recording");
                        result.error("RECORDING_FAILED", "Failed to start FFmpeg recording", null);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Failed to start recording", e);
                    instance.methodChannel.invokeMethod("_onRecordingError", e.getMessage());
                    result.error("RECORDING_FAILED", e.getMessage(), null);
                }
                return true;
            }
            case "startFFmpegPreRoll": {
                String path = call.argument("path");
                Integer preRollSeconds = call.argument("preRollSeconds");
                if (path == null) {
                    result.error("INVALID_ARGS", "Missing path", null);
                    return true;
                }
                if (instance.dataSource == null || instance.dataSource.isEmpty()) {
                    result.error("NO_DATA_SOURCE", "No data source set for recording", null);
                    return true;
                }
                int seconds = preRollSeconds == null ? 5 : preRollSeconds;
                try {
                    if (instance.ffmpegRecorder == null) {
                        instance.ffmpegRecorder = new FijkFFmpegRecorder();
                    }
                    boolean started = instance.ffmpegRecorder.startPreRoll(instance.dataSource, path, seconds);
                    if (started) {
                        result.success(null);
                    } else {
                        instance.methodChannel.invokeMethod("_onRecordingError", "Failed to start pre-roll");
                        result.error("RECORDING_FAILED", "Failed to start FFmpeg pre-roll", null);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Failed to start pre-roll", e);
                    instance.methodChannel.invokeMethod("_onRecordingError", e.getMessage());
                    result.error("RECORDING_FAILED", e.getMessage(), null);
                }
                return true;
            }
            case "commitFFmpegPreRoll": {
                try {
                    boolean committed = true;
                    if (instance.ffmpegRecorder != null) {
                        committed = instance.ffmpegRecorder.commitPreRoll();
                    }
                    if (committed) {
                        result.success(null);
                    } else {
                        result.error("RECORDING_FAILED", "Failed to commit pre-roll", null);
                    }
                } catch (Exception e) {
                    Log.e(TAG, "Failed to commit pre-roll", e);
                    result.error("RECORDING_FAILED", e.getMessage(), null);
                }
                return true;
            }
            case "stopFFmpegRecording":
            case "stopRecording": {
                try {
                    boolean stopped = true;
                    if (instance.ffmpegRecorder != null) {
                        stopped = instance.ffmpegRecorder.stopRecording();
                    }
                    if (!stopped) {
                        instance.methodChannel.invokeMethod("_onRecordingError",
                                                            "No video packets were written");
                        result.error("STOP_RECORDING_FAILED",
                                     "Recording completed without video frames",
                                     null);
                        return true;
                    }
                    instance.methodChannel.invokeMethod("_onRecordingStopped", null);
                    result.success(null);
                } catch (Exception e) {
                    Log.e(TAG, "Failed to stop recording", e);
                    result.error("STOP_RECORDING_FAILED", e.getMessage(), null);
                }
                return true;
            }
            case "isFFmpegRecording": {
                boolean isRec = instance.ffmpegRecorder != null && instance.ffmpegRecorder.isRecording();
                result.success(isRec);
                return true;
            }
            default:
                return false;
        }
    }

    private void handleSetDataSource(MethodCall call,
                                     MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        String url = call.argument("url");

        if (playerId == null || url == null) {
            result.error("INVALID_ARGS", "Missing playerId or url", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            instance.player.setDataSource(url);
            Log.i(TAG, "Data source set: player=" + playerId + ", url=" + url);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to set data source", e);
            result.error("SET_DATASOURCE_FAILED", e.getMessage(), null);
        }
    }

    private void handlePrepareAsync(MethodCall call,
                                    MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            instance.player.prepareAsync();
            Log.i(TAG, "Prepare async started: player=" + playerId);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to prepare", e);
            result.error("PREPARE_FAILED", e.getMessage(), null);
        }
    }

    private void handleStart(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            instance.player.start();
            Log.i(TAG, "Playback started: player=" + playerId);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to start playback", e);
            result.error("START_FAILED", e.getMessage(), null);
        }
    }

    private void handlePause(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            instance.player.pause();
            Log.i(TAG, "Playback paused: player=" + playerId);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to pause playback", e);
            result.error("PAUSE_FAILED", e.getMessage(), null);
        }
    }

    private void handleStop(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            instance.player.stop();
            Log.i(TAG, "Playback stopped: player=" + playerId);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to stop playback", e);
            result.error("STOP_FAILED", e.getMessage(), null);
        }
    }

    private void handleReset(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            // Reset player to IDLE state - allows setting new data source
            instance.player.reset();
            // Reset state to idle after successful reset
            instance.sendStateChange(PlayerInstance.STATE_IDLE);
            Log.i(TAG, "Player reset: player=" + playerId);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to reset player", e);
            result.error("RESET_FAILED", e.getMessage(), null);
        }
    }

    private void handleRelease(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.remove(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            instance.release();
            Log.i(TAG, "Player released: player=" + playerId);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to release player", e);
            result.error("RELEASE_FAILED", e.getMessage(), null);
        }
    }

    private void handleSeekTo(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        Number msec = call.argument("msec");

        if (playerId == null || msec == null) {
            result.error("INVALID_ARGS", "Missing playerId or msec", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            instance.player.seekTo(msec.longValue());
            Log.i(TAG, "Seek to: player=" + playerId + ", position=" + msec);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to seek", e);
            result.error("SEEK_FAILED", e.getMessage(), null);
        }
    }

    private void handleGetPosition(MethodCall call,
                                   MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            long position = instance.player.getCurrentPosition();
            result.success(position);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get position", e);
            result.error("GET_POSITION_FAILED", e.getMessage(), null);
        }
    }

    private void handleGetDuration(MethodCall call,
                                   MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            long duration = instance.player.getDuration();
            result.success(duration);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get duration", e);
            result.error("GET_DURATION_FAILED", e.getMessage(), null);
        }
    }

    private void handleSetVolume(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        Number volume = call.argument("volume");

        if (playerId == null || volume == null) {
            result.error("INVALID_ARGS", "Missing playerId or volume", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            float vol = volume.floatValue();
            instance.player.setVolume(vol);
            Log.i(TAG, "Volume set: player=" + playerId + ", volume=" + vol);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to set volume", e);
            result.error("SET_VOLUME_FAILED", e.getMessage(), null);
        }
    }

    private void handleSetSpeed(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        Number speed = call.argument("speed");

        if (playerId == null || speed == null) {
            result.error("INVALID_ARGS", "Missing playerId or speed", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            // NativePlayer doesn't support setSpeed yet - return success for
            // compatibility
            Log.w(TAG, "setSpeed not yet implemented in NativePlayer: player=" +
                           playerId + ", speed=" + speed);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to set speed", e);
            result.error("SET_SPEED_FAILED", e.getMessage(), null);
        }
    }

    private void handleSetupSurface(MethodCall call,
                                    MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        // Return the texture ID that was created during init
        long textureId = instance.textureEntry.id();
        // Log.i(TAG,
        //       "setupSurface: player=" + playerId + ", textureId=" + textureId);
        result.success(textureId);
    }

    private void handleLogLevel(MethodCall call, MethodChannel.Result result) {
        // Log level control - just acknowledge for now
        Integer level = call.argument("level");
        // Log. "Log level set to: " + level);
        result.success(null);
    }

    private void handleSetLoop(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        Number loop = call.argument("loop");

        if (playerId == null || loop == null) {
            result.error("INVALID_ARGS", "Missing playerId or loop", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            // NativePlayer doesn't support setLooping yet - return success for
            // compatibility
            Log.w(TAG,
                  "setLooping not yet implemented in NativePlayer: player=" +
                      playerId + ", loop=" + loop);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to set loop", e);
            result.error("SET_LOOP_FAILED", e.getMessage(), null);
        }
    }

    private void handleGetVideoWidth(MethodCall call,
                                     MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            int width = instance.player.getVideoWidth();
            result.success(width);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get video width", e);
            result.error("GET_WIDTH_FAILED", e.getMessage(), null);
        }
    }

    private void handleGetVideoHeight(MethodCall call,
                                      MethodChannel.Result result) {
        Integer playerId = call.argument("pid");

        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }

        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND",
                         "Player " + playerId + " not found", null);
            return;
        }

        try {
            int height = instance.player.getVideoHeight();
            result.success(height);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get video height", e);
            result.error("GET_HEIGHT_FAILED", e.getMessage(), null);
        }
    }

    /**
     * Internal class to hold player instance data with per-player channels
     */
    private class PlayerInstance {
        final int playerId;
        final FJKNativePlayer player;
        final TextureRegistry.SurfaceTextureEntry textureEntry;
        final SurfaceTextureManager surfaceManager;
        final MethodChannel methodChannel;
        final EventChannel eventChannel;
        final EventStreamHandler eventStreamHandler;
        EventChannel.EventSink eventSink;

        // FijkState values matching Dart enum
        private static final int STATE_IDLE = 0;
        private static final int STATE_INITIALIZED = 1;
        private static final int STATE_ASYNC_PREPARING = 2;
        private static final int STATE_PREPARED = 3;
        private static final int STATE_STARTED = 4;
        private static final int STATE_PAUSED = 5;
        private static final int STATE_COMPLETED = 6;
        private static final int STATE_STOPPED = 7;
        private static final int STATE_ERROR = 8;
        private static final int STATE_END = 9;

        private int currentState = STATE_IDLE;
        private int videoWidth = 0;
        private int videoHeight = 0;
        private String dataSource = null;
        private FijkFFmpegRecorder ffmpegRecorder = null;

        PlayerInstance(int playerId, FJKNativePlayer player,
                       TextureRegistry.SurfaceTextureEntry textureEntry,
                       SurfaceTextureManager surfaceManager,
                       MethodChannel methodChannel, EventChannel eventChannel) {
            this.playerId = playerId;
            this.player = player;
            this.textureEntry = textureEntry;
            this.surfaceManager = surfaceManager;
            this.methodChannel = methodChannel;
            this.eventChannel = eventChannel;
            this.eventStreamHandler = new EventStreamHandler(this);
        }

        void sendStateChange(int newState) {
            int oldState = currentState;
            
            // Only send event if state actually changed
            if (oldState == newState) {
                Log.d(TAG, "Skipping duplicate state event: state=" + newState);
                return;
            }
            
            currentState = newState;

            Map<String, Object> event = new HashMap<>();
            event.put("event", "state_change");
            event.put("new", newState);
            event.put("old", oldState);

            if (eventSink != null) {
                eventSink.success(event);
                Log.d(TAG, "State changed: " + oldState + " -> " + newState);
            }
        }

        void handleNativeEvent(int eventType, int arg1, int arg2) {
            // Map native events to FijkState transitions
            switch (eventType) {
            case FJKNativePlayer.EVENT_PREPARED:
                sendStateChange(STATE_PREPARED);
                // Send 'prepared' event with duration so Dart side gets the duration value
                if (eventSink != null) {
                    long durationMs = player.getDuration();
                    Map<String, Object> preparedEvent = new HashMap<>();
                    preparedEvent.put("event", "prepared");
                    preparedEvent.put("duration", (int)durationMs);
                    eventSink.success(preparedEvent);
                }
                break;
            case FJKNativePlayer.EVENT_STARTED:
                sendStateChange(STATE_STARTED);
                break;
            case FJKNativePlayer.EVENT_PAUSED:
                sendStateChange(STATE_PAUSED);
                break;
            case FJKNativePlayer.EVENT_COMPLETED:
                sendStateChange(STATE_COMPLETED);
                break;
            case FJKNativePlayer.EVENT_ERROR:
                sendStateChange(STATE_ERROR);
                // Also send error event with details
                if (eventSink != null) {
                    Map<String, Object> errorEvent = new HashMap<>();
                    errorEvent.put("event", eventType);
                    errorEvent.put("arg1", arg1);
                    errorEvent.put("arg2", arg2);
                    eventSink.success(errorEvent);
                }
                break;
            case FJKNativePlayer.EVENT_VIDEO_SIZE_CHANGED:
                // Cache video size
                videoWidth = arg1;
                videoHeight = arg2;
                
                // Send size_changed event to Flutter (not video_size!)
                Map<String, Object> sizeEvent = new HashMap<>();
                sizeEvent.put("event", "size_changed");
                sizeEvent.put("width", arg1);
                sizeEvent.put("height", arg2);
                if (eventSink != null) {
                    eventSink.success(sizeEvent);
                }
                break;
            default:
                // Send other events as-is (buffering, seek complete, etc.)
                if (eventSink != null) {
                    Map<String, Object> genericEvent = new HashMap<>();
                    genericEvent.put("event", eventType);
                    genericEvent.put("arg1", arg1);
                    genericEvent.put("arg2", arg2);
                    eventSink.success(genericEvent);
                }
                break;
            }
        }

        void release() {
            // Stop any active recording
            if (ffmpegRecorder != null && ffmpegRecorder.isRecording()) {
                try {
                    ffmpegRecorder.stopRecording();
                } catch (Exception e) {
                    Log.w(TAG, "Error stopping recording during release", e);
                }
            }
            
            // Clean up event channel
            if (eventChannel != null) {
                eventChannel.setStreamHandler(null);
            }

            // Clean up method channel
            if (methodChannel != null) {
                methodChannel.setMethodCallHandler(null);
            }

            // Release player resources
            if (player != null) {
                player.release();
            }
            if (surfaceManager != null) {
                surfaceManager.release();
            }
            if (textureEntry != null) {
                textureEntry.release();
            }
        }
    }

    /**
     * Event stream handler for per-player event channels
     */
    private class EventStreamHandler implements EventChannel.StreamHandler {
        private final PlayerInstance instance;

        EventStreamHandler(PlayerInstance instance) {
            this.instance = instance;
        }

        @Override
        public void onListen(Object arguments, EventChannel.EventSink events) {
            instance.eventSink = events;
            
            // Send cached video size immediately if available
            if (instance.videoWidth > 0 && instance.videoHeight > 0) {
                Map<String, Object> args = new HashMap<>();
                args.put("event", "size_changed");
                args.put("width", instance.videoWidth);
                args.put("height", instance.videoHeight);
                events.success(args);
            }
        }

        @Override
        public void onCancel(Object arguments) {
            instance.eventSink = null;
        }
    }
}
