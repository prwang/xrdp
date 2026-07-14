#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <string.h>
#include <stdlib.h>
#include "xrdp_avc444_convert.h"
#include "test_xrdp.h"

/* ------------------------------------------------------------------------ */
/* Full-range BT.709 color vectors (computed by hand from the spec matrix)  */

START_TEST(test_avc444_color_primaries)
{
    int y;
    int u;
    int v;

    xrdp_avc444_rgb_to_yuv709fr(0, 0, 0, &y, &u, &v);
    ck_assert_int_eq(y, 0);
    ck_assert_int_eq(u, 128);
    ck_assert_int_eq(v, 128);

    xrdp_avc444_rgb_to_yuv709fr(255, 255, 255, &y, &u, &v);
    ck_assert_int_eq(y, 254);
    ck_assert_int_eq(u, 128);
    ck_assert_int_eq(v, 128);

    xrdp_avc444_rgb_to_yuv709fr(255, 0, 0, &y, &u, &v);
    ck_assert_int_eq(y, 53);
    ck_assert_int_eq(u, 99);
    ck_assert_int_eq(v, 255);

    xrdp_avc444_rgb_to_yuv709fr(0, 255, 0, &y, &u, &v);
    ck_assert_int_eq(y, 182);
    ck_assert_int_eq(u, 29);
    ck_assert_int_eq(v, 12);

    xrdp_avc444_rgb_to_yuv709fr(0, 0, 255, &y, &u, &v);
    ck_assert_int_eq(y, 17);
    ck_assert_int_eq(u, 255);
    ck_assert_int_eq(v, 116);

    xrdp_avc444_rgb_to_yuv709fr(128, 128, 128, &y, &u, &v);
    ck_assert_int_eq(y, 127);
    ck_assert_int_eq(u, 128);
    ck_assert_int_eq(v, 128);
}
END_TEST

/* ------------------------------------------------------------------------ */
/* Reference decoder (independent reimplementation of MS-RDPEGFX 3.3.8.3.2  */
/* / FreeRDP general_LumaToYUV444 + general_ChromaV1ToYUV444) used to prove */
/* the encoder split round-trips the full YUV444 chroma exactly.           */

static unsigned char
main_y(struct xrdp_avc444_conv *c, int x, int y)
{
    return c->main_nv12[y * c->coded_width + x];
}
static unsigned char
main_u(struct xrdp_avc444_conv *c, int cx, int cy)
{
    return c->main_nv12[c->coded_width * c->coded_height + cy * c->coded_width + 2 * cx];
}
static unsigned char
main_v(struct xrdp_avc444_conv *c, int cx, int cy)
{
    return c->main_nv12[c->coded_width * c->coded_height + cy * c->coded_width + 2 * cx + 1];
}
static unsigned char
aux_y(struct xrdp_avc444_conv *c, int x, int y)
{
    return c->aux_nv12[y * c->coded_width + x];
}
static unsigned char
aux_u(struct xrdp_avc444_conv *c, int cx, int cy)
{
    return c->aux_nv12[c->coded_width * c->coded_height + cy * c->coded_width + 2 * cx];
}
static unsigned char
aux_v(struct xrdp_avc444_conv *c, int cx, int cy)
{
    return c->aux_nv12[c->coded_width * c->coded_height + cy * c->coded_width + 2 * cx + 1];
}

static void
decode_444(struct xrdp_avc444_conv *c, int w, int h,
           unsigned char *Yd, unsigned char *Ud, unsigned char *Vd)
{
    int x;
    int y;
    int cx;
    int cy;
    int uY = 0;
    int vY = 0;
    int padH = h + 16 - h % 16;

    /* Y identity */
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            Yd[y * w + x] = main_y(c, x, y);
        }
    }
    /* B2/B3: replicate main chroma across each 2x2 block */
    for (cy = 0; cy < (h + 1) / 2; cy++)
    {
        for (cx = 0; cx < (w + 1) / 2; cx++)
        {
            int u = main_u(c, cx, cy);
            int v = main_v(c, cx, cy);
            int dx;
            int dy;
            for (dy = 0; dy < 2; dy++)
            {
                for (dx = 0; dx < 2; dx++)
                {
                    int px = 2 * cx + dx;
                    int py = 2 * cy + dy;
                    if (px < w && py < h)
                    {
                        Ud[py * w + px] = (unsigned char)u;
                        Vd[py * w + px] = (unsigned char)v;
                    }
                }
            }
        }
    }
    /* B6/B7: odd-col even-row */
    for (cy = 0; cy < h / 2; cy++)
    {
        for (cx = 0; cx < w / 2; cx++)
        {
            int px = 2 * cx + 1;
            int py = 2 * cy;
            Ud[py * w + px] = aux_u(c, cx, cy);
            Vd[py * w + px] = aux_v(c, cx, cy);
        }
    }
    /* B4/B5: banded odd rows, full width */
    for (y = 0; y < padH; y++)
    {
        if (y % 16 < 8)
        {
            int pos = 2 * uY + 1;
            uY++;
            if (pos >= h)
            {
                continue;
            }
            for (x = 0; x < w; x++)
            {
                Ud[pos * w + x] = aux_y(c, x, y);
            }
        }
        else
        {
            int pos = 2 * vY + 1;
            vY++;
            if (pos >= h)
            {
                continue;
            }
            for (x = 0; x < w; x++)
            {
                Vd[pos * w + x] = aux_y(c, x, y);
            }
        }
    }
}

static void
build_source(unsigned char *xrgb, int stride, int w, int h)
{
    int x;
    int y;
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            unsigned int r = (unsigned int)((x * 7 + 3) & 0xff);
            unsigned int g = (unsigned int)((y * 11 + 5) & 0xff);
            unsigned int b = (unsigned int)(((x + y) * 5 + 17) & 0xff);
            unsigned int px = (r << 16) | (g << 8) | b;
            memcpy(xrgb + y * stride + x * 4, &px, 4);
        }
    }
}

/* prove the split is a lossless permutation of the YUV444 chroma */
START_TEST(test_avc444_roundtrip_exact)
{
    const int w = 20;
    const int h = 20;
    const int stride = w * 4;
    unsigned char xrgb[20 * 20 * 4];
    unsigned char Yd[20 * 20];
    unsigned char Ud[20 * 20];
    unsigned char Vd[20 * 20];
    struct xrdp_avc444_conv *c;
    int x;
    int y;

    build_source(xrgb, stride, w, h);
    c = xrdp_avc444_conv_create(w, h);
    ck_assert_ptr_ne(c, NULL);
    ck_assert_int_eq(c->coded_width, 32);
    ck_assert_int_eq(c->coded_height, 32);
    ck_assert_int_eq(xrdp_avc444_conv_update(c, xrgb, stride, w, h), 0);

    memset(Ud, 0xAA, sizeof(Ud));
    memset(Vd, 0x55, sizeof(Vd));
    decode_444(c, w, h, Yd, Ud, Vd);

    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            int ey;
            int eu;
            int ev;
            unsigned int px;
            memcpy(&px, xrgb + y * stride + x * 4, 4);
            xrdp_avc444_rgb_to_yuv709fr((px >> 16) & 0xff, (px >> 8) & 0xff,
                                        px & 0xff, &ey, &eu, &ev);
            ck_assert_int_eq(Yd[y * w + x], ey);
            ck_assert_int_eq(Ud[y * w + x], eu);
            ck_assert_int_eq(Vd[y * w + x], ev);
        }
    }
    xrdp_avc444_conv_delete(c);
}
END_TEST

START_TEST(test_avc444_dims_and_padding)
{
    struct xrdp_avc444_conv *c;
    unsigned char xrgb[8 * 4 * 4];
    int i;

    /* coded dims round up to 16; arbitrary visible dims allowed */
    c = xrdp_avc444_conv_create(1919, 1079);
    ck_assert_ptr_ne(c, NULL);
    ck_assert_int_eq(c->coded_width, 1920);
    ck_assert_int_eq(c->coded_height, 1088);
    xrdp_avc444_conv_delete(c);

    /* invalid dims rejected */
    ck_assert_ptr_eq(xrdp_avc444_conv_create(0, 100), NULL);
    ck_assert_ptr_eq(xrdp_avc444_conv_create(100, -1), NULL);
    ck_assert_ptr_eq(xrdp_avc444_conv_create(99999, 100), NULL);

    /* mismatched update dims rejected, no write */
    for (i = 0; i < 8 * 4; i++)
    {
        ((unsigned int *)xrgb)[i] = 0;
    }
    c = xrdp_avc444_conv_create(8, 4);
    ck_assert_ptr_ne(c, NULL);
    ck_assert_int_eq(xrdp_avc444_conv_update(c, xrgb, 8 * 4, 7, 4), 1);
    ck_assert_int_eq(xrdp_avc444_conv_update(c, xrgb, 8 * 4, 8, 4), 0);
    /* padding columns/rows are initialized (edge replicated), never left
     * uninitialized: a solid-black source yields Y=0 everywhere in Y plane */
    for (i = 0; i < c->coded_width * c->coded_height; i++)
    {
        ck_assert_int_eq(c->main_nv12[i], 0);
    }
    xrdp_avc444_conv_delete(c);
}
END_TEST

/******************************************************************************/
/* Odd visible dimensions must round the coded (H.264) dimensions up to the
 * next multiple of 16 and edge-replicate the source into the padding columns
 * and rows, so the encoder never reads uninitialized memory and the padding
 * carries the nearest real pixel (no ringing at the coded border). This is
 * the 16-pixel-alignment path a client resize to an odd size exercises. */
START_TEST(test_avc444_odd_dims_alignment)
{
    /* {visible_w, visible_h, expected_coded_w, expected_coded_h} */
    static const int cases[][4] =
    {
        {   1,   1,   16,   16},
        {  15,  15,   16,   16},
        {  16,  16,   16,   16},
        {  17,  17,   32,   32},
        {1281, 721, 1296,  736}, /* odd resize target from the task */
        {1366, 769, 1376,  784}
    };
    int n = (int)(sizeof(cases) / sizeof(cases[0]));
    int t;

    for (t = 0; t < n; t++)
    {
        struct xrdp_avc444_conv *c;
        int w = cases[t][0];
        int h = cases[t][1];
        int expect_cw = cases[t][2];
        int expect_ch = cases[t][3];
        int expect_size;

        c = xrdp_avc444_conv_create(w, h);
        ck_assert_ptr_ne(c, NULL);
        ck_assert_int_eq(c->coded_width, expect_cw);
        ck_assert_int_eq(c->coded_height, expect_ch);
        /* coded dims are a multiple of 16 and cover the visible surface */
        ck_assert_int_eq(c->coded_width % 16, 0);
        ck_assert_int_eq(c->coded_height % 16, 0);
        ck_assert_int_ge(c->coded_width, w);
        ck_assert_int_lt(c->coded_width - w, 16);
        ck_assert_int_ge(c->coded_height, h);
        ck_assert_int_lt(c->coded_height - h, 16);
        expect_size = expect_cw * expect_ch + expect_cw * (expect_ch / 2);
        ck_assert_int_eq(c->nv12_size, expect_size);
        xrdp_avc444_conv_delete(c);
    }
}
END_TEST

/******************************************************************************/
/* Edge replication into the coded padding: with a per-column color gradient,
 * the padding columns [w, coded_width) of the main Y plane must equal the
 * last real column (w-1), and the padding rows [h, coded_height) must equal
 * the last real row (h-1). Verified at an odd size (1281x721 -> 1296x736). */
START_TEST(test_avc444_odd_padding_edge_replicated)
{
    struct xrdp_avc444_conv *c;
    unsigned char *xrgb;
    int w = 1281;
    int h = 721;
    int stride = w * 4;
    int x;
    int y;

    xrgb = (unsigned char *)malloc((size_t)stride * h);
    ck_assert_ptr_ne(xrgb, NULL);
    /* horizontal gradient: R varies by column, so each column has a distinct
     * luma; the last real column (w-1) is what padding should replicate */
    for (y = 0; y < h; y++)
    {
        for (x = 0; x < w; x++)
        {
            unsigned int r = (unsigned int)(x & 0xff);
            unsigned int g = (unsigned int)((x * 3 + y) & 0xff);
            unsigned int b = (unsigned int)((y * 5) & 0xff);
            unsigned int px = (r << 16) | (g << 8) | b;
            memcpy(xrgb + (size_t)y * stride + (size_t)x * 4, &px, 4);
        }
    }
    c = xrdp_avc444_conv_create(w, h);
    ck_assert_ptr_ne(c, NULL);
    ck_assert_int_eq(c->coded_width, 1296);
    ck_assert_int_eq(c->coded_height, 736);
    ck_assert_int_eq(xrdp_avc444_conv_update(c, xrgb, stride, w, h), 0);

    /* padding columns of each real row replicate the last real column */
    for (y = 0; y < h; y++)
    {
        unsigned char edge = c->main_nv12[y * c->coded_width + (w - 1)];
        for (x = w; x < c->coded_width; x++)
        {
            ck_assert_int_eq(c->main_nv12[y * c->coded_width + x], edge);
        }
    }
    /* padding rows replicate the last real row across the whole coded width */
    for (x = 0; x < c->coded_width; x++)
    {
        unsigned char edge = c->main_nv12[(h - 1) * c->coded_width + x];
        for (y = h; y < c->coded_height; y++)
        {
            ck_assert_int_eq(c->main_nv12[y * c->coded_width + x], edge);
        }
    }
    xrdp_avc444_conv_delete(c);
    free(xrgb);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_avc444_convert(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Convert");
    tc = tcase_create("avc444_convert");
    tcase_add_test(tc, test_avc444_color_primaries);
    tcase_add_test(tc, test_avc444_roundtrip_exact);
    tcase_add_test(tc, test_avc444_dims_and_padding);
    tcase_add_test(tc, test_avc444_odd_dims_alignment);
    tcase_add_test(tc, test_avc444_odd_padding_edge_replicated);
    suite_add_tcase(s, tc);
    return s;
}
