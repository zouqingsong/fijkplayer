#define DISABLE_VERBOSE_LOGS 1
/*
 * Audio Decoder Implementation
 * 
 * Hardware-accelerated AAC/MP3 decoding using Android MediaCodec
 * Fallback to FFmpeg software decoder for other codecs
 */

#include "audio_decoder.h"
#include <media/NdkMediaCodec.h>
#include <media/NdkMediaFormat.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <android/log.h>

#define TAG "AudioDecoder"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)

/* AudioDecoder structure */
struct AudioDecoder {
    AudioDecoderBackend backend;
    AudioDecoderState state;
    AudioDecoderConfig config;
    
    /* MediaCodec backend */
    AMediaCodec* media_codec;
    
    /* FFmpeg backend (future) */
    void* ffmpeg_decoder;  // FFmpegAudioDecoder* - to be implemented
    
    /* Output buffer */
    uint8_t* output_buffer;
    int output_buffer_size;
};

/* Helper: Get MIME type for codec */
static const char* get_mime_type(FFAudioCodec codec) {
    switch (codec) {
        case FF_AUDIO_CODEC_AAC: return "audio/mp4a-latm";
        case FF_AUDIO_CODEC_MP3: return "audio/mpeg";
        case FF_AUDIO_CODEC_OPUS: return "audio/opus";
        default: return NULL;
    }
}

/* Helper: Check if codec supported by MediaCodec */
static bool is_mediacodec_supported(FFAudioCodec codec) {
    const char* mime = get_mime_type(codec);
    if (!mime) return false;
    
    // MediaCodec on Android supports AAC and MP3 widely
    // Opus/Vorbis/FLAC support varies by device
    return (codec == FF_AUDIO_CODEC_AAC || codec == FF_AUDIO_CODEC_MP3);
}

/* Create decoder */
AudioDecoder* audio_decoder_create() {
    AudioDecoder* decoder = (AudioDecoder*)calloc(1, sizeof(AudioDecoder));
    if (!decoder) {
        LOGE("Failed to allocate AudioDecoder");
        return NULL;
    }
    
    decoder->state = AUDIO_DECODER_STATE_UNINITIALIZED;
    decoder->output_buffer_size = 8192 * 4; // 8KB per channel, stereo, 16-bit
    decoder->output_buffer = (uint8_t*)malloc(decoder->output_buffer_size);
    
    if (!decoder->output_buffer) {
        LOGE("Failed to allocate output buffer");
        free(decoder);
        return NULL;
    }
    
    LOGI("Audio decoder created");
    return decoder;
}

/* Configure decoder */
int audio_decoder_configure(AudioDecoder* decoder, AudioDecoderConfig* config) {
    if (!decoder || !config) {
        LOGE("Invalid arguments");
        return -EINVAL;
    }
    
    if (decoder->state != AUDIO_DECODER_STATE_UNINITIALIZED) {
        LOGE("Decoder already configured");
        return -EINVAL;
    }
    
    // Copy configuration
    decoder->config = *config;
    if (config->extradata && config->extradata_size > 0) {
        decoder->config.extradata = (uint8_t*)malloc(config->extradata_size);
        if (decoder->config.extradata) {
            memcpy(decoder->config.extradata, config->extradata, config->extradata_size);
        }
    }
    
    // Select backend
    if (config->backend == AUDIO_DECODER_MEDIACODEC && is_mediacodec_supported(config->codec)) {
        decoder->backend = AUDIO_DECODER_MEDIACODEC;
    } else if (is_mediacodec_supported(config->codec)) {
        decoder->backend = AUDIO_DECODER_MEDIACODEC;
        LOGI("Auto-selected MediaCodec backend for codec %d", config->codec);
    } else {
        decoder->backend = AUDIO_DECODER_FFMPEG;
        LOGI("Selected FFmpeg backend for codec %d (MediaCodec not supported)", config->codec);
    }
    
    // Configure MediaCodec
    if (decoder->backend == AUDIO_DECODER_MEDIACODEC) {
        const char* mime = get_mime_type(config->codec);
        if (!mime) {
            LOGE("Unsupported codec: %d", config->codec);
            return -ENOTSUP;
        }
        
        decoder->media_codec = AMediaCodec_createDecoderByType(mime);
        if (!decoder->media_codec) {
            LOGE("Failed to create MediaCodec for %s", mime);
            return -ENOMEM;
        }
        
        AMediaFormat* format = AMediaFormat_new();
        AMediaFormat_setString(format, AMEDIAFORMAT_KEY_MIME, mime);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, config->sample_rate);
        AMediaFormat_setInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, config->channels);
        
        // Set extradata (CSD-0 for AAC)
        if (config->extradata && config->extradata_size > 0) {
            LOGI("🎵 Setting csd-0: %d bytes", config->extradata_size);
            // Log first 16 bytes (or less) of extradata for debugging
            char hex_str[128] = {0};
            int log_size = config->extradata_size < 16 ? config->extradata_size : 16;
            for (int i = 0; i < log_size && i < 8; i++) {
                sprintf(hex_str + i*3, "%02x ", config->extradata[i]);
            }
            LOGI("🎵 csd-0 data: %s", hex_str);
            AMediaFormat_setBuffer(format, "csd-0", config->extradata, config->extradata_size);
        } else {
            LOGW("🎵 No extradata provided - MediaCodec AAC may not work!");
        }
        
        media_status_t status = AMediaCodec_configure(decoder->media_codec, format, NULL, NULL, 0);
        AMediaFormat_delete(format);
        
        if (status != AMEDIA_OK) {
            LOGE("Failed to configure MediaCodec: %d", status);
            AMediaCodec_delete(decoder->media_codec);
            decoder->media_codec = NULL;
            return -EIO;
        }
        
        LOGI("MediaCodec configured: %s, %d Hz, %d ch", mime, config->sample_rate, config->channels);
    } else {
        // FFmpeg decoder configuration - to be implemented in Phase 2.2
        LOGW("FFmpeg audio decoder not yet implemented");
        return -ENOTSUP;
    }
    
    decoder->state = AUDIO_DECODER_STATE_CONFIGURED;
    return 0;
}

/* Start decoder */
int audio_decoder_start(AudioDecoder* decoder) {
    if (!decoder) return -EINVAL;
    
    if (decoder->state != AUDIO_DECODER_STATE_CONFIGURED) {
        LOGE("Decoder not configured");
        return -EINVAL;
    }
    
    if (decoder->backend == AUDIO_DECODER_MEDIACODEC) {
        media_status_t status = AMediaCodec_start(decoder->media_codec);
        if (status != AMEDIA_OK) {
            LOGE("Failed to start MediaCodec: %d", status);
            return -EIO;
        }
        LOGI("MediaCodec started");
    }
    
    decoder->state = AUDIO_DECODER_STATE_RUNNING;
    return 0;
}

/* Send packet */
int audio_decoder_send_packet(AudioDecoder* decoder, FFPacket* packet, int64_t timeout_us) {
    if (!decoder || !packet) return -EINVAL;
    
    if (decoder->state != AUDIO_DECODER_STATE_RUNNING) {
        LOGE("Decoder not running");
        return -EINVAL;
    }
    
    if (decoder->backend == AUDIO_DECODER_MEDIACODEC) {
        // Debug: Log first few packets to understand what we're getting
        // Disabled verbose logging - uncomment for debugging
        // static int send_count = 0;
        // send_count++;
        // if (send_count <= 5) {
        //     LOGI("🎵 Send packet #%d: codec=%d, size=%d, FFmpeg_AAC=%d", 
        //          send_count, decoder->config.codec, packet->size, FF_AUDIO_CODEC_AAC);
        // }
        
        // Filter out AAC AudioSpecificConfig packets (typically 2-9 bytes)
        // These are configuration packets that should only be in extradata/csd-0, not sent as input
        if (decoder->config.codec == FF_AUDIO_CODEC_AAC && packet->size <= 10) {
            // Disabled verbose logging - uncomment for debugging
            // static int filtered_count = 0;
            // filtered_count++;
            // if (filtered_count <= 5) {
            //     LOGI("🎵 Filtered AAC config packet #%d (size=%d bytes) - not sending to decoder", 
            //          filtered_count, packet->size);
            // }
            return 0; // Success, but packet filtered out
        }
        ssize_t input_index = AMediaCodec_dequeueInputBuffer(decoder->media_codec, timeout_us);
        if (input_index < 0) {
            if (input_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
                return -EAGAIN;
            }
            LOGE("Failed to dequeue input buffer: %zd", input_index);
            return -EIO;
        }
        
        size_t buffer_size;
        uint8_t* buffer = AMediaCodec_getInputBuffer(decoder->media_codec, input_index, &buffer_size);
        if (!buffer) {
            LOGE("Failed to get input buffer");
            return -EIO;
        }
        
        size_t copy_size = packet->size < buffer_size ? packet->size : buffer_size;
        memcpy(buffer, packet->data, copy_size);
        
        media_status_t status = AMediaCodec_queueInputBuffer(
            decoder->media_codec,
            input_index,
            0,
            copy_size,
            packet->pts,
            0
        );
        
        if (status != AMEDIA_OK) {
            LOGE("Failed to queue input buffer: %d", status);
            return -EIO;
        }
        
        // Log every 100 packets
        static int packet_count = 0;
        packet_count++;
        // Disabled verbose logging - uncomment for debugging
        // if (packet_count % 100 == 0) {
        //     LOGI("🎵 Sent %d audio packets to MediaCodec", packet_count);
        // }
    }
    
    return 0;
}

/* Receive decoded audio */
int audio_decoder_receive_audio(AudioDecoder* decoder, DecodedAudio* decoded_audio, int64_t timeout_us) {
    if (!decoder || !decoded_audio) return -EINVAL;
    
    if (decoder->state != AUDIO_DECODER_STATE_RUNNING) {
        return -EINVAL;
    }
    
    memset(decoded_audio, 0, sizeof(DecodedAudio));
    
    if (decoder->backend == AUDIO_DECODER_MEDIACODEC) {
        AMediaCodecBufferInfo info;
        ssize_t output_index = AMediaCodec_dequeueOutputBuffer(decoder->media_codec, &info, timeout_us);
        
        // Debug logging for receive attempts
        static int receive_attempt = 0;
        receive_attempt++;
        // Disabled verbose logging - uncomment for debugging
        // if (receive_attempt <= 20 || receive_attempt % 100 == 0) {
        //     LOGI("🎧 Receive attempt #%d: output_index=%zd, timeout=%lld us", 
        //          receive_attempt, output_index, (long long)timeout_us);
        // }
        
        if (output_index == AMEDIACODEC_INFO_TRY_AGAIN_LATER) {
            return -EAGAIN;
        }
        
        if (output_index == AMEDIACODEC_INFO_OUTPUT_FORMAT_CHANGED) {
            AMediaFormat* format = AMediaCodec_getOutputFormat(decoder->media_codec);
            int32_t sample_rate, channels;
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &sample_rate);
            AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &channels);
            AMediaFormat_delete(format);
            // Disabled verbose logging - uncomment for debugging
            // LOGI("🎵 Audio output format changed: %d Hz, %d ch", sample_rate, channels);
            
            // Update decoder config with actual output format
            decoder->config.sample_rate = sample_rate;
            decoder->config.channels = channels;
            
            return -EAGAIN;
        }
        
        if (output_index == AMEDIACODEC_INFO_OUTPUT_BUFFERS_CHANGED) {
            LOGI("Output buffers changed");
            return -EAGAIN;
        }
        
        if (output_index < 0) {
            LOGE("Unexpected output buffer index: %zd", output_index);
            return -EIO;
        }
        
        size_t buffer_size;
        uint8_t* buffer = AMediaCodec_getOutputBuffer(decoder->media_codec, output_index, &buffer_size);
        if (!buffer || info.size <= 0) {
            AMediaCodec_releaseOutputBuffer(decoder->media_codec, output_index, false);
            return -EAGAIN;
        }
        
        // Allocate output buffer
        decoded_audio->data = (uint8_t*)malloc(info.size);
        if (!decoded_audio->data) {
            LOGE("Failed to allocate output audio buffer");
            AMediaCodec_releaseOutputBuffer(decoder->media_codec, output_index, false);
            return -ENOMEM;
        }
        
        memcpy(decoded_audio->data, buffer + info.offset, info.size);
        decoded_audio->size = info.size;
        decoded_audio->pts = info.presentationTimeUs;
        decoded_audio->sample_rate = decoder->config.sample_rate;
        decoded_audio->channels = decoder->config.channels;
        decoded_audio->format = AUDIO_FORMAT_S16;
        decoded_audio->samples = info.size / (2 * decoder->config.channels); // 16-bit = 2 bytes
        
        AMediaCodec_releaseOutputBuffer(decoder->media_codec, output_index, false);
        return 0;
    }
    
    return -ENOTSUP;
}

/* Flush decoder */
int audio_decoder_flush(AudioDecoder* decoder) {
    if (!decoder) return -EINVAL;
    
    if (decoder->backend == AUDIO_DECODER_MEDIACODEC && decoder->media_codec) {
        media_status_t status = AMediaCodec_flush(decoder->media_codec);
        if (status != AMEDIA_OK) {
            LOGE("Failed to flush MediaCodec: %d", status);
            return -EIO;
        }
        LOGI("MediaCodec flushed");
    }
    
    return 0;
}

/* Stop decoder */
void audio_decoder_stop(AudioDecoder* decoder) {
    if (!decoder) return;
    
    if (decoder->backend == AUDIO_DECODER_MEDIACODEC && decoder->media_codec) {
        AMediaCodec_stop(decoder->media_codec);
        LOGI("MediaCodec stopped");
    }
    
    decoder->state = AUDIO_DECODER_STATE_CONFIGURED;
}

/* Destroy decoder */
void audio_decoder_destroy(AudioDecoder* decoder) {
    if (!decoder) return;
    
    if (decoder->media_codec) {
        AMediaCodec_stop(decoder->media_codec);
        AMediaCodec_delete(decoder->media_codec);
    }
    
    if (decoder->config.extradata) {
        free(decoder->config.extradata);
    }
    
    if (decoder->output_buffer) {
        free(decoder->output_buffer);
    }
    
    free(decoder);
    LOGI("Audio decoder destroyed");
}

/* Get state */
AudioDecoderState audio_decoder_get_state(AudioDecoder* decoder) {
    return decoder ? decoder->state : AUDIO_DECODER_STATE_UNINITIALIZED;
}

/* Get backend */
AudioDecoderBackend audio_decoder_get_backend(AudioDecoder* decoder) {
    return decoder ? decoder->backend : AUDIO_DECODER_MEDIACODEC;
}

/* Free decoded audio */
void audio_decoder_free_audio(DecodedAudio* decoded_audio) {
    if (decoded_audio && decoded_audio->data) {
        free(decoded_audio->data);
        decoded_audio->data = NULL;
    }
}
