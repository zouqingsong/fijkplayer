// ffmpeg_kit_execute.h
// Thread-safe wrapper for FFmpeg command execution
// Replaces ffmpeg_kit_flutter dependency

#ifndef FFMPEG_KIT_EXECUTE_H
#define FFMPEG_KIT_EXECUTE_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Execute an FFmpeg command.
 * Thread-safe: serialized with a mutex (only one command at a time).
 *
 * @param argc Number of arguments
 * @param argv Argument array (argv[0] should be "ffmpeg")
 * @return 0 on success, non-zero on error
 */
int ffmpeg_kit_execute(int argc, char **argv);

/**
 * Cancel the currently running FFmpeg command.
 */
void ffmpeg_kit_cancel(void);

/**
 * Get FFmpeg version string.
 * @return Static version string
 */
const char *ffmpeg_kit_get_version(void);

#ifdef __cplusplus
}
#endif

#endif // FFMPEG_KIT_EXECUTE_H
