/**
 * Native Player Implementation
 * Integrates FFmpeg demuxer, MediaCodec decoder, frame queue, and GL renderer
 */

#include "native_player.h"
#include "ffmpeg_demuxer.h"
#include "mediacodec_decoder.h"
#include "ffmpeg_soft_decoder.h"
#include "frame_queue.h"
#include "gl_renderer.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#ifdef USE_SOFTWARE_DECODER
#include "ffmpeg/include/libavcodec/avcodec.h"
#endif

#define LOG_TAG "NativePlayer"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

// Player structure
struct NativePlayer {
    // Components
    FFDemuxer* demuxer;
    MediaCodecDecoder* decoder;
    FFmpegSoftDecoder* soft_decoder;  // For diagnostic testing
    FrameQueue* frame_queue;
    GLRenderer* renderer;
    
    // Java callback
    JavaVM* jvm;
    jobject java_obj;        // Global reference to Java player object
    jmethodID callback_method;
    
    // State
    PlayerState state;
    char* data_source;
    int video_stream_index;
    
    // Threading
    pthread_t demuxer_thread;
    pthread_t decoder_thread;
    pthread_mutex_t state_mutex;
    bool stop_requested;
    bool paused;
    
    // Configuration
    PlayerOptions options;
    
    // Statistics
    PlayerStats stats;
    int64_t start_time;
    int64_t current_position;
    
    // Surface
    ANativeWindow* native_window;
};

// Forward declarations
static void* demuxer_thread_func(void* arg);
static void* decoder_thread_func(void* arg);
static void post_event(NativePlayer* player, PlayerEvent event, int arg1, int arg2);

NativePlayer* native_player_create(JNIEnv* env, jobject thiz) {
    NativePlayer* player = (NativePlayer*)calloc(1, sizeof(NativePlayer));
    if (!player) {
        LOGE("Failed to allocate player");
        return NULL;
    }
    
    // Get Java VM for callbacks
    (*env)->GetJavaVM(env, &player->jvm);
    
    // Create global reference to Java object
    player->java_obj = (*env)->NewGlobalRef(env, thiz);
    
    // Get callback method
    jclass clazz = (*env)->GetObjectClass(env, thiz);
    player->callback_method = (*env)->GetMethodID(env, clazz, 
                                                   "onNativeEvent", "(III)V");
    
    // Initialize state
    player->state = PLAYER_STATE_IDLE;
    pthread_mutex_init(&player->state_mutex, NULL);
    
    // Set default options
    player->options.buffer_size = 3;
    player->options.timeout_ms = 10000;
    player->options.enable_hw_decoder = true;
    player->options.enable_frame_drop = true;
    player->options.max_video_width = 0;
    player->options.max_video_height = 0;
    
    LOGD("Native player created");
    return player;
}

int native_player_set_data_source(NativePlayer* player, const char* url) {
    if (!player || !url) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    
    if (player->state != PLAYER_STATE_IDLE) {
        LOGE("Cannot set data source in state %d", player->state);
        pthread_mutex_unlock(&player->state_mutex);
        return -1;
    }
    
    // Store URL
    if (player->data_source) {
        free(player->data_source);
    }
    player->data_source = strdup(url);
    
    player->state = PLAYER_STATE_INITIALIZED;
    
    pthread_mutex_unlock(&player->state_mutex);
    
    LOGD("Data source set: %s", url);
    return 0;
}

int native_player_set_options(NativePlayer* player, const PlayerOptions* options) {
    if (!player || !options) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    player->options = *options;
    pthread_mutex_unlock(&player->state_mutex);
    
    return 0;
}

int native_player_set_surface(NativePlayer* player, JNIEnv* env, jobject surface) {
    if (!player) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    
    // Release old window
    if (player->native_window) {
        ANativeWindow_release(player->native_window);
        player->native_window = NULL;
    }
    
    // Get new window
    if (surface) {
        player->native_window = ANativeWindow_fromSurface(env, surface);
        if (!player->native_window) {
            LOGE("Failed to get native window from surface");
            pthread_mutex_unlock(&player->state_mutex);
            return -1;
        }
        LOGD("Surface set: %p", player->native_window);
    }
    
    pthread_mutex_unlock(&player->state_mutex);
    return 0;
}

int native_player_prepare_async(NativePlayer* player) {
    if (!player) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    
    if (player->state != PLAYER_STATE_INITIALIZED) {
        LOGE("Cannot prepare in state %d", player->state);
        pthread_mutex_unlock(&player->state_mutex);
        return -1;
    }
    
    player->state = PLAYER_STATE_PREPARING;
    pthread_mutex_unlock(&player->state_mutex);
    
    // Create demuxer
    player->demuxer = ff_demuxer_create();
    if (!player->demuxer) {
        LOGE("Failed to create demuxer");
        post_event(player, PLAYER_EVENT_ERROR, -1, 0);
        return -1;
    }
    
    // Open media
    if (ff_demuxer_open(player->demuxer, player->data_source, NULL) < 0) {
        LOGE("Failed to open media: %s", player->data_source);
        post_event(player, PLAYER_EVENT_ERROR, -2, 0);
        return -1;
    }
    
    // Find video stream
    player->video_stream_index = -1;
    int stream_count = ff_demuxer_get_stream_count(player->demuxer);
    for (int i = 0; i < stream_count; i++) {
        FFStream* stream = ff_demuxer_get_stream(player->demuxer, i);
        if (stream && stream->type == FF_STREAM_TYPE_VIDEO) {
            player->video_stream_index = i;
            break;
        }
    }
    
    if (player->video_stream_index < 0) {
        LOGE("No video stream found");
        post_event(player, PLAYER_EVENT_ERROR, -3, 0);
        return -1;
    }
    
    FFStream* video_stream = ff_demuxer_get_stream(player->demuxer, player->video_stream_index);
    if (!video_stream) {
        LOGE("Failed to get video stream");
        post_event(player, PLAYER_EVENT_ERROR, -3, 0);
        return -1;
    }
    
    // Update stats
    player->stats.video_width = video_stream->width;
    player->stats.video_height = video_stream->height;
    player->stats.duration = ff_demuxer_get_duration(player->demuxer);
    
    LOGI("Video stream: %dx%d, codec: %d, duration: %lld us",
         video_stream->width, video_stream->height,
         video_stream->video_codec, (long long)player->stats.duration);
    
#ifdef USE_SOFTWARE_DECODER
    // ========== DIAGNOSTIC MODE: Use FFmpeg Software Decoder ==========
    LOGI("🔧 DIAGNOSTIC MODE: Using FFmpeg software decoder");
    
    SoftDecoderConfig soft_config = {
        .width = video_stream->width,
        .height = video_stream->height,
        .codec_id = AV_CODEC_ID_H264,
        .extradata = video_stream->extradata,
        .extradata_size = video_stream->extradata_size
    };
    
    player->soft_decoder = ffmpeg_soft_decoder_create(&soft_config);
    if (!player->soft_decoder) {
        LOGE("Failed to create software decoder");
        post_event(player, PLAYER_EVENT_ERROR, -4, 0);
        return -1;
    }
    
    if (ffmpeg_soft_decoder_start(player->soft_decoder) < 0) {
        LOGE("Failed to start software decoder");
        post_event(player, PLAYER_EVENT_ERROR, -6, 0);
        return -1;
    }
    
    LOGI("✅ Software decoder started successfully");
#else
    // ========== NORMAL MODE: Use MediaCodec Hardware Decoder ==========
    player->decoder = mediacodec_decoder_create();
    if (!player->decoder) {
        LOGE("Failed to create decoder");
        post_event(player, PLAYER_EVENT_ERROR, -4, 0);
        return -1;
    }
    
    DecoderConfig config = {
        .codec = video_stream->video_codec,
        .width = video_stream->width,
        .height = video_stream->height,
        .extradata = video_stream->extradata,
        .extradata_size = video_stream->extradata_size,
        .use_surface = player->native_window != NULL,
        .surface = player->native_window
    };
    
    if (video_stream->extradata_size > 0) {
        LOGI("✅ CSD data present: %d bytes", video_stream->extradata_size);
    } else {
        LOGW("⚠️ No CSD data - MediaCodec may fail to initialize!");
    }
    
    if (mediacodec_decoder_configure(player->decoder, &config) < 0) {
        LOGE("Failed to configure decoder");
        post_event(player, PLAYER_EVENT_ERROR, -5, 0);
        return -1;
    }
    
    if (mediacodec_decoder_start(player->decoder) < 0) {
        LOGE("Failed to start decoder");
        post_event(player, PLAYER_EVENT_ERROR, -6, 0);
        return -1;
    }
#endif
    
    // Create frame queue
    FrameQueueConfig queue_config = {
        .max_frames = player->options.buffer_size,
        .max_duration_us = 500000,
        .enable_frame_drop = player->options.enable_frame_drop,
        .enable_frame_pool = true
    };
    
    player->frame_queue = frame_queue_create(&queue_config);
    if (!player->frame_queue) {
        LOGE("Failed to create frame queue");
        post_event(player, PLAYER_EVENT_ERROR, -7, 0);
        return -1;
    }
    
    // No GLRenderer needed - MediaCodec renders directly to Surface
    // Flutter's Texture widget will display the SurfaceTexture
    // This is the ijkplayer approach: decoder -> surface -> texture widget
    
    pthread_mutex_lock(&player->state_mutex);
    player->state = PLAYER_STATE_PREPARED;
    pthread_mutex_unlock(&player->state_mutex);
    
    // Notify prepared
    post_event(player, PLAYER_EVENT_PREPARED, 0, 0);
    post_event(player, PLAYER_EVENT_VIDEO_SIZE, 
               video_stream->width, video_stream->height);
    
    LOGD("Player prepared successfully");
    return 0;
}

int native_player_start(NativePlayer* player) {
    if (!player) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    
    if (player->state != PLAYER_STATE_PREPARED && 
        player->state != PLAYER_STATE_PAUSED) {
        LOGE("Cannot start in state %d", player->state);
        pthread_mutex_unlock(&player->state_mutex);
        return -1;
    }
    
    player->stop_requested = false;
    player->paused = false;
    player->state = PLAYER_STATE_STARTED;
    player->start_time = 0;
    
    // Start demuxer thread
    pthread_create(&player->demuxer_thread, NULL, demuxer_thread_func, player);
    
    // Start decoder thread
    pthread_create(&player->decoder_thread, NULL, decoder_thread_func, player);
    
    pthread_mutex_unlock(&player->state_mutex);
    
    post_event(player, PLAYER_EVENT_STARTED, 0, 0);
    
    LOGD("Playback started");
    return 0;
}

int native_player_pause(NativePlayer* player) {
    if (!player) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    
    if (player->state != PLAYER_STATE_STARTED) {
        pthread_mutex_unlock(&player->state_mutex);
        return -1;
    }
    
    player->paused = true;
    player->state = PLAYER_STATE_PAUSED;
    
    pthread_mutex_unlock(&player->state_mutex);
    
    post_event(player, PLAYER_EVENT_PAUSED, 0, 0);
    
    LOGD("Playback paused");
    return 0;
}

int native_player_resume(NativePlayer* player) {
    if (!player) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    
    if (player->state != PLAYER_STATE_PAUSED) {
        pthread_mutex_unlock(&player->state_mutex);
        return -1;
    }
    
    player->paused = false;
    player->state = PLAYER_STATE_STARTED;
    
    pthread_mutex_unlock(&player->state_mutex);
    
    post_event(player, PLAYER_EVENT_STARTED, 0, 0);
    
    LOGD("Playback resumed");
    return 0;
}

int native_player_stop(NativePlayer* player) {
    if (!player) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    
    if (player->state != PLAYER_STATE_STARTED && 
        player->state != PLAYER_STATE_PAUSED) {
        pthread_mutex_unlock(&player->state_mutex);
        return -1;
    }
    
    player->stop_requested = true;
    player->state = PLAYER_STATE_STOPPED;
    
    pthread_mutex_unlock(&player->state_mutex);
    
    // Interrupt demuxer to unblock any pending operations
    if (player->demuxer) {
        ff_demuxer_interrupt(player->demuxer);
    }
    
    // Wait for threads
    pthread_join(player->demuxer_thread, NULL);
    pthread_join(player->decoder_thread, NULL);
    
    // Flush queue
    if (player->frame_queue) {
        frame_queue_flush(player->frame_queue);
    }
    
    LOGD("Playback stopped");
    return 0;
}

int native_player_seek(NativePlayer* player, int64_t position_ms) {
    if (!player || !player->demuxer) return -1;
    
    // Convert milliseconds to microseconds
    int64_t position_us = position_ms * 1000;
    
    // Seek demuxer
    if (ff_demuxer_seek(player->demuxer, player->video_stream_index, position_us, 0) < 0) {
        LOGE("Seek failed");
        return -1;
    }
    
    // Flush decoder and queue
    if (player->decoder) {
        mediacodec_decoder_flush(player->decoder);
    }
    
    if (player->frame_queue) {
        frame_queue_flush(player->frame_queue);
    }
    
    player->current_position = position_us;
    
    post_event(player, PLAYER_EVENT_SEEK_COMPLETE, 0, 0);
    
    LOGD("Seeked to %lld ms", (long long)position_ms);
    return 0;
}

int64_t native_player_get_position(NativePlayer* player) {
    if (!player) return 0;
    
    // Thread-safe read with mutex
    pthread_mutex_lock(&player->state_mutex);
    int64_t position = player->current_position;
    pthread_mutex_unlock(&player->state_mutex);
    
    return position / 1000;  // Convert to milliseconds
}

int64_t native_player_get_duration(NativePlayer* player) {
    if (!player) return 0;
    
    // Thread-safe read with mutex
    pthread_mutex_lock(&player->state_mutex);
    int64_t duration = player->stats.duration;
    pthread_mutex_unlock(&player->state_mutex);
    
    return duration / 1000;  // Convert to milliseconds
}

int native_player_get_video_width(NativePlayer* player) {
    return player ? player->stats.video_width : 0;
}

int native_player_get_video_height(NativePlayer* player) {
    return player ? player->stats.video_height : 0;
}

bool native_player_is_playing(NativePlayer* player) {
    if (!player) return false;
    
    pthread_mutex_lock(&player->state_mutex);
    bool playing = (player->state == PLAYER_STATE_STARTED);
    pthread_mutex_unlock(&player->state_mutex);
    
    return playing;
}

PlayerState native_player_get_state(NativePlayer* player) {
    if (!player) return PLAYER_STATE_IDLE;
    
    pthread_mutex_lock(&player->state_mutex);
    PlayerState state = player->state;
    pthread_mutex_unlock(&player->state_mutex);
    
    return state;
}

int native_player_get_stats(NativePlayer* player, PlayerStats* stats) {
    if (!player || !stats) return -1;
    
    pthread_mutex_lock(&player->state_mutex);
    *stats = player->stats;
    pthread_mutex_unlock(&player->state_mutex);
    
    return 0;
}

int native_player_render_frame(NativePlayer* player) {
    if (!player || !player->renderer || !player->frame_queue) return -1;
    
    // Try to pop frame from queue (non-blocking)
    VideoFrame* frame = NULL;
    if (frame_queue_try_pop(player->frame_queue, &frame) < 0) {
        return -1;  // No frame available
    }
    
    // Render frame
    int ret = gl_renderer_render_frame(player->renderer, frame);
    
    // Update position
    player->current_position = frame->pts;
    
    // Update stats
    player->stats.frames_rendered++;
    
    // Free frame
    frame_queue_free_frame(player->frame_queue, frame);
    
    return ret;
}

int native_player_set_volume(NativePlayer* player, float volume) {
    // TODO: Audio volume control
    LOGD("Set volume: %.2f (not implemented yet)", volume);
    return 0;
}

void native_player_release(NativePlayer* player) {
    if (!player) return;
    
    // Interrupt demuxer first to unblock any operations
    if (player->demuxer) {
        ff_demuxer_interrupt(player->demuxer);
    }
    
    // Stop if playing
    if (player->state == PLAYER_STATE_STARTED || 
        player->state == PLAYER_STATE_PAUSED) {
        native_player_stop(player);
    }
    
    // Release components
    if (player->renderer) {
        gl_renderer_destroy(player->renderer);
    }
    
    if (player->frame_queue) {
        frame_queue_destroy(player->frame_queue);
    }
    
    if (player->decoder) {
        mediacodec_decoder_stop(player->decoder);
        mediacodec_decoder_destroy(player->decoder);
    }
    
    if (player->soft_decoder) {
        ffmpeg_soft_decoder_free(player->soft_decoder);
    }
    
    if (player->demuxer) {
        ff_demuxer_close(player->demuxer);
    }
    
    if (player->native_window) {
        ANativeWindow_release(player->native_window);
    }
    
    // Release Java references
    if (player->java_obj && player->jvm) {
        JNIEnv* env;
        (*player->jvm)->AttachCurrentThread(player->jvm, &env, NULL);
        (*env)->DeleteGlobalRef(env, player->java_obj);
        (*player->jvm)->DetachCurrentThread(player->jvm);
    }
    
    if (player->data_source) {
        free(player->data_source);
    }
    
    pthread_mutex_destroy(&player->state_mutex);
    
    free(player);
    
    LOGD("Player released");
}

// Thread functions
static void* demuxer_thread_func(void* arg) {
    NativePlayer* player = (NativePlayer*)arg;
    
    LOGD("Demuxer thread started (dummy - decoder thread does all work)");
    
    // This thread is not used anymore - decoder thread does both demuxing and decoding
    // Just wait for stop signal
    while (!player->stop_requested) {
        usleep(100000);  // Sleep 100ms
    }
    
    LOGD("Demuxer thread finished");
    return NULL;
}

static void* decoder_thread_func(void* arg) {
    NativePlayer* player = (NativePlayer*)arg;
    
    LOGD("Decoder thread started");
    
    bool eos_reached = false;
    bool keyframe_received = false;  // Track if we've received first keyframe
    int video_packet_count = 0;      // Count video packets processed
    int total_frames_received = 0;   // Track total output frames
    int64_t last_frame_log_time = 0; // For periodic logging
    
    while (!player->stop_requested && !eos_reached) {
        if (player->paused) {
            usleep(10000);
            continue;
        }
        
        // PULL FRAMES: Non-blocking to maximize throughput for network streaming
        // Flutter consumes frames at vsync (~16ms intervals)
        // If buffers fill up (4/4 used), we'll get EAGAIN and sleep briefly
        
        DecodedFrame* frame = NULL;
        // Non-blocking (0ms) - if buffer ready, grab immediately
        int recv_ret = mediacodec_decoder_receive_frame(player->decoder, &frame, 0);
        
        if (recv_ret == 0 && frame) {
            total_frames_received++;
            player->current_position = frame->pts;
            
            // Log every 240 frames
            if (total_frames_received % 240 == 0) {
                LOGI("📊 Frame #%d decoded, PTS: %lld ms", 
                     total_frames_received, 
                     (long long)(frame->pts / 1000));
            }
            
            if (frame->data[0]) free(frame->data[0]);
            free(frame);
            // No sleep needed - dequeueOutputBuffer timeout provides natural pacing
        } else if (recv_ret == -EAGAIN) {
            // No frame available yet - buffers may be full
            // Sleep briefly to let Flutter consume frames
            usleep(5000);  // 5ms - gives Flutter time to update texture
        } else if (recv_ret < 0) {
            // Error receiving frame
            LOGE("❌ Error receiving frame: %d, stopping decoder", recv_ret);
            break;
        }
        
        // Check if demuxer is still valid
        if (!player->demuxer) {
            LOGE("Demuxer is NULL");
            break;
        }
        
        // Get packet from demuxer - loop until we get a video packet or error
        FFPacket* packet = NULL;
        int ret = 0;
        
        // Read packets until we get a video packet (skip audio/subtitle packets efficiently)
        while (!player->stop_requested) {
            ret = ff_demuxer_read_packet(player->demuxer, &packet);
            
            if (ret < 0) {
                // Error or EOF
                break;
            }
            
            if (packet && packet->stream_index == player->video_stream_index) {
                // Found a video packet, break to process it
                break;
            }
            
            // Not a video packet, free it and continue
            if (packet) {
                ff_packet_free(packet);
                packet = NULL;
            }
        }
        
        if (ret == 0 && packet) {
            // Video packet read (logging disabled for performance)
            
            // Process VIDEO packet
            if (packet->stream_index == player->video_stream_index) {
                video_packet_count++;
                
                // Accept first video packet OR wait for keyframe
                // First packet is always accepted even if not marked as keyframe because:
                // 1. It might contain essential CSD data
                // 2. MP4 seeking sometimes doesn't set keyframe flag properly
                if (!keyframe_received) {
                    if (video_packet_count == 1 || packet->is_key_frame) {
                        keyframe_received = true;
                        LOGI("✅ Starting decode: packet#%d, pts=%lld, size=%d, is_key=%d", 
                             video_packet_count, (long long)packet->pts, packet->size, packet->is_key_frame);
                    } else {
                        LOGD("⏭️ Skipping non-keyframe: packet#%d, size=%d, pts=%lld", 
                             video_packet_count, packet->size, (long long)packet->pts);
                        ff_packet_free(packet);
                        continue;
                    }
                }
                
                // Video packet sent to decoder (logging disabled for performance)
                
#ifdef USE_SOFTWARE_DECODER
                // ========== SOFTWARE DECODER PATH ==========
                // Send packet to software decoder
                ret = ffmpeg_soft_decoder_send_packet(player->soft_decoder, packet);
                if (ret < 0) {
                    LOGE("❌ Software decoder send_packet failed: %d", ret);
                    ff_packet_free(packet);
                    continue;
                }
                
                // Try to receive frame immediately
                SoftDecodedFrame* soft_frame = NULL;
                ret = ffmpeg_soft_decoder_receive_frame(player->soft_decoder, &soft_frame);
                
                if (ret == 0 && soft_frame) {
                    LOGI("✅ SOFTWARE DECODER OUTPUT: %dx%d, pts=%lld, format=%d", 
                         soft_frame->width, soft_frame->height, 
                         (long long)soft_frame->pts, soft_frame->format);
                    
                    // Print YUV data sizes
                    LOGI("   Y plane: %p, linesize=%d", soft_frame->data[0], soft_frame->linesize[0]);
                    LOGI("   U plane: %p, linesize=%d", soft_frame->data[1], soft_frame->linesize[1]);
                    LOGI("   V plane: %p, linesize=%d", soft_frame->data[2], soft_frame->linesize[2]);
                    
                    ffmpeg_soft_decoder_free_frame(soft_frame);
                    total_frames_received++;
                } else if (ret == -EAGAIN) {
                    LOGD("🔧 Software decoder needs more input");
                } else {
                    LOGW("⚠️ Software decoder receive failed: %d", ret);
                }
                
                ff_packet_free(packet);
                continue;
#else
                // ========== MEDIACODEC DECODER PATH ==========
                // Try to drain 1 frame before sending - keeps pipeline flowing
                // Safe now that Java side consumes immediately
                int pre_drained = 0;
                for (int i = 0; i < 1; i++) {
                    DecodedFrame* temp_frame = NULL;
                    int recv_ret = mediacodec_decoder_receive_frame(player->decoder, &temp_frame, 0);  // Non-blocking
                    
                    if (recv_ret == 0 && temp_frame) {
                        player->current_position = temp_frame->pts;
                        pre_drained++;
                        total_frames_received++;
                        
                        // Log periodic frame info
                        if (total_frames_received % 240 == 0) {
                            LOGI("📊 Frame #%d decoded (pre-drain), PTS: %lld ms", 
                                 total_frames_received, 
                                 (long long)(temp_frame->pts / 1000));
                        }
                        
                        if (temp_frame->data[0]) free(temp_frame->data[0]);
                        free(temp_frame);
                    } else {
                        // No more frames available right now - that's OK
                        break;
                    }
                }
                     
                // Now try to send packet with retry
                int retry_count = 0;
                const int MAX_RETRIES = 200;  // 2 seconds max
                
                while (retry_count < MAX_RETRIES) {
                    ret = mediacodec_decoder_send_packet(player->decoder, packet, 0);  // Non-blocking
                    
                    if (ret == 0) {
                        // Success
                        break;
                    } else if (ret == -11) {  // EAGAIN - still no buffer
                        retry_count++;
                        
                        // Drain frames with BLOCKING wait to prevent deadlock
                        // This is critical: if input buffers are full but output buffers
                        // aren't ready yet, we MUST wait for output buffers
                        DecodedFrame* temp_frame = NULL;
                        int recv_ret = mediacodec_decoder_receive_frame(player->decoder, &temp_frame, 10000);  // 10ms blocking
                        
                        if (recv_ret == 0 && temp_frame) {
                            player->current_position = temp_frame->pts;
                            total_frames_received++;
                            
                            // Log periodic frame info
                            if (total_frames_received % 240 == 0) {
                                LOGI("📊 Frame #%d decoded (retry path), PTS: %lld ms", 
                                     total_frames_received, 
                                     (long long)(temp_frame->pts / 1000));
                            }
                            
                            if (temp_frame->data[0]) free(temp_frame->data[0]);
                            free(temp_frame);
                            // Got a frame, will retry send immediately
                        } else if (recv_ret == -EAGAIN) {
                            // Still no frame after 10ms timeout - very unusual
                            LOGW("⚠️ No frame after 10ms wait (retry %d/%d)", retry_count, MAX_RETRIES);
                            usleep(1000);  // 1ms additional delay
                        } else {
                            // Error
                            LOGE("❌ Receive frame error in retry: %d", recv_ret);
                            break;  // Break retry loop
                        }
                    } else {
                        // Real error
                        LOGE("Failed to send packet: error=%d (size=%d, pts=%lld)", 
                             ret, packet->size, (long long)packet->pts);
                        break;
                    }
                }
                
                if (retry_count >= MAX_RETRIES) {
                    LOGE("Gave up after %d retries - decoder stalled", MAX_RETRIES);
                }
#endif  // USE_SOFTWARE_DECODER
            } else {
                LOGD("Skipping non-video packet from stream %d", packet->stream_index);
            }
            
            // Free packet
            ff_packet_free(packet);
        } else if (ret == -EOF) {
            LOGI("Demuxer reached EOF - draining decoder");
            eos_reached = true;
            
            // Send EOS to decoder
            mediacodec_decoder_send_packet(player->decoder, NULL, 0);
            
            // Drain all remaining frames
            LOGD("Draining remaining decoded frames...");
            int total_drained = 0;
            while (!player->stop_requested) {
                DecodedFrame* temp_frame = NULL;
                int recv_ret = mediacodec_decoder_receive_frame(player->decoder, &temp_frame, 1000);  // 1ms timeout
                
                if (recv_ret == 0 && temp_frame) {
                    // Frame received
                    player->current_position = temp_frame->pts;
                    total_drained++;
                    
                    if (temp_frame->data[0]) free(temp_frame->data[0]);
                    free(temp_frame);
                } else if (recv_ret == -EOF) {
                    // Decoder finished
                    LOGI("Decoder drained - %d frames received", total_drained);
                    break;
                } else {
                    // Timeout or error - might mean more frames coming
                    usleep(5000);  // Wait 5ms
                }
            }
        } else if (ret < 0) {
            // Error reading packet
            LOGE("Error reading packet from demuxer: %d", ret);
            eos_reached = true;
            post_event(player, PLAYER_EVENT_ERROR, ret, 0);
        }
    }
    
    if (eos_reached) {
        post_event(player, PLAYER_EVENT_COMPLETED, 0, 0);
    }
    
    LOGD("Decoder thread finished");
    return NULL;
}

// Post event to Java layer
static void post_event(NativePlayer* player, PlayerEvent event, int arg1, int arg2) {
    if (!player || !player->jvm || !player->java_obj) return;
    
    JNIEnv* env;
    int get_env_result = (*player->jvm)->GetEnv(player->jvm, (void**)&env, JNI_VERSION_1_6);
    bool need_detach = false;
    
    if (get_env_result == JNI_EDETACHED) {
        // Thread not attached, attach it
        if ((*player->jvm)->AttachCurrentThread(player->jvm, &env, NULL) != JNI_OK) {
            LOGE("Failed to attach thread for event callback");
            return;
        }
        need_detach = true;
    } else if (get_env_result != JNI_OK) {
        LOGE("Failed to get JNI environment");
        return;
    }
    
    if (player->callback_method) {
        (*env)->CallVoidMethod(env, player->java_obj, player->callback_method,
                              (jint)event, (jint)arg1, (jint)arg2);
    }
    
    // Only detach if we attached in this function
    if (need_detach) {
        (*player->jvm)->DetachCurrentThread(player->jvm);
    }
}
