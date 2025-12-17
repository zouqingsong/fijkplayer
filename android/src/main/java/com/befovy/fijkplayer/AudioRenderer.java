package com.befovy.fijkplayer;

import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.util.Log;

/**
 * Audio renderer using Android AudioTrack
 * Handles PCM playback for fijkplayer
 */
public class AudioRenderer {
    private static final String TAG = "AudioRenderer";
    
    private AudioTrack audioTrack;
    private int sampleRate;
    private int channels;
    private int bufferSizeBytes;
    private boolean isPlaying = false;
    
    /**
     * Initialize AudioTrack
     * 
     * @param sampleRate Sample rate in Hz (e.g., 44100)
     * @param channels Number of channels (1=mono, 2=stereo)
     * @return true on success
     */
    public boolean init(int sampleRate, int channels) {
        this.sampleRate = sampleRate;
        this.channels = channels;
        
        int channelConfig = (channels == 1) ? 
                AudioFormat.CHANNEL_OUT_MONO : AudioFormat.CHANNEL_OUT_STEREO;
        int audioFormat = AudioFormat.ENCODING_PCM_16BIT;
        
        // Calculate buffer size - use minimal buffer for tighter pacing
        // Minimum buffer + small overhead (not 2x)
        int minBufferSize = AudioTrack.getMinBufferSize(sampleRate, channelConfig, audioFormat);
        bufferSizeBytes = minBufferSize + (minBufferSize / 4);  // Add 25% for smooth playback
        
        if (bufferSizeBytes <= 0) {
            Log.e(TAG, "Invalid buffer size: " + bufferSizeBytes);
            return false;
        }
        
        try {
            AudioAttributes audioAttributes = new AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_MEDIA)
                    .setContentType(AudioAttributes.CONTENT_TYPE_MUSIC)
                    .build();
            
            AudioFormat format = new AudioFormat.Builder()
                    .setSampleRate(sampleRate)
                    .setEncoding(audioFormat)
                    .setChannelMask(channelConfig)
                    .build();
            
            audioTrack = new AudioTrack(
                    audioAttributes,
                    format,
                    bufferSizeBytes,
                    AudioTrack.MODE_STREAM,
                    0  // sessionId
            );
            
            if (audioTrack.getState() != AudioTrack.STATE_INITIALIZED) {
                Log.e(TAG, "AudioTrack not initialized");
                audioTrack = null;
                return false;
            }
            
            Log.i(TAG, String.format("AudioTrack initialized: %d Hz, %d ch, buffer=%d bytes",
                    sampleRate, channels, bufferSizeBytes));
            return true;
            
        } catch (Exception e) {
            Log.e(TAG, "Failed to create AudioTrack", e);
            audioTrack = null;
            return false;
        }
    }
    
    /**
     * Start audio playback
     */
    public void start() {
        if (audioTrack != null && !isPlaying) {
            // Set volume to maximum to ensure audio is audible
            float maxVolume = AudioTrack.getMaxVolume();
            audioTrack.setVolume(maxVolume);
            
            // Also set stereo volume explicitly
            audioTrack.setStereoVolume(maxVolume, maxVolume);
            Log.i(TAG, "AudioTrack volume set to: " + maxVolume + " (stereo: " + maxVolume + ", " + maxVolume + ")");
            
            // Start playback
            audioTrack.play();
            isPlaying = true;
            Log.i(TAG, "AudioTrack started, playState=" + audioTrack.getPlayState() + 
                       ", state=" + audioTrack.getState());
        }
    }
    
    /**
     * Pause audio playback
     */
    public void pause() {
        if (audioTrack != null && isPlaying) {
            audioTrack.pause();
            isPlaying = false;
            Log.i(TAG, "AudioTrack paused");
        }
    }
    
    /**
     * Resume audio playback
     */
    public void resume() {
        if (audioTrack != null && !isPlaying) {
            audioTrack.play();
            isPlaying = true;
            Log.i(TAG, "AudioTrack resumed");
        }
    }
    
    /**
     * Stop audio playback and flush buffers
     */
    public void stop() {
        if (audioTrack != null) {
            if (isPlaying) {
                audioTrack.pause();
                isPlaying = false;
            }
            audioTrack.flush();
            Log.i(TAG, "AudioTrack stopped");
        }
    }
    
    /**
     * Write PCM data to AudioTrack
     * 
     * @param data PCM data (S16 interleaved: L,R,L,R...)
     * @param sizeBytes Size in bytes
     * @return Number of bytes written, or -1 on error
     */
    public int write(byte[] data, int sizeBytes) {
        if (audioTrack == null || data == null || sizeBytes <= 0) {
            return -1;
        }
        
        // Check if AudioTrack is still playing
        int playState = audioTrack.getPlayState();
        if (playState != AudioTrack.PLAYSTATE_PLAYING) {
            Log.w(TAG, "AudioTrack not playing! playState=" + playState + ", restarting...");
            audioTrack.play();
        }
        
        // Use BLOCKING mode - this will pace writes to real-time playback speed
        // AudioTrack won't accept more data until it's consumed by hardware
        int written = audioTrack.write(data, 0, sizeBytes, AudioTrack.WRITE_BLOCKING);
        
        if (written < 0) {
            Log.e(TAG, "Write error: " + written);
        }
        
        return written;
    }
    
    /**
     * Set playback volume
     * 
     * @param volume Volume level (0.0 to 1.0)
     */
    public void setVolume(float volume) {
        if (audioTrack != null) {
            float clampedVolume = Math.max(0.0f, Math.min(1.0f, volume));
            audioTrack.setVolume(clampedVolume);
        }
    }
    
    /**
     * Get current playback position in microseconds
     */
    public long getPlaybackPositionUs() {
        if (audioTrack == null || sampleRate == 0) {
            return 0;
        }
        
        int framesPlayed = audioTrack.getPlaybackHeadPosition();
        return (long)framesPlayed * 1000000L / sampleRate;
    }
    
    /**
     * Release AudioTrack resources
     */
    public void release() {
        if (audioTrack != null) {
            if (isPlaying) {
                audioTrack.pause();
                isPlaying = false;
            }
            audioTrack.flush();
            audioTrack.release();
            audioTrack = null;
            Log.i(TAG, "AudioTrack released");
        }
    }
}
