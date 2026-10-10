// fijk_player.cpp
// Flutter player wrapper implementation

#include "fijk_player.h"
#include "fijk_texture.h"
#include <flutter/standard_method_codec.h>
#include <atomic>
#include <filesystem>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

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

namespace {

// `FijkOption.playerCategory` in the Dart side, the only category an option this
// backend acts on belongs to.
constexpr int kPlayerOptionCategory = 4;

// Whether `args` carries the option `key` of `category`. Dart sends it as
// {"cat": <int>, "key": <string>, "long"|"str": <value>}.
bool IsOption(const flutter::EncodableMap& args, int category, const char* key) {
    const auto category_it = args.find(flutter::EncodableValue("cat"));
    const auto key_it = args.find(flutter::EncodableValue("key"));
    if (category_it == args.end() || key_it == args.end()) {
        return false;
    }
    const auto* sent_category = std::get_if<int32_t>(&category_it->second);
    const auto* sent_key = std::get_if<std::string>(&key_it->second);
    return sent_category != nullptr && sent_key != nullptr &&
           *sent_category == category && *sent_key == key;
}

// Whether an option's value is on. The standard codec sends an integer as int32
// or int64 depending on its size, and ijkplayer reads anything else as off.
bool OptionValueIsOn(const flutter::EncodableMap& args) {
    const auto it = args.find(flutter::EncodableValue("long"));
    if (it == args.end()) {
        return false;
    }
    if (const auto* small = std::get_if<int32_t>(&it->second)) {
        return *small != 0;
    }
    if (const auto* large = std::get_if<int64_t>(&it->second)) {
        return *large != 0;
    }
    return false;
}

}  // namespace

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

void FijkPlayer::RunRecording(const std::string& source,
                              const std::string& destination) {
    // The FFmpeg CLI, which this plugin links into the process. A copy, not a
    // re-encode: the packets are already what the user wants to keep.
    std::vector<std::string> args = {"ffmpeg", "-y"};

    if (source.rfind("rtsp://", 0) == 0) {
        // Both of these are options of the RTSP input. Asking for them on a file
        // — a bundled sample, say — is not ignored: ffmpeg fails the run with
        // "Option rtsp_transport not found" before a single byte is written.
        args.push_back("-rtsp_transport");
        args.push_back("tcp");
        // A camera that stops sending has stopped: without this the run waits for
        // packets that never arrive, and stopping it holds the caller until the
        // read gives up of its own accord. This is the spelling that reaches the
        // socket — the RTSP demuxer passes it down as `?timeout=`, which is also
        // how the player sets it — and the same option the player already uses.
        args.push_back("-timeout");
        args.push_back("5000000");
    }

    args.push_back("-i");
    args.push_back(source);
    args.push_back("-c");
    args.push_back("copy");
    args.push_back("-movflags");
    args.push_back("+faststart");
    args.push_back(destination);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (std::string& arg : args) {
        argv.push_back(arg.data());
    }
    argv.push_back(nullptr);

    const int result = ffmpeg_kit_execute(static_cast<int>(args.size()),
                                          argv.data());
    is_recording_.store(false);

    // The file is the recording, whatever ffmpeg's exit code says: a run that
    // fails part way through still leaves what it managed to write, and a clip
    // the user can watch beats an error message about a code.
    bool written = false;
    try {
        written = std::filesystem::exists(destination) &&
                  std::filesystem::file_size(destination) > 0;
    } catch (...) {
        written = false;
    }

    if (written) {
        ReportRecordingStopped(destination);
    } else {
        ReportRecordingError("ffmpeg could not record this source (exit code " +
                             std::to_string(result) + ")");
    }
}

void FijkPlayer::ReportRecordingStopped(const std::string& destination) {
    if (!method_channel_) return;
    flutter::EncodableMap args;
    args[flutter::EncodableValue("path")] = flutter::EncodableValue(destination);
    method_channel_->InvokeMethod(
        "_onRecordingStopped",
        std::make_unique<flutter::EncodableValue>(flutter::EncodableValue(args)));
}

void FijkPlayer::ReportRecordingError(const std::string& message) {
    if (!method_channel_) return;
    method_channel_->InvokeMethod(
        "_onRecordingError",
        std::make_unique<flutter::EncodableValue>(flutter::EncodableValue(message)));
}

void FijkPlayer::Shutdown() {
    // Stop recording if active, and wait for it: the recording thread reports the
    // end of a recording and reads the method channel to do it, so it has to be
    // finished before the channels are taken down. Cancelling it is graceful —
    // ffmpeg finalises the file rather than being killed.
    if (is_recording_.exchange(false)) {
        ffmpeg_kit_cancel();
    }
    if (recording_thread_.joinable()) {
        recording_thread_.join();
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

void FijkPlayer::ReportState(FFPlayerState state) {
    if (!event_sink_) return;

    const int new_state = static_cast<int>(state);
    flutter::EncodableMap map;
    map[flutter::EncodableValue("event")] = flutter::EncodableValue("state_change");
    map[flutter::EncodableValue("old")] = flutter::EncodableValue(last_state_);
    map[flutter::EncodableValue("new")] = flutter::EncodableValue(new_state);
    event_sink_->Success(flutter::EncodableValue(map));
    last_state_ = new_state;
}

void FijkPlayer::HandlePlayerEvent(FFPlayerEvent event, int arg1, int arg2) {
    if (!event_sink_) return;

    // Dart reads the *name* of an event, never its number. This used to send the
    // raw FFPlayerEvent as an int, which matched none of the names the Dart side
    // switches on, so its state machine stayed in `initialized` and it never told
    // this backend to start — and since the FFmpeg player waits for exactly that
    // before decoding anything, the preview stayed black.
    switch (event) {
        case FFPLAYER_EVENT_PREPARED: {
            ReportState(FFPLAYER_STATE_PREPARED);
            // The duration is only known once the streams are open, and Dart
            // keeps it in a field of its own.
            flutter::EncodableMap prepared;
            prepared[flutter::EncodableValue("event")] = flutter::EncodableValue("prepared");
            prepared[flutter::EncodableValue("duration")] = flutter::EncodableValue(
                static_cast<int>(ffplayer_get_duration(native_player_)));
            event_sink_->Success(flutter::EncodableValue(prepared));
            if (start_on_prepared_) {
                // Dart asked for this when it started the player, and ijkplayer
                // honours it on the other platforms. This callback runs on the
                // read thread, before it waits for a start command, so starting
                // here is what keeps the picture from sitting ready and unseen
                // when the open took longer than Dart's wait.
                ffplayer_start(native_player_);
            }
            break;
        }
        case FFPLAYER_EVENT_STARTED:
            ReportState(FFPLAYER_STATE_PLAYING);
            break;
        case FFPLAYER_EVENT_PAUSED:
            ReportState(FFPLAYER_STATE_PAUSED);
            break;
        case FFPLAYER_EVENT_COMPLETED:
            ReportState(FFPLAYER_STATE_COMPLETED);
            break;
        case FFPLAYER_EVENT_ERROR: {
            ReportState(FFPLAYER_STATE_ERROR);
            // The code is FFmpeg's own, which is what a bug report needs; the
            // message is this backend's, and deliberately says nothing more
            // than what the code already tells anyone who looks it up.
            flutter::EncodableMap error;
            error[flutter::EncodableValue("event")] = flutter::EncodableValue("error");
            error[flutter::EncodableValue("code")] = flutter::EncodableValue(arg1);
            error[flutter::EncodableValue("message")] =
                flutter::EncodableValue("the source could not be read");
            event_sink_->Success(flutter::EncodableValue(error));
            break;
        }
        case FFPLAYER_EVENT_VIDEO_SIZE_CHANGED: {
            // Dart sizes the preview from this, and it is the only way it learns
            // the shape of what is playing.
            flutter::EncodableMap size;
            size[flutter::EncodableValue("event")] = flutter::EncodableValue("size_changed");
            size[flutter::EncodableValue("width")] = flutter::EncodableValue(arg1);
            size[flutter::EncodableValue("height")] = flutter::EncodableValue(arg2);
            event_sink_->Success(flutter::EncodableValue(size));
            break;
        }
        case FFPLAYER_EVENT_SEEK_COMPLETE: {
            flutter::EncodableMap seek;
            seek[flutter::EncodableValue("event")] = flutter::EncodableValue("seek_complete");
            event_sink_->Success(flutter::EncodableValue(seek));
            break;
        }
        case FFPLAYER_EVENT_BUFFERING:
            // This backend does not fire it: there is no buffer to report on, and
            // Dart's `buffering` event carries a position it would have to invent.
            break;
    }
}

void FijkPlayer::HandleFrameReady() {
    if (!texture_ || !native_player_) return;

    int w = 0, h = 0;
    const uint8_t* frame = ffplayer_get_frame(native_player_, &w, &h);
    if (frame && w > 0 && h > 0) {
        texture_->UpdateBuffer(frame, w, h);
        texture_->MarkFrameAvailable();

        // The picture is on the texture now, so the still the UI keeps over it can
        // come off. Reported once per source: without it a viewer that passes a
        // cover image shows that still for ever while the video plays behind it.
        if (!reported_render_start_) {
            reported_render_start_ = true;
            if (event_sink_) {
                flutter::EncodableMap map;
                map[flutter::EncodableValue("event")] = flutter::EncodableValue("rendering_start");
                map[flutter::EncodableValue("type")] = flutter::EncodableValue("video");
                event_sink_->Success(flutter::EncodableValue(map));
            }
        }
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
                // A new source has its own first frame, and its own options:
                // Dart sets them again after this call, and one left over from a
                // previous source would start a player that was meant to wait.
                reported_render_start_ = false;
                start_on_prepared_ = false;
                int ret = ffplayer_set_data_source(native_player_, data_source_.c_str());
                // Accepting a source is a state (initialized), and Dart starts
                // the player as soon as this call returns. Nothing else tells it
                // where the player got to.
                if (ret == 0) ReportState(ffplayer_get_state(native_player_));
                result->Success(flutter::EncodableValue(ret));
                return;
            }
        }
        result->Error("INVALID_ARGS", "Missing url");
    }
    else if (method == "prepareAsync") {
        int ret = ffplayer_prepare_async(native_player_);
        // Preparing reports itself when it finishes; this says it has begun, so
        // Dart is not left thinking the player is still waiting for a source.
        if (ret == 0) ReportState(ffplayer_get_state(native_player_));
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "start") {
        int ret = ffplayer_start(native_player_);
        // Native start only takes effect from prepared or paused, so the state
        // that is reported is read back from the player rather than assumed.
        if (ret == 0) ReportState(ffplayer_get_state(native_player_));
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "pause") {
        int ret = ffplayer_pause(native_player_);
        if (ret == 0) ReportState(ffplayer_get_state(native_player_));
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "stop") {
        int ret = ffplayer_stop(native_player_);
        // The FFmpeg player stops without announcing it: it tears the read
        // thread down and returns.
        ReportState(ffplayer_get_state(native_player_));
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "reset") {
        int ret = ffplayer_reset(native_player_);
        // A reset source has to be set again, so the rendered first frame is
        // forgotten with it.
        reported_render_start_ = false;
        ReportState(ffplayer_get_state(native_player_));
        result->Success(flutter::EncodableValue(ret));
    }
    else if (method == "setOption") {
        // One option is acted on; the rest are compatibility no-ops, as they are
        // on macOS. The options Dart sets are ijkplayer's: `enable-snapshot`,
        // which this backend always supports (ffplayer_snapshot_png), and
        // `start-on-prepared`, which is honoured below because Dart's own wait
        // for the player to become ready is shorter than opening a stream can
        // take. Rejecting the call would fail every playback before it began —
        // which is exactly what it used to do.
        const auto* args = std::get_if<flutter::EncodableMap>(call.arguments());
        if (args != nullptr &&
            IsOption(*args, kPlayerOptionCategory, "start-on-prepared")) {
            start_on_prepared_ = OptionValueIsOn(*args);
        }
        result->Success(flutter::EncodableValue(0));
    }
    else if (method == "applyOptions") {
        result->Success(flutter::EncodableValue(0));
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
            // Dart sends "path" (as Android, iOS and macOS all expect). The old
            // "outputPath" spelling is still accepted so a caller written against
            // it keeps working.
            for (const char* key : {"path", "outputPath"}) {
                auto it = args->find(flutter::EncodableValue(key));
                if (it != args->end()) {
                    if (const auto* value = std::get_if<std::string>(&it->second)) {
                        output_path = *value;
                    }
                    if (!output_path.empty()) {
                        break;
                    }
                }
            }
        }
        if (output_path.empty() || data_source_.empty()) {
            result->Error("INVALID_ARGS", "Missing path or data source");
            return;
        }

        // The flag above says no recording is running, so the previous thread has
        // finished its work — but a joinable std::thread still has to be joined
        // before it is assigned over, or the process is taken down with it.
        if (recording_thread_.joinable()) {
            recording_thread_.join();
        }

        is_recording_.store(true);

        // Notify Flutter that recording has started
        if (method_channel_) {
            method_channel_->InvokeMethod("_onRecordingStarted", nullptr);
        }

        std::string src = data_source_;
        std::string dst = output_path;
        recording_thread_ = std::thread([this, src, dst]() {
            RunRecording(src, dst);
        });
        result->Success(flutter::EncodableValue(true));
    }
    else if (method == "stopFFmpegRecording" || method == "stopRecording") {
        // Only a running recording is stopped; one that has already ended — a
        // file source that ran to its end, or a run that failed — reported itself
        // when it did, and saying so twice would state a second recording that
        // never happened.
        if (!is_recording_.exchange(false)) {
            result->Success(flutter::EncodableValue(false));
            return;
        }

        // ffmpeg is asked to stop rather than killed, so it writes the trailer the
        // file needs to be playable, and then waited for: the thread reports the
        // end of the recording, and the caller gets its answer once the file is
        // whole. Waiting is the point — the caller reads the file as soon as this
        // returns, and a clip still being written is worse than a moment's pause.
        ffmpeg_kit_cancel();
        if (recording_thread_.joinable()) {
            recording_thread_.join();
        }
        result->Success(flutter::EncodableValue(true));
    }
    else if (method == "isFFmpegRecording") {
        result->Success(flutter::EncodableValue(is_recording_.load()));
    }
    else {
        result->NotImplemented();
    }
}
