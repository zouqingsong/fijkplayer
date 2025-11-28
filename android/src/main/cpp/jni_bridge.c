/**
 * JNI Bridge - Java Native Interface methods
 * 
 * These methods are called from Java/Kotlin layer to control the native player
 */

#include <jni.h>
#include "native_player.h"
#include <android/log.h>
#include <libavformat/avformat.h>
#include <string.h>
#include <stdbool.h>

#define LOG_TAG "JNI"
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

// JNI method name prefix
#define JNI_METHOD(name) Java_com_befovy_fijkplayer_NativePlayer_##name

// Helper: Get player handle from Java long field
static NativePlayer* get_player(JNIEnv* env, jobject thiz) {
    jclass clazz = (*env)->GetObjectClass(env, thiz);
    if (!clazz) {
        LOGE("Failed to get object class");
        return NULL;
    }
    jfieldID field = (*env)->GetFieldID(env, clazz, "mNativeHandle", "J");
    if (!field) {
        LOGE("Failed to get field mNativeHandle");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return NULL;
    }
    jlong handle = (*env)->GetLongField(env, thiz, field);
    LOGD("get_player: handle = %lld", (long long)handle);
    return (NativePlayer*)handle;
}

// Helper: Set player handle to Java long field
static void set_player(JNIEnv* env, jobject thiz, NativePlayer* player) {
    jclass clazz = (*env)->GetObjectClass(env, thiz);
    if (!clazz) {
        LOGE("Failed to get object class in set_player");
        return;
    }
    jfieldID field = (*env)->GetFieldID(env, clazz, "mNativeHandle", "J");
    if (!field) {
        LOGE("Failed to get field mNativeHandle in set_player");
        (*env)->ExceptionDescribe(env);
        (*env)->ExceptionClear(env);
        return;
    }
    LOGD("set_player: setting handle = %p (%lld)", player, (long long)(jlong)player);
    (*env)->SetLongField(env, thiz, field, (jlong)player);
}

// JNI Methods

JNIEXPORT jlong JNICALL
JNI_METHOD(nativeInit)(JNIEnv* env, jobject thiz) {
    LOGD("nativeInit called");
    
    NativePlayer* player = native_player_create(env, thiz);
    if (!player) {
        LOGE("Failed to create native player");
        return 0;
    }
    
    LOGD("nativeInit returning handle = %p (%lld)", player, (long long)(jlong)player);
    return (jlong)player;
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeSetDataSource)(JNIEnv* env, jobject thiz, jlong handle, jstring url) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) {
        LOGE("Player not initialized");
        return;
    }
    
    const char* url_str = (*env)->GetStringUTFChars(env, url, NULL);
    native_player_set_data_source(player, url_str);
    (*env)->ReleaseStringUTFChars(env, url, url_str);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeSetSurface)(JNIEnv* env, jobject thiz, jlong handle, jobject surface) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) {
        LOGE("Player not initialized");
        return;
    }
    
    native_player_set_surface(player, env, surface);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativePrepareAsync)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) {
        LOGE("Player not initialized");
        return;
    }
    
    native_player_prepare_async(player);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeStart)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) {
        LOGE("Player not initialized");
        return;
    }
    
    native_player_start(player);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativePause)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return;
    
    native_player_pause(player);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeResume)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return;
    
    native_player_resume(player);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeStop)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return;
    
    native_player_stop(player);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeSeekTo)(JNIEnv* env, jobject thiz, jlong handle, jlong position_ms) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return;
    
    native_player_seek(player, position_ms);
}

JNIEXPORT jlong JNICALL
JNI_METHOD(nativeGetCurrentPosition)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return 0;
    
    return native_player_get_position(player);
}

JNIEXPORT jlong JNICALL
JNI_METHOD(nativeGetDuration)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return 0;
    
    return native_player_get_duration(player);
}

JNIEXPORT jdouble JNICALL
JNI_METHOD(nativeGetFrameRate)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return 0.0;
    
    return native_player_get_frame_rate(player);
}

JNIEXPORT jint JNICALL
JNI_METHOD(nativeGetVideoWidth)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return 0;
    
    return native_player_get_video_width(player);
}

JNIEXPORT jint JNICALL
JNI_METHOD(nativeGetVideoHeight)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return 0;
    
    return native_player_get_video_height(player);
}

JNIEXPORT jboolean JNICALL
JNI_METHOD(nativeIsPlaying)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return JNI_FALSE;
    
    return native_player_is_playing(player) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeRenderFrame)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return;
    
    native_player_render_frame(player);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeSetVolume)(JNIEnv* env, jobject thiz, jlong handle, jfloat volume) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return;
    
    native_player_set_volume(player, volume);
}

JNIEXPORT void JNICALL
JNI_METHOD(nativeRelease)(JNIEnv* env, jobject thiz, jlong handle) {
    NativePlayer* player = (NativePlayer*)handle;
    if (!player) return;
    
    native_player_release(player);
}

// JNI_OnLoad - called when library is loaded
JNIEXPORT jint JNICALL
JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGD("JNI_OnLoad called");
    
    // Initialize FFmpeg network support for HTTP/HTTPS URLs
    int ret = avformat_network_init();
    if (ret < 0) {
        LOGE("Failed to initialize FFmpeg network: %d", ret);
    } else {
        LOGD("FFmpeg network initialized successfully");
    }
    
    // Log available protocols for debugging
    void* opaque = NULL;
    const char* protocol;
    bool has_http = false, has_https = false, has_tls = false;
    
    LOGD("Available input protocols:");
    while ((protocol = avio_enum_protocols(&opaque, 0)) != NULL) {
        LOGD("  - %s", protocol);
        if (strcmp(protocol, "http") == 0) has_http = true;
        if (strcmp(protocol, "https") == 0) has_https = true;
        if (strcmp(protocol, "tls") == 0) has_tls = true;
    }
    
    LOGD("Protocol support: HTTP=%s, HTTPS=%s, TLS=%s",
         has_http ? "YES" : "NO",
         has_https ? "YES" : "NO",
         has_tls ? "YES" : "NO");
    
    return JNI_VERSION_1_6;
}
