/*
 * Audio Decoder for fijkplayer
 * 
 * Hardware-accelerated AAC/MP3 decoding using Android MediaCodec
 * Fallback to FFmpeg software decoder for other codecs
 * Takes FFmpeg audio packets as input, outputs decoded PCM
 */

#ifndef FIJKPLAYER_AUDIO_DECODER_H
#define FIJKPLAYER_AUDIO_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include "ffmpeg_demuxer.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations */
typedef struct AudioDecoder AudioDecoder;
typedef struct DecodedAudio DecodedAudio;

/* Audio sample format */
typedef enum {
    AUDIO_FORMAT_S16 = 0,       // Signed 16-bit PCM
    AUDIO_FORMAT_FLT,           // Float 32-bit PCM
    AUDIO_FORMAT_S32            // Signed 32-bit PCM
} AudioFormat;

/* Decoder backend */
typedef enum {
    AUDIO_DECODER_MEDIACODEC = 0,  // Android MediaCodec (hardware)
    AUDIO_DECODER_FFMPEG          // FFmpeg libavcodec (software)
} AudioDecoderBackend;

/* Decoder state */
typedef enum {
    AUDIO_DECODER_STATE_UNINITIALIZED = 0,
    AUDIO_DECODER_STATE_CONFIGURED,
    AUDIO_DECODER_STATE_RUNNING,
    AUDIO_DECODER_STATE_FLUSHING,
    AUDIO_DECODER_STATE_ERROR
} AudioDecoderState;

/* Decoded audio structure */
struct DecodedAudio {
    uint8_t* data;              // PCM data buffer
    int size;                   // Buffer size in bytes
    int sample_rate;            // Sample rate (Hz)
    int channels;               // Number of channels (1=mono, 2=stereo)
    AudioFormat format;         // Sample format
    int64_t pts;                // Presentation timestamp (microseconds)
    int samples;                // Number of samples per channel
};

/* Decoder configuration */
typedef struct {
    FFAudioCodec codec;         // Audio codec type
    int sample_rate;            // Sample rate (Hz)
    int channels;               // Number of channels
    uint8_t* extradata;         // Codec extradata (codec-specific)
    int extradata_size;         // Extradata size
    AudioFormat output_format;  // Desired output format (default: S16)
    AudioDecoderBackend backend; // Preferred backend (auto-select if not specified)
} AudioDecoderConfig;

/*
 * Create an audio decoder instance
 * Returns NULL on failure
 */
AudioDecoder* audio_decoder_create();

/*
 * Configure the decoder with audio parameters
 * Will automatically select best backend (MediaCodec → FFmpeg fallback)
 * 
 * @param decoder The decoder instance
 * @param config Decoder configuration
 * @return 0 on success, negative on error
 */
int audio_decoder_configure(AudioDecoder* decoder, AudioDecoderConfig* config);

/*
 * Start the decoder
 * Must be called after configure and before sending packets
 * 
 * @param decoder The decoder instance
 * @return 0 on success, negative on error
 */
int audio_decoder_start(AudioDecoder* decoder);

/*
 * Send an audio packet to the decoder
 * 
 * @param decoder The decoder instance
 * @param packet Input packet from demuxer
 * @param timeout_us Timeout in microseconds (-1 for infinite, 0 for non-blocking)
 * @return 0 on success, -EAGAIN if would block, negative on error
 */
int audio_decoder_send_packet(AudioDecoder* decoder, FFPacket* packet, int64_t timeout_us);

/*
 * Receive decoded audio from the decoder
 * Caller must free decoded_audio->data when done
 * 
 * @param decoder The decoder instance
 * @param decoded_audio Output decoded audio (allocated by decoder)
 * @param timeout_us Timeout in microseconds (-1 for infinite, 0 for non-blocking)
 * @return 0 on success, -EAGAIN if would block, negative on error
 */
int audio_decoder_receive_audio(AudioDecoder* decoder, DecodedAudio* decoded_audio, int64_t timeout_us);

/*
 * Flush the decoder (clear buffers)
 * Use before seeking
 * 
 * @param decoder The decoder instance
 * @return 0 on success, negative on error
 */
int audio_decoder_flush(AudioDecoder* decoder);

/*
 * Stop the decoder
 * 
 * @param decoder The decoder instance
 */
void audio_decoder_stop(AudioDecoder* decoder);

/*
 * Destroy the decoder and free resources
 * 
 * @param decoder The decoder instance (can be NULL)
 */
void audio_decoder_destroy(AudioDecoder* decoder);

/*
 * Get decoder state
 * 
 * @param decoder The decoder instance
 * @return Current decoder state
 */
AudioDecoderState audio_decoder_get_state(AudioDecoder* decoder);

/*
 * Get decoder backend being used
 * 
 * @param decoder The decoder instance
 * @return Backend type
 */
AudioDecoderBackend audio_decoder_get_backend(AudioDecoder* decoder);

/*
 * Free decoded audio data
 * 
 * @param decoded_audio The decoded audio structure
 */
void audio_decoder_free_audio(DecodedAudio* decoded_audio);

#ifdef __cplusplus
}
#endif

#endif /* FIJKPLAYER_AUDIO_DECODER_H */
