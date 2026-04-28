// MIT License - FijkPlayer iOS Audio Decoder
// Decodes AAC/MP3 using AudioToolbox AudioConverter

#ifndef FJK_AUDIO_DECODER_H
#define FJK_AUDIO_DECODER_H

#include <stdint.h>
#include "ffmpeg_demuxer.h"

typedef struct FJKAudioDecoder FJKAudioDecoder;

/**
 * Create audio decoder for the given stream
 */
FJKAudioDecoder* fjk_audio_decoder_create(FFStream* stream);

/**
 * Destroy audio decoder
 */
void fjk_audio_decoder_destroy(FJKAudioDecoder* decoder);

/**
 * Send encoded packet to decoder
 * Returns 0 on success, negative on error
 */
int fjk_audio_decoder_send_packet(FJKAudioDecoder* decoder, FFPacket* packet);

/**
 * Receive decoded PCM samples
 * @param samples Output buffer (S16 interleaved)
 * @param max_samples Maximum samples per channel to read
 * @param pts Output PTS of decoded samples
 * Returns number of samples decoded, 0 if need more data, negative on error
 */
int fjk_audio_decoder_receive_samples(FJKAudioDecoder* decoder, 
                                      int16_t* samples, 
                                      int max_samples,
                                      int64_t* pts);

/**
 * Flush decoder
 */
void fjk_audio_decoder_flush(FJKAudioDecoder* decoder);

#endif // FJK_AUDIO_DECODER_H
