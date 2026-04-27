/**
 * Native Player - Unified player integrating all components
 * 
 * This module provides the main player interface that combines:
 * - FFmpeg Demuxer (stream opening and packet reading)
 * - MediaCodec Decoder (hardware video decoding)
 * - Frame Queue (thread-safe frame buffering)
 * - OpenGL Renderer (GPU-accelerated rendering)
 * 
 * Threading model:
 * - Demuxer thread: reads packets from network/file
 * - Decoder thread: decodes video packets
 * - Render thread: renders frames to screen (driven by Flutter)
 * 
 * Lifecycle:
 * 1. create() - allocate player
 * 2. setDataSource() - specify media URL
 * 3. setSurface() - set output surface
 * 4. prepareAsync() - start demuxer, detect streams
 * 5. start() - begin playback (decoder + render)
 * 6. pause() / resume() / seek()
 * 7. stop() - stop all threads
 * 8. release() - cleanup resources
 */

#ifndef FIJKPLAYER_NATIVE_PLAYER_H
#define FIJKPLAYER_NATIVE_PLAYER_H

#include <jni.h>
#include <stdbool.h>
#include <pthread.h>
#include "ffmpeg_demuxer.h"
#include "mediacodec_decoder.h"
#include "frame_queue.h"
#include "gl_renderer.h"

#ifdef __cplusplus
extern "C" {
#endif

// Player states
typedef enum {
    PLAYER_STATE_IDLE,           // Created but not prepared
    PLAYER_STATE_INITIALIZED,    // Data source set
    PLAYER_STATE_PREPARING,      // Async preparation in progress
    PLAYER_STATE_PREPARED,       // Ready to play
    PLAYER_STATE_STARTED,        // Playing
    PLAYER_STATE_PAUSED,         // Paused
    PLAYER_STATE_STOPPED,        // Stopped
    PLAYER_STATE_COMPLETED,      // Playback completed
    PLAYER_STATE_ERROR           // Error occurred
} PlayerState;

// Player events (callbacks to Java)
typedef enum {
    PLAYER_EVENT_PREPARED,       // Preparation complete
    PLAYER_EVENT_VIDEO_SIZE,     // Video size changed
    PLAYER_EVENT_STARTED,        // Playback started
    PLAYER_EVENT_PAUSED,         // Playback paused
    PLAYER_EVENT_SEEK_COMPLETE,  // Seek operation done
    PLAYER_EVENT_COMPLETED,      // Playback finished
    PLAYER_EVENT_ERROR,          // Error occurred
    PLAYER_EVENT_BUFFERING,      // Buffering update
    PLAYER_EVENT_INFO            // General info
} PlayerEvent;

// Player options
typedef struct {
    int buffer_size;             // Frame queue size (default: 3)
    int timeout_ms;              // Network timeout (default: 10000)
    bool enable_hw_decoder;      // Use hardware decoder (default: true)
    bool enable_frame_drop;      // Drop frames when behind (default: true)
    int max_video_width;         // Max video width (0 = no limit)
    int max_video_height;        // Max video height (0 = no limit)
    int enable_audio;            // Enable audio decoding/playback (default: true)
    int max_latency_ms;          // Maximum acceptable latency for live streams (0 = no limit)
    int playback_mode;           // 0=LIVE_LOW_LATENCY, 1=LIVE_WITH_AUDIO, 2=VOD_OPTIMIZED
} PlayerOptions;

// Player statistics
typedef struct {
    int64_t duration;            // Total duration (microseconds)
    int64_t position;            // Current position (microseconds)
    int video_width;             // Video width
    int video_height;            // Video height
    float fps;                   // Current FPS
    int frames_decoded;          // Total decoded frames
    int frames_rendered;         // Total rendered frames
    int frames_dropped;          // Total dropped frames
    int64_t bitrate;             // Current bitrate
} PlayerStats;

// Opaque player handle
typedef struct NativePlayer NativePlayer;

/**
 * Create a new native player
 * Must be called from Java layer with valid JNI env
 * 
 * @param env JNI environment
 * @param thiz Java object reference (for callbacks)
 * @return Player handle, or NULL on error
 */
NativePlayer* native_player_create(JNIEnv* env, jobject thiz);

/**
 * Set data source (URL or file path)
 * 
 * @param player Player handle
 * @param url Media URL or file path
 * @return 0 on success, -1 on error
 */
int native_player_set_data_source(NativePlayer* player, const char* url);

/**
 * Set player options
 * 
 * @param player Player handle
 * @param options Player options
 * @return 0 on success, -1 on error
 */
int native_player_set_options(NativePlayer* player, const PlayerOptions* options);

/**
 * Set surface for rendering (Android Surface object)
 * 
 * @param player Player handle
 * @param env JNI environment
 * @param surface Android Surface object
 * @return 0 on success, -1 on error
 */
int native_player_set_surface(NativePlayer* player, JNIEnv* env, jobject surface);

/**
 * Set playback mode for different scenarios
 * 
 * Configure the player for optimal performance based on use case:
 * - Mode 0 (LIVE_LOW_LATENCY): Real-time live video, no audio, minimum latency
 * - Mode 1 (LIVE_WITH_AUDIO): Live video with synchronized audio
 * - Mode 2 (VOD_OPTIMIZED): Video-on-demand with smooth buffering
 * 
 * Should be called after setDataSource and before prepareAsync
 * 
 * @param player Player handle
 * @param mode Playback mode (0=LIVE_LOW_LATENCY, 1=LIVE_WITH_AUDIO, 2=VOD_OPTIMIZED)
 * @param bufferMs Custom buffer size in milliseconds (-1 for mode default)
 * @param enableAudio Enable audio decoding/playback (-1 for mode default)
 * @param maxLatencyMs Maximum acceptable latency in ms (-1 for mode default)
 * @param enableFrameDrop Enable frame dropping when behind (-1 for mode default)
 * @return 0 on success, -1 on error
 */
int native_player_set_playback_mode(NativePlayer* player, int mode, int bufferMs, 
                                     int enableAudio, int maxLatencyMs, int enableFrameDrop);

/**
 * Prepare player asynchronously
 * Opens media, detects streams, configures decoder
 * Calls PLAYER_EVENT_PREPARED when done
 * 
 * @param player Player handle
 * @return 0 on success, -1 on error
 */
int native_player_prepare_async(NativePlayer* player);

/**
 * Start playback
 * Starts decoder and render threads
 * 
 * @param player Player handle
 * @return 0 on success, -1 on error
 */
int native_player_start(NativePlayer* player);

/**
 * Pause playback
 * 
 * @param player Player handle
 * @return 0 on success, -1 on error
 */
int native_player_pause(NativePlayer* player);

/**
 * Resume playback
 * 
 * @param player Player handle
 * @return 0 on success, -1 on error
 */
int native_player_resume(NativePlayer* player);

/**
 * Stop playback
 * Stops all threads and clears buffers
 * 
 * @param player Player handle
 * @return 0 on success, -1 on error
 */
int native_player_stop(NativePlayer* player);

/**
 * Reset player to IDLE state
 * Stops playback, releases resources, and allows setting a new data source
 * 
 * @param player Player handle
 * @return 0 on success, -1 on error
 */
int native_player_reset(NativePlayer* player);

/**
 * Seek to position
 * 
 * @param player Player handle
 * @param position_ms Position in milliseconds
 * @return 0 on success, -1 on error
 */
int native_player_seek(NativePlayer* player, int64_t position_ms);

/**
 * Get current position
 * 
 * @param player Player handle
 * @return Current position in milliseconds
 */
int64_t native_player_get_position(NativePlayer* player);

/**
 * Get media duration
 * 
 * @param player Player handle
 * @return Duration in milliseconds
 */
int64_t native_player_get_duration(NativePlayer* player);

/**
 * Get video frame rate
 * 
 * @param player Player handle
 * @return Frame rate in fps (frames per second)
 */
double native_player_get_frame_rate(NativePlayer* player);

/**
 * Get video width
 * 
 * @param player Player handle
 * @return Video width in pixels
 */
int native_player_get_video_width(NativePlayer* player);

/**
 * Get video height
 * 
 * @param player Player handle
 * @return Video height in pixels
 */
int native_player_get_video_height(NativePlayer* player);

/**
 * Get audio sample rate
 * 
 * @param player Player handle
 * @return Audio sample rate in Hz, or 0 if no audio
 */
int native_player_get_audio_sample_rate(NativePlayer* player);

/**
 * Get audio channel count
 * 
 * @param player Player handle
 * @return Number of audio channels, or 0 if no audio
 */
int native_player_get_audio_channels(NativePlayer* player);

/**
 * Check if audio stream exists
 * 
 * @param player Player handle
 * @return true if audio stream is present
 */
bool native_player_has_audio(NativePlayer* player);

/**
 * Check if playing
 * 
 * @param player Player handle
 * @return true if playing
 */
bool native_player_is_playing(NativePlayer* player);

/**
 * Get current state
 * 
 * @param player Player handle
 * @return Current player state
 */
PlayerState native_player_get_state(NativePlayer* player);

/**
 * Get player statistics
 * 
 * @param player Player handle
 * @param stats Output statistics
 * @return 0 on success, -1 on error
 */
int native_player_get_stats(NativePlayer* player, PlayerStats* stats);

/**
 * Render next frame (called from render thread)
 * Pops frame from queue and renders to current GL context
 * 
 * @param player Player handle
 * @return 0 on success, -1 on error
 */
int native_player_render_frame(NativePlayer* player);

/**
 * Set volume
 * 
 * @param player Player handle
 * @param volume Volume level (0.0 to 1.0)
 * @return 0 on success, -1 on error
 */
int native_player_set_volume(NativePlayer* player, float volume);

/**
 * Release player and free all resources
 * 
 * @param player Player handle
 */
void native_player_release(NativePlayer* player);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_NATIVE_PLAYER_H
