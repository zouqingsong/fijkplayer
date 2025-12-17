/*
 * Audio Queue Implementation
 */

#include "audio_queue.h"
#include <stdlib.h>
#include <string.h>
#include <android/log.h>

#define LOG_TAG "AudioQueue"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/* Internal audio queue structure */
struct AudioQueue {
    // Configuration
    int sample_rate;
    int channels;
    AudioQueueFormat format;
    int bytes_per_sample;      // Bytes per sample (2 for S16, 4 for F32)
    int bytes_per_frame;       // Bytes per frame (channels * bytes_per_sample)
    
    // Circular buffer
    uint8_t* buffer;
    int capacity_samples;      // Total capacity in samples per channel
    int capacity_bytes;        // Total capacity in bytes
    
    // Read/write positions (in samples)
    int read_pos;
    int write_pos;
    int available_samples;     // Samples available per channel
    
    // PTS tracking
    int64_t* pts_buffer;       // PTS for each frame in buffer
    int64_t current_pts;       // PTS of current read position
    
    // Thread safety
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_full;
    pthread_cond_t cond_not_empty;
    
    bool abort_request;
};

/* Create audio queue */
AudioQueue* audio_queue_create(const AudioQueueConfig* config) {
    if (!config || config->sample_rate <= 0 || config->channels <= 0) {
        LOGE("Invalid audio queue config");
        return NULL;
    }
    
    AudioQueue* queue = (AudioQueue*)calloc(1, sizeof(AudioQueue));
    if (!queue) {
        LOGE("Failed to allocate audio queue");
        return NULL;
    }
    
    queue->sample_rate = config->sample_rate;
    queue->channels = config->channels;
    queue->format = config->format;
    queue->bytes_per_sample = (config->format == AUDIO_QUEUE_FORMAT_S16) ? 2 : 4;
    queue->bytes_per_frame = queue->channels * queue->bytes_per_sample;
    
    // Calculate capacity
    int capacity_ms = config->capacity_ms > 0 ? config->capacity_ms : 500;  // Default 500ms
    queue->capacity_samples = (config->sample_rate * capacity_ms) / 1000;
    queue->capacity_bytes = queue->capacity_samples * queue->bytes_per_frame;
    
    // Allocate buffer
    queue->buffer = (uint8_t*)malloc(queue->capacity_bytes);
    if (!queue->buffer) {
        LOGE("Failed to allocate audio buffer: %d bytes", queue->capacity_bytes);
        free(queue);
        return NULL;
    }
    
    // Allocate PTS buffer (one PTS per 1024 samples - typical AAC frame size)
    int pts_capacity = (queue->capacity_samples / 1024) + 1;
    queue->pts_buffer = (int64_t*)malloc(pts_capacity * sizeof(int64_t));
    if (!queue->pts_buffer) {
        LOGE("Failed to allocate PTS buffer");
        free(queue->buffer);
        free(queue);
        return NULL;
    }
    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond_not_full, NULL);
    pthread_cond_init(&queue->cond_not_empty, NULL);
    
    LOGI("Audio queue created: %d Hz, %d ch, %d ms capacity (%d samples)",
         queue->sample_rate, queue->channels, capacity_ms, queue->capacity_samples);
    
    return queue;
}

/* Destroy audio queue */
void audio_queue_destroy(AudioQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->abort_request = true;
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_full);
    pthread_cond_destroy(&queue->cond_not_empty);
    
    free(queue->buffer);
    free(queue->pts_buffer);
    free(queue);
}

/* Push PCM samples to queue */
int audio_queue_push(AudioQueue* queue, const void* data, int samples, int64_t pts) {
    if (!queue || !data || samples <= 0) return -2;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->abort_request) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // Wait until queue has space (BLOCKING like ijkplayer's frame_queue_peek_writable)
    while (queue->available_samples + samples > queue->capacity_samples && !queue->abort_request) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }
    
    if (queue->abort_request) {
        pthread_mutex_unlock(&queue->mutex);
        return -1;
    }
    
    // Copy data to circular buffer
    int bytes_to_write = samples * queue->bytes_per_frame;
    int write_offset = queue->write_pos * queue->bytes_per_frame;
    
    // Check if write wraps around
    int bytes_to_end = queue->capacity_bytes - write_offset;
    if (bytes_to_write <= bytes_to_end) {
        // No wrap - single copy
        memcpy(queue->buffer + write_offset, data, bytes_to_write);
    } else {
        // Wrap - two copies
        memcpy(queue->buffer + write_offset, data, bytes_to_end);
        memcpy(queue->buffer, (uint8_t*)data + bytes_to_end, bytes_to_write - bytes_to_end);
    }
    
    // Update write position
    queue->write_pos = (queue->write_pos + samples) % queue->capacity_samples;
    queue->available_samples += samples;
    
    // Store PTS for this chunk (simplified - store one PTS per push)
    if (queue->available_samples <= samples) {
        queue->current_pts = pts;
    }
    
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

/* Pop PCM samples from queue */
int audio_queue_pop(AudioQueue* queue, void* buffer, int max_samples, int64_t* pts) {
    if (!queue || !buffer || max_samples <= 0) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->abort_request) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    
    // Get available samples (non-blocking)
    int samples_to_read = (max_samples < queue->available_samples) ? max_samples : queue->available_samples;
    
    if (samples_to_read <= 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;
    }
    
    // Copy data from circular buffer
    int bytes_to_read = samples_to_read * queue->bytes_per_frame;
    int read_offset = queue->read_pos * queue->bytes_per_frame;
    
    // Check if read wraps around
    int bytes_to_end = queue->capacity_bytes - read_offset;
    if (bytes_to_read <= bytes_to_end) {
        // No wrap - single copy
        memcpy(buffer, queue->buffer + read_offset, bytes_to_read);
    } else {
        // Wrap - two copies
        memcpy(buffer, queue->buffer + read_offset, bytes_to_end);
        memcpy((uint8_t*)buffer + bytes_to_end, queue->buffer, bytes_to_read - bytes_to_end);
    }
    
    // Output PTS
    if (pts) {
        *pts = queue->current_pts;
    }
    
    // Update read position
    queue->read_pos = (queue->read_pos + samples_to_read) % queue->capacity_samples;
    queue->available_samples -= samples_to_read;
    
    // Update PTS for next read (simplified - increment based on samples)
    if (queue->sample_rate > 0) {
        queue->current_pts += (int64_t)samples_to_read * 1000000LL / queue->sample_rate;
    }
    
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return samples_to_read;
}

/* Get available samples */
int audio_queue_get_available(AudioQueue* queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    int available = queue->available_samples;
    pthread_mutex_unlock(&queue->mutex);
    
    return available;
}

/* Get queue fill level */
int audio_queue_get_level(AudioQueue* queue) {
    if (!queue || queue->capacity_samples == 0) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    int level = (queue->available_samples * 100) / queue->capacity_samples;
    pthread_mutex_unlock(&queue->mutex);
    
    return level;
}

/* Flush queue */
void audio_queue_flush(AudioQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->read_pos = 0;
    queue->write_pos = 0;
    queue->available_samples = 0;
    queue->current_pts = 0;
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    LOGI("Audio queue flushed");
}

/* Check if empty */
bool audio_queue_is_empty(AudioQueue* queue) {
    if (!queue) return true;
    
    pthread_mutex_lock(&queue->mutex);
    bool empty = (queue->available_samples == 0);
    pthread_mutex_unlock(&queue->mutex);
    
    return empty;
}

/* Check if full */
bool audio_queue_is_full(AudioQueue* queue) {
    if (!queue) return false;
    
    pthread_mutex_lock(&queue->mutex);
    bool full = (queue->available_samples >= queue->capacity_samples);
    pthread_mutex_unlock(&queue->mutex);
    
    return full;
}
