// fijk_player.cpp
// Flutter player wrapper implementation

#include "fijk_player.h"
#include "fijk_texture.h"
#include <flutter/standard_method_codec.h>
#include <sstream>
#include <thread>
#include <atomic>

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
    // Stop recording if active
    if (is_recording_.load()) {
        ffmpeg_kit_cancel();
        is_recording_.store(false);
    }
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

    // Send 'prepared' event with duration so Dart side gets the duration value
    if (event == FFPLAYER_EVENT_PREPARED && native_player_) {
        int64_t dur = ffplayer_get_duration(native_player_);
        flutter::EncodableMap prep_map;
        prep_map[flutter::EncodableValue("event")] = flutter::EncodableValue("prepared");
        prep_map[flutter::EncodableValue("duration")] = flutter::EncodableValue(static_cast<int>(dur));
        event_sink_->Success(flutter::EncodableValue(prep_map));
    }
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
    else if (method == "snapshot") {
        if (!native_player_) {
            result->Error("SNAPSHOT_FAILED", "No player");
            return;
        }
        int png_size = 0;
        uint8_t* png_data = ffplayer_snapshot_png(native_player_, &png_size);
        if (png_data && png_size > 0) {
            std::vector<uint8_t> png_vec(png_data, png_data + png_size);
            free(png_data);
            
            flutter::EncodableMap args;
            args[flutter::EncodableValue("data")] = flutter::EncodableValue(png_vec);
            method_channel_->InvokeMethod("_onSnapshot",
                std::make_unique<flutter::EncodableValue>(flutter::EncodableValue(args)));
            result->Success();
        } else {
            result->Error("SNAPSHOT_FAILED", "No frame available");
        }
    }
    else if (method == "startFFmpegRecording" || method == "startRecording") {
        if (is_recording_.load()) {
            result->Success(flutter::EncodableValue(false));
            return;
        }
        const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
        std::string output_path;
        if (args) {
            auto it = args->find(flutter::EncodableValue("outputPath"));
            if (it != args->end()) {
                output_path = std::get<std::string>(it->second);
            }
        }
        if (output_path.empty() || data_source_.empty()) {
            result->Error("INVALID_ARGS", "Missing outputPath or data source");
            return;
        }
        recording_path_ = output_path;
        is_recording_.store(true);

        // Notify Flutter that recording has started
        if (method_channel_) {
            method_channel_->InvokeMethod("_onRecordingStarted", nullptr);
        }

        // Run FFmpeg recording in background thread
        std::string src = data_source_;
        std::string dst = output_path;
        std::atomic<bool>* flag = &is_recording_;
        recording_thread_ = std::thread([src, dst, flag]() {
            // Build argv: ffmpeg -rtsp_transport tcp -i <src> -c copy -movflags +faststart <dst>
            const char* argv[] = {
                "ffmpeg", "-y",
                "-rtsp_transport", "tcp",
                "-i", src.c_str(),
                "-c", "copy",
                "-movflags", "+faststart",
                dst.c_str(),
                NULL
            };
            int argc = 0;
            while (argv[argc]) argc++;

            // ffmpeg_kit_execute takes non-const argv, cast is safe since it doesn't modify
            ffmpeg_kit_execute(argc, const_cast<char**>(argv));
            flag->store(false);
        });
        recording_thread_.detach();
        result->Success(flutter::EncodableValue(true));
    }
    else if (method == "stopFFmpegRecording" || method == "stopRecording") {
        if (is_recording_.load()) {
            ffmpeg_kit_cancel();
            is_recording_.store(false);
            
            // Notify Flutter with the recorded file path
            if (method_channel_) {
                flutter::EncodableMap args;
                args[flutter::EncodableValue("path")] = flutter::EncodableValue(recording_path_);
                method_channel_->InvokeMethod("_onRecordingStopped",
                    std::make_unique<flutter::EncodableValue>(flutter::EncodableValue(args)));
            }
            result->Success(flutter::EncodableValue(true));
        } else {
            result->Success(flutter::EncodableValue(false));
        }
    }
    else if (method == "isFFmpegRecording") {
        result->Success(flutter::EncodableValue(is_recording_.load()));
    }
    else {
        result->NotImplemented();
    }
}
