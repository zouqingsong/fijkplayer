package com.befovy.fijkplayer;

import android.content.Context;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import java.util.HashMap;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicInteger;

import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.view.TextureRegistry;

/**
 * Production handler for FijkPlayer - Backward compatible with mobile_fijk_player
 * 
 * This handler provides the same API as the old ijkplayer-based implementation,
 * but uses the new NativePlayer (FFmpeg-based) implementation under the hood.
 * 
 * Channel: befovy.com/fijk
 */
public class FijkPlayerHandler implements MethodChannel.MethodCallHandler {
    
    private static final String TAG = "FijkPlayerHandler";
    
    private final Context context;
    private final TextureRegistry textureRegistry;
    private final AtomicInteger playerIdCounter = new AtomicInteger(0);
    
    // Map of player ID to player instance
    private final ConcurrentHashMap<Integer, PlayerInstance> players = new ConcurrentHashMap<>();
    
    public FijkPlayerHandler(Context context, TextureRegistry textureRegistry) {
        this.context = context;
        this.textureRegistry = textureRegistry;
        Log.i(TAG, "FijkPlayerHandler initialized");
    }
    
    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
        Log.d(TAG, "Method call: " + call.method);
        
        switch (call.method) {
            case "init":
            case "createPlayer":  // Alias for backward compatibility
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
            TextureRegistry.SurfaceTextureEntry textureEntry = textureRegistry.createSurfaceTexture();
            long textureId = textureEntry.id();
            
            // Create native player
            NativePlayer player = new NativePlayer();
            
            // Create surface texture manager
            SurfaceTextureManager surfaceManager = new SurfaceTextureManager(textureEntry);
            
            // Set surface on player
            Surface surface = surfaceManager.getSurface();
            player.setSurface(surface);
            
            // Store player instance
            PlayerInstance instance = new PlayerInstance(playerId, player, textureEntry, surfaceManager);
            players.put(playerId, instance);
            
            Log.i(TAG, "Player created: id=" + playerId + ", texture=" + textureId);
            
            Map<String, Object> resultMap = new HashMap<>();
            resultMap.put("playerId", playerId);
            resultMap.put("textureId", textureId);
            result.success(resultMap);
            
        } catch (Exception e) {
            Log.e(TAG, "Failed to create player", e);
            result.error("CREATE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleSetDataSource(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        String url = call.argument("url");
        
        if (playerId == null || url == null) {
            result.error("INVALID_ARGS", "Missing playerId or url", null);
            return;
        }
        
        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
    
    private void handlePrepareAsync(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        
        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }
        
        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
            return;
        }
        
        try {
            // NativePlayer doesn't have reset(), so we stop and seek to 0
            instance.player.stop();
            instance.player.seekTo(0);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
    
    private void handleGetPosition(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        
        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }
        
        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
    
    private void handleGetDuration(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        
        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }
        
        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
            return;
        }
        
        try {
            // NativePlayer doesn't support setSpeed yet - return success for compatibility
            Log.w(TAG, "setSpeed not yet implemented in NativePlayer: player=" + playerId + ", speed=" + speed);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to set speed", e);
            result.error("SET_SPEED_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleSetupSurface(MethodCall call, MethodChannel.Result result) {
        // Surface is already set up in init, so this is a no-op for compatibility
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
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
            return;
        }
        
        try {
            // NativePlayer doesn't support setLooping yet - return success for compatibility
            Log.w(TAG, "setLooping not yet implemented in NativePlayer: player=" + playerId + ", loop=" + loop);
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to set loop", e);
            result.error("SET_LOOP_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleGetVideoWidth(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        
        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }
        
        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
    
    private void handleGetVideoHeight(MethodCall call, MethodChannel.Result result) {
        Integer playerId = call.argument("pid");
        
        if (playerId == null) {
            result.error("INVALID_ARGS", "Missing playerId", null);
            return;
        }
        
        PlayerInstance instance = players.get(playerId);
        if (instance == null) {
            result.error("PLAYER_NOT_FOUND", "Player " + playerId + " not found", null);
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
     * Internal class to hold player instance data
     */
    private static class PlayerInstance {
        final int playerId;
        final NativePlayer player;
        final TextureRegistry.SurfaceTextureEntry textureEntry;
        final SurfaceTextureManager surfaceManager;
        
        PlayerInstance(int playerId, NativePlayer player, 
                      TextureRegistry.SurfaceTextureEntry textureEntry,
                      SurfaceTextureManager surfaceManager) {
            this.playerId = playerId;
            this.player = player;
            this.textureEntry = textureEntry;
            this.surfaceManager = surfaceManager;
        }
        
        void release() {
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
}
