/*
 * avc444_pack_bench — offline ms/frame benchmark for the AVC444 capture
 * conversion loops (no X server / session / GPU needed).
 *
 * Compares, over identical pseudo-random ARGB input:
 *   old   : the pre-FR-CAPTURE-6 xorgxrdp pass (ARGB -> planar YUV444,
 *           RDP_VECTORIZE'd single flat loop) — NOTE this was only HALF
 *           the old pipeline; xrdp then re-walked every pixel again
 *           (xrdp_avc444_conv_update) plus staging memcpy + pipe write.
 *   scalar: the FIRST-CUT FR-CAPTURE-6 packers (xorgxrdp 75c19283ab87) —
 *           kept as the NEGATIVE EXAMPLE for PRD FR-CAPTURE-7: per-sample
 *           helper calls with clamp branches, U and V re-decoding the
 *           same pixel, no RDP_VECTORIZE attribute. ~7x slower/pixel.
 *   vector: the row-decode restructure shipped after the fix
 *           (ARGB -> [main NV12][aux NV12 ChromaV2]), verbatim copies —
 *           the ONLY pixel pass in the new pipeline.
 *
 * Function bodies are verbatim copies from module/rdpCapture.c (xorgxrdp
 * dd431cc156fd for "old", 75c19283ab87 for "new") so the compiler sees
 * exactly the shipped code shape; keep them in sync when the packers
 * change. Build like the module builds (-O2; the per-function attributes
 * carry their own O3/vectorize flags):
 *
 *   gcc -O2 -o avc444_pack_bench avc444_pack_bench.c
 *
 * Damage cases: full 4K (3840x2400), 2000x1000 and 500x200 rects — the
 * drag-cost-vs-damage-size question (2026-07-26).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <time.h>

#define RDPCLAMP(v, lo, hi) ((v) < (lo) ? (lo) : (v) > (hi) ? (hi) : (v))

#if defined(__GNUC__) && !defined(__clang__)
#if defined(__x86_64__) || defined(__i386__)
#define RDP_VECTORIZE \
    __attribute__((optimize("O3", "tree-vectorize"), \
                   target_clones("default", "avx2")))
#else
#define RDP_VECTORIZE __attribute__((optimize("O3", "tree-vectorize")))
#endif
#define RDP_LOOP_VECTORIZE
#else
#define RDP_VECTORIZE
#define RDP_LOOP_VECTORIZE
#endif

/* ------------------------- OLD: planar YUV444 ------------------------- */
/* verbatim from xorgxrdp dd431cc156fd module/rdpCapture.c */
RDP_VECTORIZE
static int
a8r8g8b8_to_yuv444_709fr_box(const uint8_t *s8, int src_stride,
                             uint8_t *d8_y, uint8_t *d8_u, uint8_t *d8_v,
                             int dst_stride, int width, int height)
{
    int index;
    int jndex;

    for (jndex = 0; jndex < height; jndex++)
    {
        const uint32_t *s32 = (const uint32_t *) (s8 + src_stride * jndex);
        uint8_t *yp = d8_y + dst_stride * jndex;
        uint8_t *up = d8_u + dst_stride * jndex;
        uint8_t *vp = d8_v + dst_stride * jndex;
        RDP_LOOP_VECTORIZE
        for (index = 0; index < width; index++)
        {
            int pixel = s32[index];
            int R = (pixel >> 16) & 0xff;
            int G = (pixel >>  8) & 0xff;
            int B = (pixel >>  0) & 0xff;
            int Y =  ( 54 * R + 183 * G +  18 * B) >> 8;
            int U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
            int V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
            yp[index] = RDPCLAMP(Y, 0, 255);
            up[index] = RDPCLAMP(U, 0, 255);
            vp[index] = RDPCLAMP(V, 0, 255);
        }
    }
    return 0;
}

/* ------------------- NEW: packed wire views (shipped) ------------------- */
/* verbatim from xorgxrdp 75c19283ab87 module/rdpCapture.c */

static uint32_t
avc444_px(const uint8_t *src, int src_stride, int x, int y, int w, int h)
{
    x = (x < w) ? x : (w - 1);
    y = (y < h) ? y : (h - 1);
    return *((const uint32_t *) (src + src_stride * y + x * 4));
}

static int
avc444_px_u(uint32_t pixel)
{
    int R = (pixel >> 16) & 0xff;
    int G = (pixel >>  8) & 0xff;
    int B = (pixel >>  0) & 0xff;
    int U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
    return RDPCLAMP(U, 0, 255);
}

static int
avc444_px_v(uint32_t pixel)
{
    int R = (pixel >> 16) & 0xff;
    int G = (pixel >>  8) & 0xff;
    int B = (pixel >>  0) & 0xff;
    int V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
    return RDPCLAMP(V, 0, 255);
}

static int
a8r8g8b8_to_avc444_main_box(const uint8_t *src, int src_stride,
                            uint8_t *dst_y, uint8_t *dst_uv, int cw,
                            int x1, int y1, int xe, int ye,
                            int w, int h, int point_chroma)
{
    int x;
    int y;
    int cx;
    int cy;

    for (y = y1; y < ye; y++)
    {
        const uint32_t *s32 =
            (const uint32_t *) (src + src_stride * ((y < h) ? y : (h - 1)));
        uint8_t *yp = dst_y + cw * y;
        int xlim = (xe < w) ? xe : w;
        RDP_LOOP_VECTORIZE
        for (x = x1; x < xlim; x++)
        {
            int pixel = s32[x];
            int R = (pixel >> 16) & 0xff;
            int G = (pixel >>  8) & 0xff;
            int B = (pixel >>  0) & 0xff;
            int Y = (54 * R + 183 * G + 18 * B) >> 8;
            yp[x] = RDPCLAMP(Y, 0, 255);
        }
        for (x = xlim; x < xe; x++)
        {
            int pixel = s32[w - 1];
            int R = (pixel >> 16) & 0xff;
            int G = (pixel >>  8) & 0xff;
            int B = (pixel >>  0) & 0xff;
            int Y = (54 * R + 183 * G + 18 * B) >> 8;
            yp[x] = RDPCLAMP(Y, 0, 255);
        }
    }
    for (cy = y1 / 2; cy < ye / 2; cy++)
    {
        uint8_t *uvp = dst_uv + cw * cy;
        for (cx = x1 / 2; cx < xe / 2; cx++)
        {
            if (point_chroma)
            {
                uint32_t p = avc444_px(src, src_stride,
                                       2 * cx, 2 * cy, w, h);
                uvp[2 * cx] = avc444_px_u(p);
                uvp[2 * cx + 1] = avc444_px_v(p);
            }
            else
            {
                uint32_t p0 = avc444_px(src, src_stride,
                                        2 * cx, 2 * cy, w, h);
                uint32_t p1 = avc444_px(src, src_stride,
                                        2 * cx + 1, 2 * cy, w, h);
                uint32_t p2 = avc444_px(src, src_stride,
                                        2 * cx, 2 * cy + 1, w, h);
                uint32_t p3 = avc444_px(src, src_stride,
                                        2 * cx + 1, 2 * cy + 1, w, h);
                uvp[2 * cx] = (avc444_px_u(p0) + avc444_px_u(p1) +
                               avc444_px_u(p2) + avc444_px_u(p3) + 2) / 4;
                uvp[2 * cx + 1] = (avc444_px_v(p0) + avc444_px_v(p1) +
                                   avc444_px_v(p2) + avc444_px_v(p3) + 2) / 4;
            }
        }
    }
    return 0;
}

static int
a8r8g8b8_to_avc444v2_aux_box(const uint8_t *src, int src_stride,
                             uint8_t *dst_ay, uint8_t *dst_auv, int cw,
                             int x1, int y1, int xe, int ye,
                             int w, int h)
{
    int x;
    int y;
    int cx;
    int cy;

    for (y = y1; y < ye; y++)
    {
        uint8_t *yp = dst_ay + cw * y;
        for (cx = x1 / 2; cx < (xe + 1) / 2; cx++)
        {
            uint32_t p = avc444_px(src, src_stride, 2 * cx + 1, y, w, h);
            yp[cx] = avc444_px_u(p);
            yp[cw / 2 + cx] = avc444_px_v(p);
        }
    }
    for (cy = y1 / 2; cy < ye / 2; cy++)
    {
        uint8_t *uvp = dst_auv + cw * cy;
        int row = 2 * cy + 1;
        for (x = x1 / 4; x < (xe + 3) / 4 && x < cw / 4; x++)
        {
            uint32_t pa = avc444_px(src, src_stride, 4 * x, row, w, h);
            uint32_t pb = avc444_px(src, src_stride, 4 * x + 2, row, w, h);
            uvp[2 * x] = avc444_px_u(pa);
            uvp[2 * x + 1] = avc444_px_u(pb);
            uvp[2 * (cw / 4 + x)] = avc444_px_v(pa);
            uvp[2 * (cw / 4 + x) + 1] = avc444_px_v(pb);
        }
    }
    return 0;
}

/* --------------- OPTIMIZED: shipped after the fix (75c1928+) --------------- */
/* decode one source row through the SAME vectorized flat loop as the
 * historic planar converter: Y lands directly in the main view, U/V in
 * row buffers for the pack steps below. Edge replication (right pad /
 * odd-width last column) is handled here once, so the pack loops run
 * clamp- and branch-free. */
RDP_VECTORIZE
static void
avc444_decode_row(const uint32_t *s32, uint8_t *yp, uint8_t *urow,
                  uint8_t *vrow, int x1, int xe, int w)
{
    int x;
    int xlim = (xe < w) ? xe : w;

    RDP_LOOP_VECTORIZE
    for (x = x1; x < xlim; x++)
    {
        int pixel = s32[x];
        int R = (pixel >> 16) & 0xff;
        int G = (pixel >>  8) & 0xff;
        int B = (pixel >>  0) & 0xff;
        int Y =  ( 54 * R + 183 * G +  18 * B) >> 8;
        int U = ((-29 * R -  99 * G + 128 * B) >> 8) + 128;
        int V = ((128 * R - 116 * G -  12 * B) >> 8) + 128;
        yp[x] = RDPCLAMP(Y, 0, 255);
        urow[x] = RDPCLAMP(U, 0, 255);
        vrow[x] = RDPCLAMP(V, 0, 255);
    }
    for (x = xlim; x < xe; x++)
    {
        yp[x] = yp[xlim - 1];
        urow[x] = urow[xlim - 1];
        vrow[x] = vrow[xlim - 1];
    }
    /* replicated slack so the pack loops read odd/4-grid neighbours
     * (2cx+1, 4x+2) branch-free at the rect edge; the row buffers carry
     * 16 spare bytes for this */
    for (x = xe; x < xe + 4; x++)
    {
        urow[x] = urow[xe - 1];
        vrow[x] = vrow[xe - 1];
    }
}

/* U/V row buffers for one 2-row band. The capture path runs on the
 * single X server thread, so plain static storage is safe; 16384 is the
 * coded-width ceiling. */
static uint8_t g_avc444_u0[16384 + 16];
static uint8_t g_avc444_v0[16384 + 16];
static uint8_t g_avc444_u1[16384 + 16];
static uint8_t g_avc444_v1[16384 + 16];

/* main view (+ optional ChromaV2 aux view) for one damage rect, two
 * source rows per iteration. aux_y == NULL packs the main view only
 * (external AVC420, and the v1 mode whose banded aux is built by
 * a8r8g8b8_to_avc444v1_aux below). point_chroma: v1 stores the
 * (even,even) point sample instead of the 2x2 average. */
RDP_VECTORIZE
static int
a8r8g8b8_to_avc444_box(const uint8_t *src, int src_stride,
                       uint8_t *dst_y, uint8_t *dst_uv,
                       uint8_t *aux_y, uint8_t *aux_uv, int cw,
                       int x1, int y1, int xe, int ye,
                       int w, int h, int point_chroma)
{
    int x;
    int y;
    int cx;
    int xd;

    /* pack loops below read the row buffers at 4-grid indices */
    xd = x1 & ~3;
    for (y = y1; y < ye; y += 2)
    {
        int sy0 = (y < h) ? y : (h - 1);
        int sy1 = (y + 1 < h) ? (y + 1) : (h - 1);
        const uint32_t *s0 = (const uint32_t *) (src + src_stride * sy0);
        const uint32_t *s1 = (const uint32_t *) (src + src_stride * sy1);
        uint8_t *uvp = dst_uv + cw * (y / 2);

        avc444_decode_row(s0, dst_y + cw * y, g_avc444_u0, g_avc444_v0,
                          xd, xe, w);
        avc444_decode_row(s1, dst_y + cw * (y + 1), g_avc444_u1,
                          g_avc444_v1, xd, xe, w);
        /* pack loops: flat, branch-free (edge handling lives in the
         * replicated row-buffer tails), one output stream per loop so
         * the auto-vectorizer sees plain stride-2/stride-4 gathers
         * with contiguous or pair-interleaved stores */
        if (point_chroma)
        {
            RDP_LOOP_VECTORIZE
            for (cx = x1 / 2; cx < xe / 2; cx++)
            {
                uvp[2 * cx] = g_avc444_u0[2 * cx];
                uvp[2 * cx + 1] = g_avc444_v0[2 * cx];
            }
        }
        else
        {
            RDP_LOOP_VECTORIZE
            for (cx = x1 / 2; cx < xe / 2; cx++)
            {
                uvp[2 * cx] =
                    (g_avc444_u0[2 * cx] + g_avc444_u0[2 * cx + 1] +
                     g_avc444_u1[2 * cx] + g_avc444_u1[2 * cx + 1] + 2) / 4;
                uvp[2 * cx + 1] =
                    (g_avc444_v0[2 * cx] + g_avc444_v0[2 * cx + 1] +
                     g_avc444_v1[2 * cx] + g_avc444_v1[2 * cx + 1] + 2) / 4;
            }
        }
        if (aux_y != NULL)
        {
            /* ChromaV2 aux (reference: fill_aux_v2, the exact inverse of
             * FreeRDP general_ChromaV2ToYUV444, MS-RDPEGFX 3.3.8.3.3) */
            uint8_t *ay0 = aux_y + cw * y;
            uint8_t *ay1 = aux_y + cw * (y + 1);
            uint8_t *auvp = aux_uv + cw * (y / 2);
            int half = cw / 2;
            int quarter = cw / 4;
            int cx1 = x1 / 2;
            int cx_end = (xe + 1) / 2;
            int x_end = (xe + 3) / 4;

            if (x_end > quarter)
            {
                x_end = quarter;
            }
            RDP_LOOP_VECTORIZE
            for (cx = cx1; cx < cx_end; cx++)
            {
                ay0[cx] = g_avc444_u0[2 * cx + 1];
            }
            RDP_LOOP_VECTORIZE
            for (cx = cx1; cx < cx_end; cx++)
            {
                ay0[half + cx] = g_avc444_v0[2 * cx + 1];
            }
            RDP_LOOP_VECTORIZE
            for (cx = cx1; cx < cx_end; cx++)
            {
                ay1[cx] = g_avc444_u1[2 * cx + 1];
            }
            RDP_LOOP_VECTORIZE
            for (cx = cx1; cx < cx_end; cx++)
            {
                ay1[half + cx] = g_avc444_v1[2 * cx + 1];
            }
            RDP_LOOP_VECTORIZE
            for (x = xd / 4; x < x_end; x++)
            {
                auvp[2 * x] = g_avc444_u1[4 * x];
                auvp[2 * x + 1] = g_avc444_u1[4 * x + 2];
            }
            RDP_LOOP_VECTORIZE
            for (x = xd / 4; x < x_end; x++)
            {
                auvp[2 * (quarter + x)] = g_avc444_v1[4 * x];
                auvp[2 * (quarter + x) + 1] = g_avc444_v1[4 * x + 2];
            }
        }
    }
    return 0;
}

/* ------------------------------ harness ------------------------------ */

static double
now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

#define W 3840
#define H 2400
#define ITERS 10

static double
bench(const char *name, const uint8_t *src, uint8_t *out,
      int x1, int y1, int x2, int y2, int use_new)
{
    int i;
    double t0;
    double best = 1e9;

    for (i = 0; i < ITERS + 2; i++)
    {
        double dt;
        t0 = now_ms();
        if (use_new == 2)
        {
            a8r8g8b8_to_avc444_box(src, W * 4, out, out + W * H,
                                   out + W * H * 3 / 2,
                                   out + W * H * 5 / 2, W,
                                   x1, y1, x2, y2, W, H, 0);
        }
        else if (use_new == 1)
        {
            a8r8g8b8_to_avc444_main_box(src, W * 4, out, out + W * H, W,
                                        x1, y1, x2, y2, W, H, 0);
            a8r8g8b8_to_avc444v2_aux_box(src, W * 4,
                                         out + W * H * 3 / 2,
                                         out + W * H * 5 / 2, W,
                                         x1, y1, x2, y2, W, H);
        }
        else
        {
            /* old pass ran per rect too; bench the same sub-rect */
            a8r8g8b8_to_yuv444_709fr_box(src + (y1 * W + x1) * 4, W * 4,
                                         out + y1 * W + x1,
                                         out + W * H + y1 * W + x1,
                                         out + 2 * W * H + y1 * W + x1,
                                         W, x2 - x1, y2 - y1);
        }
        dt = now_ms() - t0;
        if (i >= 2 && dt < best)
        {
            best = dt;
        }
    }
    printf("%-34s %4dx%-4d rect: %7.2f ms/frame\n",
           name, x2 - x1, y2 - y1, best);
    return best;
}

int
main(void)
{
    uint8_t *src = malloc((size_t)W * H * 4);
    uint8_t *out = malloc((size_t)W * H * 3);
    size_t i;
    uint32_t seed = 0x12345678;

    if (src == NULL || out == NULL)
    {
        return 1;
    }
    for (i = 0; i < (size_t)W * H; i++)
    {
        seed = seed * 1664525u + 1013904223u;
        ((uint32_t *)src)[i] = seed;
    }
    memset(out, 0, (size_t)W * H * 3);

    printf("AVC444 capture-conversion benchmark, %dx%d source\n", W, H);
    printf("(old = HALF the old pipeline: xrdp re-walked every pixel "
           "again;\n new = the ONLY pixel pass)\n\n");
    bench("old planar YUV444 (xorgxrdp half)", src, out, 0, 0, W, H, 0);
    bench("packed views SCALAR (neg. example)", src, out, 0, 0, W, H, 1);
    bench("packed views VECTORIZED (shipped)",  src, out, 0, 0, W, H, 2);
    bench("old planar YUV444 (xorgxrdp half)", src, out, 0, 0, 2000, 1000, 0);
    bench("packed views SCALAR (neg. example)", src, out, 0, 0, 2000, 1000, 1);
    bench("packed views VECTORIZED (shipped)",  src, out, 0, 0, 2000, 1000, 2);
    bench("old planar YUV444 (xorgxrdp half)", src, out, 0, 0, 500, 200, 0);
    bench("packed views SCALAR (neg. example)", src, out, 0, 0, 500, 200, 1);
    bench("packed views VECTORIZED (shipped)",  src, out, 0, 0, 500, 200, 2);
    free(src);
    free(out);
    return 0;
}
