/*
 * MediaCodec Decoder Implementation for fijkplayer
 */

#include "mediacodec_decoder.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <media/NdkMediaError.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <android/log.h>

#define LOG_TAG "MediaCodecDecoder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

#define DEFAULT_TIMEOUT_US 10000  // 10ms default timeout

/* Internal decoder structure */
struct MediaCodecDecoder {
    AMediaCodec* codec;
    AMediaFormat* format;
    DecoderConfig config;
    DecoderState state;
    char error_msg[512];
    int64_t frame_count;
    int64_t decode_errors;
    int64_t start_time_ns;  // System nanoTime when playback started
    int64_t first_frame_pts;  // PTS of first frame for offset calculation
};

/* Get MIME type from codec */
static const char* get_mime_type(FFVideoCodec codec) {
    switch (codec) {
        case FF_VIDEO_CODEC_H264:
            return "video/avc";
        case FF_VIDEO_CODEC_H265:
            return "video/hevc";
        case FF_VIDEO_CODEC_VP8:
            return "video/x-vnd.on2.vp8";
        case FF_VIDEO_CODEC_VP9:
            return "video/x-vnd.on2.vp9";
        default:
            return NULL;
    }
}

/* Create decoder */
MediaCodecDecoder* mediacodec_decoder_create() {
    MediaCodecDecoder* decoder = (MediaCodecDecoder*)calloc(1, sizeof(MediaCodecDecoder));
    if (!decoder) {
        LOGE("Failed to allocate decoder");
        return NULL;
    }
    
    decoder->state = DECODER_STATE_UNINITIALIZED;
    decoder->codec = NULL;
    decoder->format = NULL;
    
    return decoder;
}

/* Configure decoder */
int mediacodec_decoder_configure(MediaCodecDecoder* decoder, DecoderConfig* config) {
    if (!decoder || !config) {
        return -EINVAL;
    }
    
    if (decoder->state != DECODER_STATE_UNINITIALIZED) {
        LOGE("Decoder already configured");
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Decoder already configured");
        return -EINVAL;
    }
    
    // Get MIME type
    const char* mime = get_mime_type(config->codec);
    if (!mime) {
        LOGE("Unsupported codec: %d", config->codec);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Unsupported codec: %d", config->codec);
        return -EINVAL;
    }
    
    LOGI("Configuring decoder: %s, %dx%d, fps=%d/%d",
         mime, config->width, config->height, config->fps_num, config->fps_den);
    
    // Create MediaCodec
    decoder->codec = AMediaCodec_createDecoderByType(mime);
    if (!decoder->codec) {
        LOGE("Failed to create MediaCodec for %s", mime);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Failed to create MediaCodec");
        return -ENODEV;
    }
    
    // Create format
    decoder->format = AMediaFormat_new();
    if (!decoder->format) {
        LOGE("Failed to create MediaFormat");
        AMediaCodec_delete(decoder->codec);
        decoder->codec = NULL;
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Failed to create MediaFormat");
        return -ENOMEM;
    }
    
    // Set format parameters
    AMediaFormat_setString(decoder->format, AMEDIAFORMAT_KEY_MIME, mime);
    AMediaFormat_setInt32(decoder->format, AMEDIAFORMAT_KEY_WIDTH, config->width);
    AMediaFormat_setInt32(decoder->format, AMEDIAFORMAT_KEY_HEIGHT, config->height);
    
    // Set max input size
    int max_input_size = config->max_input_size > 0 ? 
                         config->max_input_size : 
                         config->width * config->height;
    AMediaFormat_setInt32(decoder->format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, max_input_size);
    
    // Set framerate if available
    if (config->fps_num > 0 && config->fps_den > 0) {
        float fps = (float)config->fps_num / config->fps_den;
        AMediaFormat_setFloat(decoder->format, AMEDIAFORMAT_KEY_FRAME_RATE, fps);
    }
    
    // Set codec-specific data (SPS/PPS for H.264)
    if (config->extradata && config->extradata_size > 0) {
        LOGI("Setting codec extradata: %d bytes", config->extradata_size);
        AMediaFormat_setBuffer(decoder->format, "csd-0", 
                              config->extradata, config->extradata_size);
    }
    
    // Configure codec (with surface for direct rendering if provided)
    media_status_t status = AMediaCodec_configure(decoder->codec, decoder->format,
                                                  (ANativeWindow*)config->surface, NULL, 0);
    if (status != AMEDIA_OK) {
        LOGE("Failed to configure MediaCodec: %d", status);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Failed to configure MediaCodec: %d", status);
        AMediaFormat_delete(decoder->format);
        AMediaCodec_delete(decoder->codec);
        decoder->format = NULL;
        decoder->codec = NULL;
        return -EINVAL;
    }
    
    // Save configuration
    memcpy(&decoder->config, config, sizeof(DecoderConfig));
    
    // Copy extradata
    if (config->extradata && config->extradata_size > 0) {
        decoder->config.extradata = (uint8_t*)malloc(config->extradata_size);
        if (decoder->config.extradata) {
            memcpy(decoder->config.extradata, config->extradata, config->extradata_size);
        }
    }
    
    decoder->state = DECODER_STATE_CONFIGURED;
    LOGI("Decoder configured successfully");
    
    return 0;
}

/* Start decoder */
int mediacodec_decoder_start(MediaCodecDecoder* decoder) {
    if (!decoder || !decoder->codec) {
        return -EINVAL;
    }
    
    if (decoder->state != DECODER_STATE_CONFIGURED) {
        LOGE("Decoder not configured");
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Decoder not configured");
        return -EINVAL;
    }
    
    media_status_t status = AMediaCodec_start(decoder->codec);
    if (status != AMEDIA_OK) {
        LOGE("Failed to start MediaCodec: %d", status);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Failed to start MediaCodec: %d", status);
        decoder->state = DECODER_STATE_ERROR;
        return -EIO;
    }
    
    decoder->state = DECODER_STATE_RUNNING;
    decoder->frame_count = 0;
    decoder->decode_errors = 0;
    
    // Timing will be initialized on first frame
    decoder->start_time_ns = 0;
    decoder->first_frame_pts = -1;
    
    LOGI("Decoder started");
    return 0;
}

/* Send packet */
int mediacodec_decoder_send_packet(MediaCodecDecoder* decoder, FFPacket* packet, 
                                   int64_t timeout_us) {
    if (!decoder || !decoder->codec) {
        return -EINVAL;
    }
    
    if (decoder->state != DECODER_STATE_RUNNING) {
        LOGE("Decoder not running");
        return -EINVAL;
    }
    
    if (!packet || !packet->data || packet->size <= 0) {
        LOGE("Invalid packet: packet=%p, data=%p, size=%d", 
             packet, packet ? packet->data : NULL, packet ? packet->size : 0);
        return -EINVAL;
    }
    
    // Use default timeout if not specified
    if (timeout_us < 0) {
        timeout_us = DEFAULT_TIMEOUT_US;
    }
    
    // Get input buffer
    ssize_t buf_idx = AMediaCodec_dequeueInputBuffer(decoder->codec, timeout_us);
    if (buf_idx < 0) {
        if (buf_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            return -EAGAIN;
        }
        LOGE("Failed to dequeue input buffer: %zd", buf_idx);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Failed to dequeue input buffer: %zd", buf_idx);
        return -EIO;
    }
    
    // Get buffer pointer
    size_t buf_size;
    uint8_t* buf = AMediaCodec_getInputBuffer(decoder->codec, buf_idx, &buf_size);
    if (!buf) {
        LOGE("Failed to get input buffer");
        return -EIO;
    }
    
    // Check buffer size
    if ((size_t)packet->size > buf_size) {
        LOGE("Packet too large: %d > %zu", packet->size, buf_size);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Packet too large: %d > %zu", packet->size, buf_size);
        return -EINVAL;
    }
    
    // Copy packet data
    memcpy(buf, packet->data, packet->size);
    
    // Queue input buffer
    uint32_t flags = packet->is_key_frame ? AMEDIACODEC_BUFFER_FLAG_KEY_FRAME : 0;
    media_status_t status = AMediaCodec_queueInputBuffer(decoder->codec, buf_idx,
                                                         0, packet->size,
                                                         packet->pts, flags);
    if (status != AMEDIA_OK) {
        LOGE("Failed to queue input buffer: %d", status);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Failed to queue input buffer: %d", status);
        return -EIO;
    }
    
    return 0;
}

/* Receive frame */
int mediacodec_decoder_receive_frame(MediaCodecDecoder* decoder, DecodedFrame** out_frame,
                                     int64_t timeout_us) {
    if (!decoder || !decoder->codec || !out_frame) {
        return -EINVAL;
    }
    
    if (decoder->state != DECODER_STATE_RUNNING && 
        decoder->state != DECODER_STATE_FLUSHING) {
        LOGE("Decoder not running");
        return -EINVAL;
    }
    
    // Use default timeout if not specified
    if (timeout_us < 0) {
        timeout_us = DEFAULT_TIMEOUT_US;
    }
    
    // Get output buffer
    AMediaCodecBufferInfo info;
    ssize_t buf_idx = AMediaCodec_dequeueOutputBuffer(decoder->codec, &info, timeout_us);
    
    if (buf_idx >= 0) {
        // Output buffer dequeued (logging disabled for performance)
        
        // Got a frame
        DecodedFrame* frame = (DecodedFrame*)calloc(1, sizeof(DecodedFrame));
        if (!frame) {
            AMediaCodec_releaseOutputBuffer(decoder->codec, buf_idx, false);
            return -ENOMEM;
        }
        
        // Get format for frame dimensions
        AMediaFormat* format = AMediaCodec_getOutputFormat(decoder->codec);
        int32_t width = 0, height = 0;
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width);
        AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height);
        AMediaFormat_delete(format);
        
        // Fill frame info
        frame->width = width > 0 ? width : decoder->config.width;
        frame->height = height > 0 ? height : decoder->config.height;
        frame->pts = info.presentationTimeUs;
        frame->duration = 0;  // Not provided by MediaCodec
        frame->is_key_frame = (info.flags & AMEDIACODEC_BUFFER_FLAG_KEY_FRAME) != 0;
        frame->format = VIDEO_FORMAT_YUV420;  // MediaCodec typically outputs YUV420
        
        // If using surface rendering, release buffer immediately for Flutter texture
        // Otherwise, copy buffer data for software processing
        if (decoder->config.use_surface) {
            // For Flutter SurfaceTexture rendering, immediate release works best
            // The key is decoder thread pacing (30ms dequeue timeout) controls rate
            // Flutter will call updateTexImage() on its render thread at vsync
            
            // Disabled verbose logging - uncomment for debugging
            // Log first 10 frames to verify rendering
            // if (decoder->frame_count < 10) {
            //     LOGI("🖼️  Releasing frame #%d to surface (render=true): %dx%d, PTS=%lld", 
            //          decoder->frame_count + 1, frame->width, frame->height, frame->pts);
            // }
            
            AMediaCodec_releaseOutputBuffer(decoder->codec, buf_idx, true);
        } else {
            // Copy buffer data for software rendering
            size_t buf_size;
            uint8_t* buf = AMediaCodec_getOutputBuffer(decoder->codec, buf_idx, &buf_size);
            
            if (buf && info.size > 0) {
                frame->data[0] = (uint8_t*)malloc(info.size);
                if (frame->data[0]) {
                    memcpy(frame->data[0], buf + info.offset, info.size);
                    frame->linesize[0] = frame->width;
                    frame->linesize[1] = frame->width / 2;
                    frame->linesize[2] = frame->width / 2;
                    
                    // Set plane pointers (assuming YUV420 planar)
                    int y_size = frame->width * frame->height;
                    int uv_size = y_size / 4;
                    frame->data[1] = frame->data[0] + y_size;
                    frame->data[2] = frame->data[1] + uv_size;
                }
            }
            
            // Release buffer without rendering
            AMediaCodec_releaseOutputBuffer(decoder->codec, buf_idx, false);
        }
        
        decoder->frame_count++;
        *out_frame = frame;
        
        // Check for EOS
        if (info.flags & AMEDIACODEC_BUFFER_FLAG_END_OF_STREAM) {
            LOGI("Received EOS");
            return -EOF;
        }
        
        return 0;
        
    } else if (buf_idx == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
        return -EAGAIN;
        
    } else if (buf_idx == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
        AMediaFormat* format = AMediaCodec_getOutputFormat(decoder->codec);
        if (format) {
            int32_t width = 0, height = 0, color_format = 0;
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &width);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &height);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_COLOR_FORMAT, &color_format);
            LOGI("Output format changed: %dx%d, color_format=%d", width, height, color_format);
            AMediaFormat_delete(format);
        }
        return -EAGAIN;
        
    } else if (buf_idx == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
        LOGD("Output buffers changed");
        return -EAGAIN;
        
    } else {
        LOGE("Unexpected dequeueOutputBuffer result: %zd", buf_idx);
        decoder->decode_errors++;
        return -EIO;
    }
}

/* Free frame */
void mediacodec_decoder_free_frame(DecodedFrame* frame) {
    if (frame) {
        if (frame->data[0]) {
            free(frame->data[0]);  // Frees the entire buffer (all planes)
        }
        free(frame);
    }
}

/* Flush decoder */
int mediacodec_decoder_flush(MediaCodecDecoder* decoder) {
    if (!decoder || !decoder->codec) {
        return -EINVAL;
    }
    
    LOGI("Flushing decoder");
    decoder->state = DECODER_STATE_FLUSHING;
    
    media_status_t status = AMediaCodec_flush(decoder->codec);
    if (status != AMEDIA_OK) {
        LOGE("Failed to flush MediaCodec: %d", status);
        snprintf(decoder->error_msg, sizeof(decoder->error_msg),
                 "Failed to flush MediaCodec: %d", status);
        decoder->state = DECODER_STATE_ERROR;
        return -EIO;
    }
    
    decoder->state = DECODER_STATE_RUNNING;
    return 0;
}

/* Stop decoder */
void mediacodec_decoder_stop(MediaCodecDecoder* decoder) {
    if (!decoder || !decoder->codec) {
        return;
    }
    
    LOGI("Stopping decoder (decoded %lld frames, %lld errors)",
         (long long)decoder->frame_count, (long long)decoder->decode_errors);
    
    AMediaCodec_stop(decoder->codec);
    decoder->state = DECODER_STATE_CONFIGURED;
}

/* Destroy decoder */
void mediacodec_decoder_destroy(MediaCodecDecoder* decoder) {
    if (!decoder) {
        return;
    }
    
    LOGI("Destroying decoder");
    
    if (decoder->codec) {
        if (decoder->state == DECODER_STATE_RUNNING) {
            AMediaCodec_stop(decoder->codec);
        }
        AMediaCodec_delete(decoder->codec);
    }
    
    if (decoder->format) {
        AMediaFormat_delete(decoder->format);
    }
    
    if (decoder->config.extradata) {
        free(decoder->config.extradata);
    }
    
    free(decoder);
}

/* Get state */
DecoderState mediacodec_decoder_get_state(MediaCodecDecoder* decoder) {
    return decoder ? decoder->state : DECODER_STATE_UNINITIALIZED;
}

/* Get error message */
const char* mediacodec_decoder_get_error(MediaCodecDecoder* decoder) {
    if (!decoder || decoder->error_msg[0] == '\0') {
        return NULL;
    }
    return decoder->error_msg;
}

/* Check if codec is supported */
bool mediacodec_decoder_is_supported(FFVideoCodec codec, int width, int height) {
    const char* mime = get_mime_type(codec);
    if (!mime) {
        return false;
    }
    
    // Try to create a decoder
    AMediaCodec* test_codec = AMediaCodec_createDecoderByType(mime);
    if (!test_codec) {
        return false;
    }
    
    AMediaCodec_delete(test_codec);
    return true;
}

/* Get input buffer count */
int mediacodec_decoder_get_input_buffer_count(MediaCodecDecoder* decoder) {
    // MediaCodec doesn't provide this directly
    // Return estimated value
    return decoder ? 4 : 0;
}

/* Get output buffer count */
int mediacodec_decoder_get_output_buffer_count(MediaCodecDecoder* decoder) {
    // MediaCodec doesn't provide this directly
    // Return estimated value
    return decoder ? 4 : 0;
}
