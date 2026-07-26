#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "xrdp.h"
#include "xrdp_client_info.h"
#include "xup_client_info.h"
#include "test_xrdp.h"

/* Exercises xrdp_mm_avc444_probe_dims(): the external ffmpeg AVC backend is
 * probed at the LARGEST single-monitor coded size (one ffmpeg child per
 * monitor), NOT the virtual-desktop bounding box. Origins are inclusive
 * (width = right - left + 1); the result is rounded up to a 16-pixel multiple.
 *
 * Also exercises xup_cap_h264_shmem_layout(): the shared xrdp<->xorgxrdp
 * capture-shmem contract that gives every monitor a DISJOINT plane region
 * (the multimon cross-monitor plane-overwrite ghost fix). The layout uses
 * minfo (same source dev->minfo is copied from), not minfo_wm.
 */

static void
set_monitor(struct display_size_description *d, int i,
            int left, int top, int right, int bottom)
{
    d->minfo_wm[i].left = left;
    d->minfo_wm[i].top = top;
    d->minfo_wm[i].right = right;
    d->minfo_wm[i].bottom = bottom;
}

static void
set_monitor_cap(struct display_size_description *d, int i,
                int left, int top, int right, int bottom)
{
    d->minfo[i].left = left;
    d->minfo[i].top = top;
    d->minfo[i].right = right;
    d->minfo[i].bottom = bottom;
}

START_TEST(test_probe_dims_no_monitors_uses_screen)
{
    struct display_size_description d;
    int cw = -1;
    int ch = -1;

    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 0;
    /* 1920x1080 already 16-aligned in width, 1080 -> 1088 */
    xrdp_mm_avc444_probe_dims(&d, 1920, 1080, &cw, &ch);
    ck_assert_int_eq(cw, 1920);
    ck_assert_int_eq(ch, 1088);
}
END_TEST

START_TEST(test_probe_dims_null_uses_screen)
{
    int cw = -1;
    int ch = -1;

    xrdp_mm_avc444_probe_dims(NULL, 1024, 768, &cw, &ch);
    ck_assert_int_eq(cw, 1024);
    ck_assert_int_eq(ch, 768);
}
END_TEST

START_TEST(test_probe_dims_dual_equal_1024x768)
{
    struct display_size_description d;
    int cw = -1;
    int ch = -1;

    /* two side-by-side 1024x768 monitors: virtual desktop is 2048x768 but the
     * probe size must be a single 1024x768 monitor (both 16-aligned) */
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 2;
    set_monitor(&d, 0, 0, 0, 1023, 767);
    set_monitor(&d, 1, 1024, 0, 2047, 767);
    xrdp_mm_avc444_probe_dims(&d, 2048, 768, &cw, &ch);
    ck_assert_int_eq(cw, 1024);
    ck_assert_int_eq(ch, 768);
}
END_TEST

START_TEST(test_probe_dims_takes_max_per_axis)
{
    struct display_size_description d;
    int cw = -1;
    int ch = -1;

    /* mixed sizes: widest is monitor 1 (1920), tallest is monitor 0 (1200);
     * the probe takes the per-axis maximum across monitors, 16-aligned */
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 2;
    set_monitor(&d, 0, 0, 0, 1599, 1199);      /* 1600x1200 */
    set_monitor(&d, 1, 1600, 0, 3519, 1079);   /* 1920x1080 */
    xrdp_mm_avc444_probe_dims(&d, 3520, 1200, &cw, &ch);
    ck_assert_int_eq(cw, 1920);   /* max width, already aligned */
    ck_assert_int_eq(ch, 1200);   /* max height, already aligned */
}
END_TEST

START_TEST(test_probe_dims_alignment_round_up)
{
    struct display_size_description d;
    int cw = -1;
    int ch = -1;

    /* odd single-monitor size rounds each axis up to the next 16 multiple */
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 1;
    set_monitor(&d, 0, 0, 0, 1365, 767);   /* 1366x768 */
    xrdp_mm_avc444_probe_dims(&d, 1366, 768, &cw, &ch);
    ck_assert_int_eq(cw, 1376);   /* 1366 -> 1376 */
    ck_assert_int_eq(ch, 768);    /* already aligned */
}
END_TEST

START_TEST(test_cap_layout_no_monitors_session_at_zero)
{
    int offs[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;

    /* single screen: one region at offset 0, 16-aligned coded dims.
     * packed views ([main NV12][aux NV12], 1.5 B/px each) total the same
     * 3 B/px as the former planar YUV444 when the view size is already a
     * page multiple (1376*768*1.5 = 387 pages exactly) */
    total = xup_cap_h264_shmem_layout(NULL, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      1366, 768, offs);
    ck_assert_int_eq(total, 1376 * 768 * 3);
    ck_assert_int_eq(offs[0], 0);
    /* the aux view starts on the next page after the main view */
    ck_assert_int_eq(xup_cap_avc444_aux_offset(1366, 768, 16),
                     1376 * 768 * 3 / 2);
    ck_assert_int_eq(xup_cap_avc444_aux_offset(1366, 768, 16)
                     % XUP_CAP_PAGE_ALIGN, 0);
}
END_TEST

START_TEST(test_cap_layout_single_monitor_matches_session_formula)
{
    struct display_size_description d;
    int offs[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;

    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 1;
    set_monitor_cap(&d, 0, 0, 0, 3839, 2399);   /* 3840x2400 */
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      3840, 2400, offs);
    ck_assert_int_eq(total, 3840 * 2400 * 3);   /* == old session formula */
    ck_assert_int_eq(offs[0], 0);
    /* main-only (external AVC420, nv12_709fr under CC_GFX_AVC444) needs
     * just the one view */
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_AVC444,
                                      XRDP_nv12_709fr, 16,
                                      3840, 2400, offs);
    ck_assert_int_eq(total, 3840 * 2400 * 3 / 2);
}
END_TEST

START_TEST(test_cap_layout_owner_dual_disjoint)
{
    struct display_size_description d;
    int offs[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;
    int mon0_bytes;

    /* the reproduced ghost layout: primary 2560x1440 on top at +594+0,
     * 4K 3840x2400 below at +0+1440. The 4K's region must start beyond
     * ALL of the primary's plane bytes (was: both at offset 0, primary
     * frames overwrote 4K rows 0..2879 -> 1-2px ghost lines) */
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 2;
    set_monitor_cap(&d, 0, 594, 0, 3153, 1439);     /* 2560x1440 */
    set_monitor_cap(&d, 1, 0, 1440, 3839, 3839);    /* 3840x2400 */
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      3840, 3840, offs);
    mon0_bytes = 2560 * 1440 * 3;
    ck_assert_int_eq(offs[0], 0);
    ck_assert_int_eq(offs[1], mon0_bytes);          /* already page-aligned */
    ck_assert_int_ge(offs[1], mon0_bytes);          /* disjoint */
    ck_assert_int_eq(total, mon0_bytes + 3840 * 2400 * 3);
}
END_TEST

START_TEST(test_cap_layout_nv12_dual)
{
    struct display_size_description d;
    int offs[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;

    /* CC_GFX_A2 (NV12/AVC420 GFX): same latent upstream hazard, 2 B/px
     * slack factor kept from the historical session formula */
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 2;
    set_monitor_cap(&d, 0, 0, 0, 1023, 767);
    set_monitor_cap(&d, 1, 1024, 0, 2047, 767);
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_A2,
                                      XRDP_nv12_709fr, 0,
                                      2048, 768, offs);
    ck_assert_int_eq(offs[0], 0);
    ck_assert_int_eq(offs[1], 1024 * 768 * 2);      /* 384 pages exactly */
    ck_assert_int_eq(total, 2 * (1024 * 768 * 2));
}
END_TEST

START_TEST(test_cap_layout_unaligned_dims_stay_disjoint)
{
    struct display_size_description d;
    int offs[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;
    int mon0_bytes;
    int mon1_bytes;

    /* odd dims: regions round up (16-px coded dims, page-aligned views
     * and regions) and must never overlap or overrun the total. mon1's
     * view (1376*784*1.5) is NOT a page multiple, so its aux offset and
     * region get page padding */
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 2;
    set_monitor_cap(&d, 0, 0, 0, 1365, 766);       /* 1366x767 */
    set_monitor_cap(&d, 1, 1366, 0, 2732, 769);    /* 1367x770 */
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      2733, 770, offs);
    mon0_bytes = 1376 * 768 * 3;
    mon1_bytes = xup_cap_avc444_aux_offset(1367, 770, 16)
                 + 1376 * 784 * 3 / 2;
    ck_assert_int_ge(xup_cap_avc444_aux_offset(1367, 770, 16),
                     1376 * 784 * 3 / 2);
    ck_assert_int_eq(xup_cap_avc444_aux_offset(1367, 770, 16)
                     % XUP_CAP_PAGE_ALIGN, 0);
    ck_assert_int_eq(offs[0], 0);
    ck_assert_int_ge(offs[1], mon0_bytes);
    ck_assert_int_eq(offs[1] % XUP_CAP_PAGE_ALIGN, 0);
    ck_assert_int_ge(total, offs[1] + mon1_bytes);
}
END_TEST

START_TEST(test_cap_layout_degenerate_monitor_zero_bytes)
{
    struct display_size_description d;
    int offs[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;

    /* a degenerate (1x1) monitor still gets a minimal coded region and
     * later monitors get valid disjoint offsets after it */
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 2;
    set_monitor_cap(&d, 0, 0, 0, 0, 0);            /* 1x1 -> 16x16 coded */
    set_monitor_cap(&d, 1, 0, 0, 1023, 767);
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      1024, 768, offs);
    ck_assert_int_eq(offs[0], 0);
    /* 16x16 coded: main view 384 B, aux view on the next page; the
     * region is page-padded so monitor 1 starts at 2 pages */
    ck_assert_int_eq(offs[1], 2 * XUP_CAP_PAGE_ALIGN);
    ck_assert_int_eq(total, 2 * XUP_CAP_PAGE_ALIGN + 1024 * 768 * 3);
}
END_TEST

/******************************************************************************/
Suite *
make_suite_avc444_multimon(void)
{
    Suite *s;
    TCase *tc;

    s = suite_create("Avc444Multimon");
    tc = tcase_create("avc444_multimon");
    tcase_add_test(tc, test_probe_dims_no_monitors_uses_screen);
    tcase_add_test(tc, test_probe_dims_null_uses_screen);
    tcase_add_test(tc, test_probe_dims_dual_equal_1024x768);
    tcase_add_test(tc, test_probe_dims_takes_max_per_axis);
    tcase_add_test(tc, test_probe_dims_alignment_round_up);
    tcase_add_test(tc, test_cap_layout_no_monitors_session_at_zero);
    tcase_add_test(tc, test_cap_layout_single_monitor_matches_session_formula);
    tcase_add_test(tc, test_cap_layout_owner_dual_disjoint);
    tcase_add_test(tc, test_cap_layout_nv12_dual);
    tcase_add_test(tc, test_cap_layout_unaligned_dims_stay_disjoint);
    tcase_add_test(tc, test_cap_layout_degenerate_monitor_zero_bytes);
    suite_add_tcase(s, tc);
    return s;
}
