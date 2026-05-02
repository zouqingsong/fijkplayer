package com.befovy.fijkplayer;

import java.util.List;
import java.util.Map;

/**
 * FFmpeg Kit - Expose FFmpeg functions like ffmpeg_kit_flutter
 * Provides access to FFmpeg command execution, filters, and utilities
 */
public class FijkFFmpegKit {
    
    /**
     * FFmpeg execution callback interface
     */
    public interface ExecutionCallback {
        void onExecutionStarted(int executionId);
        void onExecutionCompleted(int executionId, int returnCode);
        void onExecutionFailed(int executionId, String error);
        void onOutput(int executionId, String output);
    }
    
    /**
     * Media information callback interface
     */
    public interface MediaInfoCallback {
        void onMediaInfoReady(MediaInfo mediaInfo);
        void onMediaInfoError(String error);
    }
    
    /**
     * Media information container
     */
    public static class MediaInfo {
        public final String format;
        public final long duration;
        public final long bitrate;
        public final int videoWidth;
        public final int videoHeight;
        public final double frameRate;
        public final String videoCodec;
        public final String audioCodec;
        public final int audioChannels;
        public final int audioSampleRate;
        
        public MediaInfo(String format, long duration, long bitrate, 
                        int videoWidth, int videoHeight, double frameRate,
                        String videoCodec, String audioCodec, 
                        int audioChannels, int audioSampleRate) {
            this.format = format;
            this.duration = duration;
            this.bitrate = bitrate;
            this.videoWidth = videoWidth;
            this.videoHeight = videoHeight;
            this.frameRate = frameRate;
            this.videoCodec = videoCodec;
            this.audioCodec = audioCodec;
            this.audioChannels = audioChannels;
            this.audioSampleRate = audioSampleRate;
        }
    }
    
    private ExecutionCallback executionCallback;
    private MediaInfoCallback mediaInfoCallback;
    
    /**
     * Set execution callback for FFmpeg commands
     */
    public void setExecutionCallback(ExecutionCallback callback) {
        this.executionCallback = callback;
    }
    
    /**
     * Set media info callback
     */
    public void setMediaInfoCallback(MediaInfoCallback callback) {
        this.mediaInfoCallback = callback;
    }
    
    /**
     * Execute FFmpeg command
     * @param command FFmpeg command (e.g., "-i input.mp4 -vcodec h264 output.mp4")
     * @return execution ID for tracking
     */
    public int executeFFmpegCommand(String command) {
        return nativeExecuteFFmpegCommand(command);
    }
    
    /**
     * Execute FFmpeg command with arguments array
     * @param arguments FFmpeg arguments array
     * @return execution ID for tracking
     */
    public int executeFFmpegCommand(String[] arguments) {
        return nativeExecuteFFmpegCommandArray(arguments);
    }
    
    /**
     * Cancel FFmpeg execution
     * @param executionId execution ID returned from executeFFmpegCommand
     * @return true if cancellation was successful
     */
    public boolean cancelExecution(int executionId) {
        return nativeCancelExecution(executionId);
    }
    
    /**
     * Get media information from file or stream
     * @param path file path or stream URL
     */
    public void getMediaInfo(String path) {
        nativeGetMediaInfo(path);
    }
    
    /**
     * Get FFmpeg version information
     * @return FFmpeg version string
     */
    public String getFFmpegVersion() {
        return nativeGetFFmpegVersion();
    }
    
    /**
     * Get list of supported formats
     * @return array of supported format names
     */
    public String[] getSupportedFormats() {
        return nativeGetSupportedFormats();
    }
    
    /**
     * Get list of supported codecs
     * @return array of supported codec names
     */
    public String[] getSupportedCodecs() {
        return nativeGetSupportedCodecs();
    }
    
    /**
     * Apply video filter
     * @param inputPath input video file
     * @param outputPath output video file
     * @param filter FFmpeg filter string (e.g., "scale=1280:720")
     * @return execution ID for tracking
     */
    public int applyVideoFilter(String inputPath, String outputPath, String filter) {
        String command = String.format("-i %s -vf %s -c:a copy %s", inputPath, filter, outputPath);
        return executeFFmpegCommand(command);
    }
    
    /**
     * Convert video format
     * @param inputPath input video file
     * @param outputPath output video file
     * @param format target format (e.g., "mp4", "avi", "mov")
     * @return execution ID for tracking
     */
    public int convertVideoFormat(String inputPath, String outputPath, String format) {
        String command = String.format("-i %s -c copy %s", inputPath, outputPath);
        return executeFFmpegCommand(command);
    }
    
    /**
     * Extract audio from video
     * @param inputPath input video file
     * @param outputPath output audio file
     * @return execution ID for tracking
     */
    public int extractAudio(String inputPath, String outputPath) {
        String command = String.format("-i %s -vn -c:a copy %s", inputPath, outputPath);
        return executeFFmpegCommand(command);
    }
    
    /**
     * Merge audio and video
     * @param videoPath video file path
     * @param audioPath audio file path
     * @param outputPath output file path
     * @return execution ID for tracking
     */
    public int mergeAudioVideo(String videoPath, String audioPath, String outputPath) {
        String command = String.format("-i %s -i %s -c copy %s", videoPath, audioPath, outputPath);
        return executeFFmpegCommand(command);
    }
    
    /**
     * Create video thumbnail
     * @param inputPath input video file
     * @param outputPath output image file
     * @param timeSeconds time in seconds to capture thumbnail
     * @return execution ID for tracking
     */
    public int createThumbnail(String inputPath, String outputPath, double timeSeconds) {
        String command = String.format("-i %s -ss %.2f -vframes 1 %s", inputPath, timeSeconds, outputPath);
        return executeFFmpegCommand(command);
    }
    
    // Called from native code when execution events occur
    @SuppressWarnings("unused")
    private void onExecutionEvent(int executionId, int eventType, String data) {
        if (executionCallback == null) return;
        
        switch (eventType) {
            case 0: // Started
                executionCallback.onExecutionStarted(executionId);
                break;
            case 1: // Completed
                executionCallback.onExecutionCompleted(executionId, Integer.parseInt(data));
                break;
            case 2: // Failed
                executionCallback.onExecutionFailed(executionId, data);
                break;
            case 3: // Output
                executionCallback.onOutput(executionId, data);
                break;
        }
    }
    
    // Called from native code when media info is ready
    @SuppressWarnings("unused")
    private void onMediaInfoReady(String format, long duration, long bitrate, 
                                 int videoWidth, int videoHeight, double frameRate,
                                 String videoCodec, String audioCodec, 
                                 int audioChannels, int audioSampleRate) {
        if (mediaInfoCallback != null) {
            MediaInfo info = new MediaInfo(format, duration, bitrate, videoWidth, videoHeight,
                                         frameRate, videoCodec, audioCodec, audioChannels, audioSampleRate);
            mediaInfoCallback.onMediaInfoReady(info);
        }
    }
    
    // Called from native code when media info error occurs
    @SuppressWarnings("unused")
    private void onMediaInfoError(String error) {
        if (mediaInfoCallback != null) {
            mediaInfoCallback.onMediaInfoError(error);
        }
    }
    
    // Native method declarations (to be implemented in native code)
    private native int nativeExecuteFFmpegCommand(String command);
    private native int nativeExecuteFFmpegCommandArray(String[] arguments);
    private native boolean nativeCancelExecution(int executionId);
    private native void nativeGetMediaInfo(String path);
    private native String nativeGetFFmpegVersion();
    private native String[] nativeGetSupportedFormats();
    private native String[] nativeGetSupportedCodecs();
    
    // Load native library
    static {
        System.loadLibrary("fijkplayer_ffmpeg_kit");
    }
}