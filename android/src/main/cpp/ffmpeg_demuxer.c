/*
 * FFmpeg Demuxer Implementation for fijkplayer
 */

#include "ffmpeg_demuxer.h"
#include "async_io_protocol.h"
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavutil/opt.h>
#include <libavutil/error.h>
#include <libavutil/time.h>
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "FFDemuxer"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

/* Internal demuxer structure */
struct FFDemuxer {
    AVFormatContext* format_ctx;
    AVIOContext* avio_ctx;           // Custom AVIO context
    AsyncIOContext* async_io;        // Async I/O context (for MP4)
    AVBSFContext* h264_bsf;          // H.264 bitstream filter (AVCC to AnnexB)
    uint8_t* avio_buffer;            // Buffer for custom AVIO
    char error_msg[512];
    char url[1024];              // Store URL for reopen
    bool is_active;
    bool use_async_io;           // Whether to use async I/O
    int64_t start_time;
    volatile bool interrupt_requested;
    int64_t last_operation_time;
};

/* Initialize FFmpeg */
void ff_demuxer_init() {
    static bool initialized = false;
    if (!initialized) {
        LOGI("Initializing FFmpeg library");
        
        // Initialize network protocols (required for HTTPS/SSL)
        int ret = avformat_network_init();
        if (ret < 0) {
            LOGE("Failed to initialize network protocols: %d", ret);
        } else {
            LOGI("✅ Network protocols initialized");
        }
        
        // List available protocols for debugging
        void *opaque = NULL;
        const char *protocol;
        LOGI("📋 Available protocols:");
        while ((protocol = avio_enum_protocols(&opaque, 0)) != NULL) {
            LOGI("  - %s", protocol);
        }
        
        initialized = true;
    }
}

/* Custom AVIO read callback using async I/O */
static int async_avio_read(void* opaque, uint8_t* buf, int buf_size) {
    FFDemuxer* demuxer = (FFDemuxer*)opaque;
    return async_io_read(demuxer->async_io, buf, buf_size);
}

/* Custom AVIO seek callback */
static int64_t async_avio_seek(void* opaque, int64_t offset, int whence) {
    FFDemuxer* demuxer = (FFDemuxer*)opaque;
    
    // CRITICAL: For MP4 files, arbitrary byte-level seeks break the demuxer's ability to parse packets
    // MP4 is a container format that requires reading the moov atom (metadata) to locate packets correctly
    // After a byte-level seek, the demuxer tries to parse from the middle of a packet, causing corruption
    // 
    // SOLUTION: Only allow SEEK_CUR (relative seeks for reading) and SEEK_END (for file size)
    // Block SEEK_SET (absolute seeks) which are used by async I/O for prefetching
    // This forces sequential reading, which works reliably with MP4
    
    if (whence == SEEK_SET) {
        // Get current position for comparison
        int64_t current_pos = async_io_seek(demuxer->async_io, 0, SEEK_CUR);
        
        // Only allow forward seeks that are very close (within 1MB) - likely just skipping ahead
        // Block backward seeks and large jumps which break packet parsing
        if (offset < current_pos || (offset - current_pos) > 1024*1024) {
            LOGW("🚫 Blocking async I/O seek from %lld to %lld (would break MP4 demuxer)", 
                 (long long)current_pos, (long long)offset);
            return current_pos;  // Return current position, don't actually seek
        }
    }
    
    int64_t result = async_io_seek(demuxer->async_io, offset, whence);
    
    // If we did allow a seek, flush BSF to reset state for any discontinuity
    if (result >= 0 && whence == SEEK_SET && demuxer->h264_bsf != NULL) {
        av_bsf_flush(demuxer->h264_bsf);
        LOGD("🔄 BSF flushed after allowed seek to offset %lld", (long long)offset);
    }
    
    return result;
}

/* Check if URL should use async I/O */
static bool should_use_async_io(const char* url) {
    // CRITICAL: Do NOT use async I/O for MP4 files (local OR network)!
    // 
    // Why: MP4 is a container format that requires proper frame-level seeking using the index.
    // Async I/O performs arbitrary byte-level seeks for buffering/prefetching, which breaks
    // the MP4 demuxer's ability to parse packets correctly. After a byte-level seek, the
    // demuxer tries to parse from the middle of a packet, causing BSF conversion errors.
    //
    // Solution: 
    // - For MP4 files: Use FFmpeg's direct I/O (supports HTTP byte-range requests automatically)
    // - For true streaming protocols (RTSP/HLS/RTMP): Use async I/O for buffering
    // - FFmpeg's HTTP implementation handles seeking correctly for MP4 over HTTP
    
    // Check if URL ends with .mp4 extension (case insensitive)
    size_t url_len = strlen(url);
    if (url_len > 4) {
        const char* ext = url + url_len - 4;
        if (strcasecmp(ext, ".mp4") == 0 || strcasecmp(ext, ".MP4") == 0) {
            LOGI("🎬 MP4 file detected, using direct I/O (FFmpeg handles HTTP seeking correctly)");
            return false;
        }
    }
    
    // Use async I/O only for true streaming protocols (not MP4)
    if (strstr(url, "rtsp://") == url || strstr(url, "rtmp://") == url) {
        return true;
    }
    if (strstr(url, ".m3u8") != NULL || strstr(url, ".M3U8") != NULL) {
        return true;  // HLS streams
    }
    
    // Default: no async I/O (let FFmpeg handle it)
    return false;
}

/* Interrupt callback for FFmpeg operations */
static int interrupt_callback(void* ctx) {
    FFDemuxer* demuxer = (FFDemuxer*)ctx;
    if (!demuxer) {
        return 0;
    }
    
    // Check if interrupt requested
    if (demuxer->interrupt_requested) {
        LOGD("Interrupt requested");
        return 1;
    }
    
    // Check for timeout (30 seconds)
    int64_t now = av_gettime();
    if (demuxer->last_operation_time > 0 && 
        (now - demuxer->last_operation_time) > 30000000) {
        LOGE("Operation timeout after 30 seconds");
        return 1;
    }
    
    return 0;
}

/* Create demuxer */
FFDemuxer* ff_demuxer_create() {
    FFDemuxer* demuxer = (FFDemuxer*)calloc(1, sizeof(FFDemuxer));
    if (!demuxer) {
        LOGE("Failed to allocate demuxer");
        return NULL;
    }
    demuxer->is_active = false;
    demuxer->start_time = AV_NOPTS_VALUE;
    demuxer->interrupt_requested = false;
    demuxer->last_operation_time = 0;
    return demuxer;
}

/* Map FFmpeg codec ID to our enum */
static FFVideoCodec map_video_codec(enum AVCodecID codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_H264: return FF_VIDEO_CODEC_H264;
        case AV_CODEC_ID_HEVC: return FF_VIDEO_CODEC_H265;
        case AV_CODEC_ID_VP8: return FF_VIDEO_CODEC_VP8;
        case AV_CODEC_ID_VP9: return FF_VIDEO_CODEC_VP9;
        default: return FF_VIDEO_CODEC_UNKNOWN;
    }
}

static FFAudioCodec map_audio_codec(enum AVCodecID codec_id) {
    switch (codec_id) {
        case AV_CODEC_ID_AAC: return FF_AUDIO_CODEC_AAC;
        case AV_CODEC_ID_MP3: return FF_AUDIO_CODEC_MP3;
        case AV_CODEC_ID_OPUS: return FF_AUDIO_CODEC_OPUS;
        default: return FF_AUDIO_CODEC_UNKNOWN;
    }
}

/* Open stream */
int ff_demuxer_open(FFDemuxer* demuxer, const char* url, FFDemuxerOptions* options) {
    if (!demuxer || !url) {
        return -1;
    }
    
    LOGI("Opening URL: %s", url);
    
    // Store URL for potential reopen
    strncpy(demuxer->url, url, sizeof(demuxer->url) - 1);
    demuxer->url[sizeof(demuxer->url) - 1] = '\0';
    
    int ret;
    AVDictionary* opts = NULL;
    int playback_mode = options ? options->playback_mode : 0;  // Default to LIVE_LOW_LATENCY
    
    // ═══════════════════════════════════════════════════════════════════
    // MODE-SPECIFIC FFmpeg OPTIONS
    // ═══════════════════════════════════════════════════════════════════
    
    if (playback_mode == 0) {
        // MODE 0: LIVE_LOW_LATENCY - Ultra-low latency for live surveillance
        LOGI("📹 Applying LIVE_LOW_LATENCY FFmpeg options");
        
        // Format options - minimal buffering
        av_dict_set(&opts, "fflags", "nobuffer", 0);           // Disable buffering
        av_dict_set(&opts, "flags", "low_delay", 0);           // Low delay mode
        av_dict_set(&opts, "flush_packets", "1", 0);           // Flush packets immediately
        av_dict_set(&opts, "max_delay", "0", 0);               // No muxing delay
        av_dict_set(&opts, "probesize", "32", 0);              // 32 bytes minimal probe
        av_dict_set(&opts, "analyzeduration", "0", 0);         // No pre-analysis
        
        // RTSP options
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);        // TCP for reliability
        av_dict_set(&opts, "rtsp_flags", "prefer_tcp", 0);
        
        // Network buffer
        av_dict_set(&opts, "buffer_size", "1024", 0);          // 1KB buffer
        
    } else if (playback_mode == 1) {
        // MODE 1: LIVE_WITH_AUDIO - Balanced latency with audio sync
        LOGI("🎬 Applying LIVE_WITH_AUDIO FFmpeg options");
        
        // Format options - moderate buffering
        av_dict_set(&opts, "fflags", "nobuffer", 0);
        av_dict_set(&opts, "flags", "low_delay", 0);
        av_dict_set(&opts, "probesize", "5000", 0);            // 5KB probe
        av_dict_set(&opts, "analyzeduration", "1000000", 0);   // 1 second analysis
        
        // RTSP options
        av_dict_set(&opts, "rtsp_transport", "tcp", 0);
        
        // Network buffer
        av_dict_set(&opts, "buffer_size", "4096", 0);          // 4KB buffer
        
    } else {
        // MODE 2: VOD_OPTIMIZED - Smooth playback for on-demand content
        LOGI("🎞️ Applying VOD_OPTIMIZED FFmpeg options");
        
        // Format options - full buffering
        av_dict_set(&opts, "probesize", "5000000", 0);         // 5MB probe
        av_dict_set(&opts, "analyzeduration", "5000000", 0);   // 5 seconds analysis
        
        // Network buffer
        av_dict_set(&opts, "buffer_size", "32768", 0);         // 32KB buffer
    }
    
    // ═══════════════════════════════════════════════════════════════════
    // COMMON OPTIONS (all modes)
    // ═══════════════════════════════════════════════════════════════════
    
    // Timeout
    if (options && options->timeout_us > 0) {
        char timeout_str[32];
        snprintf(timeout_str, sizeof(timeout_str), "%lld", (long long)options->timeout_us);
        av_dict_set(&opts, "timeout", timeout_str, 0);
    } else {
        av_dict_set(&opts, "timeout", "10000000", 0);  // 10 seconds default
    }
    
    // User agent
    if (options && options->user_agent) {
        av_dict_set(&opts, "user_agent", options->user_agent, 0);
    }
    
    // Protocol-specific options
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        // HTTP/HTTPS options
        av_dict_set(&opts, "reconnect", "1", 0);
        av_dict_set(&opts, "reconnect_streamed", "1", 0);
        av_dict_set(&opts, "reconnect_delay_max", "5", 0);
        av_dict_set(&opts, "multiple_requests", "1", 0);
        
        if (playback_mode != 2) {  // Live modes treat HTTP as non-seekable
            av_dict_set(&opts, "seekable", "0", 0);
        }
        
        // HTTPS/SSL options
        if (strncmp(url, "https://", 8) == 0) {
            LOGI("🔒 HTTPS URL detected, configuring TLS options");
            av_dict_set(&opts, "tls_verify", "0", 0);
            av_dict_set(&opts, "method", "GET", 0);
        }
    }
    
    // Allocate format context
    demuxer->format_ctx = avformat_alloc_context();
    if (!demuxer->format_ctx) {
        LOGE("Failed to allocate format context");
        av_dict_free(&opts);
        snprintf(demuxer->error_msg, sizeof(demuxer->error_msg), 
                 "Failed to allocate format context");
        return -1;
    }
    
    // Set up interrupt callback to prevent hangs
    demuxer->format_ctx->interrupt_callback.callback = interrupt_callback;
    demuxer->format_ctx->interrupt_callback.opaque = demuxer;
    demuxer->last_operation_time = av_gettime();
    
    // Set probe size and analyze duration for faster startup
    if (options && options->max_probe_size > 0) {
        demuxer->format_ctx->probesize = options->max_probe_size;
    } else {
        demuxer->format_ctx->probesize = 5000000; // 5MB default
    }
    
    if (options && options->max_analyze_duration_us > 0) {
        demuxer->format_ctx->max_analyze_duration = options->max_analyze_duration_us;
    } else {
        demuxer->format_ctx->max_analyze_duration = 5000000; // 5 seconds default
    }
    
    // Set max packet size to prevent buffer overflows (especially for HTTP)
    demuxer->format_ctx->max_picture_buffer = 10 * 1024 * 1024;  // 10MB max for video frames
    
    // Limit I/O buffer size for network streams to prevent memory issues
    if (strncmp(url, "http://", 7) == 0 || strncmp(url, "https://", 8) == 0) {
        // Set a reasonable buffer size for HTTP streaming
        demuxer->format_ctx->pb = NULL;  // Will be allocated by avformat_open_input
    }
    
    // IJKPLAYER APPROACH: Never use custom async I/O, let FFmpeg handle everything
    // FFmpeg's built-in protocols handle:
    // - Local files: Standard file I/O with proper seeking
    // - HTTP/HTTPS: HTTP protocol with range requests for seeking
    // - RTSP: Native streaming without seeking
    // Custom async I/O causes issues with MP4 demuxer's packet parsing after seeks
    
    LOGI("Using FFmpeg's native I/O for URL: %s", url);
    demuxer->use_async_io = false;  // Never use async I/O
    
    // Standard open - let FFmpeg choose the right protocol
    ret = avformat_open_input(&demuxer->format_ctx, url, NULL, &opts);
    
    // Log any remaining options (indicates unsupported options)
    AVDictionaryEntry* e = NULL;
    while ((e = av_dict_get(opts, "", e, AV_DICT_IGNORE_SUFFIX))) {
        LOGI("Unused option: %s=%s", e->key, e->value);
    }
    av_dict_free(&opts);
    
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Failed to open input: %s (error code: %d)", err_buf, ret);
        LOGE("URL was: %s", url);
        snprintf(demuxer->error_msg, sizeof(demuxer->error_msg), 
                 "Failed to open input: %s", err_buf);
        avformat_free_context(demuxer->format_ctx);
        demuxer->format_ctx = NULL;
        return ret;
    }
    
    // IJKPLAYER APPROACH: Single open with critical fixes from ijkplayer source
    // Key discoveries from bilibili/ijkplayer:
    // 1. fps_probe_size = 0 (MOST CRITICAL - prevents FPS calculation packet reads)
    // 2. pb->eof_reached = 0 reset after stream info (prevents premature EOF)
    // 3. Reasonable probesize (32KB-64KB range works best)
    // 4. Use interrupt_callback for timeout prevention
    // Reference: ff_ffplay.c line 3185, ijklivehook.c line 148
    
    // CRITICAL: Set fps_probe_size to 0 BEFORE calling avformat_find_stream_info
    // This prevents FFmpeg from reading many packets to calculate FPS
    // ijkplayer sets this in ijklas.c:1390 and ijklivehook.c:148
    demuxer->format_ctx->fps_probe_size = 0;
    
    demuxer->format_ctx->probesize = 32768;           // 32KB 
    demuxer->format_ctx->max_analyze_duration = 100000; // 0.1s
    
    const char* format_name = demuxer->format_ctx->iformat->name;
    LOGI("Getting stream info with fps_probe_size=0 for format: %s", format_name);
    
    ret = avformat_find_stream_info(demuxer->format_ctx, NULL);
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Failed to find stream info: %s", err_buf);
        snprintf(demuxer->error_msg, sizeof(demuxer->error_msg), 
                 "Failed to find stream info: %s", err_buf);
        avformat_close_input(&demuxer->format_ctx);
        return ret;
    }
    
    // CRITICAL ijkplayer fix: Reset EOF flag after stream info
    // This is essential - FFmpeg's stream analysis may set EOF incorrectly
    // Reference: ff_ffplay.c line 3185
    // Comment from ijkplayer: "FIXME hack, ffplay maybe should not use avio_feof() to test for the end"
    if (demuxer->format_ctx->pb) {
        demuxer->format_ctx->pb->eof_reached = 0;
        LOGI("✅ Reset pb->eof_reached=0 after stream info (ijkplayer fix)");
    }
    
    // Save stream info
    int64_t duration = demuxer->format_ctx->duration;
    int64_t start_time = demuxer->format_ctx->start_time;
    int64_t bitrate = demuxer->format_ctx->bit_rate;
    int nb_streams = demuxer->format_ctx->nb_streams;
    
    LOGI("✅ Stream info ready with ijkplayer fixes: Duration=%lld us, Bitrate=%lld, Streams=%d, fps_probe_size=%d",
         (long long)duration, (long long)bitrate, nb_streams, demuxer->format_ctx->fps_probe_size);
    
    // Initialize H.264 bitstream filter (AVCC → AnnexB conversion)
    // This is CRITICAL for MP4 files - they store H.264 in AVCC format (length-prefixed)
    // but MediaCodec and FFmpeg decoders expect AnnexB format (start code prefixed: 0x00000001)
    for (int i = 0; i < demuxer->format_ctx->nb_streams; i++) {
        AVStream* stream = demuxer->format_ctx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO &&
            stream->codecpar->codec_id == AV_CODEC_ID_H264) {
            
            LOGI("🔄 Initializing h264_mp4toannexb bitstream filter for stream %d", i);
            
            const AVBitStreamFilter* bsf = av_bsf_get_by_name("h264_mp4toannexb");
            if (!bsf) {
                LOGE("Failed to find h264_mp4toannexb filter");
                continue;
            }
            
            ret = av_bsf_alloc(bsf, &demuxer->h264_bsf);
            if (ret < 0) {
                char err_buf[128];
                av_strerror(ret, err_buf, sizeof(err_buf));
                LOGE("Failed to allocate BSF: %s", err_buf);
                continue;
            }
            
            // Copy codec parameters to BSF
            ret = avcodec_parameters_copy(demuxer->h264_bsf->par_in, stream->codecpar);
            if (ret < 0) {
                char err_buf[128];
                av_strerror(ret, err_buf, sizeof(err_buf));
                LOGE("Failed to copy codec parameters: %s", err_buf);
                av_bsf_free(&demuxer->h264_bsf);
                continue;
            }
            
            // Initialize the BSF
            ret = av_bsf_init(demuxer->h264_bsf);
            if (ret < 0) {
                char err_buf[128];
                av_strerror(ret, err_buf, sizeof(err_buf));
                LOGE("Failed to initialize BSF: %s", err_buf);
                av_bsf_free(&demuxer->h264_bsf);
                continue;
            }
            
            LOGI("✅ H.264 bitstream filter initialized (AVCC → AnnexB)");
            break; // Only need one video stream
        }
    }
    
    demuxer->is_active = true;
    demuxer->start_time = start_time;
    
    return 0;
}

/* Get stream count */
int ff_demuxer_get_stream_count(FFDemuxer* demuxer) {
    if (!demuxer || !demuxer->format_ctx) {
        return 0;
    }
    return demuxer->format_ctx->nb_streams;
}

/* Get stream info */
FFStream* ff_demuxer_get_stream(FFDemuxer* demuxer, int index) {
    if (!demuxer || !demuxer->format_ctx || 
        index < 0 || index >= (int)demuxer->format_ctx->nb_streams) {
        return NULL;
    }
    
    AVStream* av_stream = demuxer->format_ctx->streams[index];
    AVCodecParameters* codecpar = av_stream->codecpar;
    
    FFStream* stream = (FFStream*)calloc(1, sizeof(FFStream));
    if (!stream) {
        return NULL;
    }
    
    stream->index = index;
    
    // Determine stream type
    if (codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        stream->type = FF_STREAM_TYPE_VIDEO;
        stream->video_codec = map_video_codec(codecpar->codec_id);
        stream->width = codecpar->width;
        stream->height = codecpar->height;
        
        // Get framerate
        AVRational fps = av_stream->avg_frame_rate;
        if (fps.num == 0 || fps.den == 0) {
            fps = av_stream->r_frame_rate;
        }
        stream->fps_num = fps.num;
        stream->fps_den = fps.den;
        
        // Copy extradata (SPS/PPS for H.264)
        if (codecpar->extradata_size > 0) {
            stream->extradata = (uint8_t*)malloc(codecpar->extradata_size);
            if (stream->extradata) {
                memcpy(stream->extradata, codecpar->extradata, codecpar->extradata_size);
                stream->extradata_size = codecpar->extradata_size;
            }
        }
        
        LOGI("Video stream %d: %dx%d, %.2f fps, codec=%d", 
             index, stream->width, stream->height,
             stream->fps_den > 0 ? (float)stream->fps_num / stream->fps_den : 0,
             stream->video_codec);
        
    } else if (codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
        stream->type = FF_STREAM_TYPE_AUDIO;
        stream->audio_codec = map_audio_codec(codecpar->codec_id);
        stream->sample_rate = codecpar->sample_rate;
        stream->channels = codecpar->ch_layout.nb_channels;
        stream->bits_per_sample = codecpar->bits_per_coded_sample;
        
        // Copy extradata (AudioSpecificConfig for AAC, etc.)
        if (codecpar->extradata_size > 0) {
            stream->extradata = (uint8_t*)malloc(codecpar->extradata_size);
            if (stream->extradata) {
                memcpy(stream->extradata, codecpar->extradata, codecpar->extradata_size);
                stream->extradata_size = codecpar->extradata_size;
                LOGI("Audio stream %d extradata: %d bytes", index, codecpar->extradata_size);
            }
        }
        
        LOGI("Audio stream %d: %d Hz, %d channels, codec=%d",
             index, stream->sample_rate, stream->channels, stream->audio_codec);
        
    } else if (codecpar->codec_type == AVMEDIA_TYPE_SUBTITLE) {
        stream->type = FF_STREAM_TYPE_SUBTITLE;
    } else {
        stream->type = FF_STREAM_TYPE_UNKNOWN;
    }
    
    // Duration and bitrate
    if (av_stream->duration != AV_NOPTS_VALUE) {
        stream->duration_us = av_rescale_q(av_stream->duration, 
                                           av_stream->time_base, 
                                           (AVRational){1, 1000000});
    } else {
        stream->duration_us = -1;
    }
    
    stream->bitrate = codecpar->bit_rate;
    
    return stream;
}

/* Free stream */
void ff_stream_free(FFStream* stream) {
    if (stream) {
        if (stream->extradata) {
            free(stream->extradata);
        }
        free(stream);
    }
}

/* Find video stream */
int ff_demuxer_find_video_stream(FFDemuxer* demuxer) {
    if (!demuxer || !demuxer->format_ctx) {
        return -1;
    }
    
    int ret = av_find_best_stream(demuxer->format_ctx, AVMEDIA_TYPE_VIDEO, 
                                   -1, -1, NULL, 0);
    LOGD("Best video stream: %d", ret);
    return ret;
}

/* Find audio stream */
int ff_demuxer_find_audio_stream(FFDemuxer* demuxer) {
    if (!demuxer || !demuxer->format_ctx) {
        return -1;
    }
    
    int ret = av_find_best_stream(demuxer->format_ctx, AVMEDIA_TYPE_AUDIO, 
                                   -1, -1, NULL, 0);
    LOGD("Best audio stream: %d", ret);
    return ret;
}

/* Read packet */
int ff_demuxer_read_packet(FFDemuxer* demuxer, FFPacket** out_packet) {
    if (!demuxer || !demuxer->format_ctx || !out_packet) {
        return -1;
    }
    
    // Check if interrupt was requested
    if (demuxer->interrupt_requested) {
        LOGD("Read packet interrupted");
        return -1;
    }
    
    AVPacket* av_pkt = av_packet_alloc();
    if (!av_pkt) {
        LOGE("Failed to allocate packet");
        return -1;
    }
    
    // Update operation time before potentially blocking call
    demuxer->last_operation_time = av_gettime();
    
    int ret = av_read_frame(demuxer->format_ctx, av_pkt);
    if (ret < 0) {
        if (ret == AVERROR_EOF) {
            LOGI("End of stream");
            demuxer->is_active = false;
        } else if (ret == AVERROR(EINTR) || ret == AVERROR_EXIT) {
            LOGI("Operation interrupted");
        } else {
            char err_buf[128];
            av_strerror(ret, err_buf, sizeof(err_buf));
            LOGE("Error reading frame: %s (code: %d)", err_buf, ret);
            snprintf(demuxer->error_msg, sizeof(demuxer->error_msg),
                     "Error reading frame: %s", err_buf);
        }
        av_packet_free(&av_pkt);
        return ret;
    }
    
    // Apply bitstream filter for H.264 video packets (AVCC → AnnexB conversion)
    if (demuxer->h264_bsf != NULL) {
        AVStream* stream = demuxer->format_ctx->streams[av_pkt->stream_index];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            // Send packet to BSF
            ret = av_bsf_send_packet(demuxer->h264_bsf, av_pkt);
            if (ret < 0) {
                char err_buf[128];
                av_strerror(ret, err_buf, sizeof(err_buf));
                LOGE("Failed to send packet to BSF: %s", err_buf);
                av_packet_free(&av_pkt);
                return ret;
            }
            
            // Receive filtered packet
            ret = av_bsf_receive_packet(demuxer->h264_bsf, av_pkt);
            if (ret < 0) {
                char err_buf[128];
                av_strerror(ret, err_buf, sizeof(err_buf));
                LOGW("BSF error: %s (ret=%d) - discarding packet", err_buf, ret);
                
                // Clean up the failed packet
                av_packet_free(&av_pkt);
                
                // Flush BSF to reset state
                av_bsf_flush(demuxer->h264_bsf);
                
                // Return error - let caller handle retry
                // Don't do recursive retries here as it can cause stack overflow
                // and doesn't handle HTTP connection issues properly
                return AVERROR_INVALIDDATA;
            }
            
            // BSF conversion successful (logging disabled for performance)
        }
    }
    
    // Convert to our packet structure
    FFPacket* packet = (FFPacket*)calloc(1, sizeof(FFPacket));
    if (!packet) {
        av_packet_free(&av_pkt);
        return -1;
    }
    
    packet->stream_index = av_pkt->stream_index;
    packet->size = av_pkt->size;
    packet->data = (uint8_t*)malloc(av_pkt->size);
    if (!packet->data) {
        free(packet);
        av_packet_free(&av_pkt);
        return -1;
    }
    memcpy(packet->data, av_pkt->data, av_pkt->size);
    
    // Convert timestamps to microseconds
    AVStream* stream = demuxer->format_ctx->streams[av_pkt->stream_index];
    if (av_pkt->pts != AV_NOPTS_VALUE) {
        packet->pts = av_rescale_q(av_pkt->pts, stream->time_base, 
                                   (AVRational){1, 1000000});
    } else {
        packet->pts = AV_NOPTS_VALUE;
    }
    
    if (av_pkt->dts != AV_NOPTS_VALUE) {
        packet->dts = av_rescale_q(av_pkt->dts, stream->time_base, 
                                   (AVRational){1, 1000000});
    } else {
        packet->dts = AV_NOPTS_VALUE;
    }
    
    if (av_pkt->duration > 0) {
        packet->duration = av_rescale_q(av_pkt->duration, stream->time_base,
                                       (AVRational){1, 1000000});
    } else {
        packet->duration = 0;
    }
    
    packet->is_key_frame = (av_pkt->flags & AV_PKT_FLAG_KEY) != 0;
    
    av_packet_free(&av_pkt);
    *out_packet = packet;
    
    return 0;
}

/* Seek */
int ff_demuxer_seek(FFDemuxer* demuxer, int stream_index, int64_t timestamp_us, int flags) {
    if (!demuxer || !demuxer->format_ctx) {
        return -1;
    }
    
    if (stream_index < 0 || stream_index >= (int)demuxer->format_ctx->nb_streams) {
        return -1;
    }
    
    AVStream* stream = demuxer->format_ctx->streams[stream_index];
    int64_t seek_target = av_rescale_q(timestamp_us, (AVRational){1, 1000000},
                                       stream->time_base);
    
    LOGI("Seeking stream %d to %lld us (target=%lld, flags=%d)", 
         stream_index, (long long)timestamp_us, (long long)seek_target, flags);
    
    // Flush internal buffers before seeking to prevent memory corruption
    avformat_flush(demuxer->format_ctx);
    
    int ret;
    
    // For seeking to start, use byte-level seek + avformat_seek_file (ijkplayer approach)
    if (timestamp_us == 0) {
        LOGI("Using byte-level seek to beginning (ijkplayer approach)");
        // First, do a byte-level seek to position 0 in the file
        // This bypasses the format layer and goes directly to file I/O
        if (demuxer->format_ctx->pb) {
            int64_t pos = avio_seek(demuxer->format_ctx->pb, 0, SEEK_SET);
            LOGI("avio_seek to 0 returned: %lld", (long long)pos);
        }
        // Then use avformat_seek_file to reset the format context state
        // Use AVSEEK_FLAG_BYTE (1) to force byte-based seeking
        ret = avformat_seek_file(demuxer->format_ctx, stream_index, 
                                 0, 0, 0, AVSEEK_FLAG_BYTE);
    } else {
        // For non-zero seeks, use av_seek_frame with provided flags
        ret = av_seek_frame(demuxer->format_ctx, stream_index, seek_target, flags);
    }
    
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Seek failed: %s (ret=%d)", err_buf, ret);
        snprintf(demuxer->error_msg, sizeof(demuxer->error_msg),
                 "Seek failed: %s", err_buf);
        return ret;
    }
    
    // CRITICAL: Flush BSF after seek to reset internal state
    // Without this, BSF will try to continue from old state with new (discontinuous) data
    // causing "Invalid data" errors
    if (demuxer->h264_bsf != NULL) {
        av_bsf_flush(demuxer->h264_bsf);
        LOGI("🔄 H.264 BSF flushed after seek (reset state for discontinuous stream)");
    }
    
    LOGI("Seek completed successfully");
    return 0;
}

/* Free packet */
void ff_packet_free(FFPacket* packet) {
    if (packet) {
        if (packet->data) {
            free(packet->data);
        }
        free(packet);
    }
}

/* Close demuxer */
void ff_demuxer_close(FFDemuxer* demuxer) {
    if (demuxer) {
        // Free bitstream filter
        if (demuxer->h264_bsf) {
            av_bsf_free(&demuxer->h264_bsf);
        }
        
        if (demuxer->format_ctx) {
            LOGI("Closing demuxer");
            avformat_close_input(&demuxer->format_ctx);
        }
        
        // Clean up async I/O resources
        if (demuxer->avio_ctx) {
            av_freep(&demuxer->avio_ctx->buffer);
            avio_context_free(&demuxer->avio_ctx);
        }
        
        if (demuxer->async_io) {
            LOGI("Closing async I/O context");
            async_io_close(demuxer->async_io);
        }
        
        free(demuxer);
    }
}

/* Get error message */
const char* ff_demuxer_get_error(FFDemuxer* demuxer) {
    if (!demuxer || demuxer->error_msg[0] == '\0') {
        return NULL;
    }
    return demuxer->error_msg;
}

/* Clear EOF flag - ijkplayer approach */
void ff_demuxer_clear_eof(FFDemuxer* demuxer) {
    if (!demuxer || !demuxer->format_ctx || !demuxer->format_ctx->pb) {
        return;
    }
    
    // Clear the EOF flag that may have been set by avformat_find_stream_info()
    // This allows us to read packets from the beginning without reopening
    demuxer->format_ctx->pb->eof_reached = 0;
    LOGI("EOF flag cleared");
}

/* Check if active */
bool ff_demuxer_is_active(FFDemuxer* demuxer) {
    return demuxer && demuxer->is_active;
}

/* Get duration */
int64_t ff_demuxer_get_duration(FFDemuxer* demuxer) {
    if (!demuxer || !demuxer->format_ctx) {
        return -1;
    }
    
    if (demuxer->format_ctx->duration == AV_NOPTS_VALUE) {
        return -1;
    }
    
    return av_rescale_q(demuxer->format_ctx->duration, (AVRational){1, AV_TIME_BASE},
                        (AVRational){1, 1000000});
}

/* Get bitrate */
int64_t ff_demuxer_get_bitrate(FFDemuxer* demuxer) {
    if (!demuxer || !demuxer->format_ctx) {
        return -1;
    }
    return demuxer->format_ctx->bit_rate;
}

/* Request interrupt */
void ff_demuxer_interrupt(FFDemuxer* demuxer) {
    if (demuxer) {
        LOGD("Requesting demuxer interrupt");
        demuxer->interrupt_requested = true;
    }
}

/* Reopen demuxer - closes and reopens the same URL to reset state */
int ff_demuxer_reopen(FFDemuxer* demuxer) {
    if (!demuxer || !demuxer->url[0]) {
        LOGE("Cannot reopen: invalid demuxer or no URL stored");
        return -1;
    }
    
    LOGI("Reopening demuxer for URL: %s", demuxer->url);
    
    // Save the URL before closing
    char saved_url[1024];
    strncpy(saved_url, demuxer->url, sizeof(saved_url) - 1);
    saved_url[sizeof(saved_url) - 1] = '\0';
    
    // Close existing context if open
    if (demuxer->format_ctx) {
        avformat_close_input(&demuxer->format_ctx);
        demuxer->format_ctx = NULL;
    }
    
    // Reopen with minimal stream analysis to avoid reading the entire file
    AVDictionary* opts = NULL;
    av_dict_set(&opts, "timeout", "10000000", 0);
    
    // Allocate format context
    demuxer->format_ctx = avformat_alloc_context();
    if (!demuxer->format_ctx) {
        LOGE("Failed to allocate format context on reopen");
        av_dict_free(&opts);
        return -1;
    }
    
    // Set minimal probe size and analyze duration to avoid reading ahead
    // These values are small enough to only read the first few packets
    demuxer->format_ctx->probesize = 32768;           // 32 KB
    demuxer->format_ctx->max_analyze_duration = 100000; // 0.1 seconds
    
    // Set interrupt callback
    demuxer->format_ctx->interrupt_callback.callback = interrupt_callback;
    demuxer->format_ctx->interrupt_callback.opaque = demuxer;
    demuxer->last_operation_time = av_gettime();
    
    LOGI("Calling avformat_open_input with minimal analysis");
    int ret = avformat_open_input(&demuxer->format_ctx, saved_url, NULL, &opts);
    av_dict_free(&opts);
    
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Failed to reopen: %s", err_buf);
        snprintf(demuxer->error_msg, sizeof(demuxer->error_msg),
                 "Failed to reopen: %s", err_buf);
        return ret;
    }
    
    // Must call avformat_find_stream_info to initialize codec contexts
    // But with minimal duration to avoid reading too much
    LOGI("Finding stream info with minimal analysis");
    ret = avformat_find_stream_info(demuxer->format_ctx, NULL);
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Failed to find stream info on reopen: %s", err_buf);
        avformat_close_input(&demuxer->format_ctx);
        return ret;
    }
    
    LOGI("Demuxer reopened successfully with minimal analysis");
    demuxer->is_active = true;
    
    return 0;
}
