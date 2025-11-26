/*
 * MediaCodec Decoder Wrapper for fijkplayer
 * 
 * Hardware-accelerated H.264/H.265 decoding using Android MediaCodec
 * Takes FFmpeg packets as input, outputs decoded YUV frames
 */

#ifndef FIJKPLAYER_MEDIACODEC_DECODER_H
#define FIJKPLAYER_MEDIACODEC_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include "ffmpeg_demuxer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct MediaCodecDecoder MediaCodecDecoder;
typedef struct DecodedFrame DecodedFrame;

/* Video format */
typedef enum {
    VIDEO_FORMAT_YUV420 = 0,
    VIDEO_FORMAT_NV12,
    VIDEO_FORMAT_NV21,
    VIDEO_FORMAT_RGBA
} VideoFormat;

/* Decoder state */
typedef enum {
    DECODER_STATE_UNINITIALIZED = 0,
    DECODER_STATE_CONFIGURED,
    DECODER_STATE_RUNNING,
    DECODER_STATE_FLUSHING,
    DECODER_STATE_ERROR
} DecoderState;

/* Decoded frame structure */
struct DecodedFrame {
    uint8_t* data[3];           // Plane pointers (Y, U, V or Y, UV for NV12)
    int linesize[3];            // Stride for each plane
    int width;                  // Frame width
    int height;                 // Frame height
    VideoFormat format;         // Pixel format
    int64_t pts;                // Presentation timestamp (microseconds)
    int64_t duration;           // Frame duration (microseconds)
    bool is_key_frame;          // Is this a keyframe?
    void* native_buffer;        // Native MediaCodec buffer reference
};

/* Decoder configuration */
typedef struct {
    FFVideoCodec codec;         // Video codec type
    int width;                  // Video width
    int height;                 // Video height
    int fps_num;                // Framerate numerator
    int fps_den;                // Framerate denominator
    uint8_t* extradata;         // Codec extradata (SPS/PPS)
    int extradata_size;         // Extradata size
    bool use_surface;           // Output to Surface (for rendering)
    void* surface;              // ANativeWindow* for direct rendering
    VideoFormat output_format;  // Desired output format
    int max_input_size;         // Max input buffer size (default: width*height)
} DecoderConfig;

/*
 * Create a MediaCodec decoder instance
 * Returns NULL on failure
 */
MediaCodecDecoder* mediacodec_decoder_create();

/*
 * Configure the decoder with video parameters
 * Must be called before sending any packets
 * 
 * @param decoder The decoder instance
 * @param config Decoder configuration
 * @return 0 on success, negative on error
 */
int mediacodec_decoder_configure(MediaCodecDecoder* decoder, DecoderConfig* config);

/*
 * Start the decoder
 * Must be called after configure and before sending packets
 * 
 * @param decoder The decoder instance
 * @return 0 on success, negative on error
 */
int mediacodec_decoder_start(MediaCodecDecoder* decoder);

/*
 * Send a packet to the decoder
 * 
 * @param decoder The decoder instance
 * @param packet Input packet from demuxer
 * @param timeout_us Timeout in microseconds (-1 for infinite, 0 for non-blocking)
 * @return 0 on success, -EAGAIN if would block, negative on error
 */
int mediacodec_decoder_send_packet(MediaCodecDecoder* decoder, FFPacket* packet, int64_t timeout_us);

/*
 * Receive a decoded frame
 * 
 * @param decoder The decoder instance
 * @param frame Output frame (allocated by this function)
 * @param timeout_us Timeout in microseconds (-1 for infinite, 0 for non-blocking)
 * @return 0 on success, -EAGAIN if would block, negative on EOF or error
 * 
 * Caller must free frame with mediacodec_decoder_free_frame() when done
 */
int mediacodec_decoder_receive_frame(MediaCodecDecoder* decoder, DecodedFrame** frame, int64_t timeout_us);

/*
 * Free a decoded frame
 */
void mediacodec_decoder_free_frame(DecodedFrame* frame);

/*
 * Flush the decoder
 * Clears internal buffers, useful for seeking
 * 
 * @param decoder The decoder instance
 * @return 0 on success, negative on error
 */
int mediacodec_decoder_flush(MediaCodecDecoder* decoder);

/*
 * Stop the decoder
 * Can be restarted with mediacodec_decoder_start()
 * 
 * @param decoder The decoder instance
 */
void mediacodec_decoder_stop(MediaCodecDecoder* decoder);

/*
 * Destroy the decoder and free resources
 */
void mediacodec_decoder_destroy(MediaCodecDecoder* decoder);

/*
 * Get decoder state
 */
DecoderState mediacodec_decoder_get_state(MediaCodecDecoder* decoder);

/*
 * Get error message for last operation
 * Returns NULL if no error
 */
const char* mediacodec_decoder_get_error(MediaCodecDecoder* decoder);

/*
 * Get decoder capabilities
 * 
 * @param codec Video codec to check
 * @param width Video width
 * @param height Video height
 * @return true if decoder can handle this configuration
 */
bool mediacodec_decoder_is_supported(FFVideoCodec codec, int width, int height);

/*
 * Get input buffer count
 * Returns number of packets waiting to be decoded
 */
int mediacodec_decoder_get_input_buffer_count(MediaCodecDecoder* decoder);

/*
 * Get output buffer count
 * Returns number of frames ready to be consumed
 */
int mediacodec_decoder_get_output_buffer_count(MediaCodecDecoder* decoder);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_MEDIACODEC_DECODER_H
