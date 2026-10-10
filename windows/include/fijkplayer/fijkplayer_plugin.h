// fijkplayer_plugin.h
// Windows/Linux plugin entry point header

#ifndef FIJKPLAYER_PLUGIN_H
#define FIJKPLAYER_PLUGIN_H

#include <flutter/plugin_registrar.h>

// The generated plugin registrant may include another Windows plugin first
// (camera_windows defines this macro for the application translation unit).
// Redefining it with a different expansion is C4005, which the runner's /WX
// turns into C2220, so keep the existing definition and otherwise match
// Flutter's own dllexport/dllimport pairing.
#if defined(FLUTTER_PLUGIN_IMPL)
#ifdef _WIN32
#define FLUTTER_PLUGIN_EXPORT __declspec(dllexport)
#else
#define FLUTTER_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif
#elif !defined(FLUTTER_PLUGIN_EXPORT)
#ifdef _WIN32
#define FLUTTER_PLUGIN_EXPORT __declspec(dllimport)
#else
#define FLUTTER_PLUGIN_EXPORT
#endif
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
