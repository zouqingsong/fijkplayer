#ifndef ASYNC_IO_PROTOCOL_H
#define ASYNC_IO_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Async I/O Protocol - Inspired by ijkplayer's ijkasync.c
 * 
 * This creates a ring buffer between FFmpeg and actual I/O operations.
 * Prevents FFmpeg's internal buffer corruption by isolating I/O.
 */

// Ring buffer size (512KB should be enough for streaming)
#define ASYNC_BUFFER_SIZE (512 * 1024)

typedef struct AsyncIOContext {
    // Underlying protocol context (http, file, etc.)
    void* inner_protocol;
    
    // Ring buffer for async reading
    uint8_t* ring_buffer;
    size_t buffer_size;
    
    // Read/write positions in ring buffer
    size_t read_pos;
    size_t write_pos;
    
    // Background I/O thread
    pthread_t io_thread;
    pthread_mutex_t mutex;
    pthread_cond_t cond_read;   // Signal when data available
    pthread_cond_t cond_write;  // Signal when space available
    
    // State
    bool eof_reached;
    bool abort_request;  // True = exit thread
    bool seek_request;   // True = pause for seek
    int io_error;
    
    // Statistics
    int64_t bytes_read;
    int64_t bytes_written;
    
} AsyncIOContext;

/**
 * Initialize async I/O protocol wrapper
 * @param inner_url The actual URL to open (http://, file://, rtsp://, etc.)
 * @return AsyncIOContext or NULL on error
 */
AsyncIOContext* async_io_open(const char* inner_url);

/**
 * Read data from async buffer (called by FFmpeg)
 * This will block until data is available or EOF/error
 */
int async_io_read(AsyncIOContext* ctx, uint8_t* buf, int size);

/**
 * Seek in the stream (if supported)
 */
int64_t async_io_seek(AsyncIOContext* ctx, int64_t offset, int whence);

/**
 * Close and cleanup
 */
void async_io_close(AsyncIOContext* ctx);

#ifdef __cplusplus
}
#endif

#endif // ASYNC_IO_PROTOCOL_H
