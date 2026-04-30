package com.befovy.fijkplayer;

import android.graphics.Bitmap;
import android.view.Surface;
import java.io.ByteArrayOutputStream;

/**
 * Native Player - Java wrapper for the unified C player
 * Integrates FFmpeg demuxer, MediaCodec decoder, frame queue, and OpenGL
 * renderer
 */
public class FJKNativePlayer {

    // Load native library
    static { System.loadLibrary("fijkplayer_native_player"); }

    // Native player handle (accessed by JNI)
    private long mNativeHandle = 0;

    // Position caching to reduce JNI calls
    private long mCachedPosition = 0;
    private long mLastPositionUpdate = 0;
    private static final long POSITION_CACHE_INTERVAL_MS =
        100; // Cache for 100ms

    // Event callback interface
    public interface EventCallback {
        void onNativeEvent(int eventType, int arg1, int arg2);
    }

    private EventCallback eventCallback;

    /**
     * Event types matching native PlayerEvent enum
     */
    public static final int EVENT_PREPARED = 0;
    public static final int EVENT_VIDEO_SIZE_CHANGED = 1;
    public static final int EVENT_STARTED = 2;
    public static final int EVENT_PAUSED = 3;
    public static final int EVENT_SEEK_COMPLETE = 4;
    public static final int EVENT_COMPLETED = 5;
    public static final int EVENT_ERROR = 6;
    public static final int EVENT_BUFFERING = 7;
    public static final int EVENT_INFO = 8;

    /**
     * Constructor
     */
    public FJKNativePlayer() { mNativeHandle = nativeInit(); }

    /**
     * Get native handle for JNI callbacks
     */
    public long getNativeHandle() { return mNativeHandle; }

    /**
     * Set event callback
     */
    public void setEventCallback(EventCallback callback) {
        this.eventCallback = callback;
    }

    /**
     * Set data source URL
     */
    public void setDataSource(String url) {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        nativeSetDataSource(mNativeHandle, url);
    }

    /**
     * Set display surface
     */
    public void setSurface(Surface surface) {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        nativeSetSurface(mNativeHandle, surface);
    }

    /**
     * Set playback mode for different scenarios
     *
     * @param mode Playback mode: 0=LIVE_LOW_LATENCY, 1=LIVE_WITH_AUDIO,
     *     2=VOD_OPTIMIZED
     * @param bufferMs Custom buffer size in milliseconds (or -1 for default)
     * @param enableAudio Whether to enable audio decoding (ignored if mode
     *     doesn't support audio)
     * @param maxLatencyMs Maximum acceptable latency in milliseconds (or -1 for
     *     default)
     * @param enableFrameDrop Whether to enable frame dropping when behind
     *     schedule
     */
    public void setPlaybackMode(int mode, int bufferMs, boolean enableAudio,
                                int maxLatencyMs, boolean enableFrameDrop) {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        nativeSetPlaybackMode(mNativeHandle, mode, bufferMs,
                              enableAudio ? 1 : 0, maxLatencyMs,
                              enableFrameDrop ? 1 : 0);
    }

    /**
     * Prepare player asynchronously
     * EVENT_PREPARED will be fired when ready
     */
    public void prepareAsync() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        nativePrepareAsync(mNativeHandle);
    }

    /**
     * Start playback
     */
    public void start() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        invalidatePositionCache(); // Invalidate cache on state change
        nativeStart(mNativeHandle);
    }

    /**
     * Pause playback
     */
    public void pause() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        invalidatePositionCache(); // Invalidate cache on state change
        nativePause(mNativeHandle);
    }

    /**
     * Resume playback
     */
    public void resume() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        invalidatePositionCache(); // Invalidate cache on state change
        nativeResume(mNativeHandle);
    }

    /**
     * Stop playback
     */
    public void stop() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        nativeStop(mNativeHandle);
    }

    /**
     * Reset player to IDLE state
     * Stops playback, releases resources, and allows setting a new data source
     */
    public void reset() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        invalidatePositionCache(); // Clear cached position
        nativeReset(mNativeHandle);
    }

    /**
     * Seek to position (milliseconds)
     */
    public void seekTo(long positionMs) {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        invalidatePositionCache(); // Invalidate cache on seek
        nativeSeekTo(mNativeHandle, positionMs);
    }

    /**
     * Get current playback position (milliseconds)
     * Uses caching to reduce expensive JNI calls
     */
    public long getCurrentPosition() {
        if (mNativeHandle == 0) {
            return 0;
        }

        long currentTime = System.currentTimeMillis();

        // Return cached position if within cache interval
        if (currentTime - mLastPositionUpdate < POSITION_CACHE_INTERVAL_MS) {
            // Log.d("NativePlayer", "📱 Position from cache: " +
            // mCachedPosition);
            return mCachedPosition;
        }

        // Update cached position
        mCachedPosition = nativeGetCurrentPosition(mNativeHandle);
        mLastPositionUpdate = currentTime;
        // Log.d("NativePlayer", "🔄 Position from JNI: " + mCachedPosition);

        return mCachedPosition;
    }

    /**
     * Get media duration (milliseconds)
     */
    public long getDuration() {
        if (mNativeHandle == 0) {
            return 0;
        }
        return nativeGetDuration(mNativeHandle);
    }

    /**
     * Get video frame rate (fps)
     */
    public double getFrameRate() {
        if (mNativeHandle == 0) {
            return 0.0;
        }
        return nativeGetFrameRate(mNativeHandle);
    }

    /**
     * Invalidate position cache (call on seek, pause, resume)
     */
    private void invalidatePositionCache() { mLastPositionUpdate = 0; }

    /**
     * Get video width
     */
    public int getVideoWidth() {
        if (mNativeHandle == 0) {
            return 0;
        }
        return nativeGetVideoWidth(mNativeHandle);
    }

    /**
     * Get video height
     */
    public int getVideoHeight() {
        if (mNativeHandle == 0) {
            return 0;
        }
        return nativeGetVideoHeight(mNativeHandle);
    }

    /**
     * Get audio sample rate
     */
    public int getAudioSampleRate() {
        if (mNativeHandle == 0) {
            return 0;
        }
        return nativeGetAudioSampleRate(mNativeHandle);
    }

    /**
     * Get audio channel count
     */
    public int getAudioChannels() {
        if (mNativeHandle == 0) {
            return 0;
        }
        return nativeGetAudioChannels(mNativeHandle);
    }

    /**
     * Check if audio stream exists
     */
    public boolean hasAudio() {
        if (mNativeHandle == 0) {
            return false;
        }
        return nativeHasAudio(mNativeHandle);
    }

    /**
     * Check if playing
     */
    public boolean isPlaying() {
        if (mNativeHandle == 0) {
            return false;
        }
        return nativeIsPlaying(mNativeHandle);
    }

    /**
     * Render one frame (call from rendering thread)
     */
    public void renderFrame() {
        if (mNativeHandle == 0) {
            return;
        }
        nativeRenderFrame(mNativeHandle);
    }

    /**
     * Set volume (0.0 to 1.0)
     */
    public void setVolume(float volume) {
        if (mNativeHandle == 0) {
            return;
        }
        nativeSetVolume(mNativeHandle, volume);
    }

    /**
     * Release player resources
     */
    public void release() {
        if (mNativeHandle != 0) {
            nativeRelease(mNativeHandle);
            mNativeHandle = 0;
        }
    }

    /**
     * Called from native code when an event occurs
     * This method is invoked via JNI callback
     */
    @SuppressWarnings("unused")
    private void onNativeEvent(int eventType, int arg1, int arg2) {
        if (eventCallback != null) {
            eventCallback.onNativeEvent(eventType, arg1, arg2);
        }
    }

    // Native method declarations
    private native long nativeInit();
    private native void nativeSetDataSource(long handle, String url);
    private native void nativeSetSurface(long handle, Surface surface);
    private native void nativeSetPlaybackMode(long handle, int mode,
                                              int bufferMs, int enableAudio,
                                              int maxLatencyMs,
                                              int enableFrameDrop);
    private native void nativePrepareAsync(long handle);
    private native void nativeStart(long handle);
    private native void nativePause(long handle);
    private native void nativeResume(long handle);
    private native void nativeStop(long handle);
    private native void nativeReset(long handle);
    private native void nativeSeekTo(long handle, long positionMs);
    private native long nativeGetCurrentPosition(long handle);
    private native long nativeGetDuration(long handle);
    private native double nativeGetFrameRate(long handle);
    private native int nativeGetVideoWidth(long handle);
    private native int nativeGetVideoHeight(long handle);
    private native int nativeGetAudioSampleRate(long handle);
    private native int nativeGetAudioChannels(long handle);
    private native boolean nativeHasAudio(long handle);
    private native boolean nativeIsPlaying(long handle);
    private native void nativeRenderFrame(long handle);
    private native void nativeSetVolume(long handle, float volume);
    private native void nativeRelease(long handle);
    private native byte[] nativeSnapshot(long handle, int[] outDims);

    /**
     * Capture a snapshot of the current video frame as PNG bytes.
     * @return PNG byte array, or null if no frame is available.
     */
    public byte[] snapshot() {
        if (mNativeHandle == 0) return null;
        int[] dims = new int[2];
        byte[] rgba = nativeSnapshot(mNativeHandle, dims);
        if (rgba == null || dims[0] <= 0 || dims[1] <= 0) return null;
        
        int width = dims[0];
        int height = dims[1];
        
        // Convert RGBA byte array to ARGB int array for Bitmap
        int[] pixels = new int[width * height];
        for (int i = 0; i < pixels.length; i++) {
            int r = rgba[i * 4] & 0xFF;
            int g = rgba[i * 4 + 1] & 0xFF;
            int b = rgba[i * 4 + 2] & 0xFF;
            int a = rgba[i * 4 + 3] & 0xFF;
            pixels[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
        
        Bitmap bitmap = Bitmap.createBitmap(pixels, width, height, Bitmap.Config.ARGB_8888);
        ByteArrayOutputStream baos = new ByteArrayOutputStream();
        bitmap.compress(Bitmap.CompressFormat.PNG, 100, baos);
        bitmap.recycle();
        return baos.toByteArray();
    }
}
