/*
 * avc444_convert_bench - deterministic, offline profiling of the AVC444
 * colour-conversion split, before vs after moving the RGB->YUV matrix to
 * xorgxrdp.
 *
 * BEFORE: xrdp did a scalar per-pixel RGB->YUV709fr matrix twice per surface
 *   (fill_main + fill_aux), which pinned one encoder thread at 4K (~167 ms for
 *   the observed 3840x2400 + 2560x1440 dual-monitor layout -> <1 fps).
 * AFTER: xorgxrdp emits full-chroma YUV444 (autovectorized, capture-side) and
 *   xrdp only subsamples (main 4:2:0) + repacks (aux). This benchmark times
 *   xrdp's NEW conv_update (YUV444 -> two NV12 views, no matrix).
 *
 * build (from repo root, after `make`):
 *   gcc -O2 -I xrdp -I common tools/avc444_convert_bench.c \
 *       xrdp/xrdp_avc444_convert.o common/.libs/libcommon.a -lpthread -lrt -ldl \
 *       -o /tmp/avc444_bench
 * run:
 *   /tmp/avc444_bench            # 3840x2400 + 2560x1440
 *   /tmp/avc444_bench 1920 1080  # single custom surface
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "xrdp_avc444_convert.h"

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

/* xrdp's NEW work: YUV444 planes -> main + aux NV12 (subsample + repack) */
static double
bench_xrdp_repack(int w, int h, int iters)
{
    struct xrdp_avc444_conv *conv;
    unsigned char *yuv;
    int pstride = (w + 15) & ~15;
    int ch16 = (h + 15) & ~15;
    size_t area = (size_t)pstride * ch16;
    int i;
    double t0;

    yuv = (unsigned char *)malloc(area * 3);
    if (yuv == NULL)
    {
        return -1.0;
    }
    for (i = 0; i < (int)(area * 3); i++)
    {
        yuv[i] = (unsigned char)((i * 2654435761u) >> 24);
    }
    conv = xrdp_avc444_conv_create(w, h, 16);
    if (conv == NULL)
    {
        free(yuv);
        return -1.0;
    }
    conv->chroma_v2 = 1;
    xrdp_avc444_conv_update(conv, yuv, pstride, w, h); /* warm */
    t0 = now_ms();
    for (i = 0; i < iters; i++)
    {
        xrdp_avc444_conv_update(conv, yuv, pstride, w, h);
    }
    double ms = (now_ms() - t0) / iters;
    xrdp_avc444_conv_delete(conv);
    free(yuv);
    return ms;
}

int
main(int argc, char **argv)
{
    int iters = 30;

    if (argc >= 3)
    {
        int w = atoi(argv[1]);
        int h = atoi(argv[2]);
        double ms = bench_xrdp_repack(w, h, iters);
        printf("%dx%d xrdp YUV444->NV12 repack: %.1f ms/frame (%.1f fps)\n",
               w, h, ms, 1000.0 / ms);
        return 0;
    }

    double m0 = bench_xrdp_repack(3840, 2400, iters);
    double m1 = bench_xrdp_repack(2560, 1440, iters);
    printf("== xrdp encoder-thread convert AFTER (YUV444 -> NV12, no matrix) "
           "==\n");
    printf("  mon0 3840x2400 : %6.1f ms\n", m0);
    printf("  mon1 2560x1440 : %6.1f ms\n", m1);
    printf("  dual total     : %6.1f ms  (%.1f fps ceiling, xrdp side)\n",
           m0 + m1, 1000.0 / (m0 + m1));
    printf("  (was ~167 ms / 6 fps when xrdp did the RGB->YUV matrix; the\n");
    printf("   matrix now runs autovectorized capture-side in xorgxrdp,\n");
    printf("   ~2-6 ms/frame on a different thread.)\n");
    return 0;
}
