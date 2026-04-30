/*
 * libavutil/getenv_utf8.h — getenv wrapper
 *
 * Stub for FFmpeg's private getenv_utf8.h.
 * On Windows we just use the standard getenv() for simplicity.
 */

#ifndef AVUTIL_GETENV_UTF8_H
#define AVUTIL_GETENV_UTF8_H

#include <stdlib.h>

static inline char *getenv_utf8(const char *varname)
{
    return getenv(varname);
}

static inline void freeenv_utf8(char *var)
{
    /* getenv returns a pointer into the environment block — don't free */
    (void)var;
}

#endif /* AVUTIL_GETENV_UTF8_H */
