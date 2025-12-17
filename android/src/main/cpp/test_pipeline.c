/**
 * Test Program: Complete Playback Pipeline
 * Demonstrates demuxer → decoder → frame queue integration
 * 
 * This test shows:
 * 1. Opening a media file/RTSP stream with FFmpeg demuxer
 * 2. Decoding video packets with MediaCodec
 * 3. Managing decoded frames in a thread-safe queue
 * 4. Simulating frame consumption (renderer)
 */

#include "ffmpeg_demuxer.h"
#include "mediacodec_decoder.h"
#include "frame_queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <android/log.h>

#define LOG_TAG "TestPipeline"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Test context
typedef struct {
    FFDemuxer* demuxer;
    MediaCodecDecoder* decoder;
    FrameQueue* frame_queue;
    int video_stream_index;
    bool stop_requested;
    pthread_t decoder_thread;
    pthread_t renderer_thread;
} TestContext;

/**
 * Decoder thread: reads packets and feeds to decoder, pushes frames to queue
 */
void* decoder_thread_func(void* arg) {
    TestContext* ctx = (TestContext*)arg;
    FFPacket* packet = NULL;
    int frame_count = 0;
    
    LOGD("Decoder thread started");
    
    while (!ctx->stop_requested) {
        // Read packet from demuxer
        int ret = ff_demuxer_read_packet(ctx->demuxer, &packet);
        if (ret < 0) {
            LOGE("Failed to read packet or EOF reached");
            break;
        }
        
        // Skip non-video packets
        if (packet->stream_index != ctx->video_stream_index) {
            ff_packet_free(packet);
            continue;
        }
        
        // Send packet to decoder
        ret = mediacodec_decoder_send_packet(ctx->decoder, packet->data, 
                                            packet->size, packet->pts);
        ff_packet_free(packet);
        
        if (ret < 0) {
            LOGE("Failed to send packet to decoder");
            continue;
        }
        
        // Receive decoded frames
        DecodedFrame* decoded_frame;
        while ((ret = mediacodec_decoder_receive_frame(ctx->decoder, &decoded_frame)) == 0) {
            // Convert to VideoFrame for queue
            VideoFrame* video_frame = frame_queue_alloc_frame(ctx->frame_queue,
                                                             decoded_frame->width,
                                                             decoded_frame->height,
                                                             FRAME_FORMAT_NV12);
            if (!video_frame) {
                LOGE("Failed to allocate frame from pool");
                free(decoded_frame);
                continue;
            }
            
            // Copy frame data (assuming NV12 format from MediaCodec)
            size_t y_size = decoded_frame->width * decoded_frame->height;
            size_t uv_size = y_size / 2;
            memcpy(video_frame->data[0], decoded_frame->data[0], y_size);
            memcpy(video_frame->data[1], decoded_frame->data[1], uv_size);
            
            video_frame->pts = decoded_frame->pts;
            video_frame->duration = 33333;  // ~30fps
            video_frame->frame_number = frame_count++;
            
            // Push to frame queue (blocks if full)
            ret = frame_queue_push(ctx->frame_queue, video_frame, 1000);
            frame_queue_free_frame(ctx->frame_queue, video_frame);
            free(decoded_frame);
            
            if (ret < 0) {
                LOGE("Failed to push frame to queue (timeout or error)");
                break;
            }
            
            if (frame_count % 30 == 0) {
                FrameQueueStats stats;
                frame_queue_get_stats(ctx->frame_queue, &stats);
                LOGD("Decoded %d frames, queue: %d/%d, dropped: %d", 
                     frame_count, stats.frames_queued, 3, stats.frames_dropped);
            }
        }
    }
    
    LOGD("Decoder thread finished, decoded %d frames", frame_count);
    return NULL;
}

/**
 * Renderer thread: pops frames from queue and simulates rendering
 */
void* renderer_thread_func(void* arg) {
    TestContext* ctx = (TestContext*)arg;
    VideoFrame* frame = NULL;
    int64_t last_pts = 0;
    int render_count = 0;
    
    LOGD("Renderer thread started");
    
    while (!ctx->stop_requested) {
        // Pop frame from queue (blocks if empty)
        int ret = frame_queue_pop(ctx->frame_queue, &frame, 100);
        if (ret < 0) {
            if (ctx->stop_requested) break;
            continue;  // Timeout, retry
        }
        
        // Simulate rendering delay based on frame PTS
        if (last_pts > 0) {
            int64_t delay = frame->pts - last_pts;
            if (delay > 0 && delay < 100000) {  // Max 100ms
                usleep(delay);
            }
        }
        last_pts = frame->pts;
        
        // "Render" frame (in real code, this would upload to OpenGL texture)
        render_count++;
        if (render_count % 30 == 0) {
            LOGD("Rendered %d frames, current PTS: %lld us", 
                 render_count, (long long)frame->pts);
        }
        
        // Return frame to pool
        frame_queue_free_frame(ctx->frame_queue, frame);
    }
    
    LOGD("Renderer thread finished, rendered %d frames", render_count);
    return NULL;
}

/**
 * Main test function
 */
int test_playback_pipeline(const char* url) {
    int ret = 0;
    TestContext ctx = {0};
    
    LOGD("=== Testing Playback Pipeline ===");
    LOGD("URL: %s", url);
    
    // 1. Initialize FFmpeg demuxer
    ctx.demuxer = ff_demuxer_create();
    if (!ctx.demuxer) {
        LOGE("Failed to create demuxer");
        return -1;
    }
    
    ret = ff_demuxer_open(ctx.demuxer, url, NULL);
    if (ret < 0) {
        LOGE("Failed to open media: %s", url);
        ff_demuxer_destroy(ctx.demuxer);
        return -1;
    }
    
    // Find video stream
    ctx.video_stream_index = -1;
    for (int i = 0; i < ctx.demuxer->nb_streams; i++) {
        if (ctx.demuxer->streams[i].type == FF_STREAM_VIDEO) {
            ctx.video_stream_index = i;
            break;
        }
    }
    
    if (ctx.video_stream_index < 0) {
        LOGE("No video stream found");
        ff_demuxer_close(ctx.demuxer);
        ff_demuxer_destroy(ctx.demuxer);
        return -1;
    }
    
    FFStream* video_stream = &ctx.demuxer->streams[ctx.video_stream_index];
    LOGD("Video stream: %dx%d, codec: %d, fps: %d/%d",
         video_stream->width, video_stream->height,
         video_stream->video_codec, video_stream->fps_num, video_stream->fps_den);
    
    // 2. Initialize MediaCodec decoder
    ctx.decoder = mediacodec_decoder_create();
    if (!ctx.decoder) {
        LOGE("Failed to create decoder");
        ff_demuxer_close(ctx.demuxer);
        ff_demuxer_destroy(ctx.demuxer);
        return -1;
    }
    
    DecoderConfig config = {
        .codec = video_stream->video_codec,
        .width = video_stream->width,
        .height = video_stream->height,
        .extradata = video_stream->extradata,
        .extradata_size = video_stream->extradata_size
    };
    
    ret = mediacodec_decoder_configure(ctx.decoder, &config);
    if (ret < 0) {
        LOGE("Failed to configure decoder");
        mediacodec_decoder_destroy(ctx.decoder);
        ff_demuxer_close(ctx.demuxer);
        ff_demuxer_destroy(ctx.demuxer);
        return -1;
    }
    
    ret = mediacodec_decoder_start(ctx.decoder);
    if (ret < 0) {
        LOGE("Failed to start decoder");
        mediacodec_decoder_destroy(ctx.decoder);
        ff_demuxer_close(ctx.demuxer);
        ff_demuxer_destroy(ctx.demuxer);
        return -1;
    }
    
    // 3. Create frame queue
    FrameQueueConfig queue_config = {
        .max_frames = 3,
        .max_duration_us = 500000,
        .enable_frame_drop = true,
        .enable_frame_pool = true
    };
    
    ctx.frame_queue = frame_queue_create(&queue_config);
    if (!ctx.frame_queue) {
        LOGE("Failed to create frame queue");
        mediacodec_decoder_stop(ctx.decoder);
        mediacodec_decoder_destroy(ctx.decoder);
        ff_demuxer_close(ctx.demuxer);
        ff_demuxer_destroy(ctx.demuxer);
        return -1;
    }
    
    LOGD("Pipeline initialized successfully");
    
    // 4. Start decoder and renderer threads
    ctx.stop_requested = false;
    
    pthread_create(&ctx.decoder_thread, NULL, decoder_thread_func, &ctx);
    pthread_create(&ctx.renderer_thread, NULL, renderer_thread_func, &ctx);
    
    // 5. Let it run for 10 seconds
    LOGD("Playing for 10 seconds...");
    sleep(10);
    
    // 6. Stop and wait for threads
    LOGD("Stopping playback...");
    ctx.stop_requested = true;
    
    pthread_join(ctx.decoder_thread, NULL);
    pthread_join(ctx.renderer_thread, NULL);
    
    // 7. Print final statistics
    FrameQueueStats stats;
    frame_queue_get_stats(ctx.frame_queue, &stats);
    
    LOGD("=== Final Statistics ===");
    LOGD("Frames produced: %d", stats.frames_produced);
    LOGD("Frames consumed: %d", stats.frames_consumed);
    LOGD("Frames dropped: %d", stats.frames_dropped);
    LOGD("Final queue size: %d", stats.frames_queued);
    
    // 8. Cleanup
    frame_queue_destroy(ctx.frame_queue);
    mediacodec_decoder_stop(ctx.decoder);
    mediacodec_decoder_destroy(ctx.decoder);
    ff_demuxer_close(ctx.demuxer);
    ff_demuxer_destroy(ctx.demuxer);
    
    LOGD("=== Test Complete ===");
    return 0;
}

// Entry point (can be called from JNI)
int main(int argc, char** argv) {
    const char* url = (argc > 1) ? argv[1] : "rtsp://example.com/stream";
    return test_playback_pipeline(url);
}
