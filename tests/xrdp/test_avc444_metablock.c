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
        /* width/height must be even too (strict clients hard-assert it:
         * FreeRDP sse41_YUV444ToRGB) unless clamped at an odd-sized
         * surface's far edge — the only place an odd extent can remain */
        if (right != dst->x2 - dst->x1)
        {
            ck_assert_int_eq((right - left) & 1, 0);
        }
        if (bottom != dst->y2 - dst->y1)
        {
            ck_assert_int_eq((bottom - top) & 1, 0);
        }
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

/*
 * RFX_AVC444_BITMAP_STREAM wire layout (out_RFX_AVC444_BITMAP_STREAM_view).
 *
 * Ground-truth wire capture of a real Windows Server 2022 + NVIDIA AVC444v2
 * host shows: the stream bootstraps luma-first with an LC=1 PDU (the IDR lives
 * in the luma view) and delivers the auxiliary chroma as a separate LC=2 PDU
 * (cbAvc420EncodedBitstream1 == 0, only bitstream2 present). xrdp emits the
 * pair as an LC=1 luma PDU followed by an LC=2 chroma PDU INSIDE ONE GFX frame,
 * so the wire is Windows-conforming (Apple VideoToolbox behind the macOS
 * Windows App accepts it) yet still atomic per frame -- a region-strict client
 * applies both before the ENDFRAME present, so it never shows a luma-only
 * intermediate (the failure mode of the earlier two-GFX-frame split). The
 * serializer here emits ONE view per call; these tests pin each view's layout.
 */

/* parse one RFX_AVC420_BITMAP_STREAM metablock at s->p; returns its size */
static int
parse_metablock(struct stream *s)
{
    int count;

    in_uint32_le(s, count);
    ck_assert_int_gt(count, 0);
    /* per rect: 4 x uint16 rect + qp/quality byte pair */
    in_uint8s(s, count * 8 + count * 2);
    return 4 + count * 10;
}

START_TEST(test_avc444_wire_luma_lc1)
{
    /* LC=1 luma view: info word carries LC=1 and cb = metablock+bitstream len;
     * exactly one sub-stream (the main YUV420 view) follows, nothing after. */
    struct xrdp_egfx_rect dst = {0, 0, 1024, 768};
    struct xrdp_egfx_rect rects[2];
    unsigned char main_data[733];
    struct stream *s;
    unsigned int info;
    int cb;
    int mb_len;
    int total;
    int i;

    rects[0].x1 = 33; /* odd origin on purpose */
    rects[0].y1 = 17;
    rects[0].x2 = 211;
    rects[0].y2 = 143;
    rects[1].x1 = 400;
    rects[1].y1 = 300;
    rects[1].x2 = 640;
    rects[1].y2 = 480;
    for (i = 0; i < (int)sizeof(main_data); i++)
    {
        main_data[i] = (unsigned char)(0xA5 + i);
    }
    make_stream(s);
    init_stream(s, 16384);
    ck_assert_int_eq(out_RFX_AVC444_BITMAP_STREAM_view(&dst, s, rects, 2,
                     main_data, sizeof(main_data), 1), 0);
    total = (int)(s->end - s->data);
    s->p = s->data;
    in_uint32_le(s, info);
    ck_assert_uint_eq(info >> 30, 1); /* LC == 1 (luma only) */
    cb = (int)(info & 0x3FFFFFFF);
    mb_len = parse_metablock(s);
    /* cb spans the whole (and only) sub-stream: metablock + luma bitstream */
    ck_assert_int_eq(cb, mb_len + (int)sizeof(main_data));
    ck_assert_int_eq(g_memcmp(s->p, main_data, sizeof(main_data)), 0);
    in_uint8s(s, sizeof(main_data));
    /* nothing after the luma view */
    ck_assert_int_eq(total, 4 + cb);
    ck_assert_ptr_eq(s->p, s->end);
    free_stream(s);
}
END_TEST

START_TEST(test_avc444_wire_chroma_lc2)
{
    /* LC=2 chroma view: info word carries LC=2 and cb == 0 (bitstream1 absent);
     * only bitstream2 (the aux chroma view) follows. */
    struct xrdp_egfx_rect dst = {0, 0, 1024, 768};
    struct xrdp_egfx_rect rects[2];
    unsigned char aux_data[517];
    struct stream *s;
    unsigned int info;
    int cb;
    int mb_len;
    int total;
    int i;

    rects[0].x1 = 33;
    rects[0].y1 = 17;
    rects[0].x2 = 211;
    rects[0].y2 = 143;
    rects[1].x1 = 400;
    rects[1].y1 = 300;
    rects[1].x2 = 640;
    rects[1].y2 = 480;
    for (i = 0; i < (int)sizeof(aux_data); i++)
    {
        aux_data[i] = (unsigned char)(0x5A + i * 3);
    }
    make_stream(s);
    init_stream(s, 16384);
    ck_assert_int_eq(out_RFX_AVC444_BITMAP_STREAM_view(&dst, s, rects, 2,
                     aux_data, sizeof(aux_data), 2), 0);
    total = (int)(s->end - s->data);
    s->p = s->data;
    in_uint32_le(s, info);
    ck_assert_uint_eq(info >> 30, 2); /* LC == 2 (chroma only) */
    cb = (int)(info & 0x3FFFFFFF);
    ck_assert_int_eq(cb, 0); /* bitstream1 (luma) absent */
    mb_len = parse_metablock(s);
    ck_assert_int_eq(g_memcmp(s->p, aux_data, sizeof(aux_data)), 0);
    in_uint8s(s, sizeof(aux_data));
    /* the aux sub-stream is the whole payload after the info word */
    ck_assert_int_eq(total, 4 + mb_len + (int)sizeof(aux_data));
    ck_assert_ptr_eq(s->p, s->end);
    free_stream(s);
}
END_TEST

START_TEST(test_avc444_wire_single_rect)
{
    /* minimal case: one rect, tiny payloads; LC=1 and LC=2 layout invariants */
    struct xrdp_egfx_rect dst = {0, 0, 640, 480};
    struct xrdp_egfx_rect rects[1];
    unsigned char main_data[5] = {1, 2, 3, 4, 5};
    unsigned char aux_data[3] = {9, 8, 7};
    struct stream *s;
    unsigned int info;
    int cb;
    int mb_len;

    rects[0].x1 = 0;
    rects[0].y1 = 0;
    rects[0].x2 = 64;
    rects[0].y2 = 64;
    make_stream(s);
    init_stream(s, 8192);
    /* luma LC=1 */
    ck_assert_int_eq(out_RFX_AVC444_BITMAP_STREAM_view(&dst, s, rects, 1,
                     main_data, sizeof(main_data), 1), 0);
    s->p = s->data;
    in_uint32_le(s, info);
    ck_assert_uint_eq(info >> 30, 1);
    cb = (int)(info & 0x3FFFFFFF);
    mb_len = parse_metablock(s);
    ck_assert_int_eq(cb, mb_len + (int)sizeof(main_data));
    ck_assert_int_eq(g_memcmp(s->p, main_data, sizeof(main_data)), 0);
    /* chroma LC=2 (reuse the same buffer) */
    s->p = s->data;
    s->end = s->data;
    ck_assert_int_eq(out_RFX_AVC444_BITMAP_STREAM_view(&dst, s, rects, 1,
                     aux_data, sizeof(aux_data), 2), 0);
    s->p = s->data;
    in_uint32_le(s, info);
    ck_assert_uint_eq(info >> 30, 2);
    ck_assert_uint_eq(info & 0x3FFFFFFF, 0);
    parse_metablock(s);
    ck_assert_int_eq(g_memcmp(s->p, aux_data, sizeof(aux_data)), 0);
    free_stream(s);
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
    tc = tcase_create("avc444_wire");
    tcase_add_test(tc, test_avc444_wire_luma_lc1);
    tcase_add_test(tc, test_avc444_wire_chroma_lc2);
    tcase_add_test(tc, test_avc444_wire_single_rect);
    suite_add_tcase(s, tc);
    return s;
}
