/**
 * Frame Queue Implementation
 * Thread-safe circular buffer with frame pooling and A/V sync
 */

#include "frame_queue.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <errno.h>
#include <android/log.h>

#define LOG_TAG "FrameQueue"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// Default configuration
#define DEFAULT_MAX_FRAMES 3
#define DEFAULT_MAX_DURATION_US 500000  // 500ms
#define MAX_FRAME_POOL_SIZE 10

// Frame queue structure
struct FrameQueue {
    // Ring buffer
    VideoFrame** frames;      // Array of frame pointers
    int capacity;             // Maximum frames
    int read_index;           // Consumer index
    int write_index;          // Producer index
    int size;                 // Current size
    
    // Frame pool for reuse
    VideoFrame** frame_pool;
    int pool_size;
    int pool_capacity;
    
    // Synchronization
    pthread_mutex_t mutex;
    pthread_cond_t not_empty; // Signal when frames available
    pthread_cond_t not_full;  // Signal when space available
    
    // Configuration
    FrameQueueConfig config;
    
    // Statistics
    FrameQueueStats stats;
    
    // A/V sync
    int64_t audio_clock;      // Current audio PTS
    pthread_mutex_t clock_mutex;
    
    // State
    bool flushing;
    bool destroyed;
};

// Helper: Calculate YUV420 buffer size
static size_t calc_yuv420_size(int width, int height, FrameFormat format) {
    size_t y_size = width * height;
    size_t uv_size = (width / 2) * (height / 2);
    
    switch (format) {
        case FRAME_FORMAT_YUV420P:
            return y_size + uv_size * 2;  // Y + U + V
        case FRAME_FORMAT_NV12:
        case FRAME_FORMAT_NV21:
            return y_size + uv_size * 2;  // Y + UV interleaved
        default:
            return 0;
    }
}

// Helper: Allocate frame buffer
static int alloc_frame_buffer(VideoFrame* frame, int width, int height, FrameFormat format) {
    if (!frame) return -1;
    
    size_t buffer_size = calc_yuv420_size(width, height, format);
    if (buffer_size == 0) return -1;
    
    frame->buffer_size = buffer_size;
    uint8_t* buffer = (uint8_t*)malloc(buffer_size);
    if (!buffer) {
        LOGE("Failed to allocate frame buffer: %zu bytes", buffer_size);
        return -1;
    }
    
    // Set up plane pointers
    frame->data[0] = buffer;  // Y plane
    frame->linesize[0] = width;
    
    size_t y_size = width * height;
    if (format == FRAME_FORMAT_YUV420P) {
        // Planar: separate U and V planes
        frame->data[1] = buffer + y_size;                    // U plane
        frame->data[2] = buffer + y_size + (y_size / 4);     // V plane
        frame->linesize[1] = width / 2;
        frame->linesize[2] = width / 2;
    } else {
        // Semi-planar: interleaved UV
        frame->data[1] = buffer + y_size;  // UV plane
        frame->data[2] = NULL;
        frame->linesize[1] = width;
        frame->linesize[2] = 0;
    }
    
    frame->width = width;
    frame->height = height;
    frame->format = format;
    frame->is_allocated = true;
    
    return 0;
}

// Helper: Free frame buffer
static void free_frame_buffer(VideoFrame* frame) {
    if (frame && frame->is_allocated && frame->data[0]) {
        free(frame->data[0]);
        frame->data[0] = NULL;
        frame->data[1] = NULL;
        frame->data[2] = NULL;
        frame->is_allocated = false;
    }
}

// Helper: Copy frame data
static int copy_frame(VideoFrame* dst, const VideoFrame* src) {
    if (!dst || !src) return -1;
    
    // Allocate destination buffer if needed
    if (!dst->is_allocated || dst->width != src->width || 
        dst->height != src->height || dst->format != src->format) {
        free_frame_buffer(dst);
        if (alloc_frame_buffer(dst, src->width, src->height, src->format) < 0) {
            return -1;
        }
    }
    
    // Copy plane data
    size_t y_size = src->width * src->height;
    memcpy(dst->data[0], src->data[0], y_size);
    
    if (src->format == FRAME_FORMAT_YUV420P) {
        size_t uv_size = y_size / 4;
        memcpy(dst->data[1], src->data[1], uv_size);
        memcpy(dst->data[2], src->data[2], uv_size);
    } else {
        size_t uv_size = y_size / 2;
        memcpy(dst->data[1], src->data[1], uv_size);
    }
    
    // Copy metadata
    dst->pts = src->pts;
    dst->duration = src->duration;
    dst->key_frame = src->key_frame;
    dst->frame_number = src->frame_number;
    
    return 0;
}

// Helper: Get current time in microseconds
static int64_t get_time_us(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (int64_t)ts.tv_sec * 1000000 + ts.tv_nsec / 1000;
}

// Helper: Wait with timeout
static int wait_timeout(pthread_cond_t* cond, pthread_mutex_t* mutex, int timeout_ms) {
    if (timeout_ms < 0) {
        // Infinite wait
        return pthread_cond_wait(cond, mutex);
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    int ret = pthread_cond_timedwait(cond, mutex, &ts);
    return (ret == ETIMEDOUT) ? -1 : ret;
}

FrameQueue* frame_queue_create(const FrameQueueConfig* config) {
    FrameQueue* queue = (FrameQueue*)calloc(1, sizeof(FrameQueue));
    if (!queue) {
        LOGE("Failed to allocate frame queue");
        return NULL;
    }
    
    // Set configuration
    if (config) {
        queue->config = *config;
    } else {
        queue->config.max_frames = DEFAULT_MAX_FRAMES;
        queue->config.max_duration_us = DEFAULT_MAX_DURATION_US;
        queue->config.enable_frame_drop = true;
        queue->config.enable_frame_pool = true;
    }
    
    // Allocate ring buffer
    queue->capacity = queue->config.max_frames;
    queue->frames = (VideoFrame**)calloc(queue->capacity, sizeof(VideoFrame*));
    if (!queue->frames) {
        LOGE("Failed to allocate frame buffer");
        free(queue);
        return NULL;
    }
    
    // Allocate frame pool
    if (queue->config.enable_frame_pool) {
        queue->pool_capacity = MAX_FRAME_POOL_SIZE;
        queue->frame_pool = (VideoFrame**)calloc(queue->pool_capacity, sizeof(VideoFrame*));
        if (!queue->frame_pool) {
            LOGE("Failed to allocate frame pool");
            free(queue->frames);
            free(queue);
            return NULL;
        }
    }
    
    // Initialize mutexes and conditions
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->not_empty, NULL);
    pthread_cond_init(&queue->not_full, NULL);
    pthread_mutex_init(&queue->clock_mutex, NULL);
    
    queue->audio_clock = 0;
    queue->flushing = false;
    queue->destroyed = false;
    
    LOGD("Frame queue created: capacity=%d, max_duration=%lld us", 
         queue->capacity, (long long)queue->config.max_duration_us);
    
    return queue;
}

void frame_queue_destroy(FrameQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->destroyed = true;
    
    // Wake up any waiting threads
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    // Free all frames in queue
    for (int i = 0; i < queue->capacity; i++) {
        if (queue->frames[i]) {
            free_frame_buffer(queue->frames[i]);
            free(queue->frames[i]);
        }
    }
    free(queue->frames);
    
    // Free frame pool
    if (queue->frame_pool) {
        for (int i = 0; i < queue->pool_size; i++) {
            if (queue->frame_pool[i]) {
                free_frame_buffer(queue->frame_pool[i]);
                free(queue->frame_pool[i]);
            }
        }
        free(queue->frame_pool);
    }
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->not_empty);
    pthread_cond_destroy(&queue->not_full);
    pthread_mutex_destroy(&queue->clock_mutex);
    
    free(queue);
    LOGD("Frame queue destroyed");
}

VideoFrame* frame_queue_alloc_frame(FrameQueue* queue, int width, int height, 
                                   FrameFormat format) {
    if (!queue || !queue->config.enable_frame_pool) return NULL;
    
    pthread_mutex_lock(&queue->mutex);
    
    VideoFrame* frame = NULL;
    
    // Try to get frame from pool
    if (queue->pool_size > 0) {
        frame = queue->frame_pool[--queue->pool_size];
        
        // Check if frame buffer is compatible
        if (frame->width != width || frame->height != height || frame->format != format) {
            free_frame_buffer(frame);
            if (alloc_frame_buffer(frame, width, height, format) < 0) {
                free(frame);
                frame = NULL;
            }
        }
    } else if (queue->pool_size < queue->pool_capacity) {
        // Allocate new frame
        frame = (VideoFrame*)calloc(1, sizeof(VideoFrame));
        if (frame) {
            if (alloc_frame_buffer(frame, width, height, format) < 0) {
                free(frame);
                frame = NULL;
            }
        }
    }
    
    pthread_mutex_unlock(&queue->mutex);
    return frame;
}

int frame_queue_push(FrameQueue* queue, const VideoFrame* frame, int timeout_ms) {
    if (!queue || !frame) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait until space is available
    while (queue->size >= queue->capacity && !queue->destroyed && !queue->flushing) {
        if (wait_timeout(&queue->not_full, &queue->mutex, timeout_ms) < 0) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;  // Timeout
        }
    }
    
    if (queue->destroyed || queue->flushing) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // Get or allocate frame slot
    VideoFrame* slot = queue->frames[queue->write_index];
    if (!slot) {
        slot = (VideoFrame*)calloc(1, sizeof(VideoFrame));
        if (!slot) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
        queue->frames[queue->write_index] = slot;
    }
    
    // Copy frame data
    if (copy_frame(slot, frame) < 0) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // Update indices
    queue->write_index = (queue->write_index + 1) % queue->capacity;
    queue->size++;
    queue->stats.frames_produced++;
    
    // Signal consumers
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

int frame_queue_try_push(FrameQueue* queue, const VideoFrame* frame) {
    if (!queue || !frame) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->size >= queue->capacity || queue->destroyed || queue->flushing) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    VideoFrame* slot = queue->frames[queue->write_index];
    if (!slot) {
        slot = (VideoFrame*)calloc(1, sizeof(VideoFrame));
        if (!slot) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;
        }
        queue->frames[queue->write_index] = slot;
    }
    
    if (copy_frame(slot, frame) < 0) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    queue->write_index = (queue->write_index + 1) % queue->capacity;
    queue->size++;
    queue->stats.frames_produced++;
    
    pthread_cond_signal(&queue->not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

int frame_queue_pop(FrameQueue* queue, VideoFrame** frame, int timeout_ms) {
    if (!queue || !frame) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait until frame is available
    while (queue->size == 0 && !queue->destroyed && !queue->flushing) {
        if (wait_timeout(&queue->not_empty, &queue->mutex, timeout_ms) < 0) {
            pthread_mutex_unlock(&queue->mutex);
            return -1;  // Timeout
        }
    }
    
    if (queue->destroyed || (queue->size == 0 && queue->flushing)) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // Get frame from queue
    *frame = queue->frames[queue->read_index];
    queue->frames[queue->read_index] = NULL;
    
    queue->read_index = (queue->read_index + 1) % queue->capacity;
    queue->size--;
    queue->stats.frames_consumed++;
    
    // Signal producers
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

int frame_queue_try_pop(FrameQueue* queue, VideoFrame** frame) {
    if (!queue || !frame) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->size == 0 || queue->destroyed) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    *frame = queue->frames[queue->read_index];
    queue->frames[queue->read_index] = NULL;
    
    queue->read_index = (queue->read_index + 1) % queue->capacity;
    queue->size--;
    queue->stats.frames_consumed++;
    
    pthread_cond_signal(&queue->not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

int frame_queue_peek(FrameQueue* queue, VideoFrame** frame) {
    if (!queue || !frame) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->size == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    *frame = queue->frames[queue->read_index];
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

void frame_queue_free_frame(FrameQueue* queue, VideoFrame* frame) {
    if (!queue || !frame) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Return frame to pool if enabled
    if (queue->config.enable_frame_pool && queue->pool_size < queue->pool_capacity) {
        queue->frame_pool[queue->pool_size++] = frame;
    } else {
        // Free frame completely
        free_frame_buffer(frame);
        free(frame);
    }
    
    pthread_mutex_unlock(&queue->mutex);
}

void frame_queue_flush(FrameQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->flushing = true;
    
    // Return all frames to pool
    for (int i = 0; i < queue->size; i++) {
        int index = (queue->read_index + i) % queue->capacity;
        if (queue->frames[index]) {
            if (queue->config.enable_frame_pool && queue->pool_size < queue->pool_capacity) {
                queue->frame_pool[queue->pool_size++] = queue->frames[index];
            } else {
                free_frame_buffer(queue->frames[index]);
                free(queue->frames[index]);
            }
            queue->frames[index] = NULL;
        }
    }
    
    queue->read_index = 0;
    queue->write_index = 0;
    queue->size = 0;
    queue->flushing = false;
    
    // Wake up waiting threads
    pthread_cond_broadcast(&queue->not_empty);
    pthread_cond_broadcast(&queue->not_full);
    
    pthread_mutex_unlock(&queue->mutex);
    
    LOGD("Frame queue flushed");
}

void frame_queue_get_stats(FrameQueue* queue, FrameQueueStats* stats) {
    if (!queue || !stats) return;
    
    pthread_mutex_lock(&queue->mutex);
    
    stats->frames_queued = queue->size;
    stats->frames_produced = queue->stats.frames_produced;
    stats->frames_consumed = queue->stats.frames_consumed;
    stats->frames_dropped = queue->stats.frames_dropped;
    stats->is_full = (queue->size >= queue->capacity);
    stats->is_empty = (queue->size == 0);
    
    // Calculate buffered duration
    if (queue->size >= 2) {
        VideoFrame* first = queue->frames[queue->read_index];
        int last_index = (queue->write_index - 1 + queue->capacity) % queue->capacity;
        VideoFrame* last = queue->frames[last_index];
        if (first && last) {
            stats->buffer_duration_us = last->pts - first->pts;
        } else {
            stats->buffer_duration_us = 0;
        }
    } else {
        stats->buffer_duration_us = 0;
    }
    
    pthread_mutex_unlock(&queue->mutex);
}

bool frame_queue_is_empty(FrameQueue* queue) {
    if (!queue) return true;
    
    pthread_mutex_lock(&queue->mutex);
    bool empty = (queue->size == 0);
    pthread_mutex_unlock(&queue->mutex);
    
    return empty;
}

bool frame_queue_is_full(FrameQueue* queue) {
    if (!queue) return false;
    
    pthread_mutex_lock(&queue->mutex);
    bool full = (queue->size >= queue->capacity);
    pthread_mutex_unlock(&queue->mutex);
    
    return full;
}

int frame_queue_size(FrameQueue* queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    int size = queue->size;
    pthread_mutex_unlock(&queue->mutex);
    
    return size;
}

void frame_queue_set_audio_clock(FrameQueue* queue, int64_t audio_pts) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->clock_mutex);
    queue->audio_clock = audio_pts;
    pthread_mutex_unlock(&queue->clock_mutex);
}

int64_t frame_queue_get_audio_clock(FrameQueue* queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->clock_mutex);
    int64_t pts = queue->audio_clock;
    pthread_mutex_unlock(&queue->clock_mutex);
    
    return pts;
}

int64_t frame_queue_calc_delay(FrameQueue* queue, int64_t video_pts) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->clock_mutex);
    int64_t audio_pts = queue->audio_clock;
    pthread_mutex_unlock(&queue->clock_mutex);
    
    // Calculate delay: positive = wait, negative = behind (drop frame)
    int64_t delay = video_pts - audio_pts;
    
    // If behind by more than 100ms, drop frame
    if (queue->config.enable_frame_drop && delay < -100000) {
        pthread_mutex_lock(&queue->mutex);
        queue->stats.frames_dropped++;
        pthread_mutex_unlock(&queue->mutex);
        return delay;  // Negative delay signals drop
    }
    
    // If ahead by more than 500ms, wait
    if (delay > 500000) {
        delay = 500000;  // Cap delay
    }
    
    return delay;
}
