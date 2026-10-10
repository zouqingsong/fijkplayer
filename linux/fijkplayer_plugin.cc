// fijkplayer_plugin.cc
// Linux Flutter plugin using GObject-based API
// Uses the shared ffmpeg_player.c core from windows/

#include "include/fijkplayer/fijkplayer_plugin.h"

#include <flutter_linux/flutter_linux.h>
#include <gtk/gtk.h>

#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <sstream>
#include <vector>

extern "C" {
#include "ffmpeg_player.h"
#include "ffmpeg_kit_execute.h"
}

// Forward declarations
struct _FijkplayerPlugin {
    GObject parent_instance;
    FlPluginRegistrar* registrar;
    FlMethodChannel* main_channel;
    FlMethodChannel* ffmpeg_kit_channel;
    FlMethodChannel* recorder_channel;
    FlEventChannel* event_channel;
    FlEventSink* event_sink;
    FlTextureRegistrar* texture_registrar;
};

// ========== Pixel Buffer Texture ==========

struct _FijkPixelBufferTexture {
    FlPixelBufferTexture parent_instance;
    uint8_t* buffer;
    int width;
    int height;
    GMutex mutex;
};

#define FIJK_TYPE_PIXEL_BUFFER_TEXTURE (fijk_pixel_buffer_texture_get_type())
G_DECLARE_FINAL_TYPE(FijkPixelBufferTexture, fijk_pixel_buffer_texture, FIJK, PIXEL_BUFFER_TEXTURE, FlPixelBufferTexture)
G_DEFINE_TYPE(FijkPixelBufferTexture, fijk_pixel_buffer_texture, fl_pixel_buffer_texture_get_type())

static gboolean fijk_pixel_buffer_texture_copy_pixels(FlPixelBufferTexture* texture,
                                                       const uint8_t** out_buffer,
                                                       uint32_t* width,
                                                       uint32_t* height,
                                                       GError** error) {
    FijkPixelBufferTexture* self = FIJK_PIXEL_BUFFER_TEXTURE(texture);
    g_mutex_lock(&self->mutex);
    if (self->buffer && self->width > 0 && self->height > 0) {
        *out_buffer = self->buffer;
        *width = self->width;
        *height = self->height;
        g_mutex_unlock(&self->mutex);
        return TRUE;
    }
    g_mutex_unlock(&self->mutex);
    return FALSE;
}

static void fijk_pixel_buffer_texture_dispose(GObject* object) {
    FijkPixelBufferTexture* self = FIJK_PIXEL_BUFFER_TEXTURE(object);
    g_mutex_lock(&self->mutex);
    if (self->buffer) {
        free(self->buffer);
        self->buffer = NULL;
    }
    g_mutex_unlock(&self->mutex);
    g_mutex_clear(&self->mutex);
    G_OBJECT_CLASS(fijk_pixel_buffer_texture_parent_class)->dispose(object);
}

static void fijk_pixel_buffer_texture_class_init(FijkPixelBufferTextureClass* klass) {
    FL_PIXEL_BUFFER_TEXTURE_CLASS(klass)->copy_pixels = fijk_pixel_buffer_texture_copy_pixels;
    G_OBJECT_CLASS(klass)->dispose = fijk_pixel_buffer_texture_dispose;
}

static void fijk_pixel_buffer_texture_init(FijkPixelBufferTexture* self) {
    self->buffer = NULL;
    self->width = 0;
    self->height = 0;
    g_mutex_init(&self->mutex);
}

static void fijk_pixel_buffer_texture_update(FijkPixelBufferTexture* self,
                                              const uint8_t* data, int w, int h) {
    g_mutex_lock(&self->mutex);
    int size = w * h * 4;
    if (self->width != w || self->height != h) {
        if (self->buffer) free(self->buffer);
        self->buffer = (uint8_t*)malloc(size);
        self->width = w;
        self->height = h;
    }
    if (self->buffer) {
        memcpy(self->buffer, data, size);
    }
    g_mutex_unlock(&self->mutex);
}

// ========== Player Instance ==========

struct FijkLinuxPlayer {
    int player_id;
    FFmpegPlayer* native_player;
    FijkPixelBufferTexture* texture;
    int64_t texture_id;
    FlMethodChannel* method_channel;
    FlEventChannel* event_channel;
    FlEventSink* event_sink;
    FlTextureRegistrar* texture_registrar;
    std::string data_source;

    // Recording. The recorder is the FFmpeg CLI the plugin already ships, run on
    // a thread of its own: `ffmpeg_kit_execute` blocks until the recording stops,
    // and the thread this handler runs on is the one the UI lives on.
    bool is_recording = false;
    std::string recording_path;
};

static void player_event_callback(void* opaque, FFPlayerEvent event, int arg1, int arg2) {
    FijkLinuxPlayer* player = (FijkLinuxPlayer*)opaque;
    if (!player->event_sink) return;

    g_autoptr(FlValue) map = fl_value_new_map();
    fl_value_set_string_take(map, "event", fl_value_new_int(event));
    fl_value_set_string_take(map, "arg1", fl_value_new_int(arg1));
    fl_value_set_string_take(map, "arg2", fl_value_new_int(arg2));
    // Note: fl_event_sink_success is not thread-safe, must be called from main thread
    // For production, use g_idle_add to dispatch to main thread
}

static void player_frame_callback(void* opaque) {
    FijkLinuxPlayer* player = (FijkLinuxPlayer*)opaque;
    if (!player->native_player || !player->texture) return;

    int w = 0, h = 0;
    const uint8_t* frame = ffplayer_get_frame(player->native_player, &w, &h);
    if (frame && w > 0 && h > 0) {
        fijk_pixel_buffer_texture_update(player->texture, frame, w, h);
        fl_texture_registrar_mark_texture_frame_available(
            player->texture_registrar, FL_TEXTURE(player->texture));
    }
}

static FijkLinuxPlayer* player_create(FlPluginRegistrar* registrar,
                                       FlTextureRegistrar* texture_registrar,
                                       int player_id) {
    FijkLinuxPlayer* p = new FijkLinuxPlayer();
    p->player_id = player_id;
    p->texture_registrar = texture_registrar;

    // Create texture
    p->texture = FIJK_PIXEL_BUFFER_TEXTURE(g_object_new(FIJK_TYPE_PIXEL_BUFFER_TEXTURE, NULL));
    fl_texture_registrar_register_texture(texture_registrar, FL_TEXTURE(p->texture));
    p->texture_id = fl_texture_get_id(FL_TEXTURE(p->texture));

    // Create native player
    p->native_player = ffplayer_create();
    ffplayer_set_event_callback(p->native_player, player_event_callback, p);
    ffplayer_set_frame_callback(p->native_player, player_frame_callback, p);

    return p;
}

static void player_destroy(FijkLinuxPlayer* p) {
    if (!p) return;
    // Stop a recording before the player goes: the recorder thread holds nothing
    // of this object, but the file has to be finalised rather than left as a
    // header with no index.
    if (p->is_recording) {
        p->is_recording = false;
        ffmpeg_kit_cancel();
    }
    if (p->native_player) {
        ffplayer_destroy(p->native_player);
        p->native_player = NULL;
    }
    if (p->texture) {
        fl_texture_registrar_unregister_texture(p->texture_registrar, FL_TEXTURE(p->texture));
        g_object_unref(p->texture);
        p->texture = NULL;
    }
    delete p;
}

// ========== Plugin ==========

#define FIJKPLAYER_TYPE_PLUGIN (fijkplayer_plugin_get_type())
G_DECLARE_FINAL_TYPE(FijkplayerPlugin, fijkplayer_plugin, FIJKPLAYER, PLUGIN, GObject)
G_DEFINE_TYPE(FijkplayerPlugin, fijkplayer_plugin, g_object_get_type())

static std::map<int, FijkLinuxPlayer*> g_players;
static int g_next_player_id = 0;

// ========== Recording ==========

/// What a recorder thread needs. Owned by that thread, so nothing here points at
/// the player: the app can close the preview while FFmpeg is still finishing its
/// file, and the thread must survive that.
struct FijkRecorderJob {
    std::string source;
    std::string output;
};

/// Runs the embedded FFmpeg CLI with [args] (no program name) and returns its
/// exit code. Shared by the recorder thread and the ffmpeg_kit channel.
static int run_ffmpeg_arguments(const std::vector<std::string>& args) {
    int argc = (int)args.size() + 1;
    char** argv = new char*[argc + 1];
    argv[0] = strdup("ffmpeg");
    for (int i = 0; i < (int)args.size(); i++) {
        argv[i + 1] = strdup(args[i].c_str());
    }
    argv[argc] = NULL;

    int ret = ffmpeg_kit_execute(argc, argv);

    for (int i = 0; i < argc; i++) free(argv[i]);
    delete[] argv;
    return ret;
}

/// Records [source] to [output] until it is cancelled.
static gpointer recorder_thread(gpointer data) {
    FijkRecorderJob* job = (FijkRecorderJob*)data;
    // The same command the Windows recorder uses, so a clip recorded on Linux is
    // the same kind of file: copied, not re-encoded, and seekable at the front.
    run_ffmpeg_arguments({
        "-y",
        "-rtsp_transport", "tcp",
        "-i", job->source,
        "-c", "copy",
        "-movflags", "+faststart",
        job->output,
    });
    delete job;
    return NULL;
}

static void handle_player_method_call(FlMethodChannel* channel,
                                       FlMethodCall* method_call,
                                       gpointer user_data) {
    FijkLinuxPlayer* player = (FijkLinuxPlayer*)user_data;
    const gchar* method = fl_method_call_get_name(method_call);
    FlValue* args = fl_method_call_get_args(method_call);
    g_autoptr(FlMethodResponse) response = NULL;

    if (strcmp(method, "setDataSource") == 0) {
        FlValue* url_val = fl_value_lookup_string(args, "url");
        if (url_val) {
            player->data_source = fl_value_get_string(url_val);
            ffplayer_set_data_source(player->native_player, player->data_source.c_str());
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(0)));
        } else {
            response = FL_METHOD_RESPONSE(fl_method_error_response_new("INVALID_ARGS", "Missing url", NULL));
        }
    }
    else if (strcmp(method, "prepareAsync") == 0) {
        int ret = ffplayer_prepare_async(player->native_player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(ret)));
    }
    else if (strcmp(method, "start") == 0) {
        int ret = ffplayer_start(player->native_player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(ret)));
    }
    else if (strcmp(method, "pause") == 0) {
        int ret = ffplayer_pause(player->native_player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(ret)));
    }
    else if (strcmp(method, "stop") == 0) {
        int ret = ffplayer_stop(player->native_player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(ret)));
    }
    else if (strcmp(method, "reset") == 0) {
        int ret = ffplayer_reset(player->native_player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(ret)));
    }
    else if (strcmp(method, "seekTo") == 0) {
        FlValue* msec_val = fl_value_lookup_string(args, "msec");
        if (msec_val) {
            int64_t msec = fl_value_get_int(msec_val);
            ffplayer_seek(player->native_player, msec);
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(0)));
        } else {
            response = FL_METHOD_RESPONSE(fl_method_error_response_new("INVALID_ARGS", "Missing msec", NULL));
        }
    }
    else if (strcmp(method, "setVolume") == 0) {
        FlValue* vol_val = fl_value_lookup_string(args, "volume");
        if (vol_val) {
            double vol = fl_value_get_float(vol_val);
            ffplayer_set_volume(player->native_player, (float)vol);
        }
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(0)));
    }
    else if (strcmp(method, "setupSurface") == 0) {
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(player->texture_id)));
    }
    else if (strcmp(method, "release") == 0) {
        player_destroy(player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(0)));
    }
    else if (strcmp(method, "getCurrentPosition") == 0) {
        int64_t pos = ffplayer_get_current_position(player->native_player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(pos)));
    }
    else if (strcmp(method, "getDuration") == 0) {
        int64_t dur = ffplayer_get_duration(player->native_player);
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(dur)));
    }
    else if (strcmp(method, "startFFmpegRecording") == 0 ||
             strcmp(method, "startRecording") == 0) {
        FlValue* path_val = fl_value_lookup_string(args, "path");
        std::string output = path_val ? fl_value_get_string(path_val) : "";
        if (output.empty() || player->data_source.empty()) {
            response = FL_METHOD_RESPONSE(fl_method_error_response_new(
                "INVALID_ARGS", "Missing path or data source", NULL));
        } else if (player->is_recording) {
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(FALSE)));
        } else {
            player->is_recording = true;
            player->recording_path = output;
            // Dart waits for this before it considers the recording started, so
            // it is sent from here rather than from the recorder thread, which
            // cannot touch the method channel.
            if (player->method_channel) {
                fl_method_channel_invoke_method(
                    player->method_channel, "_onRecordingStarted", NULL, NULL, NULL, NULL);
            }
            FijkRecorderJob* job = new FijkRecorderJob();
            job->source = player->data_source;
            job->output = output;
            g_thread_new("fijk-recorder", recorder_thread, job);
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(TRUE)));
        }
    }
    else if (strcmp(method, "stopFFmpegRecording") == 0 ||
             strcmp(method, "stopRecording") == 0) {
        if (player->is_recording) {
            // Cancelling makes FFmpeg finish its file and exit. Awaiting that here
            // would block the UI while a long clip is indexed, so Dart is told
            // the recording stopped and the file settles on its own.
            player->is_recording = false;
            ffmpeg_kit_cancel();
            if (player->method_channel) {
                g_autoptr(FlValue) map = fl_value_new_map();
                fl_value_set_string_take(map, "path",
                                         fl_value_new_string(player->recording_path.c_str()));
                fl_method_channel_invoke_method(
                    player->method_channel, "_onRecordingStopped", map, NULL, NULL, NULL);
            }
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(TRUE)));
        } else {
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(FALSE)));
        }
    }
    else if (strcmp(method, "isFFmpegRecording") == 0) {
        response = FL_METHOD_RESPONSE(
            fl_method_success_response_new(fl_value_new_bool(player->is_recording)));
    }
    else {
        response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    }

    fl_method_call_respond(method_call, response, NULL);
}

static void handle_main_method_call(FlMethodChannel* channel,
                                     FlMethodCall* method_call,
                                     gpointer user_data) {
    FijkplayerPlugin* plugin = FIJKPLAYER_PLUGIN(user_data);
    const gchar* method = fl_method_call_get_name(method_call);
    FlValue* args = fl_method_call_get_args(method_call);
    g_autoptr(FlMethodResponse) response = NULL;

    if (strcmp(method, "createPlayer") == 0) {
        int pid = g_next_player_id++;
        FijkLinuxPlayer* player = player_create(plugin->registrar,
                                                 plugin->texture_registrar, pid);

        // Create per-player method channel
        gchar* channel_name = g_strdup_printf("befovy.com/fijkplayer/%d", pid);
        FlMethodCodec* codec = FL_METHOD_CODEC(fl_standard_method_codec_new());
        player->method_channel = fl_method_channel_new(
            fl_plugin_registrar_get_messenger(plugin->registrar),
            channel_name, codec);
        fl_method_channel_set_method_call_handler(player->method_channel,
            handle_player_method_call, player, NULL);
        g_free(channel_name);

        g_players[pid] = player;
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(pid)));
    }
    else if (strcmp(method, "releasePlayer") == 0) {
        FlValue* pid_val = fl_value_lookup_string(args, "pid");
        if (pid_val) {
            int pid = fl_value_get_int(pid_val);
            auto it = g_players.find(pid);
            if (it != g_players.end()) {
                player_destroy(it->second);
                g_players.erase(it);
            }
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(NULL));
        } else {
            response = FL_METHOD_RESPONSE(fl_method_error_response_new("INVALID_ARGS", "Missing pid", NULL));
        }
    }
    else if (strcmp(method, "getPlatformVersion") == 0) {
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_string("Linux")));
    }
    else if (strcmp(method, "onLoad") == 0 || strcmp(method, "logLevel") == 0) {
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(NULL));
    }
    else if (strcmp(method, "setOrientationPortrait") == 0 ||
             strcmp(method, "setOrientationLandscape") == 0 ||
             strcmp(method, "setOrientationAuto") == 0 ||
             strcmp(method, "setScreenOn") == 0 ||
             strcmp(method, "isScreenKeptOn") == 0 ||
             strcmp(method, "setBrightness") == 0 ||
             strcmp(method, "brightness") == 0) {
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_bool(FALSE)));
    }
    else if (strcmp(method, "volumeUp") == 0 ||
             strcmp(method, "volumeDown") == 0 ||
             strcmp(method, "volumeMute") == 0) {
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(NULL));
    }
    else {
        response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    }

    fl_method_call_respond(method_call, response, NULL);
}

static void handle_ffmpeg_kit_method_call(FlMethodChannel* channel,
                                           FlMethodCall* method_call,
                                           gpointer user_data) {
    const gchar* method = fl_method_call_get_name(method_call);
    FlValue* args = fl_method_call_get_args(method_call);
    g_autoptr(FlMethodResponse) response = NULL;

    if (strcmp(method, "execute") == 0) {
        std::vector<std::string> cmd_args;

        FlValue* args_list = fl_value_lookup_string(args, "arguments");
        if (args_list && fl_value_get_type(args_list) == FL_VALUE_TYPE_LIST) {
            for (size_t i = 0; i < fl_value_get_length(args_list); i++) {
                FlValue* item = fl_value_get_list_value(args_list, i);
                cmd_args.push_back(fl_value_get_string(item));
            }
        } else {
            FlValue* cmd_val = fl_value_lookup_string(args, "command");
            if (cmd_val) {
                std::string cmd = fl_value_get_string(cmd_val);
                std::istringstream iss(cmd);
                std::string token;
                while (iss >> token) {
                    cmd_args.push_back(token);
                }
            }
        }

        if (cmd_args.empty()) {
            response = FL_METHOD_RESPONSE(fl_method_error_response_new("INVALID_ARGS", "No command provided", NULL));
        } else {
            int ret = run_ffmpeg_arguments(cmd_args);
            response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_int(ret)));
        }
    }
    else if (strcmp(method, "cancel") == 0) {
        ffmpeg_kit_cancel();
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(NULL));
    }
    else if (strcmp(method, "getFFmpegVersion") == 0) {
        const char* version = ffmpeg_kit_get_version();
        response = FL_METHOD_RESPONSE(fl_method_success_response_new(fl_value_new_string(version)));
    }
    else {
        response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    }

    fl_method_call_respond(method_call, response, NULL);
}

static void handle_recorder_method_call(FlMethodChannel* channel,
                                         FlMethodCall* method_call,
                                         gpointer user_data) {
    g_autoptr(FlMethodResponse) response = FL_METHOD_RESPONSE(fl_method_not_implemented_response_new());
    fl_method_call_respond(method_call, response, NULL);
}

static void fijkplayer_plugin_dispose(GObject* object) {
    FijkplayerPlugin* self = FIJKPLAYER_PLUGIN(object);

    // Clean up all players
    for (auto& pair : g_players) {
        player_destroy(pair.second);
    }
    g_players.clear();

    g_clear_object(&self->main_channel);
    g_clear_object(&self->ffmpeg_kit_channel);
    g_clear_object(&self->recorder_channel);

    G_OBJECT_CLASS(fijkplayer_plugin_parent_class)->dispose(object);
}

static void fijkplayer_plugin_class_init(FijkplayerPluginClass* klass) {
    G_OBJECT_CLASS(klass)->dispose = fijkplayer_plugin_dispose;
}

static void fijkplayer_plugin_init(FijkplayerPlugin* self) {}

void fijkplayer_plugin_register_with_registrar(FlPluginRegistrar* registrar) {
    FijkplayerPlugin* plugin = FIJKPLAYER_PLUGIN(
        g_object_new(FIJKPLAYER_TYPE_PLUGIN, NULL));

    plugin->registrar = registrar;
    plugin->texture_registrar = fl_plugin_registrar_get_texture_registrar(registrar);

    FlMethodCodec* codec = FL_METHOD_CODEC(fl_standard_method_codec_new());

    // Main channel
    plugin->main_channel = fl_method_channel_new(
        fl_plugin_registrar_get_messenger(registrar),
        "befovy.com/fijk", codec);
    fl_method_channel_set_method_call_handler(plugin->main_channel,
        handle_main_method_call, plugin, NULL);

    // FFmpegKit channel
    plugin->ffmpeg_kit_channel = fl_method_channel_new(
        fl_plugin_registrar_get_messenger(registrar),
        "befovy.com/fijk/ffmpeg_kit", codec);
    fl_method_channel_set_method_call_handler(plugin->ffmpeg_kit_channel,
        handle_ffmpeg_kit_method_call, plugin, NULL);

    // Recorder channel (stub)
    plugin->recorder_channel = fl_method_channel_new(
        fl_plugin_registrar_get_messenger(registrar),
        "befovy.com/fijk/recorder", codec);
    fl_method_channel_set_method_call_handler(plugin->recorder_channel,
        handle_recorder_method_call, plugin, NULL);

    g_object_unref(plugin);
}
