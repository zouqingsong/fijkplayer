#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

// FFmpeg headers from ijkplayer
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/avutil.h"
#include "libavutil/time.h"
#include "libavutil/opt.h"

#define LOG_TAG "FFmpegRecorder"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

typedef struct {
    AVFormatContext *input_ctx;
    AVFormatContext *output_ctx;
    int video_stream_index;
    pthread_t recording_thread;
    bool is_recording;
    bool stop_requested;
    // Pre-roll (ring buffer) support. When pre_roll_mode is true the recorder
    // buffers packets instead of writing them, until commit_requested is set.
    bool pre_roll_mode;
    int pre_roll_seconds;
    bool commit_requested;
    AVPacket *buffer;       // ref-counted copies of buffered video packets
    int buffer_count;
    int buffer_capacity;
    char *rtsp_url;
    char *output_path;
    int written_packets;
} RecorderContext;

// Hard upper bound on buffered packets when PTS-based trimming cannot run
// (e.g. timestamps missing). ~20s at 60fps with headroom.
#define PRE_ROLL_MAX_PACKETS 1200

static RecorderContext g_recorder = {0};
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

static int buffer_append(RecorderContext *ctx, const AVPacket *pkt) {
    if (ctx->buffer_count == ctx->buffer_capacity) {
        int new_cap = ctx->buffer_capacity > 0 ? ctx->buffer_capacity * 2 : 64;
        AVPacket *nb = (AVPacket *)realloc(ctx->buffer, (size_t)new_cap * sizeof(AVPacket));
        if (!nb) {
            LOGE("Failed to grow pre-roll buffer");
            return -1;
        }
        ctx->buffer = nb;
        ctx->buffer_capacity = new_cap;
    }
    av_init_packet(&ctx->buffer[ctx->buffer_count]);
    int ret = av_packet_ref(&ctx->buffer[ctx->buffer_count], pkt);
    if (ret < 0) {
        LOGE("Failed to ref packet into pre-roll buffer: %s", av_err2str(ret));
        return ret;
    }
    ctx->buffer_count++;
    return 0;
}

static void buffer_clear(RecorderContext *ctx) {
    for (int i = 0; i < ctx->buffer_count; i++) {
        av_packet_unref(&ctx->buffer[i]);
    }
    ctx->buffer_count = 0;
}

static void buffer_drop_front(RecorderContext *ctx, int count) {
    if (count <= 0) return;
    if (count >= ctx->buffer_count) {
        buffer_clear(ctx);
        return;
    }
    for (int i = 0; i < count; i++) {
        av_packet_unref(&ctx->buffer[i]);
    }
    memmove(ctx->buffer, ctx->buffer + count,
            (size_t)(ctx->buffer_count - count) * sizeof(AVPacket));
    ctx->buffer_count -= count;
}

static void buffer_free(RecorderContext *ctx) {
    buffer_clear(ctx);
    if (ctx->buffer) {
        free(ctx->buffer);
        ctx->buffer = NULL;
    }
    ctx->buffer_capacity = 0;
}

// True when the newest keyframe `pkt` is >= pre_roll_seconds ahead of the
// current anchor (buffer[0]); in that case the whole buffer can be replaced.
static bool buffer_stale(RecorderContext *ctx, const AVPacket *pkt) {
    if (ctx->buffer_count == 0) return false;
    AVStream *in = ctx->input_ctx->streams[ctx->video_stream_index];
    if (in->time_base.num <= 0) return false;
    int64_t cur = pkt->pts != AV_NOPTS_VALUE ? pkt->pts : pkt->dts;
    int64_t anchor = ctx->buffer[0].pts != AV_NOPTS_VALUE ? ctx->buffer[0].pts : ctx->buffer[0].dts;
    if (cur == AV_NOPTS_VALUE || anchor == AV_NOPTS_VALUE) return false;
    int64_t span = cur - anchor;
    if (span < 0) return false;
    int64_t window = ((int64_t)ctx->pre_roll_seconds) * in->time_base.den / in->time_base.num;
    return span >= window;
}

// Safety net for streams with missing timestamps: keep the buffer bounded by
// discarding everything before the most recent keyframe.
static void buffer_hard_cap(RecorderContext *ctx) {
    if (ctx->buffer_count <= PRE_ROLL_MAX_PACKETS) return;
    int last_key = -1;
    for (int i = ctx->buffer_count - 1; i >= 0; i--) {
        if (ctx->buffer[i].flags & AV_PKT_FLAG_KEY) {
            last_key = i;
            break;
        }
    }
    if (last_key > 0) {
        buffer_drop_front(ctx, last_key);
    } else if (last_key < 0) {
        buffer_clear(ctx);
    }
}

static int write_packet(RecorderContext *ctx, AVPacket *pkt,
                        AVStream *in, AVStream *out,
                        int64_t base_pts, int64_t base_dts) {
    int64_t rel_pts = pkt->pts;
    int64_t rel_dts = pkt->dts;
    if (base_pts != AV_NOPTS_VALUE && rel_pts != AV_NOPTS_VALUE) rel_pts -= base_pts;
    if (base_dts != AV_NOPTS_VALUE && rel_dts != AV_NOPTS_VALUE) rel_dts -= base_dts;
    if (rel_pts == AV_NOPTS_VALUE) rel_pts = rel_dts;
    if (rel_dts == AV_NOPTS_VALUE) rel_dts = rel_pts;
    if (rel_pts == AV_NOPTS_VALUE) rel_pts = 0;
    if (rel_dts == AV_NOPTS_VALUE) rel_dts = 0;

    pkt->pts = av_rescale_q_rnd(rel_pts, in->time_base, out->time_base,
                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    pkt->dts = av_rescale_q_rnd(rel_dts, in->time_base, out->time_base,
                                AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
    if (pkt->pts != AV_NOPTS_VALUE && pkt->dts != AV_NOPTS_VALUE && pkt->pts < pkt->dts) {
        pkt->pts = pkt->dts;
    }
    pkt->duration = av_rescale_q(pkt->duration, in->time_base, out->time_base);
    if (pkt->duration <= 0) pkt->duration = 1;
    pkt->stream_index = 0;
    pkt->pos = -1;

    int ret = av_interleaved_write_frame(ctx->output_ctx, pkt);
    if (ret >= 0) {
        ctx->written_packets++;
    }
    return ret;
}

// Write all buffered packets normalized relative to buffer[0] (anchor keyframe).
static int flush_buffer(RecorderContext *ctx, int64_t base_pts, int64_t base_dts) {
    AVStream *in = ctx->input_ctx->streams[ctx->video_stream_index];
    AVStream *out = ctx->output_ctx->streams[0];
    for (int i = 0; i < ctx->buffer_count; i++) {
        AVPacket tmp;
        av_init_packet(&tmp);
        if (av_packet_ref(&tmp, &ctx->buffer[i]) < 0) {
            LOGE("Failed to ref buffered packet %d", i);
            return -1;
        }
        int ret = write_packet(ctx, &tmp, in, out, base_pts, base_dts);
        av_packet_unref(&tmp);
        if (ret < 0) {
            LOGE("Failed to flush buffered packet %d: %s", i, av_err2str(ret));
            return ret;
        }
    }
    buffer_clear(ctx);
    return 0;
}

// Recording thread function
static void* recording_thread_func(void* arg) {
    RecorderContext *ctx = (RecorderContext*)arg;
    AVPacket pkt;
    int64_t base_pts = AV_NOPTS_VALUE;
    int64_t base_dts = AV_NOPTS_VALUE;
    bool got_keyframe = false;
    bool writing = !ctx->pre_roll_mode;
    int ret;

    LOGI("Recording thread started (pre_roll=%d, seconds=%d)",
         ctx->pre_roll_mode, ctx->pre_roll_seconds);

    while (!ctx->stop_requested) {
        ret = av_read_frame(ctx->input_ctx, &pkt);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                LOGI("End of stream reached");
            } else {
                LOGE("Error reading frame: %s", av_err2str(ret));
            }
            break;
        }

        if (pkt.stream_index != ctx->video_stream_index) {
            av_packet_unref(&pkt);
            continue;
        }

        AVStream *in_stream = ctx->input_ctx->streams[ctx->video_stream_index];
        AVStream *out_stream = ctx->output_ctx->streams[0];
        bool is_key = (pkt.flags & AV_PKT_FLAG_KEY) != 0;

        if (!writing) {
            // ---- pre-roll buffering phase ----
            if (ctx->commit_requested) {
                if (ctx->buffer_count > 0) {
                    base_pts = ctx->buffer[0].pts != AV_NOPTS_VALUE ? ctx->buffer[0].pts : ctx->buffer[0].dts;
                    base_dts = ctx->buffer[0].dts != AV_NOPTS_VALUE ? ctx->buffer[0].dts : base_pts;
                    int flushed = ctx->buffer_count;
                    if (flush_buffer(ctx, base_pts, base_dts) < 0) {
                        av_packet_unref(&pkt);
                        break;
                    }
                    got_keyframe = true;
                    LOGI("Pre-roll committed; flushed %d buffered packets", flushed);
                }
                writing = true;
                // fall through to write the current packet
            } else if (!got_keyframe) {
                if (is_key) {
                    if (buffer_append(ctx, &pkt) == 0) {
                        got_keyframe = true;
                    }
                }
                av_packet_unref(&pkt);
                continue;
            } else {
                if (is_key && buffer_stale(ctx, &pkt)) {
                    buffer_clear(ctx);
                }
                if (buffer_append(ctx, &pkt) == 0) {
                    buffer_hard_cap(ctx);
                }
                av_packet_unref(&pkt);
                continue;
            }
        }

        // ---- writing phase ----
        if (!got_keyframe) {
            if (is_key) {
                got_keyframe = true;
                base_pts = pkt.pts;
                base_dts = pkt.dts;
            } else {
                av_packet_unref(&pkt);
                continue;
            }
        }

        if (write_packet(ctx, &pkt, in_stream, out_stream, base_pts, base_dts) < 0) {
            LOGE("Error writing frame: %s", av_err2str(ret));
            av_packet_unref(&pkt);
            break;
        }
        av_packet_unref(&pkt);
    }

    buffer_free(ctx);
    LOGI("Recording thread finished");
    return NULL;
}

JNIEXPORT jboolean JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegRecorder_nativeStartRecording(
    JNIEnv *env, jobject thiz, jstring rtsp_url, jstring output_path) {
    
    pthread_mutex_lock(&g_mutex);

    if (g_recorder.is_recording) {
        LOGE("Recording already in progress");
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    const char *rtsp_url_str = (*env)->GetStringUTFChars(env, rtsp_url, NULL);
    const char *output_path_str = (*env)->GetStringUTFChars(env, output_path, NULL);

    LOGI("Starting FFmpeg recording: %s -> %s", rtsp_url_str, output_path_str);
    g_recorder.written_packets = 0;


    // Store paths
    g_recorder.rtsp_url = strdup(rtsp_url_str);
    g_recorder.output_path = strdup(output_path_str);

    (*env)->ReleaseStringUTFChars(env, rtsp_url, rtsp_url_str);
    (*env)->ReleaseStringUTFChars(env, output_path, output_path_str);

    int ret;

    // Open input (RTSP stream)
    AVDictionary *options = NULL;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "max_delay", "500000", 0);
    av_dict_set(&options, "probesize", "32", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);
    
    ret = avformat_open_input(&g_recorder.input_ctx, g_recorder.rtsp_url, NULL, &options);
    av_dict_free(&options);
    
    if (ret < 0) {
        LOGE("Failed to open input: %s", av_err2str(ret));
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Find stream info
    ret = avformat_find_stream_info(g_recorder.input_ctx, NULL);
    if (ret < 0) {
        LOGE("Failed to find stream info: %s", av_err2str(ret));
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Find video stream
    g_recorder.video_stream_index = -1;
    for (unsigned int i = 0; i < g_recorder.input_ctx->nb_streams; i++) {
        if (g_recorder.input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            g_recorder.video_stream_index = i;
            break;
        }
    }

    if (g_recorder.video_stream_index == -1) {
        LOGE("No video stream found");
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    LOGI("Found video stream at index %d", g_recorder.video_stream_index);

    // Create output context (MP4 file)
    ret = avformat_alloc_output_context2(&g_recorder.output_ctx, NULL, "mp4", g_recorder.output_path);
    if (ret < 0) {
        LOGE("Failed to create output context: %s", av_err2str(ret));
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Create output video stream
    AVStream *in_stream = g_recorder.input_ctx->streams[g_recorder.video_stream_index];
    AVStream *out_stream = avformat_new_stream(g_recorder.output_ctx, NULL);
    if (!out_stream) {
        LOGE("Failed to create output stream");
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Copy codec parameters from input to output
    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0) {
        LOGE("Failed to copy codec parameters: %s", av_err2str(ret));
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;

    // Open output file
    if (!(g_recorder.output_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&g_recorder.output_ctx->pb, g_recorder.output_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Failed to open output file: %s", av_err2str(ret));
            avformat_free_context(g_recorder.output_ctx);
            avformat_close_input(&g_recorder.input_ctx);
            free(g_recorder.rtsp_url);
            free(g_recorder.output_path);
            pthread_mutex_unlock(&g_mutex);
            return JNI_FALSE;
        }
    }

    // Write header with faststart for better compatibility.
    AVDictionary *muxer_opts = NULL;
    av_dict_set(&muxer_opts, "movflags", "faststart", 0);
    ret = avformat_write_header(g_recorder.output_ctx, &muxer_opts);
    av_dict_free(&muxer_opts);
    if (ret < 0) {
        LOGE("Failed to write header: %s", av_err2str(ret));
        if (!(g_recorder.output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&g_recorder.output_ctx->pb);
        }
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    LOGI("Header written successfully");

    // Start recording thread
    g_recorder.is_recording = true;
    g_recorder.stop_requested = false;
    
    ret = pthread_create(&g_recorder.recording_thread, NULL, recording_thread_func, &g_recorder);
    if (ret != 0) {
        LOGE("Failed to create recording thread");
        av_write_trailer(g_recorder.output_ctx);
        if (!(g_recorder.output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&g_recorder.output_ctx->pb);
        }
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.is_recording = false;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    LOGI("Recording started successfully");
    pthread_mutex_unlock(&g_mutex);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegRecorder_nativeStartPreRoll(
    JNIEnv *env, jobject thiz, jstring rtsp_url, jstring output_path, jint pre_roll_seconds) {

    pthread_mutex_lock(&g_mutex);

    if (g_recorder.is_recording) {
        LOGE("Recording already in progress");
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    const char *rtsp_url_str = (*env)->GetStringUTFChars(env, rtsp_url, NULL);
    const char *output_path_str = (*env)->GetStringUTFChars(env, output_path, NULL);

    LOGI("Starting FFmpeg pre-roll: %s -> %s (%d s)",
         rtsp_url_str, output_path_str, pre_roll_seconds);
    g_recorder.written_packets = 0;
    g_recorder.pre_roll_mode = true;
    g_recorder.pre_roll_seconds = pre_roll_seconds > 0 ? pre_roll_seconds : 5;
    g_recorder.commit_requested = false;
    g_recorder.buffer = NULL;
    g_recorder.buffer_count = 0;
    g_recorder.buffer_capacity = 0;
    g_recorder.is_recording = false;
    g_recorder.stop_requested = false;
    g_recorder.input_ctx = NULL;
    g_recorder.output_ctx = NULL;
    g_recorder.video_stream_index = -1;

    // Store paths
    g_recorder.rtsp_url = strdup(rtsp_url_str);
    g_recorder.output_path = strdup(output_path_str);

    (*env)->ReleaseStringUTFChars(env, rtsp_url, rtsp_url_str);
    (*env)->ReleaseStringUTFChars(env, output_path, output_path_str);

    int ret;

    // Open input (RTSP stream)
    AVDictionary *options = NULL;
    av_dict_set(&options, "rtsp_transport", "tcp", 0);
    av_dict_set(&options, "max_delay", "500000", 0);
    av_dict_set(&options, "probesize", "32", 0);
    av_dict_set(&options, "analyzeduration", "1000000", 0);

    ret = avformat_open_input(&g_recorder.input_ctx, g_recorder.rtsp_url, NULL, &options);
    av_dict_free(&options);

    if (ret < 0) {
        LOGE("Failed to open input: %s", av_err2str(ret));
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Find stream info
    ret = avformat_find_stream_info(g_recorder.input_ctx, NULL);
    if (ret < 0) {
        LOGE("Failed to find stream info: %s", av_err2str(ret));
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Find video stream
    g_recorder.video_stream_index = -1;
    for (unsigned int i = 0; i < g_recorder.input_ctx->nb_streams; i++) {
        if (g_recorder.input_ctx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            g_recorder.video_stream_index = i;
            break;
        }
    }

    if (g_recorder.video_stream_index == -1) {
        LOGE("No video stream found");
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    LOGI("Found video stream at index %d", g_recorder.video_stream_index);

    // Create output context (MP4 file)
    ret = avformat_alloc_output_context2(&g_recorder.output_ctx, NULL, "mp4", g_recorder.output_path);
    if (ret < 0) {
        LOGE("Failed to create output context: %s", av_err2str(ret));
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Create output video stream
    AVStream *in_stream = g_recorder.input_ctx->streams[g_recorder.video_stream_index];
    AVStream *out_stream = avformat_new_stream(g_recorder.output_ctx, NULL);
    if (!out_stream) {
        LOGE("Failed to create output stream");
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    // Copy codec parameters from input to output
    ret = avcodec_parameters_copy(out_stream->codecpar, in_stream->codecpar);
    if (ret < 0) {
        LOGE("Failed to copy codec parameters: %s", av_err2str(ret));
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    out_stream->codecpar->codec_tag = 0;
    out_stream->time_base = in_stream->time_base;

    // Open output file
    if (!(g_recorder.output_ctx->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&g_recorder.output_ctx->pb, g_recorder.output_path, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGE("Failed to open output file: %s", av_err2str(ret));
            avformat_free_context(g_recorder.output_ctx);
            avformat_close_input(&g_recorder.input_ctx);
            free(g_recorder.rtsp_url);
            free(g_recorder.output_path);
            g_recorder.rtsp_url = NULL;
            g_recorder.output_path = NULL;
            pthread_mutex_unlock(&g_mutex);
            return JNI_FALSE;
        }
    }

    // Write header with faststart for better compatibility.
    AVDictionary *muxer_opts = NULL;
    av_dict_set(&muxer_opts, "movflags", "faststart", 0);
    ret = avformat_write_header(g_recorder.output_ctx, &muxer_opts);
    av_dict_free(&muxer_opts);
    if (ret < 0) {
        LOGE("Failed to write header: %s", av_err2str(ret));
        if (!(g_recorder.output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&g_recorder.output_ctx->pb);
        }
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    LOGI("Header written successfully");

    // Start recording thread in pre-roll (buffering) mode.
    g_recorder.is_recording = true;
    g_recorder.stop_requested = false;

    ret = pthread_create(&g_recorder.recording_thread, NULL, recording_thread_func, &g_recorder);
    if (ret != 0) {
        LOGE("Failed to create recording thread");
        av_write_trailer(g_recorder.output_ctx);
        if (!(g_recorder.output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&g_recorder.output_ctx->pb);
        }
        avformat_free_context(g_recorder.output_ctx);
        avformat_close_input(&g_recorder.input_ctx);
        free(g_recorder.rtsp_url);
        free(g_recorder.output_path);
        g_recorder.rtsp_url = NULL;
        g_recorder.output_path = NULL;
        g_recorder.is_recording = false;
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    LOGI("Pre-roll started successfully");
    pthread_mutex_unlock(&g_mutex);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegRecorder_nativeCommitPreRoll(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&g_mutex);

    if (!g_recorder.is_recording) {
        LOGE("No recording in progress");
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    if (!g_recorder.pre_roll_mode) {
        // Nothing to commit for a normal recording.
        pthread_mutex_unlock(&g_mutex);
        return JNI_TRUE;
    }

    g_recorder.commit_requested = true;
    pthread_mutex_unlock(&g_mutex);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegRecorder_nativeStopRecording(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&g_mutex);

    if (!g_recorder.is_recording) {
        LOGE("No recording in progress");
        pthread_mutex_unlock(&g_mutex);
        return JNI_FALSE;
    }

    LOGI("Stopping FFmpeg recording");

    // Signal thread to stop
    g_recorder.stop_requested = true;
    pthread_mutex_unlock(&g_mutex);

    // Wait for thread to finish
    pthread_join(g_recorder.recording_thread, NULL);

    pthread_mutex_lock(&g_mutex);

    // Write trailer
    bool has_video = g_recorder.written_packets > 0;
    if (g_recorder.output_ctx) {
        if (g_recorder.output_ctx->pb) {
            avio_flush(g_recorder.output_ctx->pb);
        }
        av_write_trailer(g_recorder.output_ctx);
        
        // Close output file
        if (!(g_recorder.output_ctx->oformat->flags & AVFMT_NOFILE)) {
            avio_closep(&g_recorder.output_ctx->pb);
        }
        
        avformat_free_context(g_recorder.output_ctx);
        g_recorder.output_ctx = NULL;
    }

    // Close input
    if (g_recorder.input_ctx) {
        avformat_close_input(&g_recorder.input_ctx);
        g_recorder.input_ctx = NULL;
    }

    // Free paths
    if (g_recorder.rtsp_url) {
        free(g_recorder.rtsp_url);
        g_recorder.rtsp_url = NULL;
    }
    if (g_recorder.output_path) {
        free(g_recorder.output_path);
        g_recorder.output_path = NULL;
    }

    buffer_free(&g_recorder);
    g_recorder.is_recording = false;
    g_recorder.video_stream_index = -1;
    g_recorder.written_packets = 0;
    g_recorder.pre_roll_mode = false;
    g_recorder.pre_roll_seconds = 0;
    g_recorder.commit_requested = false;

    if (!has_video) {
        LOGE("Recording stopped but no video packets were written");
    } else {
        LOGI("Recording stopped successfully");
    }
    pthread_mutex_unlock(&g_mutex);
    return has_video ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegRecorder_nativeIsRecording(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&g_mutex);
    jboolean result = g_recorder.is_recording ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&g_mutex);
    return result;
}
