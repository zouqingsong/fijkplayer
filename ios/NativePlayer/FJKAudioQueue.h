// MIT License - FijkPlayer iOS Audio Queue
// Thread-safe circular buffer for PCM audio samples

#ifndef FJK_AUDIO_QUEUE_H
#define FJK_AUDIO_QUEUE_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
    FJK_AUDIO_QUEUE_FORMAT_S16 = 0,  // Signed 16-bit PCM
    FJK_AUDIO_QUEUE_FORMAT_F32 = 1   // Float 32-bit PCM
} FJKAudioQueueFormat;

typedef struct {
    int sample_rate;
    int channels;
    FJKAudioQueueFormat format;
    int capacity_ms;  // Buffer capacity in milliseconds
} FJKAudioQueueConfig;

typedef struct FJKAudioQueue FJKAudioQueue;

/**
 * Create audio queue with specified configuration
 */
FJKAudioQueue* fjk_audio_queue_create(const FJKAudioQueueConfig* config);

/**
 * Destroy audio queue and free resources
 */
void fjk_audio_queue_destroy(FJKAudioQueue* queue);

/**
 * Push PCM samples to queue (producer side)
 * Blocks if queue is full (backpressure)
 * Returns 0 on success, negative on error
 */
int fjk_audio_queue_push(FJKAudioQueue* queue, const int16_t* samples, 
                          int num_samples, int64_t pts);

/**
 * Pop PCM samples from queue (consumer side)
 * Non-blocking - returns immediately with available samples
 * Returns number of samples read, 0 if empty
 */
int fjk_audio_queue_pop(FJKAudioQueue* queue, int16_t* buffer, 
                        int buffer_size, int64_t* pts);

/**
 * Get number of samples currently in queue
 */
int fjk_audio_queue_size(FJKAudioQueue* queue);

/**
 * Flush all pending samples
 */
void fjk_audio_queue_flush(FJKAudioQueue* queue);

#endif // FJK_AUDIO_QUEUE_H
