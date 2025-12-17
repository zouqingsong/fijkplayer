#include "async_io_protocol.h"
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/error.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

// iOS logging
#ifdef __APPLE__
#include <os/log.h>
#define TAG "AsyncIO"
#define LOGI(...) os_log(OS_LOG_DEFAULT, __VA_ARGS__)
#define LOGD(...) os_log_debug(OS_LOG_DEFAULT, __VA_ARGS__)
#define LOGW(...) os_log(OS_LOG_DEFAULT, __VA_ARGS__)
#define LOGE(...) os_log_error(OS_LOG_DEFAULT, __VA_ARGS__)
#else
#include <android/log.h>
#define TAG "AsyncIO"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
#endif

// Background I/O thread function
static void* io_thread_func(void* arg) {
    AsyncIOContext* ctx = (AsyncIOContext*)arg;
    AVIOContext* inner = (AVIOContext*)ctx->inner_protocol;
    
    LOGI("I/O thread started");
    
    while (!ctx->abort_request) {
        pthread_mutex_lock(&ctx->mutex);
        
        // Pause during seek operations
        while (ctx->seek_request && !ctx->abort_request) {
            pthread_cond_wait(&ctx->cond_write, &ctx->mutex);
        }
        
        if (ctx->abort_request) {
            pthread_mutex_unlock(&ctx->mutex);
            break;
        }
        
        // Calculate available space in ring buffer
        size_t available_space;
        if (ctx->write_pos >= ctx->read_pos) {
            available_space = ctx->buffer_size - (ctx->write_pos - ctx->read_pos) - 1;
        } else {
            available_space = ctx->read_pos - ctx->write_pos - 1;
        }
        
        // If buffer is full, wait for reader to consume
        if (available_space < 4096 && !ctx->eof_reached) {
            pthread_cond_wait(&ctx->cond_write, &ctx->mutex);
            pthread_mutex_unlock(&ctx->mutex);
            continue;
        }
        
        pthread_mutex_unlock(&ctx->mutex);
        
        // Read from underlying protocol (outside mutex to avoid blocking)
        uint8_t temp_buf[32768]; // 32KB chunks
        int read_size = (int)(available_space < sizeof(temp_buf) ? available_space : sizeof(temp_buf));
        
        if (read_size > 0 && !ctx->eof_reached) {
            int ret = avio_read(inner, temp_buf, read_size);
            
            pthread_mutex_lock(&ctx->mutex);
            
            if (ret > 0) {
                // Write to ring buffer
                for (int i = 0; i < ret; i++) {
                    ctx->ring_buffer[ctx->write_pos] = temp_buf[i];
                    ctx->write_pos = (ctx->write_pos + 1) % ctx->buffer_size;
                }
                ctx->bytes_written += ret;
                
                // Signal readers that data is available
                pthread_cond_broadcast(&ctx->cond_read);
                
            } else if (ret == AVERROR_EOF) {
                ctx->eof_reached = true;
                pthread_cond_broadcast(&ctx->cond_read);
                LOGI("EOF reached in I/O thread");
            } else {
                ctx->io_error = ret;
                pthread_cond_broadcast(&ctx->cond_read);
                LOGE("I/O error: %d", ret);
            }
            
            pthread_mutex_unlock(&ctx->mutex);
        } else {
            usleep(10000); // 10ms
        }
    }
    
    LOGI("I/O thread exiting");
    return NULL;
}

AsyncIOContext* async_io_open(const char* inner_url) {
    LOGI("Opening async I/O for URL: %s", inner_url);
    
    AsyncIOContext* ctx = (AsyncIOContext*)calloc(1, sizeof(AsyncIOContext));
    if (!ctx) {
        LOGE("Failed to allocate AsyncIOContext");
        return NULL;
    }
    
    // Allocate ring buffer
    ctx->ring_buffer = (uint8_t*)malloc(ASYNC_BUFFER_SIZE);
    if (!ctx->ring_buffer) {
        LOGE("Failed to allocate ring buffer");
        free(ctx);
        return NULL;
    }
    ctx->buffer_size = ASYNC_BUFFER_SIZE;
    ctx->read_pos = 0;
    ctx->write_pos = 0;
    
    // Open underlying protocol
    AVIOContext* inner = NULL;
    int ret = avio_open2(&inner, inner_url, AVIO_FLAG_READ, NULL, NULL);
    if (ret < 0) {
        char err_buf[128];
        av_strerror(ret, err_buf, sizeof(err_buf));
        LOGE("Failed to open inner URL: %s", err_buf);
        free(ctx->ring_buffer);
        free(ctx);
        return NULL;
    }
    
    ctx->inner_protocol = inner;
    
    // Initialize synchronization
    pthread_mutex_init(&ctx->mutex, NULL);
    pthread_cond_init(&ctx->cond_read, NULL);
    pthread_cond_init(&ctx->cond_write, NULL);
    
    // Start background I/O thread
    ctx->abort_request = false;
    ctx->seek_request = false;
    ctx->eof_reached = false;
    ctx->io_error = 0;
    
    if (pthread_create(&ctx->io_thread, NULL, io_thread_func, ctx) != 0) {
        LOGE("Failed to create I/O thread");
        avio_close(inner);
        free(ctx->ring_buffer);
        free(ctx);
        return NULL;
    }
    
    LOGI("✅ Async I/O context created successfully");
    return ctx;
}

int async_io_read(AsyncIOContext* ctx, uint8_t* buf, int size) {
    if (!ctx || !buf || size <= 0) {
        return AVERROR(EINVAL);
    }
    
    pthread_mutex_lock(&ctx->mutex);
    
    // Wait for data to be available
    while (ctx->read_pos == ctx->write_pos && !ctx->eof_reached && !ctx->io_error && !ctx->abort_request) {
        pthread_cond_wait(&ctx->cond_read, &ctx->mutex);
    }
    
    if (ctx->abort_request) {
        pthread_mutex_unlock(&ctx->mutex);
        return AVERROR_EXIT;
    }
    
    if (ctx->io_error) {
        int err = ctx->io_error;
        pthread_mutex_unlock(&ctx->mutex);
        return err;
    }
    
    // Calculate available data
    size_t available;
    if (ctx->write_pos >= ctx->read_pos) {
        available = ctx->write_pos - ctx->read_pos;
    } else {
        available = ctx->buffer_size - ctx->read_pos + ctx->write_pos;
    }
    
    if (available == 0 && ctx->eof_reached) {
        pthread_mutex_unlock(&ctx->mutex);
        return AVERROR_EOF;
    }
    
    // Read from ring buffer
    int to_read = (int)(size < available ? size : available);
    for (int i = 0; i < to_read; i++) {
        buf[i] = ctx->ring_buffer[ctx->read_pos];
        ctx->read_pos = (ctx->read_pos + 1) % ctx->buffer_size;
    }
    
    ctx->bytes_read += to_read;
    
    // Signal writer that space is available
    pthread_cond_signal(&ctx->cond_write);
    
    pthread_mutex_unlock(&ctx->mutex);
    
    return to_read;
}

int64_t async_io_seek(AsyncIOContext* ctx, int64_t offset, int whence) {
    if (!ctx || !ctx->inner_protocol) {
        return AVERROR(EINVAL);
    }
    
    AVIOContext* inner = (AVIOContext*)ctx->inner_protocol;
    
    // Handle special whence values (don't need mutex for size query)
    if (whence == AVSEEK_SIZE) {
        // Return file size
        int64_t size = avio_size(inner);
        LOGD("Seek AVSEEK_SIZE returning: %lld", (long long)size);
        return size;
    }
    
    LOGI("Seeking to offset %lld, whence %d", (long long)offset, whence);
    
    pthread_mutex_lock(&ctx->mutex);
    
    // If another seek is in progress, wait for it to complete
    // FFmpeg's demuxer often makes multiple rapid seeks during initialization
    while (ctx->seek_request) {
        LOGW("Another seek in progress, waiting...");
        pthread_cond_wait(&ctx->cond_write, &ctx->mutex);
    }
    
    // Signal I/O thread to pause for seek (NOT abort)
    ctx->seek_request = true;
    pthread_cond_broadcast(&ctx->cond_read);
    
    // Release mutex briefly to let I/O thread enter pause state
    pthread_mutex_unlock(&ctx->mutex);
    usleep(5000); // 5ms should be enough for I/O thread to pause
    pthread_mutex_lock(&ctx->mutex);
    
    // Perform seek on underlying protocol
    int64_t ret = avio_seek(inner, offset, whence);
    
    if (ret >= 0) {
        // Flush ring buffer
        ctx->read_pos = 0;
        ctx->write_pos = 0;
        ctx->eof_reached = false;
        ctx->io_error = 0;
        
        LOGI("✅ Seek successful, new position: %lld", (long long)ret);
    } else {
        char err_buf[128];
        av_strerror((int)ret, err_buf, sizeof(err_buf));
        LOGE("Seek failed: %s", err_buf);
    }
    
    // Resume I/O thread
    ctx->seek_request = false;
    pthread_cond_broadcast(&ctx->cond_read);
    pthread_cond_broadcast(&ctx->cond_write);
    
    pthread_mutex_unlock(&ctx->mutex);
    
    return ret;
}

void async_io_close(AsyncIOContext* ctx) {
    if (!ctx) return;
    
    LOGI("Closing async I/O context");
    
    // Signal thread to exit
    pthread_mutex_lock(&ctx->mutex);
    ctx->abort_request = true;
    pthread_cond_broadcast(&ctx->cond_read);
    pthread_cond_broadcast(&ctx->cond_write);
    pthread_mutex_unlock(&ctx->mutex);
    
    // Wait for I/O thread to finish
    pthread_join(ctx->io_thread, NULL);
    
    // Close underlying protocol
    if (ctx->inner_protocol) {
        avio_close((AVIOContext*)ctx->inner_protocol);
    }
    
    // Cleanup
    pthread_mutex_destroy(&ctx->mutex);
    pthread_cond_destroy(&ctx->cond_read);
    pthread_cond_destroy(&ctx->cond_write);
    free(ctx->ring_buffer);
    free(ctx);
    
    LOGI("✅ Async I/O context closed");
}
