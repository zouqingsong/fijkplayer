// fijkplayer_plugin.h
// Windows/Linux plugin entry point header

#ifndef FIJKPLAYER_PLUGIN_H
#define FIJKPLAYER_PLUGIN_H

#include <flutter/plugin_registrar.h>

#ifdef FLUTTER_PLUGIN_IMPL
#ifdef _WIN32
#define FLUTTER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#else
#define FLUTTER_PLUGIN_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

FLUTTER_PLUGIN_EXPORT void FijkplayerPluginRegisterWithRegistrar(
    FlutterDesktopPluginRegistrarRef registrar);

#ifdef __cplusplus
}
#endif

#endif // FIJKPLAYER_PLUGIN_H
