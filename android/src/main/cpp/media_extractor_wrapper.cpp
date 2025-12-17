/*
 * Android MediaExtractor C++ Wrapper Implementation
 */

#include "media_extractor_wrapper.h"
#include <media/NdkMediaExtractor.h>
#include <media/NdkMediaFormat.h>
#include <android/log.h>
#include <stdlib.h>
#include <string.h>

#define LOG_TAG "MediaExtractorWrapper"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)

struct MediaExtractorWrapper {
    AMediaExtractor* extractor;
    uint8_t* sample_buffer;
    size_t sample_buffer_size;
};

MediaExtractorWrapper* media_extractor_create(void) {
    MediaExtractorWrapper* wrapper = (MediaExtractorWrapper*)calloc(1, sizeof(MediaExtractorWrapper));
    if (!wrapper) {
        LOGE("Failed to allocate wrapper");
        return NULL;
    }
    
    wrapper->extractor = AMediaExtractor_new();
    if (!wrapper->extractor) {
        LOGE("Failed to create AMediaExtractor");
        free(wrapper);
        return NULL;
    }
    
    // Allocate initial sample buffer (1MB)
    wrapper->sample_buffer_size = 1024 * 1024;
    wrapper->sample_buffer = (uint8_t*)malloc(wrapper->sample_buffer_size);
    if (!wrapper->sample_buffer) {
        LOGE("Failed to allocate sample buffer");
        AMediaExtractor_delete(wrapper->extractor);
        free(wrapper);
        return NULL;
    }
    
    LOGI("MediaExtractor created successfully");
    return wrapper;
}

int media_extractor_set_data_source(MediaExtractorWrapper* wrapper, const char* path) {
    if (!wrapper || !wrapper->extractor || !path) {
        LOGE("Invalid parameters");
        return -1;
    }
    
    LOGI("Setting data source: %s", path);
    
    media_status_t status;
    
    // Check if it's a file path or URL
    if (strncmp(path, "http://", 7) == 0 || strncmp(path, "https://", 8) == 0) {
        // Network URL
        status = AMediaExtractor_setDataSource(wrapper->extractor, path);
    } else if (strncmp(path, "file://", 7) == 0) {
        // File URI - remove file:// prefix
        status = AMediaExtractor_setDataSource(wrapper->extractor, path + 7);
    } else {
        // Direct file path
        status = AMediaExtractor_setDataSource(wrapper->extractor, path);
    }
    
    if (status != AMEDIA_OK) {
        LOGE("Failed to set data source: %d", status);
        return -1;
    }
    
    LOGI("Data source set successfully");
    return 0;
}

int media_extractor_get_track_count(MediaExtractorWrapper* wrapper) {
    if (!wrapper || !wrapper->extractor) {
        return 0;
    }
    
    size_t count = AMediaExtractor_getTrackCount(wrapper->extractor);
    LOGI("Track count: %zu", count);
    return (int)count;
}

int media_extractor_get_track_info(MediaExtractorWrapper* wrapper, int track_index, MediaTrackInfo* info) {
    if (!wrapper || !wrapper->extractor || !info) {
        return -1;
    }
    
    AMediaFormat* format = AMediaExtractor_getTrackFormat(wrapper->extractor, (size_t)track_index);
    if (!format) {
        LOGE("Failed to get track format for track %d", track_index);
        return -1;
    }
    
    memset(info, 0, sizeof(MediaTrackInfo));
    info->track_index = track_index;
    
    // Get MIME type
    const char* mime = NULL;
    if (AMediaFormat_getString(format, AMEDIAFORMAT_KEY_MIME, &mime)) {
        info->mime_type = strdup(mime);
        LOGD("Track %d MIME: %s", track_index, mime);
    }
    
    // Get video dimensions
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_WIDTH, &info->width);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_HEIGHT, &info->height);
    
    // Get audio info
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_SAMPLE_RATE, &info->sample_rate);
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_CHANNEL_COUNT, &info->channel_count);
    
    // Get duration
    AMediaFormat_getInt64(format, AMEDIAFORMAT_KEY_DURATION, &info->duration_us);
    
    // Get max input size
    AMediaFormat_getInt32(format, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, &info->max_input_size);
    
    // Get codec-specific data (CSD)
    void* csd_data = NULL;
    size_t csd_size = 0;
    if (AMediaFormat_getBuffer(format, "csd-0", &csd_data, &csd_size)) {
        info->csd_data = (uint8_t*)malloc(csd_size);
        if (info->csd_data) {
            memcpy(info->csd_data, csd_data, csd_size);
            info->csd_size = csd_size;
            LOGD("Track %d CSD size: %zu", track_index, csd_size);
        }
    }
    
    LOGI("Track %d info: %dx%d, duration=%lld us", 
         track_index, info->width, info->height, (long long)info->duration_us);
    
    AMediaFormat_delete(format);
    return 0;
}

int media_extractor_select_track(MediaExtractorWrapper* wrapper, int track_index) {
    if (!wrapper || !wrapper->extractor) {
        return -1;
    }
    
    media_status_t status = AMediaExtractor_selectTrack(wrapper->extractor, (size_t)track_index);
    if (status != AMEDIA_OK) {
        LOGE("Failed to select track %d: %d", track_index, status);
        return -1;
    }
    
    LOGI("Track %d selected", track_index);
    return 0;
}

int media_extractor_unselect_track(MediaExtractorWrapper* wrapper, int track_index) {
    if (!wrapper || !wrapper->extractor) {
        return -1;
    }
    
    media_status_t status = AMediaExtractor_unselectTrack(wrapper->extractor, (size_t)track_index);
    if (status != AMEDIA_OK) {
        LOGE("Failed to unselect track %d: %d", track_index, status);
        return -1;
    }
    
    LOGI("Track %d unselected", track_index);
    return 0;
}

int media_extractor_read_sample(MediaExtractorWrapper* wrapper, MediaSample* sample) {
    if (!wrapper || !wrapper->extractor || !sample) {
        return -1;
    }
    
    // Get sample size
    ssize_t sample_size = AMediaExtractor_getSampleSize(wrapper->extractor);
    if (sample_size < 0) {
        // End of stream
        return -1;
    }
    
    // Grow buffer if needed
    if ((size_t)sample_size > wrapper->sample_buffer_size) {
        size_t new_size = (size_t)sample_size + 1024;
        uint8_t* new_buffer = (uint8_t*)realloc(wrapper->sample_buffer, new_size);
        if (!new_buffer) {
            LOGE("Failed to grow sample buffer to %zu bytes", new_size);
            return -1;
        }
        wrapper->sample_buffer = new_buffer;
        wrapper->sample_buffer_size = new_size;
        LOGD("Grew sample buffer to %zu bytes", new_size);
    }
    
    // Read sample data
    ssize_t read_size = AMediaExtractor_readSampleData(wrapper->extractor, 
                                                         wrapper->sample_buffer, 
                                                         wrapper->sample_buffer_size);
    if (read_size < 0) {
        LOGE("Failed to read sample data");
        return -1;
    }
    
    // Allocate and copy sample data
    sample->data = (uint8_t*)malloc((size_t)read_size);
    if (!sample->data) {
        LOGE("Failed to allocate sample data");
        return -1;
    }
    memcpy(sample->data, wrapper->sample_buffer, (size_t)read_size);
    sample->size = (size_t)read_size;
    
    // Get sample metadata
    sample->pts_us = AMediaExtractor_getSampleTime(wrapper->extractor);
    sample->flags = AMediaExtractor_getSampleFlags(wrapper->extractor);
    sample->track_index = (int32_t)AMediaExtractor_getSampleTrackIndex(wrapper->extractor);
    
    return 0;
}

int media_extractor_advance(MediaExtractorWrapper* wrapper) {
    if (!wrapper || !wrapper->extractor) {
        return -1;
    }
    
    bool result = AMediaExtractor_advance(wrapper->extractor);
    return result ? 0 : -1;
}

int media_extractor_seek_to(MediaExtractorWrapper* wrapper, int64_t time_us, int mode) {
    if (!wrapper || !wrapper->extractor) {
        return -1;
    }
    
    // Convert mode
    int32_t seek_mode;
    switch (mode) {
        case SEEK_MODE_PREVIOUS_SYNC:
            seek_mode = AMEDIAEXTRACTOR_SEEK_PREVIOUS_SYNC;
            break;
        case SEEK_MODE_NEXT_SYNC:
            seek_mode = AMEDIAEXTRACTOR_SEEK_NEXT_SYNC;
            break;
        case SEEK_MODE_CLOSEST_SYNC:
            seek_mode = AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC;
            break;
        default:
            seek_mode = AMEDIAEXTRACTOR_SEEK_CLOSEST_SYNC;
            break;
    }
    
    media_status_t status = AMediaExtractor_seekTo(wrapper->extractor, time_us, seek_mode);
    if (status != AMEDIA_OK) {
        LOGE("Failed to seek to %lld us: %d", (long long)time_us, status);
        return -1;
    }
    
    LOGI("Seeked to %lld us", (long long)time_us);
    return 0;
}

int64_t media_extractor_get_sample_time(MediaExtractorWrapper* wrapper) {
    if (!wrapper || !wrapper->extractor) {
        return -1;
    }
    
    return AMediaExtractor_getSampleTime(wrapper->extractor);
}

uint32_t media_extractor_get_sample_flags(MediaExtractorWrapper* wrapper) {
    if (!wrapper || !wrapper->extractor) {
        return 0;
    }
    
    return AMediaExtractor_getSampleFlags(wrapper->extractor);
}

int32_t media_extractor_get_sample_track_index(MediaExtractorWrapper* wrapper) {
    if (!wrapper || !wrapper->extractor) {
        return -1;
    }
    
    return (int32_t)AMediaExtractor_getSampleTrackIndex(wrapper->extractor);
}

void media_extractor_free_sample(MediaSample* sample) {
    if (sample && sample->data) {
        free(sample->data);
        sample->data = NULL;
        sample->size = 0;
    }
}

void media_extractor_free_track_info(MediaTrackInfo* info) {
    if (info) {
        if (info->mime_type) {
            free((void*)info->mime_type);
            info->mime_type = NULL;
        }
        if (info->csd_data) {
            free(info->csd_data);
            info->csd_data = NULL;
            info->csd_size = 0;
        }
    }
}

void media_extractor_release(MediaExtractorWrapper* wrapper) {
    if (wrapper) {
        if (wrapper->extractor) {
            AMediaExtractor_delete(wrapper->extractor);
            wrapper->extractor = NULL;
        }
        if (wrapper->sample_buffer) {
            free(wrapper->sample_buffer);
            wrapper->sample_buffer = NULL;
        }
        free(wrapper);
        LOGI("MediaExtractor released");
    }
}
