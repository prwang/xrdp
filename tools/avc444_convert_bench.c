/*
 * avc444_convert_bench — deterministic, offline microbenchmark of the xrdp
 * AVC444 CPU colour-conversion (xrdp_avc444_conv_update: fill_main + fill_aux).
 *
 * WHY: on a 4K dual-monitor GFX session the video was <1 fps while the VAAPI
 * GPU sat at 0% busy and a single xrdp encoder thread was pinned. The hot path
 * is the per-pixel scalar RGB->YUV/NV12 reconstruction run TWICE (main + aux)
 * per surface per frame. This benchmark reproduces that cost with NO X server,
 * NO GPU and NO client — just the conversion on a synthetic XRGB frame — so the
 * bottleneck is measurable and regressions are catchable without a live rig.
 *
 * build (from repo root, after `make`):
 *   gcc -O2 -I xrdp -I common tools/avc444_convert_bench.c \
 *       xrdp/xrdp_avc444_convert.o common/.libs/libcommon.a -lpthread -lrt \
 *       -o /tmp/avc444_bench
 * run:
 *   /tmp/avc444_bench            # default: the observed 3840x2400 + 2560x1440
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

static double
bench_surface(int w, int h, int iters)
{
    struct xrdp_avc444_conv *conv;
    unsigned char *xrgb;
    int stride;
    int i;
    double t0;
    double t1;

    stride = w * 4;
    xrgb = (unsigned char *)malloc((size_t)stride * h);
    if (xrgb == NULL)
    {
        return -1.0;
    }
    /* a non-uniform pattern so the compiler / caches cannot cheat */
    for (i = 0; i < stride * h; i++)
    {
        xrgb[i] = (unsigned char)((i * 2654435761u) >> 24);
    }
    conv = xrdp_avc444_conv_create(w, h, 16);
    if (conv == NULL)
    {
        free(xrgb);
        return -1.0;
    }
    conv->chroma_v2 = 1; /* AVC444 v2 (0x000F), the deployed path */
    /* warm */
    xrdp_avc444_conv_update(conv, xrgb, stride, w, h);
    t0 = now_ms();
    for (i = 0; i < iters; i++)
    {
        xrdp_avc444_conv_update(conv, xrgb, stride, w, h);
    }
    t1 = now_ms();
    xrdp_avc444_conv_delete(conv);
    free(xrgb);
    return (t1 - t0) / iters;
}

int
main(int argc, char **argv)
{
    int iters = 30;
    double ms;

    if (argc >= 3)
    {
        int w = atoi(argv[1]);
        int h = atoi(argv[2]);
        ms = bench_surface(w, h, iters);
        printf("%dx%d AVC444v2 convert: %.1f ms/frame  (%.1f fps ceiling)\n",
               w, h, ms, 1000.0 / ms);
        return 0;
    }

    /* the observed live layout: 3840x2400 + 2560x1440 */
    double m0 = bench_surface(3840, 2400, iters);
    double m1 = bench_surface(2560, 1440, iters);
    printf("== AVC444v2 CPU conversion (per surface, per frame) ==\n");
    printf("  mon0 3840x2400 : %6.1f ms  (%.1f fps ceiling)\n",
           m0, 1000.0 / m0);
    printf("  mon1 2560x1440 : %6.1f ms  (%.1f fps ceiling)\n",
           m1, 1000.0 / m1);
    printf("  ---- one encoder thread does BOTH sequentially per frame ----\n");
    printf("  dual total     : %6.1f ms  (%.1f fps ceiling, conversion "
           "alone)\n", m0 + m1, 1000.0 / (m0 + m1));
    (void)ms;
    return 0;
}
