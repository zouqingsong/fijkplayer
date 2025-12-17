#include "ffmpeg_soft_decoder.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#include "ffmpeg/include/libavcodec/avcodec.h"
#include "ffmpeg/include/libavutil/imgutils.h"
#include "ffmpeg/include/libavutil/opt.h"

#define LOG_TAG "FFmpegSoftDecoder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

struct FFmpegSoftDecoder {
    const AVCodec* codec;
    AVCodecContext* codec_ctx;
    AVFrame* frame;
    AVPacket* packet;
    
    int width;
    int height;
    
    char error_msg[512];
};

/* Create decoder */
FFmpegSoftDecoder* ffmpeg_soft_decoder_create(const SoftDecoderConfig* config) {
    if (!config) {
        LOGE("Invalid config");
        return NULL;
    }
    
    FFmpegSoftDecoder* decoder = (FFmpegSoftDecoder*)calloc(1, sizeof(FFmpegSoftDecoder));
    if (!decoder) {
        LOGE("Failed to allocate decoder");
        return NULL;
    }
    
    decoder->width = config->width;
    decoder->height = config->height;
    
    // Find H.264 decoder
    decoder->codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!decoder->codec) {
        LOGE("H.264 decoder not found");
        snprintf(decoder->error_msg, sizeof(decoder->error_msg), "H.264 decoder not found");
        ffmpeg_soft_decoder_free(decoder);
        return NULL;
    }
    
    LOGI("Using FFmpeg software decoder: %s", decoder->codec->name);
    
    // Create codec context
    decoder->codec_ctx = avcodec_alloc_context3(decoder->codec);
    if (!decoder->codec_ctx) {
        LOGE("Failed to allocate codec context");
        snprintf(decoder->error_msg, sizeof(decoder->error_msg), "Failed to allocate codec context");
        ffmpeg_soft_decoder_free(decoder);
        return NULL;
    }
    
    // Set parameters
    decoder->codec_ctx->codec_type = AVMEDIA_TYPE_VIDEO;
    decoder->codec_ctx->codec_id = AV_CODEC_ID_H264;
    decoder->codec_ctx->width = config->width;
    decoder->codec_ctx->height = config->height;
    decoder->codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
    
    // Set extradata (SPS/PPS)
    if (config->extradata && config->extradata_size > 0) {
        decoder->codec_ctx->extradata = (uint8_t*)av_malloc(config->extradata_size + AV_INPUT_BUFFER_PADDING_SIZE);
        if (decoder->codec_ctx->extradata) {
            memcpy(decoder->codec_ctx->extradata, config->extradata, config->extradata_size);
            memset(decoder->codec_ctx->extradata + config->extradata_size, 0, AV_INPUT_BUFFER_PADDING_SIZE);
            decoder->codec_ctx->extradata_size = config->extradata_size;
            LOGI("Set extradata: %d bytes", config->extradata_size);
        }
    }
    
    // Allocate frame
    decoder->frame = av_frame_alloc();
    if (!decoder->frame) {
        LOGE("Failed to allocate frame");
        snprintf(decoder->error_msg, sizeof(decoder->error_msg), "Failed to allocate frame");
        ffmpeg_soft_decoder_free(decoder);
        return NULL;
    }
    
    // Allocate packet
    decoder->packet = av_packet_alloc();
    if (!decoder->packet) {
        LOGE("Failed to allocate packet");
        snprintf(decoder->error_msg, sizeof(decoder->error_msg), "Failed to allocate packet");
        ffmpeg_soft_decoder_free(decoder);
        return NULL;
    }
    
    LOGI("Software decoder created successfully");
    return decoder;
}

/* Start decoder */
int ffmpeg_soft_decoder_start(FFmpegSoftDecoder* decoder) {
    if (!decoder || !decoder->codec_ctx) {
        return -1;
    }
    
    // Open codec
    int ret = avcodec_open2(decoder->codec_ctx, decoder->codec, NULL);
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Failed to open codec: %s", err_buf);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg), "Failed to open codec: %s", err_buf);
        return ret;
    }
    
    LOGI("Software decoder started successfully");
    return 0;
}

/* Send packet to decoder */
int ffmpeg_soft_decoder_send_packet(FFmpegSoftDecoder* decoder, FFPacket* packet) {
    if (!decoder || !decoder->codec_ctx) {
        return -1;
    }
    
    if (!packet || !packet->data) {
        // Send NULL packet for flushing
        return avcodec_send_packet(decoder->codec_ctx, NULL);
    }
    
    // Copy packet data
    av_packet_unref(decoder->packet);
    decoder->packet->data = packet->data;
    decoder->packet->size = packet->size;
    decoder->packet->pts = packet->pts;
    decoder->packet->dts = packet->dts;
    
    if (packet->is_key_frame) {
        decoder->packet->flags |= AV_PKT_FLAG_KEY;
    }
    
    // Send to decoder
    int ret = avcodec_send_packet(decoder->codec_ctx, decoder->packet);
    
    // Clear pointers (we don't own the data)
    decoder->packet->data = NULL;
    decoder->packet->size = 0;
    
    if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Error sending packet to decoder: %s", err_buf);
        return ret;
    }
    
    return 0;
}

/* Receive decoded frame */
int ffmpeg_soft_decoder_receive_frame(FFmpegSoftDecoder* decoder, 
                                      SoftDecodedFrame** out_frame) {
    if (!decoder || !decoder->codec_ctx || !out_frame) {
        return -1;
    }
    
    *out_frame = NULL;
    
    // Try to receive frame
    int ret = avcodec_receive_frame(decoder->codec_ctx, decoder->frame);
    
    if (ret == AVERROR(EAGAIN)) {
        // Need more input
        return -EAGAIN;
    }
    
    if (ret == AVERROR_EOF) {
        // End of stream
        return -EOF;
    }
    
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Error receiving frame: %s", err_buf);
        return ret;
    }
    
    // Allocate output frame
    SoftDecodedFrame* soft_frame = (SoftDecodedFrame*)calloc(1, sizeof(SoftDecodedFrame));
    if (!soft_frame) {
        return -ENOMEM;
    }
    
    // Copy frame data
    soft_frame->width = decoder->frame->width;
    soft_frame->height = decoder->frame->height;
    soft_frame->pts = decoder->frame->pts;
    soft_frame->format = decoder->frame->format;
    
    // Copy plane pointers and linesizes
    for (int i = 0; i < 4; i++) {
        soft_frame->data[i] = decoder->frame->data[i];
        soft_frame->linesize[i] = decoder->frame->linesize[i];
    }
    
    LOGI("✅ SOFTWARE DECODED FRAME: %dx%d, pts=%lld, format=%d", 
         soft_frame->width, soft_frame->height, (long long)soft_frame->pts, soft_frame->format);
    
    *out_frame = soft_frame;
    return 0;
}

/* Free frame */
void ffmpeg_soft_decoder_free_frame(SoftDecodedFrame* frame) {
    if (frame) {
        // Note: We don't free data pointers as they belong to the decoder's internal frame
        free(frame);
    }
}

/* Stop and free decoder */
void ffmpeg_soft_decoder_free(FFmpegSoftDecoder* decoder) {
    if (!decoder) {
        return;
    }
    
    if (decoder->packet) {
        av_packet_free(&decoder->packet);
    }
    
    if (decoder->frame) {
        av_frame_free(&decoder->frame);
    }
    
    if (decoder->codec_ctx) {
        avcodec_free_context(&decoder->codec_ctx);
    }
    
    free(decoder);
    LOGI("Software decoder freed");
}

/* Get error message */
const char* ffmpeg_soft_decoder_get_error(FFmpegSoftDecoder* decoder) {
    if (!decoder) {
        return "Invalid decoder";
    }
    return decoder->error_msg;
}
