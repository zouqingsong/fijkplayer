// ffmpeg_player.c
// Platform-independent FFmpeg software player for Windows/Linux
// Demux → decode → swscale → RGBA buffer → Flutter texture

#include "ffmpeg_player.h"
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#define THREAD_FUNC DWORD WINAPI
#define THREAD_RET 0
typedef HANDLE thread_t;
typedef CRITICAL_SECTION mutex_t;
typedef CONDITION_VARIABLE cond_t;
#define mutex_init(m) InitializeCriticalSection(m)
#define mutex_destroy(m) DeleteCriticalSection(m)
#define mutex_lock(m) EnterCriticalSection(m)
#define mutex_unlock(m) LeaveCriticalSection(m)
#define cond_init(c) InitializeConditionVariable(c)
#define cond_destroy(c) ((void)0)
#define cond_signal(c) WakeConditionVariable(c)
#define cond_wait(c, m) SleepConditionVariableCS(c, m, INFINITE)
static thread_t thread_create(LPTHREAD_START_ROUTINE func, void *arg) {
    return CreateThread(NULL, 0, func, arg, 0, NULL);
}
static void thread_join(thread_t t) { WaitForSingleObject(t, INFINITE); CloseHandle(t); }
#else
#include <pthread.h>
#define THREAD_FUNC void *
#define THREAD_RET NULL
typedef pthread_t thread_t;
typedef pthread_mutex_t mutex_t;
typedef pthread_cond_t cond_t;
#define mutex_init(m) pthread_mutex_init(m, NULL)
#define mutex_destroy(m) pthread_mutex_destroy(m)
#define mutex_lock(m) pthread_mutex_lock(m)
#define mutex_unlock(m) pthread_mutex_unlock(m)
#define cond_init(c) pthread_cond_init(c, NULL)
#define cond_destroy(c) pthread_cond_destroy(c)
#define cond_signal(c) pthread_cond_signal(c)
#define cond_wait(c, m) pthread_cond_wait(c, m)
static thread_t thread_create(void *(*func)(void *), void *arg) {
    pthread_t t; pthread_create(&t, NULL, func, arg); return t;
}
static void thread_join(thread_t t) { pthread_join(t, NULL); }
#endif

struct FFmpegPlayer {
    // Format/codec contexts
    AVFormatContext *fmt_ctx;
    AVCodecContext *video_dec_ctx;
    AVCodecContext *audio_dec_ctx;
    struct SwsContext *sws_ctx;
    
    int video_stream_idx;
    int audio_stream_idx;
    
    // Data source
    char *url;
    
    // State
    FFPlayerState state;
    volatile bool stop_requested;
    volatile bool pause_requested;
    volatile bool seek_requested;
    int64_t seek_target_ms;
    
    // Video output (RGBA)
    uint8_t *rgba_buffer;
    int video_width;
    int video_height;
    int rgba_linesize;
    
    // Timing
    int64_t duration_ms;
    volatile int64_t current_pos_ms;
    float volume;
    
    // Threading
    thread_t read_thread;
    mutex_t mutex;
    cond_t cond;
    bool thread_started;
    
    // Callbacks
    FFPlayerEventCallback event_cb;
    void *event_opaque;
    FFPlayerFrameCallback frame_cb;
    void *frame_opaque;
};

FFmpegPlayer *ffplayer_create(void) {
    FFmpegPlayer *p = (FFmpegPlayer *)calloc(1, sizeof(FFmpegPlayer));
    if (!p) return NULL;
    
    p->state = FFPLAYER_STATE_IDLE;
    p->video_stream_idx = -1;
    p->audio_stream_idx = -1;
    p->volume = 1.0f;
    
    mutex_init(&p->mutex);
    cond_init(&p->cond);
    
    return p;
}

static void ffplayer_close_streams(FFmpegPlayer *p) {
    if (p->sws_ctx) { sws_freeContext(p->sws_ctx); p->sws_ctx = NULL; }
    if (p->video_dec_ctx) { avcodec_free_context(&p->video_dec_ctx); }
    if (p->audio_dec_ctx) { avcodec_free_context(&p->audio_dec_ctx); }
    if (p->fmt_ctx) { avformat_close_input(&p->fmt_ctx); }
    if (p->rgba_buffer) { av_free(p->rgba_buffer); p->rgba_buffer = NULL; }
    p->video_stream_idx = -1;
    p->audio_stream_idx = -1;
    p->video_width = 0;
    p->video_height = 0;
}

void ffplayer_destroy(FFmpegPlayer *p) {
    if (!p) return;
    
    p->stop_requested = true;
    cond_signal(&p->cond);
    
    if (p->thread_started) {
        thread_join(p->read_thread);
        p->thread_started = false;
    }
    
    ffplayer_close_streams(p);
    
    if (p->url) { free(p->url); p->url = NULL; }
    
    mutex_destroy(&p->mutex);
    cond_destroy(&p->cond);
    free(p);
}

void ffplayer_set_event_callback(FFmpegPlayer *p, FFPlayerEventCallback cb, void *opaque) {
    if (!p) return;
    p->event_cb = cb;
    p->event_opaque = opaque;
}

void ffplayer_set_frame_callback(FFmpegPlayer *p, FFPlayerFrameCallback cb, void *opaque) {
    if (!p) return;
    p->frame_cb = cb;
    p->frame_opaque = opaque;
}

static void fire_event(FFmpegPlayer *p, FFPlayerEvent event, int arg1, int arg2) {
    if (p->event_cb) {
        p->event_cb(p->event_opaque, event, arg1, arg2);
    }
}

int ffplayer_set_data_source(FFmpegPlayer *p, const char *url) {
    if (!p || !url) return -1;
    if (p->state != FFPLAYER_STATE_IDLE) return -1;
    
    if (p->url) free(p->url);
    p->url = strdup(url);
    p->state = FFPLAYER_STATE_INITIALIZED;
    return 0;
}

static int open_streams(FFmpegPlayer *p) {
    int ret;
    
    // Open input
    AVDictionary *options = NULL;
    if (strstr(p->url, "rtsp://")) {
        av_dict_set(&options, "rtsp_transport", "tcp", 0);
        av_dict_set(&options, "max_delay", "500000", 0);
    }
    av_dict_set(&options, "timeout", "10000000", 0);
    
    ret = avformat_open_input(&p->fmt_ctx, p->url, NULL, &options);
    av_dict_free(&options);
    if (ret < 0) return ret;
    
    ret = avformat_find_stream_info(p->fmt_ctx, NULL);
    if (ret < 0) return ret;
    
    p->duration_ms = (p->fmt_ctx->duration != AV_NOPTS_VALUE) 
        ? p->fmt_ctx->duration / 1000 : 0;
    
    // Open video stream
    for (unsigned i = 0; i < p->fmt_ctx->nb_streams; i++) {
        AVStream *st = p->fmt_ctx->streams[i];
        if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && p->video_stream_idx < 0) {
            const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
            if (!codec) continue;
            
            p->video_dec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(p->video_dec_ctx, st->codecpar);
            p->video_dec_ctx->thread_count = 2;
            
            ret = avcodec_open2(p->video_dec_ctx, codec, NULL);
            if (ret < 0) { avcodec_free_context(&p->video_dec_ctx); continue; }
            
            p->video_stream_idx = i;
            p->video_width = p->video_dec_ctx->width;
            p->video_height = p->video_dec_ctx->height;
        }
        else if (st->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && p->audio_stream_idx < 0) {
            const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
            if (!codec) continue;
            
            p->audio_dec_ctx = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(p->audio_dec_ctx, st->codecpar);
            
            ret = avcodec_open2(p->audio_dec_ctx, codec, NULL);
            if (ret < 0) { avcodec_free_context(&p->audio_dec_ctx); continue; }
            
            p->audio_stream_idx = i;
        }
    }
    
    if (p->video_stream_idx < 0) return -1;
    
    // Setup swscale for RGBA output
    p->sws_ctx = sws_getContext(
        p->video_width, p->video_height, p->video_dec_ctx->pix_fmt,
        p->video_width, p->video_height, AV_PIX_FMT_RGBA,
        SWS_BILINEAR, NULL, NULL, NULL);
    if (!p->sws_ctx) return -1;
    
    // Allocate RGBA buffer
    p->rgba_linesize = p->video_width * 4;
    int buf_size = av_image_get_buffer_size(AV_PIX_FMT_RGBA, p->video_width, p->video_height, 1);
    p->rgba_buffer = (uint8_t *)av_malloc(buf_size);
    if (!p->rgba_buffer) return -1;
    memset(p->rgba_buffer, 0, buf_size);
    
    return 0;
}

static THREAD_FUNC read_thread_func(void *arg) {
    FFmpegPlayer *p = (FFmpegPlayer *)arg;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    int ret;
    
    // Open streams
    ret = open_streams(p);
    if (ret < 0) {
        p->state = FFPLAYER_STATE_ERROR;
        fire_event(p, FFPLAYER_EVENT_ERROR, ret, 0);
        av_packet_free(&pkt);
        av_frame_free(&frame);
        return THREAD_RET;
    }
    
    p->state = FFPLAYER_STATE_PREPARED;
    fire_event(p, FFPLAYER_EVENT_PREPARED, 0, 0);
    fire_event(p, FFPLAYER_EVENT_VIDEO_SIZE_CHANGED, p->video_width, p->video_height);
    
    // Wait for start command
    mutex_lock(&p->mutex);
    while (!p->stop_requested && p->state == FFPLAYER_STATE_PREPARED) {
        cond_wait(&p->cond, &p->mutex);
    }
    mutex_unlock(&p->mutex);
    
    if (p->stop_requested) goto cleanup;
    
    // Main read/decode loop
    while (!p->stop_requested) {
        // Handle pause
        if (p->pause_requested) {
            mutex_lock(&p->mutex);
            while (p->pause_requested && !p->stop_requested) {
                cond_wait(&p->cond, &p->mutex);
            }
            mutex_unlock(&p->mutex);
            if (p->stop_requested) break;
        }
        
        // Handle seek
        if (p->seek_requested) {
            int64_t target = p->seek_target_ms * 1000;
            av_seek_frame(p->fmt_ctx, -1, target, AVSEEK_FLAG_BACKWARD);
            if (p->video_dec_ctx) avcodec_flush_buffers(p->video_dec_ctx);
            if (p->audio_dec_ctx) avcodec_flush_buffers(p->audio_dec_ctx);
            p->seek_requested = false;
            fire_event(p, FFPLAYER_EVENT_SEEK_COMPLETE, 0, 0);
        }
        
        ret = av_read_frame(p->fmt_ctx, pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF || avio_feof(p->fmt_ctx->pb)) {
                p->state = FFPLAYER_STATE_COMPLETED;
                fire_event(p, FFPLAYER_EVENT_COMPLETED, 0, 0);
            }
            break;
        }
        
        // Decode video
        if (pkt->stream_index == p->video_stream_idx) {
            ret = avcodec_send_packet(p->video_dec_ctx, pkt);
            if (ret >= 0) {
                while (avcodec_receive_frame(p->video_dec_ctx, frame) >= 0) {
                    // Convert to RGBA
                    uint8_t *dst[4] = { p->rgba_buffer, NULL, NULL, NULL };
                    int dst_linesize[4] = { p->rgba_linesize, 0, 0, 0 };
                    
                    sws_scale(p->sws_ctx,
                        (const uint8_t *const *)frame->data, frame->linesize,
                        0, p->video_height,
                        dst, dst_linesize);
                    
                    // Update position
                    AVStream *st = p->fmt_ctx->streams[p->video_stream_idx];
                    if (frame->pts != AV_NOPTS_VALUE) {
                        p->current_pos_ms = av_rescale_q(frame->pts, st->time_base, AV_TIME_BASE_Q) / 1000;
                    }
                    
                    // Notify frame ready
                    if (p->frame_cb) {
                        p->frame_cb(p->frame_opaque);
                    }
                    
                    // Frame pacing for non-live streams
                    if (p->duration_ms > 0 && frame->pts != AV_NOPTS_VALUE) {
                        // Simple delay based on frame duration
                        int64_t delay = av_rescale_q(frame->duration, st->time_base, AV_TIME_BASE_Q);
                        if (delay > 0 && delay < 100000) { // Max 100ms
                            av_usleep((unsigned)delay);
                        }
                    }
                    
                    av_frame_unref(frame);
                }
            }
        }
        
        av_packet_unref(pkt);
    }
    
cleanup:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    return THREAD_RET;
}

int ffplayer_prepare_async(FFmpegPlayer *p) {
    if (!p || !p->url) return -1;
    if (p->state != FFPLAYER_STATE_INITIALIZED) return -1;
    
    p->state = FFPLAYER_STATE_PREPARING;
    p->stop_requested = false;
    
    p->read_thread = thread_create(read_thread_func, p);
    p->thread_started = true;
    
    return 0;
}

int ffplayer_start(FFmpegPlayer *p) {
    if (!p) return -1;
    
    if (p->state == FFPLAYER_STATE_PREPARED || p->state == FFPLAYER_STATE_PAUSED) {
        p->pause_requested = false;
        p->state = FFPLAYER_STATE_PLAYING;
        cond_signal(&p->cond);
        fire_event(p, FFPLAYER_EVENT_STARTED, 0, 0);
        return 0;
    }
    return -1;
}

int ffplayer_pause(FFmpegPlayer *p) {
    if (!p) return -1;
    
    if (p->state == FFPLAYER_STATE_PLAYING) {
        p->pause_requested = true;
        p->state = FFPLAYER_STATE_PAUSED;
        fire_event(p, FFPLAYER_EVENT_PAUSED, 0, 0);
        return 0;
    }
    return -1;
}

int ffplayer_stop(FFmpegPlayer *p) {
    if (!p) return -1;
    
    p->stop_requested = true;
    p->pause_requested = false;
    cond_signal(&p->cond);
    
    if (p->thread_started) {
        thread_join(p->read_thread);
        p->thread_started = false;
    }
    
    ffplayer_close_streams(p);
    p->state = FFPLAYER_STATE_STOPPED;
    return 0;
}

int ffplayer_reset(FFmpegPlayer *p) {
    if (!p) return -1;
    ffplayer_stop(p);
    p->state = FFPLAYER_STATE_IDLE;
    return 0;
}

int ffplayer_seek(FFmpegPlayer *p, int64_t msec) {
    if (!p) return -1;
    p->seek_target_ms = msec;
    p->seek_requested = true;
    return 0;
}

void ffplayer_set_volume(FFmpegPlayer *p, float volume) {
    if (!p) return;
    if (volume < 0.0f) volume = 0.0f;
    if (volume > 1.0f) volume = 1.0f;
    p->volume = volume;
}

const uint8_t *ffplayer_get_frame(FFmpegPlayer *p, int *width, int *height) {
    if (!p || !p->rgba_buffer) return NULL;
    if (width) *width = p->video_width;
    if (height) *height = p->video_height;
    return p->rgba_buffer;
}

int ffplayer_get_video_width(FFmpegPlayer *p) { return p ? p->video_width : 0; }
int ffplayer_get_video_height(FFmpegPlayer *p) { return p ? p->video_height : 0; }
int64_t ffplayer_get_duration(FFmpegPlayer *p) { return p ? p->duration_ms : 0; }
int64_t ffplayer_get_current_position(FFmpegPlayer *p) { return p ? p->current_pos_ms : 0; }
FFPlayerState ffplayer_get_state(FFmpegPlayer *p) { return p ? p->state : FFPLAYER_STATE_IDLE; }
const char *ffplayer_get_data_source(FFmpegPlayer *p) { return p ? p->url : NULL; }
bool ffplayer_is_playing(FFmpegPlayer *p) { return p && p->state == FFPLAYER_STATE_PLAYING; }

uint8_t *ffplayer_snapshot_png(FFmpegPlayer *p, int *out_size) {
    if (!p || !p->rgba_buffer || p->video_width <= 0 || p->video_height <= 0) return NULL;
    if (out_size) *out_size = 0;
    
    int width = p->video_width;
    int height = p->video_height;
    
    // Find PNG encoder
    const AVCodec *png_codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
    if (!png_codec) return NULL;
    
    AVCodecContext *enc_ctx = avcodec_alloc_context3(png_codec);
    if (!enc_ctx) return NULL;
    
    enc_ctx->width = width;
    enc_ctx->height = height;
    enc_ctx->pix_fmt = AV_PIX_FMT_RGBA;
    enc_ctx->time_base = (AVRational){1, 1};
    
    int ret = avcodec_open2(enc_ctx, png_codec, NULL);
    if (ret < 0) {
        avcodec_free_context(&enc_ctx);
        return NULL;
    }
    
    // Create frame from current RGBA buffer
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        avcodec_free_context(&enc_ctx);
        return NULL;
    }
    
    frame->format = AV_PIX_FMT_RGBA;
    frame->width = width;
    frame->height = height;
    frame->data[0] = p->rgba_buffer;
    frame->linesize[0] = width * 4;
    frame->pts = 0;
    
    // Encode
    AVPacket *pkt = av_packet_alloc();
    uint8_t *result = NULL;
    
    ret = avcodec_send_frame(enc_ctx, frame);
    if (ret >= 0) {
        ret = avcodec_receive_packet(enc_ctx, pkt);
        if (ret >= 0) {
            // Copy PNG data
            result = (uint8_t *)malloc(pkt->size);
            if (result) {
                memcpy(result, pkt->data, pkt->size);
                if (out_size) *out_size = pkt->size;
            }
        }
    }
    
    av_packet_free(&pkt);
    // Don't free frame data - it points to p->rgba_buffer which is owned by the player
    frame->data[0] = NULL;
    av_frame_free(&frame);
    avcodec_free_context(&enc_ctx);
    
    return result;
}
