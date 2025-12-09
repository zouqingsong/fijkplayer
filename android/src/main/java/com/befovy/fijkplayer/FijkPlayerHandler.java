package com.befovy.fijkplayer;

import android.content.Context;
import android.util.Log;
import android.view.Surface;
import androidx.annotation.NonNull;
import io.flutter.plugin.common.BinaryMessenger;
import io.flutter.plugin.common.EventChannel;
import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.view.TextureRegistry;
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
        Log.i(TAG,
              "FijkPlayerHandler initialized with per-player channel support");
    }

    @Override
    public void onMethodCall(@NonNull MethodCall call,
                             @NonNull MethodChannel.Result result) {
        Log.d(TAG, "Method call: " + call.method);

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
        Log.d(TAG, "Per-player method call: player=" + playerId +
                       ", method=" + call.method);

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
                Log.i(TAG,
                      "Data source set: player=" + playerId + ", url=" + url);

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
                // NativePlayer doesn't have reset(), so we stop and seek to 0
                instance.player.stop();
                instance.player.seekTo(0);
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
        default:
            Log.w(TAG, "Unimplemented per-player method: " + call.method);
            result.notImplemented();
            break;
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
        Log.i(TAG,
              "setupSurface: player=" + playerId + ", textureId=" + textureId);
        result.success(textureId);
    }

    private void handleLogLevel(MethodCall call, MethodChannel.Result result) {
        // Log level control - just acknowledge for now
        Integer level = call.argument("level");
        Log.i(TAG, "Log level set to: " + level);
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
            currentState = newState;

            Map<String, Object> event = new HashMap<>();
            event.put("event", "state_change");
            event.put("new", newState);
            event.put("old", oldState);

            if (eventSink != null) {
                eventSink.success(event);
                Log.d(TAG, "State change: " + oldState + " -> " + newState);
            }
        }

        void handleNativeEvent(int eventType, int arg1, int arg2) {
            Log.d(TAG, "Native event: type=" + eventType + ", arg1=" + arg1 +
                           ", arg2=" + arg2);

            // Map native events to FijkState transitions
            switch (eventType) {
            case FJKNativePlayer.EVENT_PREPARED:
                sendStateChange(STATE_PREPARED);
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
                    Log.i(TAG, "Sent size_changed event: " + arg1 + "x" + arg2);
                } else {
                    Log.i(TAG, "Cached video size (no listener yet): " + arg1 + "x" + arg2);
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
            Log.i(TAG, "EventChannel listener attached for player " + instance.playerId);
            
            // Send cached video size immediately if available
            if (instance.videoWidth > 0 && instance.videoHeight > 0) {
                Map<String, Object> args = new HashMap<>();
                args.put("event", "size_changed");
                args.put("width", instance.videoWidth);
                args.put("height", instance.videoHeight);
                events.success(args);
                Log.i(TAG, "Sent cached video size: " + instance.videoWidth + "x" + instance.videoHeight);
            }
        }

        @Override
        public void onCancel(Object arguments) {
            instance.eventSink = null;
            Log.i(TAG, "EventChannel listener cancelled for player " + instance.playerId);
        }
    }
}
