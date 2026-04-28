/*
 * config.h for Windows/MSVC — fijkplayer fftools
 *
 * Provides feature-detection macros matching BtbN's pre-built
 * FFmpeg 6.1 LGPL shared libraries for Windows x64.
 */

#ifndef FFTOOLS_CONFIG_H
#define FFTOOLS_CONFIG_H

/* ── Threading ─────────────────────────────────────────── */
#define HAVE_PTHREADS                   0
#define HAVE_W32THREADS                 1
#define HAVE_THREADS                    1

/* ── System headers ────────────────────────────────────── */
#define HAVE_IO_H                       1
#define HAVE_UNISTD_H                   0
#define HAVE_SYS_RESOURCE_H             0
#define HAVE_SYS_SELECT_H              0
#define HAVE_SYS_TIME_H                0
#define HAVE_TERMIOS_H                  0

/* ── Windows APIs ──────────────────────────────────────── */
#define HAVE_GETPROCESSTIMES            1
#define HAVE_GETPROCESSMEMORYINFO       1
#define HAVE_SETCONSOLECTRLHANDLER      1
#define HAVE_KBHIT                      1
#define HAVE_PEEKNAMEDPIPE              1
#define HAVE_GETSTDHANDLE               1
#define HAVE_COMMANDLINETOARGVW         1
#define HAVE_SETDLLDIRECTORY            1
#define HAVE_GETMODULEHANDLE            1

/* ── POSIX APIs (not available on Windows) ─────────────── */
#define HAVE_GETRUSAGE                  0
#define HAVE_STRUCT_RUSAGE_RU_MAXRSS    0

/* ── FFmpeg library components ─────────────────────────── */
#define CONFIG_AVUTIL                   1
#define CONFIG_AVCODEC                  1
#define CONFIG_AVFORMAT                 1
#define CONFIG_AVFILTER                 1
#define CONFIG_AVDEVICE                 0
#define CONFIG_SWSCALE                  1
#define CONFIG_SWRESAMPLE               1
#define CONFIG_POSTPROC                 0

/* ── License ───────────────────────────────────────────── */
#define CONFIG_NONFREE                  0
#define CONFIG_GPL                      0
#define CONFIG_GPLV3                    0
#define CONFIG_LGPLV3                   0

/* ── Version / identification ──────────────────────────── */
#define FFMPEG_CONFIGURATION \
    "--enable-shared --disable-static --enable-small --disable-debug"
#ifndef FFMPEG_VERSION
#define FFMPEG_VERSION                  "7.1"
#endif
#define CONFIG_THIS_YEAR                2024
#define CC_IDENT                        "MSVC"
#define FFMPEG_DATADIR                  "."
#define AVCONV_DATADIR                  "."

#endif /* FFTOOLS_CONFIG_H */
