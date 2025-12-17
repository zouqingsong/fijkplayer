/*
 * Audio Queue for fijkplayer
 * 
 * Thread-safe circular buffer for decoded PCM audio data.
 * Used to buffer audio between decoder thread and playback thread.
 */

#ifndef FIJKPLAYER_AUDIO_QUEUE_H
#define FIJKPLAYER_AUDIO_QUEUE_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Audio sample format */
typedef enum {
    AUDIO_QUEUE_FORMAT_S16 = 0,  // Signed 16-bit PCM
    AUDIO_QUEUE_FORMAT_F32       // 32-bit float PCM
} AudioQueueFormat;

/* Audio queue configuration */
typedef struct {
    int sample_rate;           // Sample rate in Hz
    int channels;              // Number of channels (1=mono, 2=stereo)
    AudioQueueFormat format;   // Sample format
    int capacity_ms;           // Queue capacity in milliseconds (default: 500ms)
} AudioQueueConfig;

/* Audio queue structure (opaque) */
typedef struct AudioQueue AudioQueue;

/*
 * Create audio queue
 * Returns NULL on failure
 */
AudioQueue* audio_queue_create(const AudioQueueConfig* config);

/*
 * Destroy audio queue
 */
void audio_queue_destroy(AudioQueue* queue);

/*
 * Push PCM samples to queue
 * 
 * data: PCM data (S16 interleaved: L,R,L,R... or F32 interleaved)
 * samples: Number of samples per channel
 * pts: Presentation timestamp in microseconds
 * 
 * Returns:
 *   0: Success
 *  -1: Queue full (non-blocking mode)
 *  -2: Invalid parameters
 */
int audio_queue_push(AudioQueue* queue, const void* data, int samples, int64_t pts);

/*
 * Pop PCM samples from queue
 * 
 * buffer: Output buffer for PCM data
 * max_samples: Maximum samples per channel to read
 * pts: Output PTS of first sample (can be NULL)
 * 
 * Returns: Number of samples per channel read (0 if queue empty)
 */
int audio_queue_pop(AudioQueue* queue, void* buffer, int max_samples, int64_t* pts);

/*
 * Get number of samples available in queue
 * Returns: Samples per channel available
 */
int audio_queue_get_available(AudioQueue* queue);

/*
 * Get queue fill level as percentage (0-100)
 */
int audio_queue_get_level(AudioQueue* queue);

/*
 * Flush queue (clear all data)
 */
void audio_queue_flush(AudioQueue* queue);

/*
 * Check if queue is empty
 */
bool audio_queue_is_empty(AudioQueue* queue);

/*
 * Check if queue is full
 */
bool audio_queue_is_full(AudioQueue* queue);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_AUDIO_QUEUE_H
