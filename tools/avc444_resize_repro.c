/*
 * avc444_resize_repro - deterministic, offline reproduction of the
 * resize-to-black regression (single 4K monitor, resize from fullscreen to a
 * non-16-aligned window -> ffmpeg dies, client shows black).
 *
 * ROOT CAUSE (cross-component stride contract):
 *   xrdp reads the capture's YUV444 planes with a 16-ALIGNED stride
 *     pstride = (w + 15) & ~15            (xrdp_avc444_convert.c: conv_update)
 *   and its GFX encoder guard demands
 *     3 * align16(w) * align16(h) <= data_bytes   (xrdp_encoder.c:1360)
 *   but xorgxrdp allocated the shared buffer at the UNALIGNED surface size
 *     bytes = width * height * 3          (rdpClientCon.c:914, pre-fix)
 *   and strided the planes with the UNALIGNED id->width
 *     dst_stride = id->width             (rdpCapture.c:1532, pre-fix)
 *
 *   When the surface is already 16-aligned (3840x2400) provided == required and
 *   it happens to work. After a resize to a non-16-aligned size (3814x2233) the
 *   xorgxrdp buffer is SMALLER than the guard requires, so xrdp drops every
 *   frame at the guard (return NULL, no log) -> ffmpeg is never (re)spawned ->
 *   permanent black. This program reproduces that arithmetic deterministically
 *   and then exercises the real xrdp converter on a contract-conformant buffer.
 *
 * build (from repo root, after `make`):
 *   gcc -O2 -I xrdp -I common tools/avc444_resize_repro.c \
 *       xrdp/xrdp_avc444_convert.o common/.libs/libcommon.a \
 *       -lpthread -lrt -ldl -o /tmp/avc444_resize_repro
 * run:
 *   /tmp/avc444_resize_repro     # exits non-zero while the pre-fix formula is
 *                                # modelled; 0 once XORGXRDP_ALIGNED is defined
 *
 * Pass -DXORGXRDP_ALIGNED to model the FIXED xorgxrdp allocation and confirm
 * the contract holds for every size.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xrdp_avc444_convert.h"

#define ALIGN16(v) (((v) + 15) & ~15)

/* what xorgxrdp puts in the shared buffer for a surface of w x h. */
static long
xorgxrdp_provided_bytes(int w, int h)
{
#ifdef XORGXRDP_ALIGNED
    /* FIXED: align the capture stride/height to XRDP_H264_ALIGN (16) */
    return (long)ALIGN16(w) * ALIGN16(h) * 3;
#else
    /* PRE-FIX: unaligned surface size (rdpClientCon.c:914) */
    return (long)w * h * 3;
#endif
}

/* what xrdp's GFX encoder guard requires before it will encode a frame. */
static long
xrdp_required_bytes(int w, int h)
{
    return 3L * ALIGN16(w) * ALIGN16(h);
}

struct size_case
{
    int w;
    int h;
    const char *note;
};

int
main(void)
{
    /* sizes seen in the live log plus common non-16-aligned window sizes */
    struct size_case cases[] =
    {
        { 3840, 2400, "fullscreen 4K (16-aligned) - worked" },
        { 3814, 2233, "resized window (from the black-screen log)" },
        { 3814, 1896, "resized window (from the black-screen log)" },
        { 1366,  768, "common laptop width (1366 not 16-aligned)" },
        { 1000,  700, "small non-aligned window" },
        { 2560, 1440, "16-aligned - worked" },
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int i;
    int failures = 0;

    printf("== AVC444 capture stride contract "
#ifdef XORGXRDP_ALIGNED
           "(modelling FIXED xorgxrdp: 16-aligned) ==\n");
#else
           "(modelling PRE-FIX xorgxrdp: unaligned) ==\n");
#endif
    printf("%-11s %-14s %-14s %-6s  %s\n",
           "surface", "provided", "required", "verdict", "note");
    for (i = 0; i < n; i++)
    {
        int w = cases[i].w;
        int h = cases[i].h;
        long provided = xorgxrdp_provided_bytes(w, h);
        long required = xrdp_required_bytes(w, h);
        int ok = provided >= required;
        char surf[16];

        snprintf(surf, sizeof(surf), "%dx%d", w, h);
        printf("%-11s %-14ld %-14ld %-6s  %s\n",
               surf, provided, required,
               ok ? "OK" : "BLACK", cases[i].note);
        if (!ok)
        {
            failures++;
        }
    }

    /* Prove the real xrdp converter is correct on a CONTRACT-CONFORMANT
     * (16-aligned) buffer at a non-16-aligned size: this is what the fixed
     * xorgxrdp will deliver. A too-small (pre-fix) buffer would read OOB here,
     * which is exactly why the guard drops the frame instead. */
    {
        int w = 3814;
        int h = 2233;
        int pstride = ALIGN16(w);
        int ch16 = ALIGN16(h);
        size_t area = (size_t)pstride * ch16;
        unsigned char *yuv = (unsigned char *)malloc(area * 3);
        struct xrdp_avc444_conv *conv;
        int rv;

        if (yuv == NULL)
        {
            printf("alloc failed\n");
            return 2;
        }
        memset(yuv, 0x80, area * 3);   /* neutral chroma, mid luma */
        conv = xrdp_avc444_conv_create(w, h, 32);
        if (conv == NULL)
        {
            printf("conv_create failed for %dx%d\n", w, h);
            free(yuv);
            return 2;
        }
        conv->chroma_v2 = 1;
        rv = xrdp_avc444_conv_update(conv, yuv, pstride, w, h);
        printf("\nreal conv_update(%dx%d, pstride=%d, aligned buffer): rv=%d "
               "(%s)\n", w, h, pstride, rv, rv == 0 ? "OK" : "FAIL");
        if (rv != 0)
        {
            failures++;
        }
        xrdp_avc444_conv_delete(conv);
        free(yuv);
    }

    printf("\n%s: %d size(s) would show BLACK\n",
           failures ? "REPRO CONFIRMED" : "all sizes OK", failures);
    return failures ? 1 : 0;
}
