#include <jni.h>
#include <android/log.h>
#include <stdbool.h>
#include <pthread.h>
#include <time.h>

#define LOG_TAG "SurfaceTextureJNI"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

/**
 * JNI implementation for SurfaceTextureManager callbacks
 * This provides the native side of the frame update notification system
 */

// Track frame update state
typedef struct {
    bool frame_updated;
    pthread_mutex_t lock;
    pthread_cond_t cond;
} FrameUpdateState;

static FrameUpdateState g_frame_state = {
    .frame_updated = false,
    .lock = PTHREAD_MUTEX_INITIALIZER,
    .cond = PTHREAD_COND_INITIALIZER
};

/**
 * Called from Java when SurfaceTexture.updateTexImage() completes
 * This is the callback from SurfaceTextureManager.nativeOnFrameUpdated()
 */
JNIEXPORT void JNICALL
Java_com_befovy_fijkplayer_SurfaceTextureManager_nativeOnFrameUpdated(
    JNIEnv* env, jobject thiz, jlong native_handle)
{
    LOGD("Native frame updated callback, handle=%lld", (long long)native_handle);
    
    pthread_mutex_lock(&g_frame_state.lock);
    g_frame_state.frame_updated = true;
    pthread_cond_signal(&g_frame_state.cond);
    pthread_mutex_unlock(&g_frame_state.lock);
}

/**
 * Wait for frame update from SurfaceTexture
 * Can be called from native player code
 */
bool wait_for_frame_update(int timeout_ms) {
    pthread_mutex_lock(&g_frame_state.lock);
    
    if (g_frame_state.frame_updated) {
        g_frame_state.frame_updated = false;
        pthread_mutex_unlock(&g_frame_state.lock);
        return true;
    }
    
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeout_ms / 1000;
    ts.tv_nsec += (timeout_ms % 1000) * 1000000;
    if (ts.tv_nsec >= 1000000000) {
        ts.tv_sec++;
        ts.tv_nsec -= 1000000000;
    }
    
    int ret = pthread_cond_timedwait(&g_frame_state.cond, &g_frame_state.lock, &ts);
    
    bool updated = g_frame_state.frame_updated;
    if (updated) {
        g_frame_state.frame_updated = false;
    }
    
    pthread_mutex_unlock(&g_frame_state.lock);
    
    return updated;
}

/**
 * Reset frame update state
 */
void reset_frame_update_state(void) {
    pthread_mutex_lock(&g_frame_state.lock);
    g_frame_state.frame_updated = false;
    pthread_mutex_unlock(&g_frame_state.lock);
}
