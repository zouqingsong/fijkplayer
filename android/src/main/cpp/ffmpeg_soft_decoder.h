#ifndef FFMPEG_SOFT_DECODER_H
#define FFMPEG_SOFT_DECODER_H

#include <stdint.h>
#include <stdbool.h>
#include "ffmpeg_demuxer.h"

/* FFmpeg software video decoder for diagnostic purposes */

typedef struct FFmpegSoftDecoder FFmpegSoftDecoder;

/* Decoded frame from software decoder */
typedef struct {
    uint8_t* data[4];      // YUV planes
    int linesize[4];       // Line sizes for each plane
    int width;
    int height;
    int64_t pts;           // Presentation timestamp in microseconds
    int format;            // Pixel format (AV_PIX_FMT_*)
} SoftDecodedFrame;

/* Configuration */
typedef struct {
    int width;
    int height;
    int codec_id;          // FFmpeg codec ID
    uint8_t* extradata;    // SPS/PPS data
    int extradata_size;
} SoftDecoderConfig;

/* Create decoder */
FFmpegSoftDecoder* ffmpeg_soft_decoder_create(const SoftDecoderConfig* config);

/* Start decoder */
int ffmpeg_soft_decoder_start(FFmpegSoftDecoder* decoder);

/* Send packet to decoder */
int ffmpeg_soft_decoder_send_packet(FFmpegSoftDecoder* decoder, FFPacket* packet);

/* Receive decoded frame */
int ffmpeg_soft_decoder_receive_frame(FFmpegSoftDecoder* decoder, 
                                      SoftDecodedFrame** out_frame);

/* Free frame */
void ffmpeg_soft_decoder_free_frame(SoftDecodedFrame* frame);

/* Stop and free decoder */
void ffmpeg_soft_decoder_free(FFmpegSoftDecoder* decoder);

/* Get error message */
const char* ffmpeg_soft_decoder_get_error(FFmpegSoftDecoder* decoder);

#endif /* FFMPEG_SOFT_DECODER_H */
