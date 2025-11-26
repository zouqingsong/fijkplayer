/*
 * Test program for MediaCodec decoder
 * Demonstrates basic decoder usage with FFmpeg demuxer
 */

#include "ffmpeg_demuxer.h"
#include "mediacodec_decoder.h"
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>

static void print_frame_info(DecodedFrame* frame, int frame_num) {
    printf("Frame %d: %dx%d, pts=%lld us, key=%d, format=%d\n",
           frame_num, frame->width, frame->height,
           (long long)frame->pts, frame->is_key_frame, frame->format);
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printf("Usage: %s <video_file_or_rtsp_url>\n", argv[0]);
        return 1;
    }
    
    const char* url = argv[1];
    int ret;
    
    printf("=== FFmpeg Demuxer + MediaCodec Decoder Test ===\n");
    printf("Opening: %s\n\n", url);
    
    // Initialize FFmpeg
    ff_demuxer_init();
    
    // Create demuxer
    FFDemuxer* demuxer = ff_demuxer_create();
    if (!demuxer) {
        printf("ERROR: Failed to create demuxer\n");
        return 1;
    }
    
    // Open stream
    ret = ff_demuxer_open(demuxer, url, NULL);
    if (ret < 0) {
        printf("ERROR: Failed to open stream: %d\n", ret);
        ff_demuxer_close(demuxer);
        return 1;
    }
    
    // Get stream info
    int num_streams = ff_demuxer_get_stream_count(demuxer);
    printf("Stream opened, %d streams found\n", num_streams);
    
    // Find video stream
    int video_stream_idx = -1;
    FFStream* video_stream = NULL;
    
    for (int i = 0; i < num_streams; i++) {
        FFStream* stream = ff_demuxer_get_stream(demuxer, i);
        if (stream && stream->type == FF_STREAM_TYPE_VIDEO) {
            video_stream_idx = i;
            video_stream = stream;
            printf("\nVideo stream %d:\n", i);
            printf("  Codec: %d\n", stream->video_codec);
            printf("  Resolution: %dx%d\n", stream->width, stream->height);
            printf("  Framerate: %d/%d fps\n", stream->fps_num, stream->fps_den);
            break;
        }
    }
    
    if (video_stream_idx < 0) {
        printf("ERROR: No video stream found\n");
        ff_demuxer_close(demuxer);
        return 1;
    }
    
    // Check if decoder supports this codec
    bool supported = mediacodec_decoder_is_supported(video_stream->video_codec,
                                                     video_stream->width,
                                                     video_stream->height);
    if (!supported) {
        printf("ERROR: MediaCodec doesn't support this codec\n");
        ff_demuxer_close(demuxer);
        return 1;
    }
    printf("MediaCodec supports this codec\n");
    
    // Create decoder
    MediaCodecDecoder* decoder = mediacodec_decoder_create();
    if (!decoder) {
        printf("ERROR: Failed to create decoder\n");
        ff_demuxer_close(demuxer);
        return 1;
    }
    
    // Configure decoder
    DecoderConfig config = {0};
    config.codec = video_stream->video_codec;
    config.width = video_stream->width;
    config.height = video_stream->height;
    config.fps_num = video_stream->fps_num;
    config.fps_den = video_stream->fps_den;
    config.extradata = video_stream->extradata;
    config.extradata_size = video_stream->extradata_size;
    config.use_surface = false;
    config.output_format = VIDEO_FORMAT_YUV420;
    config.max_input_size = config.width * config.height;
    
    ret = mediacodec_decoder_configure(decoder, &config);
    if (ret < 0) {
        printf("ERROR: Failed to configure decoder: %d\n", ret);
        printf("  %s\n", mediacodec_decoder_get_error(decoder));
        mediacodec_decoder_destroy(decoder);
        ff_demuxer_close(demuxer);
        return 1;
    }
    printf("Decoder configured\n");
    
    // Start decoder
    ret = mediacodec_decoder_start(decoder);
    if (ret < 0) {
        printf("ERROR: Failed to start decoder: %d\n", ret);
        printf("  %s\n", mediacodec_decoder_get_error(decoder));
        mediacodec_decoder_destroy(decoder);
        ff_demuxer_close(demuxer);
        return 1;
    }
    printf("Decoder started\n\n");
    
    // Decode loop
    printf("=== Decoding frames ===\n");
    int packet_count = 0;
    int frame_count = 0;
    int max_frames = 100;  // Decode first 100 frames
    
    while (frame_count < max_frames) {
        // Read packet
        FFPacket* packet = NULL;
        ret = ff_demuxer_read_packet(demuxer, &packet);
        
        if (ret < 0) {
            printf("End of stream or error: %d\n", ret);
            break;
        }
        
        // Skip non-video packets
        if (packet->stream_index != video_stream_idx) {
            ff_packet_free(packet);
            continue;
        }
        
        packet_count++;
        
        // Send packet to decoder
        ret = mediacodec_decoder_send_packet(decoder, packet, 10000);
        ff_packet_free(packet);
        
        if (ret < 0 && ret != -EAGAIN) {
            printf("ERROR: Failed to send packet: %d\n", ret);
            break;
        }
        
        // Try to receive frames
        while (1) {
            DecodedFrame* frame = NULL;
            ret = mediacodec_decoder_receive_frame(decoder, &frame, 1000);
            
            if (ret == -EAGAIN) {
                // Need more input
                break;
            } else if (ret < 0) {
                printf("ERROR: Failed to receive frame: %d\n", ret);
                break;
            }
            
            // Got a frame
            frame_count++;
            print_frame_info(frame, frame_count);
            mediacodec_decoder_free_frame(frame);
            
            if (frame_count >= max_frames) {
                break;
            }
        }
    }
    
    printf("\n=== Summary ===\n");
    printf("Packets read: %d\n", packet_count);
    printf("Frames decoded: %d\n", frame_count);
    
    // Cleanup
    mediacodec_decoder_stop(decoder);
    mediacodec_decoder_destroy(decoder);
    ff_demuxer_close(demuxer);
    
    printf("\nTest completed successfully!\n");
    return 0;
}
