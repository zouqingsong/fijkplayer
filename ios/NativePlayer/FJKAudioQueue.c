// MIT License - FijkPlayer iOS Audio Queue Implementation

#include "FJKAudioQueue.h"
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdio.h>

struct FJKAudioQueue {
    // Configuration
    int sample_rate;
    int channels;
    FJKAudioQueueFormat format;
    
    // Ring buffer for S16 stereo PCM
    int16_t* buffer;
    int capacity;      // Total capacity in samples (per channel)
    int read_pos;
    int write_pos;
    int available;     // Samples available for reading
    
    // PTS tracking
    int64_t* pts_buffer;  // PTS for each sample position
    
    // Thread safety
    pthread_mutex_t mutex;
    pthread_cond_t cond_not_empty;
    pthread_cond_t cond_not_full;
};

FJKAudioQueue* fjk_audio_queue_create(const FJKAudioQueueConfig* config) {
    if (!config || config->sample_rate <= 0 || config->channels <= 0) {
        return NULL;
    }
    
    FJKAudioQueue* queue = (FJKAudioQueue*)calloc(1, sizeof(FJKAudioQueue));
    if (!queue) return NULL;
    
    queue->sample_rate = config->sample_rate;
    queue->channels = config->channels;
    queue->format = config->format;
    
    // Calculate capacity: (sample_rate * capacity_ms / 1000) samples per channel
    queue->capacity = (config->sample_rate * config->capacity_ms) / 1000;
    
    // Allocate buffer (interleaved stereo S16)
    int buffer_size = queue->capacity * config->channels;
    queue->buffer = (int16_t*)calloc(buffer_size, sizeof(int16_t));
    queue->pts_buffer = (int64_t*)calloc(queue->capacity, sizeof(int64_t));
    
    if (!queue->buffer || !queue->pts_buffer) {
        free(queue->buffer);
        free(queue->pts_buffer);
        free(queue);
        return NULL;
    }
    
    queue->read_pos = 0;
    queue->write_pos = 0;
    queue->available = 0;
    
    pthread_mutex_init(&queue->mutex, NULL);
    pthread_cond_init(&queue->cond_not_empty, NULL);
    pthread_cond_init(&queue->cond_not_full, NULL);
    
    printf("[AudioQueue] Created: %d Hz, %d ch, capacity %d samples (%.1f ms)\n",
           queue->sample_rate, queue->channels, queue->capacity,
           (queue->capacity * 1000.0) / queue->sample_rate);
    
    return queue;
}

void fjk_audio_queue_destroy(FJKAudioQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_not_empty);
    pthread_cond_destroy(&queue->cond_not_full);
    
    free(queue->buffer);
    free(queue->pts_buffer);
    free(queue);
}

int fjk_audio_queue_push(FJKAudioQueue* queue, const int16_t* samples, 
                          int num_samples, int64_t pts) {
    if (!queue || !samples || num_samples <= 0) return -1;
    
    pthread_mutex_lock(&queue->mutex);
    
    // Wait if queue is full (blocking push for backpressure)
    while (queue->available + num_samples > queue->capacity) {
        pthread_cond_wait(&queue->cond_not_full, &queue->mutex);
    }
    
    // Copy samples (interleaved: L,R,L,R,...)
    // Buffer layout: [sample0_L, sample0_R, sample1_L, sample1_R, ...]
    int samples_per_channel = num_samples;
    
    for (int i = 0; i < samples_per_channel; i++) {
        int write_index = (queue->write_pos + i) % queue->capacity;
        for (int ch = 0; ch < queue->channels; ch++) {
            queue->buffer[write_index * queue->channels + ch] = samples[i * queue->channels + ch];
        }
        queue->pts_buffer[write_index] = pts;  // Store PTS for each sample
    }
    
    queue->write_pos = (queue->write_pos + samples_per_channel) % queue->capacity;
    queue->available += samples_per_channel;
    
    // Signal consumers
    pthread_cond_signal(&queue->cond_not_empty);
    pthread_mutex_unlock(&queue->mutex);
    
    return 0;
}

int fjk_audio_queue_pop(FJKAudioQueue* queue, int16_t* buffer, 
                        int buffer_size, int64_t* pts) {
    if (!queue || !buffer || buffer_size <= 0) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    
    if (queue->available == 0) {
        pthread_mutex_unlock(&queue->mutex);
        return 0;  // Non-blocking, return immediately if empty
    }
    
    // Read up to buffer_size samples (or available, whichever is smaller)
    int samples_to_read = (buffer_size < queue->available) ? buffer_size : queue->available;
    
    // Get PTS of first sample
    if (pts) {
        *pts = queue->pts_buffer[queue->read_pos];
    }
    
    // Copy samples (interleaved: L,R,L,R,...)
    for (int i = 0; i < samples_to_read; i++) {
        int read_index = (queue->read_pos + i) % queue->capacity;
        for (int ch = 0; ch < queue->channels; ch++) {
            buffer[i * queue->channels + ch] = queue->buffer[read_index * queue->channels + ch];
        }
    }
    
    queue->read_pos = (queue->read_pos + samples_to_read) % queue->capacity;
    queue->available -= samples_to_read;
    
    // Signal producers
    pthread_cond_signal(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
    
    return samples_to_read;
}

int fjk_audio_queue_size(FJKAudioQueue* queue) {
    if (!queue) return 0;
    
    pthread_mutex_lock(&queue->mutex);
    int size = queue->available;
    pthread_mutex_unlock(&queue->mutex);
    
    return size;
}

void fjk_audio_queue_flush(FJKAudioQueue* queue) {
    if (!queue) return;
    
    pthread_mutex_lock(&queue->mutex);
    queue->read_pos = 0;
    queue->write_pos = 0;
    queue->available = 0;
    memset(queue->buffer, 0, queue->capacity * queue->channels * sizeof(int16_t));
    
    // Wake up all blocked threads
    pthread_cond_broadcast(&queue->cond_not_empty);
    pthread_cond_broadcast(&queue->cond_not_full);
    pthread_mutex_unlock(&queue->mutex);
}
