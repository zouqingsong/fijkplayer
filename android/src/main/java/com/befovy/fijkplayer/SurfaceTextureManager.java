package com.befovy.fijkplayer;

import android.graphics.SurfaceTexture;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Surface;
import io.flutter.view.TextureRegistry;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicLong;

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
    private Handler callbackHandler;
    private AtomicLong frameCount = new AtomicLong(0);
    private long lastFrameTime = 0;

    private final SurfaceTexture.OnFrameAvailableListener frameListener =
        new SurfaceTexture.OnFrameAvailableListener() {
            @Override
            public void onFrameAvailable(SurfaceTexture st) {
                if (isReleased.get() || textureEntry == null) {
                    return;
                }

                long count = frameCount.incrementAndGet();
                
                // Note: Flutter should automatically handle texture updates
                // Position updates currently trigger UI refreshes
                
                // Just track frame arrivals for debugging
                if (count % 240 == 0) {
                    long now = System.nanoTime() / 1000000;
                    long interval = (lastFrameTime > 0) ? (now - lastFrameTime) : 0;
                    lastFrameTime = now;
                    Log.i(TAG, "📺 Frame #" + count + " arrived (interval=" + interval + "ms)");
                }
            }
        };

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

        // Set frame listener on main thread with Looper
        callbackHandler = new Handler(Looper.getMainLooper());
        surfaceTexture.setOnFrameAvailableListener(frameListener, callbackHandler);

        // Create Surface for MediaCodec
        surface = new Surface(surfaceTexture);

        Log.i(TAG, "SurfaceTextureManager initialized - will consume frames in onFrameAvailable");
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
        
        Log.i(TAG, "SurfaceTextureManager released. Total frames: " + frameCount.get());
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
