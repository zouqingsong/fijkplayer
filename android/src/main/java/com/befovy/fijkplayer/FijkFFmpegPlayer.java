package com.befovy.fijkplayer;

import android.view.Surface;

/**
 * Enhanced FFmpeg Player - Clean interface for audio/video playback
 * Wraps NativePlayer with additional audio controls and clean API
 */
public class FijkFFmpegPlayer {
    
    private NativePlayer nativePlayer;
    private String currentUrl;
    private boolean isInitialized = false;
    
    // Audio settings
    private float leftVolume = 1.0f;
    private float rightVolume = 1.0f;
    private boolean isMuted = false;
    
    /**
     * Event callback interface for FFmpeg player events
     */
    public interface EventCallback {
        void onPrepared();
        void onStarted();
        void onPaused();
        void onStopped();
        void onCompleted();
        void onError(String error);
        void onVideoSizeChanged(int width, int height);
        void onBuffering(boolean buffering);
        void onSeekComplete();
    }
    
    private EventCallback eventCallback;
    
    /**
     * Constructor
     */
    public FijkFFmpegPlayer() {
        nativePlayer = new NativePlayer();
        nativePlayer.setEventCallback(this::handleNativeEvent);
        isInitialized = true;
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
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        this.currentUrl = url;
        nativePlayer.setDataSource(url);
    }
    
    /**
     * Get current data source URL
     */
    public String getDataSource() {
        return currentUrl;
    }
    
    /**
     * Set display surface
     */
    public void setSurface(Surface surface) {
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        nativePlayer.setSurface(surface);
    }
    
    /**
     * Prepare player asynchronously
     */
    public void prepareAsync() {
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        nativePlayer.prepareAsync();
    }
    
    /**
     * Start playback
     */
    public void start() {
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        nativePlayer.start();
    }
    
    /**
     * Pause playback
     */
    public void pause() {
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        nativePlayer.pause();
    }
    
    /**
     * Resume playback
     */
    public void resume() {
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        nativePlayer.resume();
    }
    
    /**
     * Stop playback
     */
    public void stop() {
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        nativePlayer.stop();
    }
    
    /**
     * Seek to position (milliseconds)
     */
    public void seekTo(long positionMs) {
        if (!isInitialized) {
            throw new IllegalStateException("Player not initialized");
        }
        nativePlayer.seekTo(positionMs);
    }
    
    /**
     * Get current playback position (milliseconds)
     */
    public long getCurrentPosition() {
        if (!isInitialized) {
            return 0;
        }
        return nativePlayer.getCurrentPosition();
    }
    
    /**
     * Get media duration (milliseconds)
     */
    public long getDuration() {
        if (!isInitialized) {
            return 0;
        }
        return nativePlayer.getDuration();
    }
    
    /**
     * Get video frame rate
     */
    public double getFrameRate() {
        if (!isInitialized) {
            return 0.0;
        }
        return nativePlayer.getFrameRate();
    }
    
    /**
     * Get video dimensions
     */
    public int getVideoWidth() {
        if (!isInitialized) {
            return 0;
        }
        return nativePlayer.getVideoWidth();
    }
    
    public int getVideoHeight() {
        if (!isInitialized) {
            return 0;
        }
        return nativePlayer.getVideoHeight();
    }
    
    /**
     * Check if playing
     */
    public boolean isPlaying() {
        if (!isInitialized) {
            return false;
        }
        return nativePlayer.isPlaying();
    }
    
    /**
     * Enhanced audio controls
     */
    
    /**
     * Set stereo volume (left and right channels)
     */
    public void setVolume(float leftVolume, float rightVolume) {
        this.leftVolume = Math.max(0.0f, Math.min(1.0f, leftVolume));
        this.rightVolume = Math.max(0.0f, Math.min(1.0f, rightVolume));
        updateNativeVolume();
    }
    
    /**
     * Set mono volume (both channels)
     */
    public void setVolume(float volume) {
        setVolume(volume, volume);
    }
    
    /**
     * Get left channel volume
     */
    public float getLeftVolume() {
        return leftVolume;
    }
    
    /**
     * Get right channel volume
     */
    public float getRightVolume() {
        return rightVolume;
    }
    
    /**
     * Mute/unmute audio
     */
    public void setMuted(boolean muted) {
        this.isMuted = muted;
        updateNativeVolume();
    }
    
    /**
     * Check if muted
     */
    public boolean isMuted() {
        return isMuted;
    }
    
    /**
     * Update native player volume based on current settings
     */
    private void updateNativeVolume() {
        if (!isInitialized) {
            return;
        }
        
        if (isMuted) {
            nativePlayer.setVolume(0.0f);
        } else {
            // Use average of left and right for mono native player
            float averageVolume = (leftVolume + rightVolume) / 2.0f;
            nativePlayer.setVolume(averageVolume);
        }
    }
    
    /**
     * Render frame (for custom rendering)
     */
    public void renderFrame() {
        if (!isInitialized) {
            return;
        }
        nativePlayer.renderFrame();
    }
    
    /**
     * Release player resources
     */
    public void release() {
        if (isInitialized) {
            nativePlayer.release();
            isInitialized = false;
        }
    }
    
    /**
     * Handle native player events and convert to FFmpeg player events
     */
    private void handleNativeEvent(int eventType, int arg1, int arg2) {
        if (eventCallback == null) {
            return;
        }
        
        switch (eventType) {
            case NativePlayer.EVENT_PREPARED:
                eventCallback.onPrepared();
                break;
            case NativePlayer.EVENT_STARTED:
                eventCallback.onStarted();
                break;
            case NativePlayer.EVENT_PAUSED:
                eventCallback.onPaused();
                break;
            case NativePlayer.EVENT_STOPPED:
                eventCallback.onStopped();
                break;
            case NativePlayer.EVENT_COMPLETED:
                eventCallback.onCompleted();
                break;
            case NativePlayer.EVENT_ERROR:
                eventCallback.onError("Native player error: " + arg1);
                break;
            case NativePlayer.EVENT_VIDEO_SIZE_CHANGED:
                eventCallback.onVideoSizeChanged(arg1, arg2);
                break;
            case NativePlayer.EVENT_BUFFERING:
                eventCallback.onBuffering(arg1 == 1);
                break;
            case NativePlayer.EVENT_SEEK_COMPLETE:
                eventCallback.onSeekComplete();
                break;
        }
    }
}