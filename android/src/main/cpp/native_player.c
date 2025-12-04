/**
 * Native Player Implementation
 * Integrates FFmpeg demuxer, MediaCodec decoder, frame queue, and GL renderer
 */

#include "native_player.h"
#include "ffmpeg_demuxer.h"
#include "mediacodec_decoder.h"
#include "ffmpeg_soft_decoder.h"
#include "audio_decoder.h"
#include "audio_queue.h"
#include "packet_queue.h"
#include "frame_queue.h"
#include "gl_renderer.h"
#include <jni.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <errno.h>
#include <math.h>
#include <android/log.h>
#include <android/native_window_jni.h>

#ifdef USE_SOFTWARE_DECODER
#include "ffmpeg/include/libavcodec/avcodec.h"
#endif

// Define AV_NOPTS_VALUE if not available
#ifndef AV_NOPTS_VALUE
#define AV_NOPTS_VALUE ((int64_t)UINT64_C(0x8000000000000000))
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
    AudioDecoder* audio_decoder;      // Audio decoder (Phase 2)
    AudioQueue* audio_queue;          // PCM buffer queue (Phase 3)
    FrameQueue* frame_queue;
    GLRenderer* renderer;
    
    // Java callback
    JavaVM* jvm;
    jobject java_obj;        // Global reference to Java player object
    jobject audio_renderer;  // Global reference to AudioRenderer.java (Phase 3)
    jmethodID callback_method;
    
    // State
    PlayerState state;
    char* data_source;
    int video_stream_index;
    int audio_stream_index;  // Audio stream index (-1 if no audio)
    
    // Packet queues (ijkplayer architecture)
    PacketQueue videoq;   // Video packet queue
    PacketQueue audioq;   // Audio packet queue
    
    // Threading
    pthread_t demuxer_thread;
    pthread_t decoder_thread;
    pthread_t audio_decoder_thread;  // Audio decoder thread (Phase 2)
    pthread_t audio_playback_thread; // Audio playback thread (Phase 3)
    pthread_mutex_t state_mutex;
    bool stop_requested;
    bool paused;
    
    // Configuration
    PlayerOptions options;
    
    // Statistics
    PlayerStats stats;
    int64_t start_time;
    int64_t current_position;
    
    // A/V Sync (ijkplayer-style)
    double frame_timer;        // Time when next frame should be displayed
    double audio_clock;        // Current audio playback position
    int64_t last_video_pts;    // Last video frame PTS for duration calculation
    pthread_mutex_t clock_mutex; // Protect clock access
    
    // Surface
    ANativeWindow* native_window;
};

// Forward declarations
static void* demuxer_thread_func(void* arg);
static void* decoder_thread_func(void* arg);
static void* audio_decoder_thread_func(void* arg);
static void* audio_playback_thread_func(void* arg);
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
    pthread_mutex_init(&player->clock_mutex, NULL);
    
    // Initialize A/V sync
    player->frame_timer = 0.0;
    player->audio_clock = 0.0;
    player->last_video_pts = AV_NOPTS_VALUE;
    
    // Initialize packet queues
    packet_queue_init(&player->videoq);
    packet_queue_init(&player->audioq);
    
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
    
    // Find audio stream (optional - video can play without audio)
    player->audio_stream_index = -1;
    for (int i = 0; i < stream_count; i++) {
        FFStream* stream = ff_demuxer_get_stream(player->demuxer, i);
        if (stream && stream->type == FF_STREAM_TYPE_AUDIO) {
            player->audio_stream_index = i;
            break;
        }
    }
    
    if (player->audio_stream_index >= 0) {
        FFStream* audio_stream = ff_demuxer_get_stream(player->demuxer, player->audio_stream_index);
        if (audio_stream) {
            LOGI("🔊 Audio stream found: %d Hz, %d channels, codec=%d",
                 audio_stream->sample_rate, audio_stream->channels, audio_stream->audio_codec);
            
            // Initialize audio decoder (Phase 2)
            player->audio_decoder = audio_decoder_create();
            if (player->audio_decoder) {
                AudioDecoderConfig audio_config = {
                    .codec = audio_stream->audio_codec,
                    .sample_rate = audio_stream->sample_rate,
                    .channels = audio_stream->channels,
                    .extradata = audio_stream->extradata,
                    .extradata_size = audio_stream->extradata_size,
                    .output_format = AUDIO_FORMAT_S16,
                    .backend = AUDIO_DECODER_MEDIACODEC  // Prefer hardware
                };
                
                if (audio_decoder_configure(player->audio_decoder, &audio_config) == 0) {
                    LOGI("✅ Audio decoder configured successfully");
                    
                    // Phase 3: Create audio queue for PCM buffering
                    AudioQueueConfig queue_config = {
                        .sample_rate = audio_stream->sample_rate,
                        .channels = audio_stream->channels,
                        .format = AUDIO_QUEUE_FORMAT_S16,
                        .capacity_ms = 200  // 200ms buffer (reduced for faster audio start)
                    };
                    player->audio_queue = audio_queue_create(&queue_config);
                    if (!player->audio_queue) {
                        LOGW("Failed to create audio queue - audio disabled");
                        audio_decoder_destroy(player->audio_decoder);
                        player->audio_decoder = NULL;
                        player->audio_stream_index = -1;
                        return -1;
                    }
                    
                    // Phase 3: Create AudioRenderer Java object
                    JNIEnv* env = NULL;
                    (*player->jvm)->GetEnv(player->jvm, (void**)&env, JNI_VERSION_1_6);
                    if (!env) {
                        LOGE("Failed to get JNIEnv");
                        audio_queue_destroy(player->audio_queue);
                        player->audio_queue = NULL;
                        audio_decoder_destroy(player->audio_decoder);
                        player->audio_decoder = NULL;
                        player->audio_stream_index = -1;
                        return -1;
                    }
                    
                    jclass renderer_class = (*env)->FindClass(env, "com/befovy/fijkplayer/AudioRenderer");
                    if (!renderer_class) {
                        LOGE("Failed to find AudioRenderer class");
                        audio_queue_destroy(player->audio_queue);
                        player->audio_queue = NULL;
                        audio_decoder_destroy(player->audio_decoder);
                        player->audio_decoder = NULL;
                        player->audio_stream_index = -1;
                        return -1;
                    }
                    
                    jmethodID constructor = (*env)->GetMethodID(env, renderer_class, "<init>", "()V");
                    jobject local_renderer = (*env)->NewObject(env, renderer_class, constructor);
                    player->audio_renderer = (*env)->NewGlobalRef(env, local_renderer);
                    (*env)->DeleteLocalRef(env, local_renderer);
                    
                    // Initialize AudioRenderer
                    jmethodID init_method = (*env)->GetMethodID(env, renderer_class, "init", "(II)Z");
                    jboolean init_result = (*env)->CallBooleanMethod(env, player->audio_renderer, 
                                                                      init_method, 
                                                                      audio_stream->sample_rate, 
                                                                      audio_stream->channels);
                    
                    if (!init_result) {
                        LOGE("Failed to initialize AudioRenderer - audio disabled");
                        (*env)->DeleteGlobalRef(env, player->audio_renderer);
                        player->audio_renderer = NULL;
                        audio_queue_destroy(player->audio_queue);
                        player->audio_queue = NULL;
                        audio_decoder_destroy(player->audio_decoder);
                        player->audio_decoder = NULL;
                        player->audio_stream_index = -1;
                        return -1;
                    }
                    
                    LOGI("🔊 Audio queue and renderer created successfully");
                } else {
                    LOGW("Failed to configure audio decoder - audio disabled");
                    audio_decoder_destroy(player->audio_decoder);
                    player->audio_decoder = NULL;
                    player->audio_stream_index = -1;
                }
            } else {
                LOGW("Failed to create audio decoder - audio disabled");
                player->audio_stream_index = -1;
            }
        }
    } else {
        LOGI("No audio stream found - video only playback");
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
    
    // Start audio decoder thread if audio is available
    if (player->audio_decoder) {
        if (audio_decoder_start(player->audio_decoder) == 0) {
            pthread_create(&player->audio_decoder_thread, NULL, audio_decoder_thread_func, player);
            LOGI("🔊 Audio decoder thread started");
        } else {
            LOGW("Failed to start audio decoder");
        }
    }
    
    // Start audio playback thread if audio queue and renderer are available
    if (player->audio_queue && player->audio_renderer) {
        pthread_create(&player->audio_playback_thread, NULL, audio_playback_thread_func, player);
        LOGI("🔊 Audio playback thread created (queue=%p, renderer=%p)", 
             player->audio_queue, player->audio_renderer);
    } else {
        LOGW("🔊 Audio playback thread NOT created (queue=%p, renderer=%p)",
             player->audio_queue, player->audio_renderer);
    }
    
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
    
    // Abort packet queues to unblock waiting threads
    packet_queue_abort(&player->videoq);
    packet_queue_abort(&player->audioq);
    
    // Interrupt demuxer to unblock any pending operations
    if (player->demuxer) {
        ff_demuxer_interrupt(player->demuxer);
    }
    
    // Wait for threads
    pthread_join(player->demuxer_thread, NULL);
    pthread_join(player->decoder_thread, NULL);
    
    // Wait for audio decoder thread if running
    if (player->audio_decoder) {
        pthread_join(player->audio_decoder_thread, NULL);
        audio_decoder_stop(player->audio_decoder);
        LOGI("🔊 Audio decoder thread stopped");
    }
    
    // Wait for audio playback thread if running
    if (player->audio_queue && player->audio_renderer) {
        pthread_join(player->audio_playback_thread, NULL);
        LOGI("🔊 Audio playback thread stopped");
    }
    
    // Flush audio queue
    if (player->audio_queue) {
        audio_queue_flush(player->audio_queue);
    }
    
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
    
    if (player->audio_decoder) {
        audio_decoder_flush(player->audio_decoder);
    }
    
    // Flush packet queues
    packet_queue_flush(&player->videoq);
    packet_queue_flush(&player->audioq);
    
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

double native_player_get_frame_rate(NativePlayer* player) {
    if (!player) return 0.0;
    
    // Thread-safe read with mutex
    pthread_mutex_lock(&player->state_mutex);
    double frame_rate = player->stats.fps;
    pthread_mutex_unlock(&player->state_mutex);
    
    // If frame rate is not available or invalid, estimate based on resolution
    if (frame_rate <= 0.0) {
        int width = player->stats.video_width;
        int height = player->stats.video_height;
        
        // Common frame rate estimates based on resolution
        if (height >= 1080) {
            frame_rate = 25.0;  // HD content often 25fps
        } else if (height >= 720) {
            frame_rate = 24.0;  // Standard cinema rate
        } else {
            frame_rate = 23.976; // True cinema rate for SD
        }
        
        LOGD("Estimated frame rate: %.3f fps (resolution: %dx%d)", 
             frame_rate, width, height);
    } else {
        LOGD("Actual frame rate: %.3f fps", frame_rate);
    }
    
    return frame_rate;
}

int native_player_get_video_width(NativePlayer* player) {
    return player ? player->stats.video_width : 0;
}

int native_player_get_video_height(NativePlayer* player) {
    return player ? player->stats.video_height : 0;
}

int native_player_get_audio_sample_rate(NativePlayer* player) {
    if (!player || player->audio_stream_index < 0) return 0;
    
    FFStream* stream = ff_demuxer_get_stream(player->demuxer, player->audio_stream_index);
    return stream ? stream->sample_rate : 0;
}

int native_player_get_audio_channels(NativePlayer* player) {
    if (!player || player->audio_stream_index < 0) return 0;
    
    FFStream* stream = ff_demuxer_get_stream(player->demuxer, player->audio_stream_index);
    return stream ? stream->channels : 0;
}

bool native_player_has_audio(NativePlayer* player) {
    return player && player->audio_stream_index >= 0;
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
    
    if (player->audio_decoder) {
        audio_decoder_stop(player->audio_decoder);
        audio_decoder_destroy(player->audio_decoder);
    }
    
    // Release audio queue
    if (player->audio_queue) {
        audio_queue_destroy(player->audio_queue);
        player->audio_queue = NULL;
        LOGI("🔊 Audio queue destroyed");
    }
    
    // Release audio renderer
    if (player->audio_renderer && player->jvm) {
        JNIEnv* env;
        (*player->jvm)->AttachCurrentThread(player->jvm, &env, NULL);
        
        jclass renderer_class = (*env)->GetObjectClass(env, player->audio_renderer);
        jmethodID release_method = (*env)->GetMethodID(env, renderer_class, "release", "()V");
        (*env)->CallVoidMethod(env, player->audio_renderer, release_method);
        
        (*env)->DeleteGlobalRef(env, player->audio_renderer);
        player->audio_renderer = NULL;
        
        (*player->jvm)->DetachCurrentThread(player->jvm);
        LOGI("🔊 AudioRenderer released");
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
    
    // Destroy packet queues
    packet_queue_destroy(&player->videoq);
    packet_queue_destroy(&player->audioq);
    
    pthread_mutex_destroy(&player->state_mutex);
    
    free(player);
    
    LOGD("Player released");
}

// Audio playback thread - reads from queue and writes to AudioTrack
static void* audio_playback_thread_func(void* arg) {
    NativePlayer* player = (NativePlayer*)arg;
    
    // Attach to JVM
    JNIEnv* env;
    (*player->jvm)->AttachCurrentThread(player->jvm, &env, NULL);
    
    LOGI("🔊 Audio playback thread started");
    
    // Get AudioRenderer methods
    jclass renderer_class = (*env)->GetObjectClass(env, player->audio_renderer);
    jmethodID write_method = (*env)->GetMethodID(env, renderer_class, "write", "([BI)I");
    jmethodID start_method = (*env)->GetMethodID(env, renderer_class, "start", "()V");
    
    // Start AudioTrack playback
    (*env)->CallVoidMethod(env, player->audio_renderer, start_method);
    LOGI("🔊 AudioTrack started");
    
    // Playback buffer - use smaller chunks for better responsiveness
    int16_t pcm_buffer[2048 * 2];  // 2048 samples = ~46ms at 44.1kHz
    int playback_count = 0;
    int empty_count = 0;
    
    LOGI("🔊 Playback loop starting (queue=%p)", player->audio_queue);
    
    while (!player->stop_requested) {
        if (player->paused) {
            usleep(10000);  // 10ms
            continue;
        }
        
        // Try to get PCM from queue (smaller chunks for better pacing)
        int64_t pts;
        int samples = audio_queue_pop(player->audio_queue, pcm_buffer, 2048, &pts);
        
        if (samples > 0) {
            playback_count++;
            
            // Update audio clock for A/V sync (like ijkplayer's set_clock)
            if (pts != AV_NOPTS_VALUE) {
                double audio_pts = (double)pts / 1000000.0;  // Convert to seconds
                pthread_mutex_lock(&player->clock_mutex);
                player->audio_clock = audio_pts;
                pthread_mutex_unlock(&player->clock_mutex);
            }
            
            // Apply 30x volume boost - MediaCodec AAC decoder outputs very low levels
            // Without this, audio is barely audible (max amplitude ~15 = 0.05% of full scale)
            for (int i = 0; i < samples * 2; i++) {  // *2 for stereo
                int32_t boosted = (int32_t)pcm_buffer[i] * 30;
                // Clamp to int16 range to prevent overflow
                if (boosted > 32767) boosted = 32767;
                else if (boosted < -32768) boosted = -32768;
                pcm_buffer[i] = (int16_t)boosted;
            }
            
            // Analyze amplitude for first few chunks to verify boost
            if (playback_count <= 3) {
                int16_t max_amplitude = 0;
                for (int i = 0; i < samples * 2; i++) {  // *2 for stereo
                    int16_t sample = pcm_buffer[i];
                    int16_t abs_sample = (sample < 0) ? -sample : sample;
                    if (abs_sample > max_amplitude) max_amplitude = abs_sample;
                }
                LOGI("🔊 Audio amplitude #%d (30x boost): max=%d (%.1f%% of full scale)",
                     playback_count, max_amplitude, (max_amplitude * 100.0 / 32767.0));
            }
            
            // Convert to byte array for Java
            int bytes = samples * 2 * 2;  // samples * channels(2) * bytes_per_sample(2)
            jbyteArray java_buffer = (*env)->NewByteArray(env, bytes);
            (*env)->SetByteArrayRegion(env, java_buffer, 0, bytes, (jbyte*)pcm_buffer);
            
            // Write to AudioTrack - BLOCKING mode will pace this to real-time
            int written = (*env)->CallIntMethod(env, player->audio_renderer, write_method, 
                                                java_buffer, bytes);
            
            (*env)->DeleteLocalRef(env, java_buffer);
            
            // Log writes with audio clock
            if (playback_count <= 10 || playback_count % 100 == 0) {
                LOGI("🔊 Played chunk #%d: %d samples, written=%d, PTS: %.2fs", 
                     playback_count, samples, written, (double)pts / 1000000.0);
            }
        } else {
            // Queue empty - sleep very briefly to avoid busy-waiting
            empty_count++;
            if (empty_count % 100 == 0) {
                LOGI("🔊 Audio queue empty %d times", empty_count);
            }
            usleep(1000);  // 1ms - minimal sleep
        }
    }
    
    LOGI("🔊 Audio playback thread stopped");
    
    // Detach from JVM
    (*player->jvm)->DetachCurrentThread(player->jvm);
    
    return NULL;
}

// Thread functions
static void* demuxer_thread_func(void* arg) {
    NativePlayer* player = (NativePlayer*)arg;
    
    LOGD("📦 Demuxer thread started (ijkplayer architecture)");
    
    int packet_count = 0;
    
    while (!player->stop_requested) {
        if (player->paused) {
            usleep(10000);
            continue;
        }
        
        // Read packet from FFmpeg demuxer
        FFPacket* packet = NULL;
        int ret = ff_demuxer_read_packet(player->demuxer, &packet);
        
        if (ret < 0) {
            // EOF or error
            LOGI("📦 Demuxer reached end: ret=%d", ret);
            // TODO: Put EOF marker packets into queues
            break;
        }
        
        if (!packet) {
            continue;
        }
        
        packet_count++;
        
        // Route packet to appropriate queue based on stream index
        if (packet->stream_index == player->video_stream_index) {
            // Video packet - put into video queue
            if (packet_queue_put(&player->videoq, packet) < 0) {
                LOGW("📦 Failed to put video packet into queue");
                ff_packet_free(packet);
            }
        } else if (packet->stream_index == player->audio_stream_index) {
            // Audio packet - put into audio queue
            if (packet_queue_put(&player->audioq, packet) < 0) {
                LOGW("📦 Failed to put audio packet into queue");
                ff_packet_free(packet);
            }
        } else {
            // Unknown stream - discard
            ff_packet_free(packet);
        }
        
        // Log progress periodically
        if (packet_count % 1000 == 0) {
            LOGI("📦 Demuxer: %d packets (videoq: %d, audioq: %d)", 
                 packet_count,
                 packet_queue_get_nb_packets(&player->videoq),
                 packet_queue_get_nb_packets(&player->audioq));
        }
    }
    
    LOGD("📦 Demuxer thread finished (packets: %d)", packet_count);
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
        
        // PULL FRAMES: Add frame timing to match expected frame rate (~24fps = ~42ms per frame)
        // This prevents decoding/displaying frames too fast
        
        DecodedFrame* frame = NULL;
        // Non-blocking (0ms) - if buffer ready, grab immediately
        int recv_ret = mediacodec_decoder_receive_frame(player->decoder, &frame, 0);
        
        if (recv_ret == 0 && frame) {
            total_frames_received++;
            player->current_position = frame->pts;
            
            // Calculate frame duration and sync delay (ijkplayer algorithm)
            double frame_duration = 0.04;  // Default 40ms (~25fps)
            if (player->last_video_pts != AV_NOPTS_VALUE && frame->pts > player->last_video_pts) {
                frame_duration = (double)(frame->pts - player->last_video_pts) / 1000000.0;  // Convert to seconds
            }
            
            // Get audio clock for sync
            pthread_mutex_lock(&player->clock_mutex);
            double audio_clock = player->audio_clock;
            double frame_pts = (double)frame->pts / 1000000.0;  // Convert to seconds
            pthread_mutex_unlock(&player->clock_mutex);
            
            // Compute target delay based on A/V diff (like ijkplayer's compute_target_delay)
            double delay = frame_duration;
            double diff = frame_pts - audio_clock;  // Positive = video ahead, negative = video behind
            
            // Sync threshold (like ijkplayer AV_SYNC_THRESHOLD_MIN/MAX)
            const double sync_threshold_min = 0.04;   // 40ms
            const double sync_threshold_max = 0.1;    // 100ms
            double sync_threshold = fmax(sync_threshold_min, fmin(sync_threshold_max, delay));
            
            if (fabs(diff) < 10.0) {  // Only sync if difference is reasonable (< 10 seconds)
                if (diff <= -sync_threshold) {
                    // Video behind audio - shorten delay
                    delay = fmax(0, delay + diff);
                } else if (diff >= sync_threshold) {
                    // Video ahead of audio - extend delay
                    delay = delay + diff;
                }
            }
            
            // Log every 120 frames with detailed sync info
            if (total_frames_received % 120 == 0) {
                LOGI("📊 Frame #%d | V_PTS: %.3fs | A_CLK: %.3fs | Diff: %+.3fs | Dur: %.0fms | Delay: %.0fms", 
                     total_frames_received, frame_pts, audio_clock, diff, 
                     frame_duration * 1000, delay * 1000);
            }
            
            player->last_video_pts = frame->pts;
            
            if (frame->data[0]) free(frame->data[0]);
            free(frame);
            
            // Sleep for computed delay (like ijkplayer's remaining_time)
            if (delay > 0 && delay < 0.5) {  // Sanity check: 0-500ms
                usleep((useconds_t)(delay * 1000000));
            } else {
                usleep(40000);  // Fallback to 40ms if delay is unreasonable
            }
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
        
        // Get packet from video queue (blocking)
        FFPacket* packet = NULL;
        int ret = packet_queue_get(&player->videoq, &packet, 1);  // Block until packet available
        
        if (ret < 0 || !packet) {
            // Queue aborted or error
            if (player->stop_requested) {
                break;
            }
            continue;
        }
        
        // Process VIDEO packet
        video_packet_count++;
        
        // Accept first video packet OR wait for keyframe
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
        
        // Free packet
        ff_packet_free(packet);
    }
    
    LOGI("Decoder thread finished");
    return NULL;
}

// Audio decoder thread - decodes audio packets to PCM
static void* audio_decoder_thread_func(void* arg) {
    NativePlayer* player = (NativePlayer*)arg;
    
    LOGI("🔊 Audio decoder thread started");
    
    int audio_packet_count = 0;
    int decoded_audio_count = 0;
    int64_t last_audio_pts = 0;
    
    while (!player->stop_requested) {
        if (player->paused) {
            usleep(10000);  // 10ms
            continue;
        }
        
        // Get packet from audio queue (blocking)
        FFPacket* packet = NULL;
        int ret = packet_queue_get(&player->audioq, &packet, 1);  // Block until packet available
        
        if (ret < 0 || !packet) {
            // Queue aborted or error
            LOGW("🔊 Failed to get audio packet: ret=%d, packet=%p", ret, packet);
            if (player->stop_requested) {
                break;
            }
            continue;
        }
        
        audio_packet_count++;
        
        // Log first few packets
        if (audio_packet_count <= 5 || audio_packet_count % 100 == 0) {
            LOGI("🔊 Got audio packet #%d: size=%d, pts=%lld", 
                 audio_packet_count, packet->size, (long long)packet->pts);
        }
        
        // Send packet to audio decoder first
        ret = audio_decoder_send_packet(player->audio_decoder, packet, 10000);  // 10ms timeout
        
        // Free packet immediately after sending
        ff_packet_free(packet);
        
        if (ret == 0) {
            // Packet sent successfully
            if (audio_packet_count <= 5 || audio_packet_count % 100 == 0) {
                LOGI("🔊 Packet #%d sent to decoder", audio_packet_count);
            }
            
            // After sending, try to drain decoded output
            // MediaCodec AAC typically needs 5-10 packets buffered before first output
            // Use blocking receive with longer timeout to allow initial buffering
            int frames_received = 0;
            int timeout_us;
            
            // Strategy: Use longer timeout for first few receive attempts to allow buffering
            if (decoded_audio_count == 0) {
                // No frames decoded yet - MediaCodec is buffering
                // Use progressively longer timeouts as we send more packets
                if (audio_packet_count < 10) {
                    timeout_us = 0;  // Non-blocking for first few packets (let it buffer)
                } else if (audio_packet_count == 10) {
                    timeout_us = 50000;  // 50ms timeout on 10th packet (should have output by now)
                    LOGI("🔊 Attempting first audio decode after %d packets (50ms timeout)...", audio_packet_count);
                } else {
                    timeout_us = 20000;  // 20ms timeout after 10th packet
                }
            } else {
                // Already receiving frames - use shorter timeout
                timeout_us = 5000;  // 5ms
            }
            
            // Try to receive decoded audio
            while (!player->stop_requested) {
                DecodedAudio audio;
                ret = audio_decoder_receive_audio(player->audio_decoder, &audio, timeout_us);
                
                if (ret == 0) {
                    decoded_audio_count++;
                    frames_received++;
                    last_audio_pts = audio.pts;
                    
                    // Log first 10 and then every 100 decoded audio frames
                    if (decoded_audio_count <= 10 || decoded_audio_count % 100 == 0) {
                        LOGI("🔊 Audio #%d decoded: %d Hz, %d ch, %d samples, PTS: %lld ms",
                             decoded_audio_count,
                             audio.sample_rate,
                             audio.channels,
                             audio.samples,
                             (long long)(audio.pts / 1000));
                    }
                    
                    // Phase 3: Queue PCM for playback (BLOCKING - will wait if full)
                    if (player->audio_queue) {
                        if (decoded_audio_count <= 5) {
                            LOGI("🔊 Pushing audio #%d to queue (samples=%d)...", 
                                 decoded_audio_count, audio.samples);
                        }
                        int push_ret = audio_queue_push(player->audio_queue, audio.data, audio.samples, audio.pts);
                        if (push_ret != 0) {
                            LOGW("🔊 Audio queue push failed: %d", push_ret);
                        } else if (decoded_audio_count <= 5) {
                            LOGI("🔊 Audio #%d pushed successfully", decoded_audio_count);
                        }
                    }
                    
                    // Free the decoded audio buffer
                    audio_decoder_free_audio(&audio);
                    
                    // After first successful receive, switch to non-blocking for additional drains
                    timeout_us = 0;
                } else {
                    // No more frames available
                    if (ret != -EAGAIN && decoded_audio_count == 0 && audio_packet_count >= 10) {
                        LOGW("🔊 Still no audio output after %d packets, error: %d", audio_packet_count, ret);
                    }
                    break;
                }
            }
            
            // Log draining progress
            if (frames_received > 0 && audio_packet_count % 100 == 0) {
                LOGI("🔊 Packet #%d: received %d frames", audio_packet_count, frames_received);
            }
        } else if (ret == -EAGAIN) {
            // Decoder input buffer full, will retry with next packet
            if (audio_packet_count % 100 == 0) {
                LOGW("🔊 Audio decoder input full (packet #%d)", audio_packet_count);
            }
        } else {
            LOGE("🔊 Error sending audio packet #%d: %d", audio_packet_count, ret);
        }
    }
    
    LOGI("🔊 Audio decoder thread finished (packets: %d, decoded: %d)", 
         audio_packet_count, decoded_audio_count);
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
