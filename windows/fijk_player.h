// fijk_player.h
// Flutter player wrapper for desktop platforms

#ifndef FIJK_PLAYER_H
#define FIJK_PLAYER_H

#include <flutter/method_channel.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar.h>
#include <flutter/texture_registrar.h>
#include <memory>
#include <string>
#include <thread>
#include <atomic>

extern "C" {
#include "ffmpeg_player.h"
}

class FijkTexture;

class FijkPlayer {
public:
    FijkPlayer(flutter::PluginRegistrar* registrar, int player_id);
    ~FijkPlayer();

    int player_id() const { return player_id_; }
    int64_t texture_id() const;

    void HandleMethodCall(
        const flutter::MethodCall<flutter::EncodableValue>& call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    // Called from C callbacks (public for trampoline access)
    void HandlePlayerEvent(FFPlayerEvent event, int arg1, int arg2);
    void HandleFrameReady();

    void Shutdown();

private:
    flutter::PluginRegistrar* registrar_;
    int player_id_;

    FFmpegPlayer* native_player_ = nullptr;
    std::unique_ptr<FijkTexture> texture_;

    std::unique_ptr<flutter::MethodChannel<flutter::EncodableValue>> method_channel_;
    std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> event_channel_;
    flutter::EventSink<flutter::EncodableValue>* event_sink_ = nullptr;

    std::string data_source_;

    // Whether the first frame has already been reported as rendered, so the UI can
    // take its cover image off the picture. Cleared when a new source is opened.
    bool reported_render_start_ = false;

    // Last state handed to Dart, as a FijkState index (the same numbering as
    // FFPlayerState). Sent as the `old` of the next state change, and updated
    // after every report.
    int last_state_ = 0;

    // Whether Dart asked for `start-on-prepared`: begin playing as soon as the
    // source is ready, without being told to. It is honoured here because Dart
    // only waits a few seconds for that to happen and opening a stream can take
    // longer — and when the wait is over Dart has stopped looking, so nothing
    // else would ever start the player.
    bool start_on_prepared_ = false;

    // Tells Dart the player moved to [state]. The Dart side switches on named
    // events and keeps its own state machine from them; without these it never
    // leaves `initialized`, and since this backend waits for an explicit start
    // before decoding, nothing would ever be played.
    void ReportState(FFPlayerState state);

    // Recording state. The path is not kept: the run that writes the file is the
    // one that reports it, from the argument it was given.
    std::atomic<bool> is_recording_{false};
    std::thread recording_thread_;

    // Runs the FFmpeg CLI over the source and reports how it went. Called on the
    // recording thread, which `Shutdown` and the next recording join before this
    // object's members go away — it reads the method channel to report, and it is
    // the only reporter: the exit code and the file are what decide whether a
    // recording succeeded, and only the run knows them.
    void RunRecording(const std::string& source, const std::string& destination);

    // Tells Dart the recording ended: the file it can be found in, or why it
    // failed.
    void ReportRecordingStopped(const std::string& destination);
    void ReportRecordingError(const std::string& message);
};

#endif // FIJK_PLAYER_H
