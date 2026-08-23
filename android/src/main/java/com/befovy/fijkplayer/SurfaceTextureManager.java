package com.befovy.fijkplayer;

import android.graphics.SurfaceTexture;
import android.util.Log;
import android.view.Surface;
import io.flutter.view.TextureRegistry;
import java.util.concurrent.atomic.AtomicBoolean;

/**
 * Manages SurfaceTexture lifecycle for MediaCodec rendering.
 * 
 * Flutter's raster thread calls updateTexImage() at vsync to pull frames.
 * If MediaCodec produces faster than Flutter consumes, buffers fill up.
 * Solution: Drop frames in native code when all buffers are used.
 */
public class SurfaceTextureManager {
    private static final String TAG = "SurfaceTextureManager";

    private SurfaceTexture surfaceTexture;
    private Surface surface;
    private TextureRegistry.SurfaceTextureEntry textureEntry;
    private AtomicBoolean isReleased = new AtomicBoolean(false);

    /**
     * Initialize the SurfaceTextureManager with a Flutter SurfaceTextureEntry
     */
    public SurfaceTextureManager(TextureRegistry.SurfaceTextureEntry entry) {
        this.textureEntry = entry;
        this.surfaceTexture = entry.surfaceTexture();

        // Set buffer size for MediaCodec
        surfaceTexture.setDefaultBufferSize(1920, 1080);

        // Don't detach - Flutter needs GL context for rendering
        // We'll handle buffer management in native code instead

        // Intentionally not calling setOnFrameAvailableListener here: a
        // SurfaceTexture only supports one listener at a time, and this
        // entry's Flutter engine already installed its own listener (which
        // schedules the vsync-timed updateTexImage() pull) when the texture
        // was created. Overriding it here starved the engine of that signal,
        // so frames only got pulled as a side effect of unrelated repaints
        // (e.g. the position-update timer), adding latency and capping the
        // effective frame rate.

        // Create Surface for MediaCodec
        surface = new Surface(surfaceTexture);

        Log.i(TAG, "SurfaceTextureManager initialized");
    }

    /**
     * Get the Surface for MediaCodec rendering
     */
    public Surface getSurface() { return surface; }

    /**
     * Get the underlying SurfaceTexture
     */
    public SurfaceTexture getSurfaceTexture() { return surfaceTexture; }

    /**
     * Release resources
     */
    public void release() {
        Log.i(TAG, "Releasing SurfaceTextureManager");

        isReleased.set(true);

        if (surface != null) {
            surface.release();
            surface = null;
        }

        // Note: Don't release surfaceTexture - Flutter manages it
        surfaceTexture = null;
        
        Log.i(TAG, "SurfaceTextureManager released");
    }

    /**
     * Set buffer size for the SurfaceTexture
     */
    public void setBufferSize(int width, int height) {
        if (surfaceTexture != null && !isReleased.get()) {
            surfaceTexture.setDefaultBufferSize(width, height);
            Log.d(TAG, "Buffer size set to " + width + "x" + height);
        }
    }
    
    /**
     * Notify Flutter about texture updates
     * Simple implementation that doesn't require EGL context
     */
    private void notifyFlutterTextureUpdate() {
        // For now, Flutter should handle texture updates automatically
        // when MediaCodec writes new frames to the Surface
    }
}
