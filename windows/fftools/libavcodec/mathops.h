/*
 * libavcodec/mathops.h — minimal stub
 *
 * Provides mid_pred() used by ffmpeg_filter.c.
 * The full FFmpeg mathops.h is a private header with many more helpers;
 * only the subset actually used by fftools is provided here.
 */

#ifndef AVCODEC_MATHOPS_H
#define AVCODEC_MATHOPS_H

#include <libavutil/attributes.h> /* av_const, av_always_inline */

#ifndef mid_pred
#define mid_pred mid_pred
static av_always_inline av_const int mid_pred(int a, int b, int c)
{
    if (a > b) {
        if (c > b) {
            if (c > a) b = a;
            else       b = c;
        }
    } else {
        if (b > c) {
            if (c > a) b = c;
            else       b = a;
        }
    }
    return b;
}
#endif

#endif /* AVCODEC_MATHOPS_H */
