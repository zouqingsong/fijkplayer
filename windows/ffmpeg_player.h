// ffmpeg_player.h
// Platform-independent FFmpeg software player for Windows/Linux
// Uses FFmpeg for demuxing, decoding, and pixel format conversion

#ifndef FFMPEG_PLAYER_H
#define FFMPEG_PLAYER_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct FFmpegPlayer FFmpegPlayer;

// How long a packet may be held back for, by default, and at either end of the
// scale. The live end is where MicVisionX's real-time mode sits; the default is
// what this backend used before the choice existed.
#define FFPLAYER_DEFAULT_BUFFER_MS 500
#define FFPLAYER_SMOOTH_BUFFER_MS 1500
#define FFPLAYER_MIN_BUFFER_MS 50
#define FFPLAYER_MAX_BUFFER_MS 10000

// At or below this the backend plays live: no demuxer reordering, no read-ahead,
// and frames emitted as soon as they are decoded.
#define FFPLAYER_LOW_LATENCY_MS 200

// Player states (matching Dart FijkState)
typedef enum {
    FFPLAYER_STATE_IDLE = 0,
    FFPLAYER_STATE_INITIALIZED = 1,
    FFPLAYER_STATE_PREPARING = 2,
    FFPLAYER_STATE_PREPARED = 3,
    FFPLAYER_STATE_PLAYING = 4,
    FFPLAYER_STATE_PAUSED = 5,
    FFPLAYER_STATE_COMPLETED = 6,
    FFPLAYER_STATE_STOPPED = 7,
    FFPLAYER_STATE_ERROR = 8
} FFPlayerState;

// Player events
typedef enum {
    FFPLAYER_EVENT_PREPARED = 0,
    FFPLAYER_EVENT_STARTED = 1,
    FFPLAYER_EVENT_PAUSED = 2,
    FFPLAYER_EVENT_COMPLETED = 3,
    FFPLAYER_EVENT_ERROR = 4,
    FFPLAYER_EVENT_BUFFERING = 5,
    FFPLAYER_EVENT_VIDEO_SIZE_CHANGED = 6,
    FFPLAYER_EVENT_SEEK_COMPLETE = 7
} FFPlayerEvent;

// Event callback
typedef void (*FFPlayerEventCallback)(void *opaque, FFPlayerEvent event, int arg1, int arg2);

// Frame callback (called when a new frame is ready for display)
typedef void (*FFPlayerFrameCallback)(void *opaque);

// Create/destroy
FFmpegPlayer *ffplayer_create(void);
void ffplayer_destroy(FFmpegPlayer *player);

// Setup callbacks
void ffplayer_set_event_callback(FFmpegPlayer *player, FFPlayerEventCallback cb, void *opaque);
void ffplayer_set_frame_callback(FFmpegPlayer *player, FFPlayerFrameCallback cb, void *opaque);

// Player controls
int ffplayer_set_data_source(FFmpegPlayer *player, const char *url);
int ffplayer_prepare_async(FFmpegPlayer *player);
int ffplayer_start(FFmpegPlayer *player);
int ffplayer_pause(FFmpegPlayer *player);
int ffplayer_stop(FFmpegPlayer *player);
int ffplayer_reset(FFmpegPlayer *player);
int ffplayer_seek(FFmpegPlayer *player, int64_t msec);
void ffplayer_set_volume(FFmpegPlayer *player, float volume);

// How much delay to trade for smoothness, in milliseconds.
//
// The one knob the UI has over latency, and the only one this backend has to
// offer: it decodes every frame as it arrives and keeps no queue of its own, so
// the buffering it can spend is the demuxer's (its reorder/delay buffer) and the
// decoder's (its reorder buffer). A budget of `kLowLatencyMs` or less is treated
// as "live": no demuxer reordering, no input buffering, and the low-delay codec
// flag, which makes a codec with B-frames emit each frame at once.
//
// Meaningful only before the source is opened: the options it writes go to
// avformat_open_input, which happens once per source.
void ffplayer_set_playback_mode(FFmpegPlayer *player, int buffer_ms);

// Video frame access (RGBA format for Flutter texture)
// Returns a pointer to the current RGBA frame data, or NULL if no frame ready
// The returned buffer is valid until the next call to ffplayer_get_frame
const uint8_t *ffplayer_get_frame(FFmpegPlayer *player, int *width, int *height);

// Player info
int ffplayer_get_video_width(FFmpegPlayer *player);
int ffplayer_get_video_height(FFmpegPlayer *player);
int64_t ffplayer_get_duration(FFmpegPlayer *player);
int64_t ffplayer_get_current_position(FFmpegPlayer *player);
FFPlayerState ffplayer_get_state(FFmpegPlayer *player);
const char *ffplayer_get_data_source(FFmpegPlayer *player);
bool ffplayer_is_playing(FFmpegPlayer *player);

// Snapshot: encode current frame as PNG.
// Returns allocated PNG buffer (caller must free) and sets *out_size.
// Returns NULL if no frame is available.
uint8_t *ffplayer_snapshot_png(FFmpegPlayer *player, int *out_size);

#ifdef __cplusplus
}
#endif

#endif // FFMPEG_PLAYER_H
