package com.befovy.fijkplayer;

import android.view.Surface;

/**
 * Native Player - Java wrapper for the unified C player
 * Integrates FFmpeg demuxer, MediaCodec decoder, frame queue, and OpenGL renderer
 */
public class NativePlayer {
    
    // Load native library
    static {
        System.loadLibrary("fijkplayer_native_player");
    }
    
    // Native player handle (accessed by JNI)
    private long mNativeHandle = 0;
    
    // Event callback interface
    public interface EventCallback {
        void onNativeEvent(int eventType, int arg1, int arg2);
    }
    
    private EventCallback eventCallback;
    
    /**
     * Event types matching native PlayerEvent enum
     */
    public static final int EVENT_PREPARED = 0;
    public static final int EVENT_STARTED = 1;
    public static final int EVENT_PAUSED = 2;
    public static final int EVENT_STOPPED = 3;
    public static final int EVENT_COMPLETED = 4;
    public static final int EVENT_ERROR = 5;
    public static final int EVENT_VIDEO_SIZE_CHANGED = 6;
    public static final int EVENT_BUFFERING = 7;
    public static final int EVENT_SEEK_COMPLETE = 8;
    
    /**
     * Constructor
     */
    public NativePlayer() {
        mNativeHandle = nativeInit();
    }
    
    /**
     * Get native handle for JNI callbacks
     */
    public long getNativeHandle() {
        return mNativeHandle;
    }
    
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
        nativeStart(mNativeHandle);
    }
    
    /**
     * Pause playback
     */
    public void pause() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        nativePause(mNativeHandle);
    }
    
    /**
     * Resume playback
     */
    public void resume() {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
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
     * Seek to position (milliseconds)
     */
    public void seekTo(long positionMs) {
        if (mNativeHandle == 0) {
            throw new IllegalStateException("Native player not initialized");
        }
        nativeSeekTo(mNativeHandle, positionMs);
    }
    
    /**
     * Get current playback position (milliseconds)
     */
    public long getCurrentPosition() {
        if (mNativeHandle == 0) {
            return 0;
        }
        return nativeGetCurrentPosition(mNativeHandle);
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
    private native void nativePrepareAsync(long handle);
    private native void nativeStart(long handle);
    private native void nativePause(long handle);
    private native void nativeResume(long handle);
    private native void nativeStop(long handle);
    private native void nativeSeekTo(long handle, long positionMs);
    private native long nativeGetCurrentPosition(long handle);
    private native long nativeGetDuration(long handle);
    private native int nativeGetVideoWidth(long handle);
    private native int nativeGetVideoHeight(long handle);
    private native boolean nativeIsPlaying(long handle);
    private native void nativeRenderFrame(long handle);
    private native void nativeSetVolume(long handle, float volume);
    private native void nativeRelease(long handle);
}
