/**
 * Frame Queue - Thread-safe circular buffer for decoded video frames
 * 
 * Features:
 * - Lock-free circular buffer with atomic operations
 * - Producer/consumer pattern for decoder->renderer communication
 * - Audio/Video synchronization using PTS (Presentation Time Stamp)
 * - Backpressure handling (blocks producer when full)
 * - Frame pooling to reduce allocations
 * - Automatic frame dropping for smooth playback
 */

#ifndef FIJKPLAYER_FRAME_QUEUE_H
#define FIJKPLAYER_FRAME_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

// Video frame format (matching decoder output)
typedef enum {
    FRAME_FORMAT_YUV420P,     // Planar YUV 4:2:0
    FRAME_FORMAT_NV12,        // Semi-planar YUV 4:2:0
    FRAME_FORMAT_NV21,        // Semi-planar YUV 4:2:0 (Android default)
} FrameFormat;

// Video frame structure
typedef struct VideoFrame {
    // Frame data (YUV planes)
    uint8_t* data[3];         // Y, U, V plane pointers
    int linesize[3];          // Stride for each plane
    
    // Frame metadata
    int width;
    int height;
    FrameFormat format;
    
    // Timing information
    int64_t pts;              // Presentation timestamp (microseconds)
    int64_t duration;         // Frame duration (microseconds)
    
    // Frame state
    bool key_frame;           // Is this a keyframe?
    int frame_number;         // Sequential frame number
    
    // Memory management
    bool is_allocated;        // Is memory allocated?
    size_t buffer_size;       // Total buffer size
} VideoFrame;

// Frame queue configuration
typedef struct FrameQueueConfig {
    int max_frames;           // Maximum frames in queue (default: 3)
    int64_t max_duration_us;  // Max buffered duration in microseconds (default: 500ms)
    bool enable_frame_drop;   // Enable frame dropping when behind (default: true)
    bool enable_frame_pool;   // Enable frame pooling (default: true)
} FrameQueueConfig;

// Frame queue statistics
typedef struct FrameQueueStats {
    int frames_queued;        // Current number of frames in queue
    int frames_produced;      // Total frames produced
    int frames_consumed;      // Total frames consumed
    int frames_dropped;       // Total frames dropped
    int64_t buffer_duration_us; // Current buffered duration
    bool is_full;             // Is queue full?
    bool is_empty;            // Is queue empty?
} FrameQueueStats;

// Opaque frame queue handle
typedef struct FrameQueue FrameQueue;

/**
 * Create a new frame queue
 * 
 * @param config Queue configuration (NULL for defaults)
 * @return Frame queue handle, or NULL on error
 */
FrameQueue* frame_queue_create(const FrameQueueConfig* config);

/**
 * Destroy frame queue and free all resources
 * 
 * @param queue Frame queue to destroy
 */
void frame_queue_destroy(FrameQueue* queue);

/**
 * Allocate a frame from the pool (producer side)
 * This provides a pre-allocated frame buffer to avoid allocations
 * 
 * @param queue Frame queue
 * @param width Frame width
 * @param height Frame height
 * @param format Frame format
 * @return Frame pointer, or NULL if pool is full
 */
VideoFrame* frame_queue_alloc_frame(FrameQueue* queue, int width, int height, 
                                   FrameFormat format);

/**
 * Push a frame into the queue (producer side)
 * Blocks if queue is full until space is available
 * 
 * @param queue Frame queue
 * @param frame Frame to push (will be copied)
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return 0 on success, -1 on timeout or error
 */
int frame_queue_push(FrameQueue* queue, const VideoFrame* frame, int timeout_ms);

/**
 * Try to push a frame without blocking
 * 
 * @param queue Frame queue
 * @param frame Frame to push
 * @return 0 on success, -1 if queue is full
 */
int frame_queue_try_push(FrameQueue* queue, const VideoFrame* frame);

/**
 * Pop a frame from the queue (consumer side)
 * Blocks if queue is empty until frame is available
 * 
 * @param queue Frame queue
 * @param frame Output frame (must be freed with frame_queue_free_frame)
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return 0 on success, -1 on timeout or error
 */
int frame_queue_pop(FrameQueue* queue, VideoFrame** frame, int timeout_ms);

/**
 * Try to pop a frame without blocking
 * 
 * @param queue Frame queue
 * @param frame Output frame
 * @return 0 on success, -1 if queue is empty
 */
int frame_queue_try_pop(FrameQueue* queue, VideoFrame** frame);

/**
 * Peek at the next frame without removing it
 * 
 * @param queue Frame queue
 * @param frame Output frame (read-only, don't free)
 * @return 0 on success, -1 if queue is empty
 */
int frame_queue_peek(FrameQueue* queue, VideoFrame** frame);

/**
 * Free a frame returned by pop/alloc
 * 
 * @param queue Frame queue
 * @param frame Frame to free (returns to pool)
 */
void frame_queue_free_frame(FrameQueue* queue, VideoFrame* frame);

/**
 * Flush all frames from queue (e.g., on seek)
 * 
 * @param queue Frame queue
 */
void frame_queue_flush(FrameQueue* queue);

/**
 * Get queue statistics
 * 
 * @param queue Frame queue
 * @param stats Output statistics
 */
void frame_queue_get_stats(FrameQueue* queue, FrameQueueStats* stats);

/**
 * Check if queue is empty
 * 
 * @param queue Frame queue
 * @return true if empty
 */
bool frame_queue_is_empty(FrameQueue* queue);

/**
 * Check if queue is full
 * 
 * @param queue Frame queue
 * @return true if full
 */
bool frame_queue_is_full(FrameQueue* queue);

/**
 * Get current queue size
 * 
 * @param queue Frame queue
 * @return Number of frames in queue
 */
int frame_queue_size(FrameQueue* queue);

/**
 * Set audio clock for A/V sync (called by audio renderer)
 * 
 * @param queue Frame queue
 * @param audio_pts Current audio PTS in microseconds
 */
void frame_queue_set_audio_clock(FrameQueue* queue, int64_t audio_pts);

/**
 * Get current audio clock
 * 
 * @param queue Frame queue
 * @return Current audio PTS in microseconds
 */
int64_t frame_queue_get_audio_clock(FrameQueue* queue);

/**
 * Calculate video delay for A/V sync
 * Returns how long to wait before displaying the next frame
 * 
 * @param queue Frame queue
 * @param video_pts Video frame PTS in microseconds
 * @return Delay in microseconds (0 = display now, negative = drop frame)
 */
int64_t frame_queue_calc_delay(FrameQueue* queue, int64_t video_pts);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_FRAME_QUEUE_H
