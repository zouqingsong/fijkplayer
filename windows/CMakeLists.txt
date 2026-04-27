cmake_minimum_required(VERSION 3.14)

set(PROJECT_NAME "fijkplayer")
project(${PROJECT_NAME} LANGUAGES CXX C)

# This value is used when generating builds using this plugin, so it must
# not be changed
set(PLUGIN_NAME "fijkplayer_plugin")

# Shared C sources from iOS (platform-independent FFmpeg code)
set(SHARED_SOURCES
  "../ios/FFmpegKit/ffmpeg_kit_execute.c"
  "../ios/FFmpegKit/ffmpeg.c"
  "../ios/FFmpegKit/ffmpeg_opt.c"
  "../ios/FFmpegKit/ffmpeg_filter.c"
  "../ios/FFmpegKit/ffmpeg_hw.c"
  "../ios/FFmpegKit/ffmpeg_demux.c"
  "../ios/FFmpegKit/ffmpeg_mux.c"
  "../ios/FFmpegKit/ffmpeg_mux_init.c"
  "../ios/FFmpegKit/ffmpeg_dec.c"
  "../ios/FFmpegKit/ffmpeg_enc.c"
  "../ios/FFmpegKit/cmdutils.c"
  "../ios/FFmpegKit/opt_common.c"
  "../ios/FFmpegKit/objpool.c"
  "../ios/FFmpegKit/sync_queue.c"
  "../ios/FFmpegKit/thread_queue.c"
)

add_library(${PLUGIN_NAME} SHARED
  "fijkplayer_plugin.cpp"
  "fijk_player.cpp"
  "fijk_texture.cpp"
  "ffmpeg_player.c"
  ${SHARED_SOURCES}
)

# Apply standard settings for the plugin
apply_standard_settings(${PLUGIN_NAME})

# Symbols are hidden by default; explicitly export the plugin entry point
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)

target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)
target_compile_definitions(${PLUGIN_NAME} PRIVATE HAVE_PTHREADS)

# FFmpeg include path (pre-built libraries)
set(FFMPEG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/FFmpeg")

target_include_directories(${PLUGIN_NAME} PRIVATE
  "${CMAKE_CURRENT_SOURCE_DIR}"
  "${FFMPEG_DIR}/include"
  "${CMAKE_CURRENT_SOURCE_DIR}/../ios/FFmpegKit"
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

# List of absolute paths to libraries that should be bundled with the plugin
set(fijkplayer_bundled_libraries
  "${FFMPEG_DIR}/bin/avformat-60.dll"
  "${FFMPEG_DIR}/bin/avcodec-60.dll"
  "${FFMPEG_DIR}/bin/avutil-58.dll"
  "${FFMPEG_DIR}/bin/swscale-7.dll"
  "${FFMPEG_DIR}/bin/swresample-4.dll"
  "${FFMPEG_DIR}/bin/avfilter-9.dll"
  PARENT_SCOPE
)
