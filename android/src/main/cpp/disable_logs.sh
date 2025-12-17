#!/bin/bash
# Add #ifdef to disable all LOGI statements
sed -i '' '1i\
#define DISABLE_VERBOSE_LOGS 1
' native_player.c audio_decoder.c

# Redefine LOGI to do nothing when DISABLE_VERBOSE_LOGS is set
for file in native_player.c audio_decoder.c; do
  # Find the LOGI definition and add conditional
  sed -i '' 's/#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)/#ifdef DISABLE_VERBOSE_LOGS\
#define LOGI(...) \/\/ __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)\
#else\
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)\
#endif/' "$file"
done
