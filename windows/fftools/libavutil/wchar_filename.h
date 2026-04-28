/*
 * libavutil/wchar_filename.h — Windows UTF-8 path helpers
 *
 * Stub for FFmpeg's private wchar_filename.h.
 * Provides utf8towchar() and get_extended_win32_path() used by
 * fopen_utf8.h on Windows.
 */

#ifndef AVUTIL_WCHAR_FILENAME_H
#define AVUTIL_WCHAR_FILENAME_H

#ifdef _WIN32

#include <windows.h>
#include <errno.h>
#include <libavutil/mem.h> /* av_calloc / av_freep — public FFmpeg API */

static inline int utf8towchar(const char *filename_utf8, wchar_t **filename_w)
{
    int num_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        filename_utf8, -1, NULL, 0);
    if (num_chars <= 0) {
        *filename_w = NULL;
        return 0;
    }
    *filename_w = (wchar_t *)av_calloc(num_chars, sizeof(wchar_t));
    if (!*filename_w) {
        errno = ENOMEM;
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1,
                        *filename_w, num_chars);
    return 0;
}

static inline int get_extended_win32_path(const char *filename_utf8,
                                          wchar_t **filename_w)
{
    int num_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                        filename_utf8, -1, NULL, 0);
    if (num_chars <= 0) {
        *filename_w = NULL;
        return 0;
    }
    *filename_w = (wchar_t *)av_calloc(num_chars + 4, sizeof(wchar_t));
    if (!*filename_w) {
        errno = ENOMEM;
        return -1;
    }
    MultiByteToWideChar(CP_UTF8, 0, filename_utf8, -1,
                        *filename_w, num_chars);
    return 0;
}

/* ── Module filename helper (used by cmdutils.c) ─────── */

static inline wchar_t *get_module_filename(HMODULE module)
{
    wchar_t *path = NULL;
    DWORD size = MAX_PATH;
    DWORD len;

    for (;;) {
        wchar_t *tmp = (wchar_t *)av_calloc(size, sizeof(wchar_t));
        if (!tmp)
            return NULL;
        len = GetModuleFileNameW(module, tmp, size);
        if (len == 0) {
            av_freep(&tmp);
            return NULL;
        }
        if (len < size) {
            path = tmp;
            break;
        }
        av_freep(&tmp);
        size *= 2;
    }
    return path;
}

/* ── wchar to UTF-8 conversion (used by cmdutils.c) ──── */

static inline int wchartoutf8(const wchar_t *wstr, char **str)
{
    int len;
    if (!wstr) {
        *str = NULL;
        return -1;
    }
    len = WideCharToMultiByte(CP_UTF8, 0, wstr, -1, NULL, 0, NULL, NULL);
    if (len <= 0) {
        *str = NULL;
        return -1;
    }
    *str = (char *)av_calloc(len, sizeof(char));
    if (!*str) {
        errno = ENOMEM;
        return -1;
    }
    WideCharToMultiByte(CP_UTF8, 0, wstr, -1, *str, len, NULL, NULL);
    return 0;
}

#endif /* _WIN32 */

#endif /* AVUTIL_WCHAR_FILENAME_H */
