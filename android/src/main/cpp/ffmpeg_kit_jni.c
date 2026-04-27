// ffmpeg_kit_jni.c
// JNI bridge for FFmpeg command execution on Android

#include <jni.h>
#include <string.h>
#include <android/log.h>
#include "fftools/ffmpeg_kit_execute.h"
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>

#define LOG_TAG "FijkFFmpegKit"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

JNIEXPORT jint JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegKit_nativeExecuteFFmpegCommand(
    JNIEnv *env, jobject thiz, jstring command) {
    
    const char *cmd = (*env)->GetStringUTFChars(env, command, NULL);
    if (!cmd) return -1;
    
    // Parse command string into argv
    int argc = 1;
    char *argv[256];
    argv[0] = strdup("ffmpeg");
    
    char *cmd_copy = strdup(cmd);
    char *token = strtok(cmd_copy, " ");
    while (token && argc < 255) {
        argv[argc++] = strdup(token);
        token = strtok(NULL, " ");
    }
    argv[argc] = NULL;
    
    (*env)->ReleaseStringUTFChars(env, command, cmd);
    free(cmd_copy);
    
    LOGI("Executing FFmpeg with %d args", argc);
    int ret = ffmpeg_kit_execute(argc, argv);
    LOGI("FFmpeg returned: %d", ret);
    
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    
    return ret;
}

JNIEXPORT jint JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegKit_nativeExecuteFFmpegCommandArray(
    JNIEnv *env, jobject thiz, jobjectArray arguments) {
    
    int len = (*env)->GetArrayLength(env, arguments);
    int argc = len + 1;
    char **argv = (char **)malloc(sizeof(char *) * (argc + 1));
    argv[0] = strdup("ffmpeg");
    
    for (int i = 0; i < len; i++) {
        jstring jarg = (jstring)(*env)->GetObjectArrayElement(env, arguments, i);
        const char *arg = (*env)->GetStringUTFChars(env, jarg, NULL);
        argv[i + 1] = strdup(arg);
        (*env)->ReleaseStringUTFChars(env, jarg, arg);
        (*env)->DeleteLocalRef(env, jarg);
    }
    argv[argc] = NULL;
    
    LOGI("Executing FFmpeg with %d args", argc);
    int ret = ffmpeg_kit_execute(argc, argv);
    LOGI("FFmpeg returned: %d", ret);
    
    for (int i = 0; i < argc; i++) {
        free(argv[i]);
    }
    free(argv);
    
    return ret;
}

JNIEXPORT jboolean JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegKit_nativeCancelExecution(
    JNIEnv *env, jobject thiz, jint executionId) {
    ffmpeg_kit_cancel();
    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegKit_nativeGetMediaInfo(
    JNIEnv *env, jobject thiz, jstring path) {
    // TODO: Implement media info retrieval
    LOGI("getMediaInfo not yet implemented");
}

JNIEXPORT jstring JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegKit_nativeGetFFmpegVersion(
    JNIEnv *env, jobject thiz) {
    const char *version = ffmpeg_kit_get_version();
    return (*env)->NewStringUTF(env, version);
}

JNIEXPORT jobjectArray JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegKit_nativeGetSupportedFormats(
    JNIEnv *env, jobject thiz) {
    // Return empty array for now
    jclass stringClass = (*env)->FindClass(env, "java/lang/String");
    return (*env)->NewObjectArray(env, 0, stringClass, NULL);
}

JNIEXPORT jobjectArray JNICALL
Java_com_befovy_fijkplayer_FijkFFmpegKit_nativeGetSupportedCodecs(
    JNIEnv *env, jobject thiz) {
    // Return empty array for now
    jclass stringClass = (*env)->FindClass(env, "java/lang/String");
    return (*env)->NewObjectArray(env, 0, stringClass, NULL);
}
