// fijk_player.cpp
// Flutter player wrapper implementation

#include "fijk_player.h"
#include "fijk_texture.h"
#include <flutter/standard_method_codec.h>
#include <sstream>

extern "C" {
#include "ffmpeg_kit_execute.h"
}

// C callback trampolines
static void event_callback(void* opaque, FFPlayerEvent event, int arg1, int arg2) {
    auto* player = static_cast<FijkPlayer*>(opaque);
    player->HandlePlayerEvent(event, arg1, arg2);
}

static void frame_callback(void* opaque) {
    auto* player = static_cast<FijkPlayer*>(opaque);
    player->HandleFrameReady();
}

FijkPlayer::FijkPlayer(flutter::PluginRegistrar* registrar, int player_id)
    : registrar_(registrar), player_id_(player_id) {

    // Create texture
    texture_ = std::make_unique<FijkTexture>(registrar->texture_registrar());

    // Create native player
    native_player_ = ffplayer_create();
    ffplayer_set_event_callback(native_player_, event_callback, this);
    ffplayer_set_frame_callback(native_player_, frame_callback, this);

    // Create per-player method channel
    std::string method_name = "befovy.com/fijkplayer/" + std::to_string(player_id);
    method_channel_ = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        method_name,
        &flutter::StandardMethodCodec::GetInstance());

    method_channel_->SetMethodCallHandler(
        [this](const flutter::MethodCall<flutter::EncodableValue>& call,
               std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
            HandleMethodCall(call, std::move(result));
        });

    // Create per-player event channel
    std::string event_name = "befovy.com/fijkplayer/event/" + std::to_string(player_id);
    event_channel_ = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        event_name,
        &flutter::StandardMethodCodec::GetInstance());

    auto handler = std::make_unique<flutter::StreamHandlerFunctions<flutter::EncodableValue>>(
        [this](const flutter::EncodableValue* arguments,
               std::unique_ptr<flutter::EventSink<flutter::EncodableValue>>&& events)
            -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
            event_sink_ = events.release();
            return nullptr;
        },
        [this](const flutter::EncodableValue* arguments)
            -> std::unique_ptr<flutter::StreamHandlerError<flutter::EncodableValue>> {
            delete event_sink_;
            event_sink_ = nullptr;
            return nullptr;
        });
    event_channel_->SetStreamHandler(std::move(handler));
}

FijkPlayer::~FijkPlayer() {
    Shutdown();
}

int64_t FijkPlayer::texture_id() const {
    return texture_ ? texture_->texture_id() : -1;
}

void FijkPlayer::Shutdown() {
    if (native_player_) {
        ffplayer_destroy(native_player_);
        native_player_ = nullptr;
    }
    texture_.reset();
    method_channel_.reset();
    event_channel_.reset();
    if (event_sink_) {
        delete event_sink_;
        event_sink_ = nullptr;
    }
}

void FijkPlayer::HandlePlayerEvent(FFPlayerEvent event, int arg1, int arg2) {
    if (!event_sink_) return;

    flutter::EncodableMap map;
    map[flutter::EncodableValue("event")] = flutter::EncodableValue(static_cast<int>(event));
    map[flutter::EncodableValue("arg1")] = flutter::EncodableValue(arg1);
    map[flutter::EncodableValue("arg2")] = flutter::EncodableValue(arg2);

    event_sink_->Success(flutter::EncodableValue(map));
}

void FijkPlayer::HandleFrameReady() {
    if (!texture_ || !native_player_) return;

    int w = 0, h = 0;
    const uint8_t* frame = ffplayer_get_frame(native_player_, &w, &h);
    if (frame && w > 0 && h > 0) {
        texture_->UpdateBuffer(frame, w, h);
        texture_->MarkFrameAvailable();
    }
}

void FijkPlayer::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    const auto& method = call.method_name();

    if (method == "setDataSource") {
        const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
        if (args) {
            auto it = args->find(flutter::EncodableValue("url"));
            if (it != args->end()) {
                data_source_ = std::get<std::string>(it->second);
                ffplayer_set_data_source(native_player_, data_source_.c_str());
                result->Success(flutter::EncodableValue(0));
                return;
            }
        }
        result->Error("INVALID_ARGS", "Missing url");
    }
    else if (method == "prepareAsync") {
        int ret = ffplayer_prepare_async(native_player_);
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "start") {
        int ret = ffplayer_start(native_player_);
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "pause") {
        int ret = ffplayer_pause(native_player_);
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "stop") {
        int ret = ffplayer_stop(native_player_);
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "reset") {
        int ret = ffplayer_reset(native_player_);
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "seekTo") {
        const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
        if (args) {
            auto it = args->find(flutter::EncodableValue("msec"));
            if (it != args->end()) {
                int64_t msec = std::get<int>(it->second);
                ffplayer_seek(native_player_, msec);
                result->Success(flutter::EncodableValue(0));
                return;
            }
        }
        result->Error("INVALID_ARGS", "Missing msec");
    }
    else if (method == "setVolume") {
        const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
        if (args) {
            auto it = args->find(flutter::EncodableValue("volume"));
            if (it != args->end()) {
                double vol = std::get<double>(it->second);
                ffplayer_set_volume(native_player_, (float)vol);
                result->Success(flutter::EncodableValue(0));
                return;
            }
        }
        result->Success(flutter::EncodableValue(0));
    }
    else if (method == "setupSurface") {
        // Return texture ID for Flutter to render
        result->Success(flutter::EncodableValue(texture_id()));
    }
    else if (method == "release") {
        Shutdown();
        result->Success(flutter::EncodableValue(0));
    }
    else if (method == "getCurrentPosition") {
        int64_t pos = ffplayer_get_current_position(native_player_);
        result->Success(flutter::EncodableValue(pos));
    }
    else if (method == "getDuration") {
        int64_t dur = ffplayer_get_duration(native_player_);
        result->Success(flutter::EncodableValue(dur));
    }
    else if (method == "startFFmpegRecording" || method == "startRecording") {
        // Recording not yet supported on desktop
        result->Success(flutter::EncodableValue(false));
    }
    else if (method == "stopFFmpegRecording" || method == "stopRecording") {
        result->Success(flutter::EncodableValue(false));
    }
    else if (method == "isFFmpegRecording") {
        result->Success(flutter::EncodableValue(false));
    }
    else {
        result->NotImplemented();
    }
}
