// fijkplayer_plugin.cpp
// Windows/Linux Flutter plugin implementation

#include "fijkplayer_plugin.h"
#include "fijk_player.h"
#include "fijk_texture.h"

#include <flutter/method_channel.h>
#include <flutter/event_channel.h>
#include <flutter/event_stream_handler_functions.h>
#include <flutter/plugin_registrar.h>
#include <flutter/standard_method_codec.h>
#include <flutter/encodable_value.h>

#include <map>
#include <memory>
#include <string>
#include <sstream>

extern "C" {
#include "ffmpeg_kit_execute.h"
}

namespace fijkplayer {

class FijkPlugin : public flutter::Plugin {
public:
    static void RegisterWithRegistrar(flutter::PluginRegistrar* registrar);

    FijkPlugin(flutter::PluginRegistrar* registrar);
    virtual ~FijkPlugin();

private:
    void HandleMethodCall(
        const flutter::MethodCall<flutter::EncodableValue>& call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    void HandleFFmpegKitCall(
        const flutter::MethodCall<flutter::EncodableValue>& call,
        std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result);

    flutter::PluginRegistrar* registrar_;
    std::map<int, std::unique_ptr<FijkPlayer>> players_;
    int next_player_id_ = 0;

    // Event channel
    std::unique_ptr<flutter::EventChannel<flutter::EncodableValue>> event_channel_;
    flutter::EventSink<flutter::EncodableValue>* event_sink_ = nullptr;

    int playing_count_ = 0;
    int playable_count_ = 0;
};

// static
void FijkPlugin::RegisterWithRegistrar(flutter::PluginRegistrar* registrar) {
    auto plugin = std::make_unique<FijkPlugin>(registrar);

    // Main channel
    auto channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        "befovy.com/fijk",
        &flutter::StandardMethodCodec::GetInstance());

    channel->SetMethodCallHandler(
        [plugin_ptr = plugin.get()](
            const flutter::MethodCall<flutter::EncodableValue>& call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
            plugin_ptr->HandleMethodCall(call, std::move(result));
        });

    // FFmpegKit channel
    auto ffmpeg_channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        "befovy.com/fijk/ffmpeg_kit",
        &flutter::StandardMethodCodec::GetInstance());

    ffmpeg_channel->SetMethodCallHandler(
        [plugin_ptr = plugin.get()](
            const flutter::MethodCall<flutter::EncodableValue>& call,
            std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
            plugin_ptr->HandleFFmpegKitCall(call, std::move(result));
        });

    // Recorder channel (stub for desktop)
    auto recorder_channel = std::make_unique<flutter::MethodChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        "befovy.com/fijk/recorder",
        &flutter::StandardMethodCodec::GetInstance());

    recorder_channel->SetMethodCallHandler(
        [](const flutter::MethodCall<flutter::EncodableValue>& call,
           std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {
            result->NotImplemented();
        });

    registrar->AddPlugin(std::move(plugin));
}

FijkPlugin::FijkPlugin(flutter::PluginRegistrar* registrar)
    : registrar_(registrar) {

    // Event channel
    event_channel_ = std::make_unique<flutter::EventChannel<flutter::EncodableValue>>(
        registrar->messenger(),
        "befovy.com/fijk/event",
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

FijkPlugin::~FijkPlugin() {
    players_.clear();
    if (event_sink_) {
        delete event_sink_;
        event_sink_ = nullptr;
    }
}

void FijkPlugin::HandleMethodCall(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    const auto& method = call.method_name();

    if (method == "createPlayer") {
        int pid = next_player_id_++;
        auto player = std::make_unique<FijkPlayer>(registrar_, pid);
        players_[pid] = std::move(player);
        result->Success(flutter::EncodableValue(pid));
    }
    else if (method == "releasePlayer") {
        const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
        if (args) {
            auto it = args->find(flutter::EncodableValue("pid"));
            if (it != args->end()) {
                int pid = std::get<int>(it->second);
                players_.erase(pid);
                result->Success();
                return;
            }
        }
        result->Error("INVALID_ARGS", "Missing pid");
    }
    else if (method == "getPlatformVersion") {
#ifdef _WIN32
        result->Success(flutter::EncodableValue("Windows"));
#else
        result->Success(flutter::EncodableValue("Linux"));
#endif
    }
    else if (method == "setOrientationPortrait" || method == "setOrientationLandscape" ||
             method == "setOrientationAuto") {
        result->Success(flutter::EncodableValue(false));
    }
    else if (method == "setScreenOn" || method == "isScreenKeptOn" ||
             method == "setBrightness" || method == "brightness") {
        result->Success(flutter::EncodableValue(false));
    }
    else if (method == "volumeUp" || method == "volumeDown" || method == "volumeMute") {
        result->Success();
    }
    else if (method == "onLoad") {
        result->Success();
    }
    else if (method == "logLevel") {
        result->Success();
    }
    else {
        result->NotImplemented();
    }
}

void FijkPlugin::HandleFFmpegKitCall(
    const flutter::MethodCall<flutter::EncodableValue>& call,
    std::unique_ptr<flutter::MethodResult<flutter::EncodableValue>> result) {

    const auto& method = call.method_name();

    if (method == "execute") {
        const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
        if (!args) {
            result->Error("INVALID_ARGS", "Missing arguments");
            return;
        }

        // Get command or arguments
        std::vector<std::string> cmd_args;

        auto args_it = args->find(flutter::EncodableValue("arguments"));
        if (args_it != args->end()) {
            const auto& list = std::get<flutter::EncodableList>(args_it->second);
            for (const auto& item : list) {
                cmd_args.push_back(std::get<std::string>(item));
            }
        } else {
            auto cmd_it = args->find(flutter::EncodableValue("command"));
            if (cmd_it != args->end()) {
                // Simple space-split (TODO: handle quotes)
                std::string cmd = std::get<std::string>(cmd_it->second);
                std::istringstream iss(cmd);
                std::string token;
                while (iss >> token) {
                    cmd_args.push_back(token);
                }
            }
        }

        if (cmd_args.empty()) {
            result->Error("INVALID_ARGS", "No command provided");
            return;
        }

        // Build argv
        int argc = (int)cmd_args.size() + 1;
        char** argv = new char*[argc + 1];
        argv[0] = strdup("ffmpeg");
        for (int i = 0; i < (int)cmd_args.size(); i++) {
            argv[i + 1] = strdup(cmd_args[i].c_str());
        }
        argv[argc] = nullptr;

        // Execute (TODO: run on background thread for async)
        int ret = ffmpeg_kit_execute(argc, argv);

        for (int i = 0; i < argc; i++) free(argv[i]);
        delete[] argv;

        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "cancel") {
        ffmpeg_kit_cancel();
        result->Success();
    }
    else if (method == "getFFmpegVersion") {
        const char* version = ffmpeg_kit_get_version();
        result->Success(flutter::EncodableValue(std::string(version)));
    }
    else {
        result->NotImplemented();
    }
}

}  // namespace fijkplayer

void FijkplayerPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar) {
    fijkplayer::FijkPlugin::RegisterWithRegistrar(
        flutter::PluginRegistrarManager::GetInstance()
            ->GetRegistrar<flutter::PluginRegistrar>(registrar));
}
