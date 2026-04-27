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
};

#endif // FIJK_PLAYER_H
