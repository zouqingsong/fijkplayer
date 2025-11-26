cmake_minimum_required(VERSION 3.6)

project(fijkplayer_ffmpeg_recorder)

# Set C standard
set(CMAKE_C_STANDARD 99)

# Support 16KB page sizes for Android 15+
# https://developer.android.com/guide/practices/page-sizes
if(ANDROID)
    set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -Wl,-z,max-page-size=16384")
endif()

# Add FFmpeg recorder source
add_library(fijkplayer_ffmpeg_recorder SHARED
    ffmpeg_recorder.c
)

# Find system libraries
find_library(log-lib log)
find_library(android-lib android)

# Try to find ijkplayer build output directory
set(IJKPLAYER_BUILD_DIR "/Users/qingsong/Documents/GitHub/ijkplayer/android/contrib/build")

# Map Android ABI to ijkplayer output directory
if(${ANDROID_ABI} STREQUAL "armeabi-v7a")
    set(FFMPEG_INCLUDE_DIR "${IJKPLAYER_BUILD_DIR}/output-armv7a/include")
    set(FFMPEG_OUTPUT_DIR "${IJKPLAYER_BUILD_DIR}/output-armv7a")
elseif(${ANDROID_ABI} STREQUAL "arm64-v8a")
    set(FFMPEG_INCLUDE_DIR "${IJKPLAYER_BUILD_DIR}/output-arm64/include")
    set(FFMPEG_OUTPUT_DIR "${IJKPLAYER_BUILD_DIR}/output-arm64")
elseif(${ANDROID_ABI} STREQUAL "x86")
    set(FFMPEG_INCLUDE_DIR "${IJKPLAYER_BUILD_DIR}/output-x86/include")
    set(FFMPEG_OUTPUT_DIR "${IJKPLAYER_BUILD_DIR}/output-x86")
elseif(${ANDROID_ABI} STREQUAL "x86_64")
    set(FFMPEG_INCLUDE_DIR "${IJKPLAYER_BUILD_DIR}/output-x86_64/include")
    set(FFMPEG_OUTPUT_DIR "${IJKPLAYER_BUILD_DIR}/output-x86_64")
endif()

# Check if FFmpeg headers exist
if(EXISTS "${FFMPEG_INCLUDE_DIR}")
    message(STATUS "Using FFmpeg headers from: ${FFMPEG_INCLUDE_DIR}")
    target_include_directories(fijkplayer_ffmpeg_recorder PRIVATE
        ${FFMPEG_INCLUDE_DIR}
    )
else()
    message(FATAL_ERROR "FFmpeg headers not found at: ${FFMPEG_INCLUDE_DIR}. Please build ijkplayer first.")
endif()

# Import ijkffmpeg as a prebuilt shared library (for compilation only, not bundling)
# The actual library comes from fijkplayer-full AAR dependency
set(IJKFFMPEG_LIB_PATH "${FFMPEG_OUTPUT_DIR}/libijkffmpeg.so")
if(EXISTS "${IJKFFMPEG_LIB_PATH}")
    message(STATUS "Using ijkffmpeg library for linking: ${IJKFFMPEG_LIB_PATH}")
    add_library(ijkffmpeg SHARED IMPORTED)
    set_target_properties(ijkffmpeg PROPERTIES
        IMPORTED_LOCATION ${IJKFFMPEG_LIB_PATH}
        IMPORTED_NO_SONAME TRUE
    )
else()
    message(WARNING "libijkffmpeg.so not found at: ${IJKFFMPEG_LIB_PATH}")
    message(WARNING "Will link against ijkffmpeg from fijkplayer-full AAR at runtime")
    # Create a dummy target - we'll link dynamically at runtime
    add_library(ijkffmpeg SHARED IMPORTED)
    set_target_properties(ijkffmpeg PROPERTIES
        IMPORTED_NO_SONAME TRUE
    )
endif()

# Link libraries
# Note: ijkffmpeg is linked for symbol resolution but won't be bundled
# It will be loaded at runtime from the fijkplayer-full AAR
target_link_libraries(fijkplayer_ffmpeg_recorder
    ${log-lib}
    ${android-lib}
    ijkffmpeg
)
