cmake_minimum_required(VERSION 3.6)

project(fijkplayer)

# Set C standard
set(CMAKE_C_STANDARD 99)
set(CMAKE_CXX_STANDARD 11)

# Get the directory where CMakeLists.txt is located
set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# Option to enable software decoder for diagnostics
option(USE_SOFTWARE_DECODER "Use FFmpeg software decoder instead of MediaCodec" OFF)

if(USE_SOFTWARE_DECODER)
    message(STATUS "Using FFmpeg software decoder (diagnostic mode)")
    add_definitions(-DUSE_SOFTWARE_DECODER)
endif()

# FFmpeg headers location (our custom build)
set(FFMPEG_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/ffmpeg/include")

# FFmpeg libraries location (pre-built .so files in jniLibs)
set(FFMPEG_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../jniLibs/${ANDROID_ABI}")

message(STATUS "Building for ABI: ${ANDROID_ABI}")
message(STATUS "FFmpeg headers: ${FFMPEG_INCLUDE_DIR}")
message(STATUS "FFmpeg libraries: ${FFMPEG_LIB_DIR}")

# Check if FFmpeg headers exist
if(NOT EXISTS "${FFMPEG_INCLUDE_DIR}")
    message(FATAL_ERROR "FFmpeg headers not found at: ${FFMPEG_INCLUDE_DIR}. Please run ffmpeg/build-android.sh first.")
endif()

# Add FFmpeg demuxer library
add_library(fijkplayer_demuxer SHARED
    ffmpeg_demuxer.c
    async_io_protocol.c
)

# Add MediaCodec decoder library
add_library(fijkplayer_decoder SHARED
    mediacodec_decoder.c
)

# Add FFmpeg software decoder library (for diagnostics)
add_library(fijkplayer_soft_decoder SHARED
    ffmpeg_soft_decoder.c
)

# Add frame queue library
add_library(fijkplayer_frame_queue SHARED
    frame_queue.c
)

# Add OpenGL renderer library
add_library(fijkplayer_gl_renderer SHARED
    gl_renderer.c
)

# Add native player library (integrates all components)
add_library(fijkplayer_native_player SHARED
    native_player.c
    jni_bridge.c
    surface_texture_jni.c
    async_io_protocol.c
)

# Add FFmpeg recorder library (legacy)
add_library(fijkplayer_ffmpeg_recorder SHARED
    ffmpeg_recorder.c
)

# Include FFmpeg headers
target_include_directories(fijkplayer_demuxer PRIVATE
    ${FFMPEG_INCLUDE_DIR}
)

target_include_directories(fijkplayer_decoder PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(fijkplayer_soft_decoder PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(fijkplayer_frame_queue PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(fijkplayer_gl_renderer PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(fijkplayer_native_player PRIVATE
    ${FFMPEG_INCLUDE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(fijkplayer_ffmpeg_recorder PRIVATE
    ${FFMPEG_INCLUDE_DIR}
)

# Find system libraries
find_library(log-lib log)
find_library(android-lib android)
find_library(mediandk-lib mediandk)
find_library(gles2-lib GLESv2)
find_library(egl-lib EGL)

# Import pre-built FFmpeg libraries
add_library(avformat SHARED IMPORTED)
set_target_properties(avformat PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavformat.so
)

add_library(avcodec SHARED IMPORTED)
set_target_properties(avcodec PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavcodec.so
)

add_library(avutil SHARED IMPORTED)
set_target_properties(avutil PROPERTIES
    IMPORTED_LOCATION ${FFMPEG_LIB_DIR}/libavutil.so
)

# Link FFmpeg demuxer
target_link_libraries(fijkplayer_demuxer
    ${log-lib}
    ${android-lib}
    avformat
    avcodec
    avutil
)

# Link MediaCodec decoder
target_link_libraries(fijkplayer_decoder
    ${log-lib}
    ${android-lib}
    ${mediandk-lib}
    avformat
    avcodec
    avutil
)

# Link FFmpeg software decoder
target_link_libraries(fijkplayer_soft_decoder
    ${log-lib}
    ${android-lib}
    avcodec
    avutil
)

# Link frame queue
target_link_libraries(fijkplayer_frame_queue
    ${log-lib}
)

# Link OpenGL renderer
target_link_libraries(fijkplayer_gl_renderer
    ${log-lib}
    ${gles2-lib}
    ${egl-lib}
)

# Link native player (with all dependencies)
target_link_libraries(fijkplayer_native_player
    fijkplayer_demuxer
    fijkplayer_decoder
    fijkplayer_soft_decoder
    fijkplayer_frame_queue
    fijkplayer_gl_renderer
    ${log-lib}
    ${android-lib}
    ${mediandk-lib}
    ${gles2-lib}
    ${egl-lib}
    avformat
    avcodec
    avutil
)

# Link FFmpeg recorder
target_link_libraries(fijkplayer_ffmpeg_recorder
    ${log-lib}
    ${android-lib}
    avformat
    avcodec
    avutil
)
