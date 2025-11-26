/*
 * Android MediaExtractor C++ Wrapper for fijkplayer
 * Replaces FFmpeg demuxer to avoid MP4 corruption issues
 */

#ifndef FIJKPLAYER_MEDIA_EXTRACTOR_WRAPPER_H
#define FIJKPLAYER_MEDIA_EXTRACTOR_WRAPPER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration */
typedef struct MediaExtractorWrapper MediaExtractorWrapper;

/* Track info structure */
typedef struct {
    int32_t track_index;
    const char* mime_type;
    int32_t width;
    int32_t height;
    int32_t sample_rate;
    int32_t channel_count;
    int64_t duration_us;
    int32_t max_input_size;
    uint8_t* csd_data;      // Codec-specific data
    size_t csd_size;
} MediaTrackInfo;

/* Sample data structure */
typedef struct {
    uint8_t* data;
    size_t size;
    int64_t pts_us;         // Presentation timestamp in microseconds
    uint32_t flags;
    int32_t track_index;
} MediaSample;

/* Sample flags */
#define SAMPLE_FLAG_SYNC 1
#define SAMPLE_FLAG_ENCRYPTED 2

/* Create MediaExtractor */
MediaExtractorWrapper* media_extractor_create(void);

/* Set data source (file path or URL) */
int media_extractor_set_data_source(MediaExtractorWrapper* extractor, const char* path);

/* Get track count */
int media_extractor_get_track_count(MediaExtractorWrapper* extractor);

/* Get track info by index */
int media_extractor_get_track_info(MediaExtractorWrapper* extractor, int track_index, MediaTrackInfo* info);

/* Select track for reading */
int media_extractor_select_track(MediaExtractorWrapper* extractor, int track_index);

/* Unselect track */
int media_extractor_unselect_track(MediaExtractorWrapper* extractor, int track_index);

/* Read next sample */
int media_extractor_read_sample(MediaExtractorWrapper* extractor, MediaSample* sample);

/* Advance to next sample */
int media_extractor_advance(MediaExtractorWrapper* extractor);

/* Seek to time */
int media_extractor_seek_to(MediaExtractorWrapper* extractor, int64_t time_us, int mode);

/* Seek modes */
#define SEEK_MODE_PREVIOUS_SYNC 0
#define SEEK_MODE_NEXT_SYNC 1
#define SEEK_MODE_CLOSEST_SYNC 2

/* Get current sample time */
int64_t media_extractor_get_sample_time(MediaExtractorWrapper* extractor);

/* Get current sample flags */
uint32_t media_extractor_get_sample_flags(MediaExtractorWrapper* extractor);

/* Get current sample track index */
int32_t media_extractor_get_sample_track_index(MediaExtractorWrapper* extractor);

/* Free sample data */
void media_extractor_free_sample(MediaSample* sample);

/* Free track info */
void media_extractor_free_track_info(MediaTrackInfo* info);

/* Release extractor */
void media_extractor_release(MediaExtractorWrapper* extractor);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_MEDIA_EXTRACTOR_WRAPPER_H
