/*
 * FFmpeg Demuxer Wrapper for fijkplayer
 * 
 * Provides a clean C API for opening media streams (RTSP, HTTP, file)
 * and extracting video/audio packets using FFmpeg's libavformat.
 * 
 * This replaces IJKPlayer's demuxer functionality.
 */

#ifndef FIJKPLAYER_FFMPEG_DEMUXER_H
#define FIJKPLAYER_FFMPEG_DEMUXER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct FFDemuxer FFDemuxer;
typedef struct FFPacket FFPacket;
typedef struct FFStream FFStream;

/* Stream types */
typedef enum {
    FF_STREAM_TYPE_UNKNOWN = 0,
    FF_STREAM_TYPE_VIDEO,
    FF_STREAM_TYPE_AUDIO,
    FF_STREAM_TYPE_SUBTITLE
} FFStreamType;

/* Video codec types */
typedef enum {
    FF_VIDEO_CODEC_UNKNOWN = 0,
    FF_VIDEO_CODEC_H264,
    FF_VIDEO_CODEC_H265,
    FF_VIDEO_CODEC_VP8,
    FF_VIDEO_CODEC_VP9
} FFVideoCodec;

/* Audio codec types */
typedef enum {
    FF_AUDIO_CODEC_UNKNOWN = 0,
    FF_AUDIO_CODEC_AAC,
    FF_AUDIO_CODEC_MP3,
    FF_AUDIO_CODEC_OPUS
} FFAudioCodec;

/* Stream information */
struct FFStream {
    int index;                    // Stream index in container
    FFStreamType type;            // Stream type
    
    // Video-specific
    FFVideoCodec video_codec;
    int width;
    int height;
    int fps_num;                  // Framerate numerator
    int fps_den;                  // Framerate denominator
    uint8_t* extradata;           // Codec extradata (SPS/PPS for H.264)
    int extradata_size;
    
    // Audio-specific
    FFAudioCodec audio_codec;
    int sample_rate;
    int channels;
    int bits_per_sample;
    
    // Common
    int64_t duration_us;          // Duration in microseconds
    int64_t bitrate;              // Bitrate in bits/sec
};

/* Packet structure */
struct FFPacket {
    int stream_index;             // Which stream this packet belongs to
    uint8_t* data;                // Packet data
    int size;                     // Data size
    int64_t pts;                  // Presentation timestamp (microseconds)
    int64_t dts;                  // Decode timestamp (microseconds)
    int64_t duration;             // Packet duration (microseconds)
    bool is_key_frame;            // Is this a keyframe?
};

/* Demuxer options */
typedef struct {
    int64_t timeout_us;           // Network timeout in microseconds (default: 5000000 = 5s)
    int max_analyze_duration_us;  // Max time to analyze stream (default: 5000000 = 5s)
    int max_probe_size;           // Max probe size (default: 5MB)
    bool enable_tcp;              // Use TCP for RTSP (default: true)
    bool enable_lowdelay;         // Enable low-delay mode for live streams (default: true)
    char* user_agent;             // User agent for HTTP streams
} FFDemuxerOptions;

/*
 * Initialize FFmpeg library
 * Must be called once before any other FFmpeg functions
 */
void ff_demuxer_init();

/*
 * Create a new demuxer instance
 * Returns NULL on failure
 */
FFDemuxer* ff_demuxer_create();

/*
 * Open a media stream (RTSP, HTTP, file)
 * 
 * @param demuxer The demuxer instance
 * @param url URL to open (rtsp://, http://, file://)
 * @param options Demuxer options (can be NULL for defaults)
 * @return 0 on success, negative on error
 */
int ff_demuxer_open(FFDemuxer* demuxer, const char* url, FFDemuxerOptions* options);

/*
 * Get number of streams in the media
 */
int ff_demuxer_get_stream_count(FFDemuxer* demuxer);

/*
 * Get information about a specific stream
 * 
 * @param demuxer The demuxer instance
 * @param index Stream index (0 to stream_count-1)
 * @return Stream info, or NULL if index invalid. Caller must free with ff_stream_free()
 */
FFStream* ff_demuxer_get_stream(FFDemuxer* demuxer, int index);

/*
 * Free stream info
 */
void ff_stream_free(FFStream* stream);

/*
 * Find the best video stream index
 * Returns -1 if no video stream found
 */
int ff_demuxer_find_video_stream(FFDemuxer* demuxer);

/*
 * Find the best audio stream index
 * Returns -1 if no audio stream found
 */
int ff_demuxer_find_audio_stream(FFDemuxer* demuxer);

/*
 * Clear EOF flag (ijkplayer approach)
 * This allows reading packets after avformat_find_stream_info() consumed the file
 */
void ff_demuxer_clear_eof(FFDemuxer* demuxer);

/*
 * Read next packet from any stream
 * 
 * @param demuxer The demuxer instance
 * @param packet Output packet (allocated by this function)
 * @return 0 on success, negative on EOF or error
 * 
 * Caller must free packet with ff_packet_free() when done
 */
int ff_demuxer_read_packet(FFDemuxer* demuxer, FFPacket** packet);

/*
 * Seek to a specific timestamp
 * 
 * @param demuxer The demuxer instance
 * @param stream_index Stream to seek in
 * @param timestamp_us Target timestamp in microseconds
 * @param flags Seek flags (0 for default)
 * @return 0 on success, negative on error
 */
int ff_demuxer_seek(FFDemuxer* demuxer, int stream_index, int64_t timestamp_us, int flags);

/*
 * Free a packet
 */
void ff_packet_free(FFPacket* packet);

/*
 * Close the demuxer and free resources
 */
void ff_demuxer_close(FFDemuxer* demuxer);

/*
 * Get error message for last operation
 * Returns NULL if no error
 */
const char* ff_demuxer_get_error(FFDemuxer* demuxer);

/*
 * Get current stream state
 * Returns true if stream is still active, false if EOF or error
 */
bool ff_demuxer_is_active(FFDemuxer* demuxer);

/*
 * Get duration of media in microseconds
 * Returns -1 if unknown (e.g., live streams)
 */
int64_t ff_demuxer_get_duration(FFDemuxer* demuxer);

/*
 * Get bitrate of media in bits/sec
 * Returns -1 if unknown
 */
int64_t ff_demuxer_get_bitrate(FFDemuxer* demuxer);

/*
 * Request interruption of demuxer operations
 * Should be called when stopping playback to prevent blocking operations
 */
void ff_demuxer_interrupt(FFDemuxer* demuxer);

/*
 * Reopen demuxer - closes and reopens the same URL to reset state
 * This is useful after avformat_find_stream_info() has consumed packets
 * Returns 0 on success, negative on error
 */
int ff_demuxer_reopen(FFDemuxer* demuxer);

/*
 * Get the underlying AVFormatContext (for software decoder setup)
 * Returns NULL if demuxer is not open
 */
struct AVFormatContext* ff_demuxer_get_format_context(FFDemuxer* demuxer);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_FFMPEG_DEMUXER_H
