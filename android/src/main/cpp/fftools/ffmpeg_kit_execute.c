// ffmpeg_kit_execute.c
// Thread-safe wrapper for FFmpeg command execution

#include "ffmpeg_kit_execute.h"
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <pthread.h>
#include <string.h>

// Forward declaration of patched ffmpeg main
extern int ffmpeg_main(int argc, char **argv);

// Mutex for serializing FFmpeg commands
static pthread_mutex_t execute_mutex = PTHREAD_MUTEX_INITIALIZER;

// Cancel flag (accessed from signal handler in ffmpeg.c)
extern volatile int received_sigterm;
extern volatile int received_nb_signals;

int ffmpeg_kit_execute(int argc, char **argv) {
    int ret;
    
    pthread_mutex_lock(&execute_mutex);
    
    // Reset global state before execution
    received_sigterm = 0;
    received_nb_signals = 0;
    
    ret = ffmpeg_main(argc, argv);
    
    pthread_mutex_unlock(&execute_mutex);
    
    return ret;
}

void ffmpeg_kit_cancel(void) {
    // Signal the running FFmpeg to stop
    received_sigterm = 1;
    received_nb_signals = 1;
}

const char *ffmpeg_kit_get_version(void) {
    return av_version_info();
}
