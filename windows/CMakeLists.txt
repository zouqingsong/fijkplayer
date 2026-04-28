cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME "fijkplayer")
project(${PROJECT_NAME} LANGUAGES CXX C)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "fijkplayer_plugin")

# FFmpegKit command-executor sources (Windows port from ios/FFmpegKit)
set(FFTOOLS_SOURCES
  "fftools/ffmpeg_kit_execute.c"
  "fftools/ffmpeg.c"
  "fftools/ffmpeg_opt.c"
  "fftools/ffmpeg_filter.c"
  "fftools/ffmpeg_hw.c"
  "fftools/ffmpeg_demux.c"
  "fftools/ffmpeg_mux.c"
  "fftools/ffmpeg_mux_init.c"
  "fftools/ffmpeg_dec.c"
  "fftools/ffmpeg_enc.c"
  "fftools/cmdutils.c"
  "fftools/opt_common.c"
  "fftools/objpool.c"
  "fftools/sync_queue.c"
  "fftools/thread_queue.c"
)

add_library(${PLUGIN_NAME} SHARED
  "fijkplayer_plugin.cpp"
  "fijk_player.cpp"
  "fijk_texture.cpp"
  "ffmpeg_player.c"
  ${FFTOOLS_SOURCES}
)

# Apply standard settings for the plugin
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default; explicitly export the plugin entry point
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_compile_definitions(${PLUGIN_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)

# C17 standard for C files (MSVC). Note: stdatomic.h shim provided in fftools/
# because CMake VS generator doesn't reliably apply /std:c17 for C in mixed targets.
set_property(TARGET ${PLUGIN_NAME} PROPERTY C_STANDARD 17)
set_property(TARGET ${PLUGIN_NAME} PROPERTY C_STANDARD_REQUIRED ON)

# Relax warnings for FFmpeg headers (third-party, triggers C4244 etc.)
# Must apply to both C and C++ since C++ files also include FFmpeg headers.
target_compile_options(${PLUGIN_NAME} PRIVATE /WX- /wd4244 /wd4267)
target_compile_options(${PLUGIN_NAME} PRIVATE
  "$<$<COMPILE_LANGUAGE:C>:/wd4996>"
  "$<$<COMPILE_LANGUAGE:C>:/wd4458>"
)

# FFmpeg pre-built libraries
set(FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/FFmpeg")

# Include order matters: fftools/ first (private compat headers),
# then FFmpeg/include (public API headers).
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
)
target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}/include/fijkplayer"
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/fftools"
  "${FFMPEG_DIR}/include"
)

target_link_directories(${PLUGIN_NAME} PRIVATE
  "${FFMPEG_DIR}/lib"
)

target_link_libraries(${PLUGIN_NAME} PRIVATE
  flutter
  flutter_wrapper_plugin
  avformat
  avcodec
  avutil
  swscale
  swresample
  avfilter
)

# List of absolute paths to libraries that should be bundled with the plugin.
# DLL version suffixes depend on FFmpeg version:
#   FFmpeg 6.1: avformat-60, avcodec-60, avutil-58, swscale-7, swresample-4, avfilter-9
#   FFmpeg 7.1: avformat-61, avcodec-61, avutil-59, swscale-8, swresample-5, avfilter-10
# Glob to support multiple FFmpeg versions.
file(GLOB FFMPEG_DLLS "${FFMPEG_DIR}/bin/*.dll")
set(fijkplayer_bundled_libraries
  ${FFMPEG_DLLS}
  PARENT_SCOPE
)
