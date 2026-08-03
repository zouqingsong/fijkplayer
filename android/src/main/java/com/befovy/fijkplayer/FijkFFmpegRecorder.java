package com.befovy.fijkplayer;

/**
 * FFmpeg Recording Interface - Clean API for video recording
 * Wraps native ffmpeg_recorder.c functionality
 */
public class FijkFFmpegRecorder {
    
    /**
     * Recording event callback interface
     */
    public interface RecordingCallback {
        void onRecordingStarted();
        void onRecordingStopped();
        void onRecordingError(String error);
    }
    
    private RecordingCallback callback;
    
    /**
     * Set recording event callback
     */
    public void setRecordingCallback(RecordingCallback callback) {
        this.callback = callback;
    }
    
    /**
     * Start recording RTSP stream to file
     * @param rtspUrl RTSP stream URL
     * @param outputPath Output file path (e.g., "/path/to/output.mp4")
     * @return true if recording started successfully
     */
    public boolean startRecording(String rtspUrl, String outputPath) {
        try {
            boolean result = nativeStartRecording(rtspUrl, outputPath);
            if (result && callback != null) {
                callback.onRecordingStarted();
            }
            return result;
        } catch (Exception e) {
            if (callback != null) {
                callback.onRecordingError("Failed to start recording: " + e.getMessage());
            }
            return false;
        }
    }
    
    /**
     * Stop current recording
     * @return true if recording stopped successfully
     */
    public boolean stopRecording() {
        try {
            boolean result = nativeStopRecording();
            if (result && callback != null) {
                callback.onRecordingStopped();
            }
            return result;
        } catch (Exception e) {
            if (callback != null) {
                callback.onRecordingError("Failed to stop recording: " + e.getMessage());
            }
            return false;
        }
    }
    
    /**
     * Check if currently recording
     * @return true if recording is active
     */
    public boolean isRecording() {
        return nativeIsRecording();
    }
    
    // Native method declarations (these map to ffmpeg_recorder.c functions)
    private native boolean nativeStartRecording(String rtspUrl, String outputPath);
    private native boolean nativeStopRecording();
    private native boolean nativeIsRecording();
    
    // Load native library
    static {
        System.loadLibrary("fijkplayer_ffmpeg_recorder");
    }
}