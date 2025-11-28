package com.befovy.fijkplayer;

import android.content.Context;
import android.util.Log;
import android.view.Surface;

import androidx.annotation.NonNull;

import java.util.HashMap;
import java.util.Map;

import io.flutter.plugin.common.MethodCall;
import io.flutter.plugin.common.MethodChannel;
import io.flutter.view.TextureRegistry;

/**
 * Test handler for NativePlayer - Direct method channel access
 * Bypasses FijkPlayer.java for quick validation testing
 */
public class NativePlayerTestHandler implements MethodChannel.MethodCallHandler {
    
    private static final String TAG = "NativePlayerTest";
    
    private final TextureRegistry textureRegistry;
    private final Context context;
    private NativePlayer player;
    private TextureRegistry.SurfaceTextureEntry textureEntry;
    private SurfaceTextureManager surfaceTextureManager;
    private MethodChannel.Result prepareResult;
    
    public NativePlayerTestHandler(Context context, TextureRegistry textureRegistry) {
        this.context = context;
        this.textureRegistry = textureRegistry;
    }
    
    @Override
    public void onMethodCall(@NonNull MethodCall call, @NonNull MethodChannel.Result result) {
        Log.d(TAG, "Method call: " + call.method);
        
        switch (call.method) {
            case "create":
                handleCreate(result);
                break;
            case "setDataSource":
                handleSetDataSource(call, result);
                break;
            case "prepare":
                handlePrepare(result);
                break;
            case "start":
                handleStart(result);
                break;
            case "pause":
                handlePause(result);
                break;
            case "stop":
                handleStop(result);
                break;
            case "seekTo":
                handleSeekTo(call, result);
                break;
            case "getPosition":
                handleGetPosition(result);
                break;
            case "getDuration":
                handleGetDuration(result);
                break;
            case "getFrameRate":
                handleGetFrameRate(result);
                break;
            case "getVideoSize":
                handleGetVideoSize(result);
                break;
            case "isPlaying":
                handleIsPlaying(result);
                break;
            case "release":
                handleRelease(result);
                break;
            case "testHttpsConnectivity":
                handleTestHttpsConnectivity(call, result);
                break;
            default:
                result.notImplemented();
        }
    }
    
    private void handleCreate(MethodChannel.Result result) {
        try {
            // Create texture for video rendering
            textureEntry = textureRegistry.createSurfaceTexture();
            
            // Create native player first
            player = new NativePlayer();
            
            // Create SurfaceTextureManager with the TextureEntry
            // This allows the manager to notify Flutter when frames are available
            surfaceTextureManager = new SurfaceTextureManager(textureEntry);
            
            // Set video size for buffer allocation
            // Will be updated when video info is available
            surfaceTextureManager.setBufferSize(1920, 1080);
            
            // Get Surface from manager and set to player
            Surface surface = surfaceTextureManager.getSurface();
            player.setSurface(surface);
            
            // Set event callback
            player.setEventCallback(this::onPlayerEvent);
            
            Map<String, Object> reply = new HashMap<>();
            reply.put("textureId", textureEntry.id());
            result.success(reply);
            
            Log.i(TAG, "Player created with texture ID: " + textureEntry.id());
        } catch (Exception e) {
            Log.e(TAG, "Failed to create player", e);
            result.error("CREATE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleSetDataSource(MethodCall call, MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        String url = call.argument("url");
        if (url == null) {
            result.error("INVALID_URL", "URL is required", null);
            return;
        }
        
        try {
            player.setDataSource(url);
            result.success(null);
            Log.i(TAG, "Data source set: " + url);
        } catch (Exception e) {
            Log.e(TAG, "Failed to set data source", e);
            result.error("SET_DATASOURCE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handlePrepare(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            // Store result for async callback
            prepareResult = result;
            player.prepareAsync();
            Log.i(TAG, "Prepare async started");
            // Result will be sent in onPlayerEvent when prepared
        } catch (Exception e) {
            Log.e(TAG, "Failed to prepare", e);
            result.error("PREPARE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleStart(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            player.start();
            result.success(null);
            Log.i(TAG, "Playback started");
        } catch (Exception e) {
            Log.e(TAG, "Failed to start", e);
            result.error("START_FAILED", e.getMessage(), null);
        }
    }
    
    private void handlePause(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            player.pause();
            result.success(null);
            Log.i(TAG, "Playback paused");
        } catch (Exception e) {
            Log.e(TAG, "Failed to pause", e);
            result.error("PAUSE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleStop(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            player.stop();
            result.success(null);
            Log.i(TAG, "Playback stopped");
        } catch (Exception e) {
            Log.e(TAG, "Failed to stop", e);
            result.error("STOP_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleSeekTo(MethodCall call, MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        Integer position = call.argument("position");
        if (position == null) {
            result.error("INVALID_POSITION", "Position is required", null);
            return;
        }
        
        try {
            player.seekTo(position.longValue());
            result.success(null);
            Log.i(TAG, "Seek to: " + position);
        } catch (Exception e) {
            Log.e(TAG, "Failed to seek", e);
            result.error("SEEK_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleGetPosition(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            long position = player.getCurrentPosition();
            result.success(position);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get position", e);
            result.error("GET_POSITION_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleGetDuration(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            long duration = player.getDuration();
            result.success(duration);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get duration", e);
            result.error("GET_DURATION_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleGetFrameRate(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            double frameRate = player.getFrameRate();
            result.success(frameRate);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get frame rate", e);
            result.error("GET_FRAMERATE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleGetVideoSize(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            Map<String, Object> size = new HashMap<>();
            size.put("width", player.getVideoWidth());
            size.put("height", player.getVideoHeight());
            result.success(size);
        } catch (Exception e) {
            Log.e(TAG, "Failed to get video size", e);
            result.error("GET_SIZE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleIsPlaying(MethodChannel.Result result) {
        if (player == null) {
            result.error("NO_PLAYER", "Player not created", null);
            return;
        }
        
        try {
            boolean playing = player.isPlaying();
            result.success(playing);
        } catch (Exception e) {
            Log.e(TAG, "Failed to check playing state", e);
            result.error("IS_PLAYING_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleRelease(MethodChannel.Result result) {
        try {
            if (player != null) {
                player.release();
                player = null;
                Log.i(TAG, "Player released");
            }
            
            if (surfaceTextureManager != null) {
                surfaceTextureManager.release();
                surfaceTextureManager = null;
                Log.i(TAG, "SurfaceTextureManager released");
            }
            
            if (textureEntry != null) {
                textureEntry.release();
                textureEntry = null;
            }
            
            result.success(null);
        } catch (Exception e) {
            Log.e(TAG, "Failed to release", e);
            result.error("RELEASE_FAILED", e.getMessage(), null);
        }
    }
    
    private void handleTestHttpsConnectivity(MethodCall call, MethodChannel.Result result) {
        String testUrl = call.argument("url");
        if (testUrl == null) {
            testUrl = "https://httpbin.org/get";
        }
        
        Log.i(TAG, "Testing HTTPS connectivity to: " + testUrl);
        
        // Run network operation on background thread to avoid NetworkOnMainThreadException
        new Thread(() -> {
            try {
                java.net.URL url = new java.net.URL(testUrl);
                java.net.HttpURLConnection connection = (java.net.HttpURLConnection) url.openConnection();
                connection.setRequestMethod("HEAD");
                connection.setConnectTimeout(5000); // 5 second timeout
                connection.setReadTimeout(5000);
                
                int responseCode = connection.getResponseCode();
                connection.disconnect();
                
                boolean success = responseCode == 200;
                Log.i(TAG, "HTTPS test result: " + responseCode + " (success: " + success + ")");
                result.success(success);
                
            } catch (Exception e) {
                Log.e(TAG, "HTTPS connectivity test failed", e);
                result.success(false); // Return false instead of error for connectivity issues
            }
        }).start();
    }
    
    private void onPlayerEvent(int eventType, int arg1, int arg2) {
        String eventName = getEventName(eventType);
        Log.i(TAG, "Player event: " + eventName + " (" + eventType + "), arg1=" + arg1 + ", arg2=" + arg2);
        
        // Handle async prepare completion
        if (eventType == NativePlayer.EVENT_PREPARED && prepareResult != null) {
            int width = player.getVideoWidth();
            int height = player.getVideoHeight();
            
            // Update SurfaceTexture buffer size with actual video dimensions
            if (surfaceTextureManager != null && width > 0 && height > 0) {
                surfaceTextureManager.setBufferSize(width, height);
                Log.i(TAG, "Updated buffer size to " + width + "x" + height);
            }
            
            Map<String, Object> info = new HashMap<>();
            info.put("width", width);
            info.put("height", height);
            info.put("duration", player.getDuration());
            prepareResult.success(info);
            prepareResult = null;
            Log.i(TAG, "Prepare completed: " + info);
        }
        
        if (eventType == NativePlayer.EVENT_ERROR && prepareResult != null) {
            prepareResult.error("PREPARE_ERROR", "Failed to prepare: code=" + arg1, null);
            prepareResult = null;
        }
    }
    
    private String getEventName(int eventType) {
        switch (eventType) {
            case NativePlayer.EVENT_PREPARED: return "PREPARED";
            case NativePlayer.EVENT_STARTED: return "STARTED";
            case NativePlayer.EVENT_PAUSED: return "PAUSED";
            case NativePlayer.EVENT_STOPPED: return "STOPPED";
            case NativePlayer.EVENT_COMPLETED: return "COMPLETED";
            case NativePlayer.EVENT_ERROR: return "ERROR";
            case NativePlayer.EVENT_VIDEO_SIZE_CHANGED: return "VIDEO_SIZE_CHANGED";
            case NativePlayer.EVENT_BUFFERING: return "BUFFERING";
            case NativePlayer.EVENT_SEEK_COMPLETE: return "SEEK_COMPLETE";
            default: return "UNKNOWN";
        }
    }
}
