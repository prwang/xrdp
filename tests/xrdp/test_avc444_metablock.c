#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <stdlib.h>
#include "arch.h"
#include "parse.h"
#include "os_calls.h"
#include "xrdp_egfx.h"
#include "xrdp_encoder.h"
#include "test_xrdp.h"

/*
 * Regression tests for the AVC420/AVC444 RFX_AVC420_METABLOCK region-rect
 * emission (out_RFX_AVC420_METABLOCK, xrdp_encoder.c).
 *
 * The FreeRDP AVC444 decoder reconstructs chroma one region rect at a time and
 * indexes the odd chroma columns/rows relative to the rect origin. An odd
 * rect left/top therefore flips chroma parity and fringes the rect's left/top
 * edge (the "burr" seen on high-contrast vertical/horizontal edges of every
 * updated region). The emitter must round every region-rect origin down to an
 * even coordinate. These tests assert that invariant on the emitted stream.
 *
 * The metablock stream begins with a 4-byte numRegionRects, followed by that
 * many region rects (left, top, right, bottom, each little-endian uint16).
 */
static void
check_all_origins_even(struct xrdp_egfx_rect *dst,
                       struct xrdp_egfx_rect *rects, int n)
{
    struct stream *s;
    int count;
    int i;

    make_stream(s);
    init_stream(s, 8192);
    ck_assert_int_eq(out_RFX_AVC420_METABLOCK(dst, s, rects, n), 0);
    s_mark_end(s);
    s->p = s->data;
    in_uint32_le(s, count);
    ck_assert_int_gt(count, 0);
    for (i = 0; i < count; i++)
    {
        int left;
        int top;
        int right;
        int bottom;

        in_uint16_le(s, left);
        in_uint16_le(s, top);
        in_uint16_le(s, right);
        in_uint16_le(s, bottom);
        /* origin must be even-aligned to the chroma grid */
        ck_assert_int_eq(left & 1, 0);
        ck_assert_int_eq(top & 1, 0);
        /* alignment only grows the rect; origin never negative or past end */
        ck_assert_int_ge(left, 0);
        ck_assert_int_ge(top, 0);
        ck_assert_int_le(right, dst->x2 - dst->x1);
        ck_assert_int_le(bottom, dst->y2 - dst->y1);
    }
    free_stream(s);
}

START_TEST(test_metablock_even_x1_origin_even)
{
    /* even x1/y1: the 1px expansion made left/top odd (301/119) pre-fix */
    struct xrdp_egfx_rect dst = {0, 0, 1479, 850};
    struct xrdp_egfx_rect rects[1];

    rects[0].x1 = 302;
    rects[0].y1 = 120;
    rects[0].x2 = 662;
    rects[0].y2 = 520;
    check_all_origins_even(&dst, rects, 1);
}
END_TEST

START_TEST(test_metablock_odd_x1_origin_even)
{
    /* odd x1/y1: the 1px expansion made left/top even; must stay even */
    struct xrdp_egfx_rect dst = {0, 0, 1479, 850};
    struct xrdp_egfx_rect rects[1];

    rects[0].x1 = 503;
    rects[0].y1 = 121;
    rects[0].x2 = 863;
    rects[0].y2 = 521;
    check_all_origins_even(&dst, rects, 1);
}
END_TEST

START_TEST(test_metablock_mixed_and_edges_origin_even)
{
    /* a mix including a rect at the surface origin (left clamps to 0) and one
     * flush against the odd-sized surface's right/bottom edge */
    struct xrdp_egfx_rect dst = {0, 0, 1479, 850};
    struct xrdp_egfx_rect rects[4];

    rects[0].x1 = 0;
    rects[0].y1 = 0;
    rects[0].x2 = 64;
    rects[0].y2 = 64;
    rects[1].x1 = 300;
    rects[1].y1 = 200;
    rects[1].x2 = 316;
    rects[1].y2 = 216;
    rects[2].x1 = 301;
    rects[2].y1 = 201;
    rects[2].x2 = 999;
    rects[2].y2 = 401;
    rects[3].x1 = 1477;
    rects[3].y1 = 848;
    rects[3].x2 = 1479;
    rects[3].y2 = 850;
    check_all_origins_even(&dst, rects, 4);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_avc444_metablock(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Metablock");
    tc = tcase_create("avc444_metablock");
    tcase_add_test(tc, test_metablock_even_x1_origin_even);
    tcase_add_test(tc, test_metablock_odd_x1_origin_even);
    tcase_add_test(tc, test_metablock_mixed_and_edges_origin_even);
    suite_add_tcase(s, tc);
    return s;
}
