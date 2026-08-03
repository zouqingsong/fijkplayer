#include <jni.h>
#include <android/log.h>
#include <pthread.h>
#include <stdbool.h>

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
    char *rtsp_url;
    char *output_path;
} RecorderContext;

static RecorderContext g_recorder = {0};
static pthread_mutex_t g_mutex = PTHREAD_MUTEX_INITIALIZER;

// Recording thread function
static void* recording_thread_func(void* arg) {
    RecorderContext *ctx = (RecorderContext*)arg;
    AVPacket pkt;
    int64_t start_pts = -1;
    int ret;

    LOGI("Recording thread started");

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

        // Only process video packets
        if (pkt.stream_index == ctx->video_stream_index) {
            AVStream *in_stream = ctx->input_ctx->streams[ctx->video_stream_index];
            AVStream *out_stream = ctx->output_ctx->streams[0];

            // Initialize start PTS
            if (start_pts == -1) {
                start_pts = pkt.pts;
            }

            // Adjust timestamps
            pkt.pts = av_rescale_q_rnd(pkt.pts - start_pts, in_stream->time_base, 
                                       out_stream->time_base, 
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            pkt.dts = av_rescale_q_rnd(pkt.dts - start_pts, in_stream->time_base, 
                                       out_stream->time_base, 
                                       AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX);
            pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
            pkt.stream_index = 0;
            pkt.pos = -1;

            // Write packet to output
            ret = av_interleaved_write_frame(ctx->output_ctx, &pkt);
            if (ret < 0) {
                LOGE("Error writing frame: %s", av_err2str(ret));
                av_packet_unref(&pkt);
                break;
            }
        }

        av_packet_unref(&pkt);
    }

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

    // Write header
    ret = avformat_write_header(g_recorder.output_ctx, NULL);
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
    if (g_recorder.output_ctx) {
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

    g_recorder.is_recording = false;
    g_recorder.video_stream_index = -1;

    LOGI("Recording stopped successfully");
    pthread_mutex_unlock(&g_mutex);
    return JNI_TRUE;
}

JNIEXPORT jboolean JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegRecorder_nativeIsRecording(JNIEnv *env, jobject thiz) {
    pthread_mutex_lock(&g_mutex);
    jboolean result = g_recorder.is_recording ? JNI_TRUE : JNI_FALSE;
    pthread_mutex_unlock(&g_mutex);
    return result;
}
