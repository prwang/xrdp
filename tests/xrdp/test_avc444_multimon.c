#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <limits.h>

#include "xrdp.h"
#include "xrdp_client_info.h"
#include "xup_client_info.h"
#include "xrdp_encoder.h"
#include <stdio.h>
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
 *
 * FR-CAPTURE-8 (two-slot pipelined capture): every CC_GFX_AVC444 region
 * holds exactly XUP_CAP_AVC444_SLOT_COUNT (2) page-aligned slots so the
 * capture of frame N+1 can overlap the synchronous encode of frame N;
 * slot_bytes[] reports the per-monitor slot stride. Single-slot modes
 * (CC_GFX_A2) are unchanged and report a zero stride.
 *
 * Finally exercises xup_cap_budget: the PER-MONITOR outstanding-capture
 * accounting the capture side runs over those slots. Two properties are
 * ratcheted here because both were wrong in production before:
 *   - the budget is m independent caps of 2, never a global pool, so
 *     each monitor reaches depth 2 and never 3; retiring is against the
 *     CUMULATIVE rect_id_ack, which is why a 2-entry ring suffices;
 *   - the slot a frame lands in comes from a PER-MONITOR counter. The
 *     dead rule derived it from global rect_id parity, and since
 *     rect_id advances by the monitor count between one monitor's
 *     consecutive sends, that pinned each monitor to a single slot at
 *     an even monitor count (measured on the fleet: the same slot on
 *     1079 of 1079 full-pass sends, two-slot pipelining fired once in
 *     1100). The alternation test below replays such a trace and is
 *     RED against the parity rule.
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
    int slots[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;

    /* single screen: one region at offset 0, 16-aligned coded dims.
     * packed views ([main NV12][aux NV12], 1.5 B/px each) total the same
     * 3 B/px as the former planar YUV444 when the view size is already a
     * page multiple (1376*768*1.5 = 387 pages exactly); the region holds
     * two such slots (FR-CAPTURE-8) */
    total = xup_cap_h264_shmem_layout(NULL, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      1366, 768, offs, slots);
    ck_assert_int_eq(total, 2 * (1376 * 768 * 3));
    ck_assert_int_eq(offs[0], 0);
    ck_assert_int_eq(slots[0], 1376 * 768 * 3);
    ck_assert_int_eq(slots[0] % XUP_CAP_PAGE_ALIGN, 0);
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
                                      3840, 2400, offs, NULL);
    /* two slots of the old session formula (FR-CAPTURE-8) */
    ck_assert_int_eq(total, 2 * (3840 * 2400 * 3));
    ck_assert_int_eq(offs[0], 0);
    /* main-only (external AVC420, nv12_709fr under CC_GFX_AVC444) needs
     * just the one view per slot; the region still holds two slots
     * because the pipelined gate keys on the capture code */
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_AVC444,
                                      XRDP_nv12_709fr, 16,
                                      3840, 2400, offs, NULL);
    ck_assert_int_eq(total, 2 * (3840 * 2400 * 3 / 2));
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
                                      3840, 3840, offs, NULL);
    /* each region is two slots (FR-CAPTURE-8); the 4K's region must
     * still start beyond ALL of the primary's plane bytes */
    mon0_bytes = 2 * (2560 * 1440 * 3);
    ck_assert_int_eq(offs[0], 0);
    ck_assert_int_eq(offs[1], mon0_bytes);          /* already page-aligned */
    ck_assert_int_ge(offs[1], mon0_bytes);          /* disjoint */
    ck_assert_int_eq(total, mon0_bytes + 2 * (3840 * 2400 * 3));
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
                                      2048, 768, offs, NULL);
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
                                      2733, 770, offs, NULL);
    /* two slots per region (FR-CAPTURE-8) */
    mon0_bytes = 2 * (1376 * 768 * 3);
    mon1_bytes = 2 * (xup_cap_avc444_aux_offset(1367, 770, 16)
                      + 1376 * 784 * 3 / 2);
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
                                      1024, 768, offs, NULL);
    ck_assert_int_eq(offs[0], 0);
    /* 16x16 coded: main view 384 B, aux view on the next page, so one
     * slot is 2 pages and the two-slot region 4 pages (FR-CAPTURE-8) */
    ck_assert_int_eq(offs[1], 4 * XUP_CAP_PAGE_ALIGN);
    ck_assert_int_eq(total, 4 * XUP_CAP_PAGE_ALIGN + 2 * (1024 * 768 * 3));
}
END_TEST

START_TEST(test_cap_layout_two_slot_strides)
{
    struct display_size_description d;
    int offs[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int slots[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    int total;
    int index;

    /* FR-CAPTURE-8 contract: exactly two slots, and a frame's slot at
     * offsets[mon] + slot_index * slot_bytes[mon] never crosses into
     * the next monitor's region or past the total */
    ck_assert_int_eq(XUP_CAP_AVC444_SLOT_COUNT, 2);
    g_memset(&d, 0, sizeof(d));
    d.monitorCount = 2;
    set_monitor_cap(&d, 0, 594, 0, 3153, 1439);     /* 2560x1440 */
    set_monitor_cap(&d, 1, 0, 1440, 3839, 3839);    /* 3840x2400 */
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      3840, 3840, offs, slots);
    ck_assert_int_eq(slots[0], 2560 * 1440 * 3);
    ck_assert_int_eq(slots[1], 3840 * 2400 * 3);
    for (index = 0; index < 2; ++index)
    {
        ck_assert_int_eq(slots[index] % XUP_CAP_PAGE_ALIGN, 0);
        ck_assert_int_eq(slots[index],
                         xup_cap_avc444_slot_bytes(
                             XRDP_yuv444_v2_stream_709fr, 16,
                             d.minfo[index].right - d.minfo[index].left + 1,
                             d.minfo[index].bottom - d.minfo[index].top + 1));
    }
    /* slot 1 of monitor 0 ends exactly where monitor 1 begins here */
    ck_assert_int_eq(offs[0] + 2 * slots[0], offs[1]);
    ck_assert_int_eq(offs[1] + 2 * slots[1], total);

    /* single-slot modes report a zero stride */
    total = xup_cap_h264_shmem_layout(&d, CC_GFX_A2,
                                      XRDP_nv12_709fr, 0,
                                      3840, 3840, offs, slots);
    ck_assert_int_eq(slots[0], 0);
    ck_assert_int_eq(slots[1], 0);

    /* no-monitor session fills slot 0's stride */
    total = xup_cap_h264_shmem_layout(NULL, CC_GFX_AVC444,
                                      XRDP_yuv444_v2_stream_709fr, 16,
                                      1366, 768, offs, slots);
    ck_assert_int_eq(slots[0], 1376 * 768 * 3);
    ck_assert_int_eq(total, 2 * slots[0]);
}
END_TEST

START_TEST(test_cap_budget_starts_empty)
{
    struct xup_cap_budget b;
    int mon;

    xup_cap_budget_reset(&b);
    for (mon = 0; mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; ++mon)
    {
        ck_assert_int_eq(xup_cap_budget_retire(&b, mon, 0), 0);
        ck_assert_int_eq(xup_cap_budget_slot(&b, mon), 0);
        ck_assert_int_eq(xup_cap_budget_has_capacity(&b, mon, 0, 2), 1);
    }
    /* an unaddressable monitor fails closed and never advances a slot */
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, -1, 0, 2), 0);
    ck_assert_int_eq(xup_cap_budget_has_capacity(
                         &b, CLIENT_MONITOR_DATA_MAXIMUM_MONITORS, 0, 2), 0);
    ck_assert_int_ne(xup_cap_budget_record_send(&b, -1, 1, 0, 2), 0);
}
END_TEST

START_TEST(test_cap_budget_cap1_is_serial)
{
    struct xup_cap_budget b;

    /* single-slot modes (every capture code but CC_GFX_AVC444) pass a
     * cap of 1: one outstanding rect, and the slot never leaves 0 */
    xup_cap_budget_reset(&b);
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, 0, 0, 1), 1);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 0, 1, 0, 1), 0);
    ck_assert_int_eq(xup_cap_budget_slot(&b, 0), 0);
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, 0, 0, 1), 0);
    /* the ack for rect 1 frees it again */
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, 0, 1, 1), 1);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 0, 2, 1, 1), 0);
    ck_assert_int_eq(xup_cap_budget_slot(&b, 0), 0);
}
END_TEST

START_TEST(test_cap_budget_cap2_reaches_depth_two)
{
    struct xup_cap_budget b;

    /* CC_GFX_AVC444 passes a cap of 2: the second send is what the
     * pipeline is for, the third is refused */
    xup_cap_budget_reset(&b);
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, 0, 0, 2), 1);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 0, 1, 0, 2), 0);
    ck_assert_int_eq(xup_cap_budget_retire(&b, 0, 0), 1);
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, 0, 0, 2), 1);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 0, 2, 0, 2), 0);
    ck_assert_int_eq(xup_cap_budget_retire(&b, 0, 0), 2);
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, 0, 0, 2), 0);
    /* a cumulative ack of rect 1 frees exactly one entry */
    ck_assert_int_eq(xup_cap_budget_retire(&b, 0, 1), 1);
    ck_assert_int_eq(xup_cap_budget_has_capacity(&b, 0, 1, 2), 1);
}
END_TEST

START_TEST(test_cap_budget_third_capture_is_signalled)
{
    struct xup_cap_budget b;

    /* the overflow is RETURNED, never silent: the caller has to be able
     * to log it loudly (there is no third slot to land in) */
    xup_cap_budget_reset(&b);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 1, 1, 0, 2), 0);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 1, 2, 0, 2), 0);
    ck_assert_int_ne(xup_cap_budget_record_send(&b, 1, 3, 0, 2), 0);
    /* the ring keeps the newest ids, so it stays bounded and the ack
     * for rect 2 still retires everything at or below it */
    ck_assert_int_eq(xup_cap_budget_retire(&b, 1, 0), 2);
    ck_assert_int_eq(xup_cap_budget_retire(&b, 1, 2), 1);
    ck_assert_int_eq(xup_cap_budget_retire(&b, 1, 3), 0);
    /* the same cap of 1 signals on the SECOND send */
    xup_cap_budget_reset(&b);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 0, 1, 0, 1), 0);
    ck_assert_int_ne(xup_cap_budget_record_send(&b, 0, 2, 0, 1), 0);
}
END_TEST

START_TEST(test_cap_budget_two_monitors_never_reach_three)
{
    struct xup_cap_budget b;
    int rect_id;
    int rect_id_ack;
    int pass;
    int mon;

    /* the real dual-monitor trace: one send per monitor per pass, one
     * global ascending rect_id, CUMULATIVE acks. Each monitor must be
     * able to hold two outstanding frames (that is the pipeline) and
     * must never hold three (there is no third slot). The aggregate 4
     * is a consequence of the two caps and is never tested for. */
    xup_cap_budget_reset(&b);
    rect_id = 0;
    rect_id_ack = 0;
    for (pass = 0; pass < 2; ++pass)
    {
        for (mon = 0; mon < 2; ++mon)
        {
            ck_assert_int_eq(xup_cap_budget_has_capacity(&b, mon,
                             rect_id_ack, 2), 1);
            ++rect_id;
            ck_assert_int_eq(xup_cap_budget_record_send(&b, mon, rect_id,
                             rect_id_ack, 2), 0);
        }
    }
    /* both monitors are now at depth 2 and both are capped */
    for (mon = 0; mon < 2; ++mon)
    {
        ck_assert_int_eq(xup_cap_budget_retire(&b, mon, rect_id_ack), 2);
        ck_assert_int_eq(xup_cap_budget_has_capacity(&b, mon,
                         rect_id_ack, 2), 0);
    }
    /* one cumulative ack covering the first pass frees exactly one
     * entry in EACH monitor's ring */
    rect_id_ack = 2;
    for (mon = 0; mon < 2; ++mon)
    {
        ck_assert_int_eq(xup_cap_budget_retire(&b, mon, rect_id_ack), 1);
        ck_assert_int_eq(xup_cap_budget_has_capacity(&b, mon,
                         rect_id_ack, 2), 1);
    }
    /* a third pass fits, and still nobody exceeds two */
    for (mon = 0; mon < 2; ++mon)
    {
        ++rect_id;
        ck_assert_int_eq(xup_cap_budget_record_send(&b, mon, rect_id,
                         rect_id_ack, 2), 0);
        ck_assert_int_eq(xup_cap_budget_retire(&b, mon, rect_id_ack), 2);
    }
}
END_TEST

START_TEST(test_cap_budget_slot_alternates_per_monitor)
{
    struct xup_cap_budget b;
    int seen[2][4];
    int rect_id;
    int rect_id_ack;
    int pass;
    int mon;

    /* THE R1 RATCHET. Same dual-monitor full-pass trace, this time
     * recording the slot each frame lands in. Per monitor the slots
     * must alternate 0,1,0,1.
     *
     * This test is RED against the dead global rule slot =
     * (rect_id + 1) & 1: on this trace rect_id advances by 2 between a
     * monitor's consecutive sends, so that rule yields a CONSTANT slot
     * per monitor (monitor 0 always 1, monitor 1 always 0) and the
     * second slot of a monitor is never written -- exactly what R1
     * measured on the fleet. */
    xup_cap_budget_reset(&b);
    rect_id = 0;
    rect_id_ack = 0;
    for (pass = 0; pass < 4; ++pass)
    {
        for (mon = 0; mon < 2; ++mon)
        {
            ck_assert_int_eq(xup_cap_budget_has_capacity(&b, mon,
                             rect_id_ack, 2), 1);
            /* the slot is read BEFORE the send: it goes on the wire as
             * the frame's shmem_offset and it selects which slot the
             * capture refreshed */
            seen[mon][pass] = xup_cap_budget_slot(&b, mon);
            ++rect_id;
            ck_assert_int_eq(xup_cap_budget_record_send(&b, mon, rect_id,
                             rect_id_ack, 2), 0);
            /* and it advances exactly once, on the send */
            ck_assert_int_eq(xup_cap_budget_slot(&b, mon),
                             (seen[mon][pass] + 1) % 2);
        }
        rect_id_ack = rect_id;
    }
    for (mon = 0; mon < 2; ++mon)
    {
        ck_assert_int_eq(seen[mon][0], 0);
        ck_assert_int_eq(seen[mon][1], 1);
        ck_assert_int_eq(seen[mon][2], 0);
        ck_assert_int_eq(seen[mon][3], 1);
    }
    /* a monitor that sends alone (partial pass) alternates too */
    xup_cap_budget_reset(&b);
    ck_assert_int_eq(xup_cap_budget_slot(&b, 0), 0);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 0, 1, 0, 2), 0);
    ck_assert_int_eq(xup_cap_budget_slot(&b, 0), 1);
    ck_assert_int_eq(xup_cap_budget_record_send(&b, 0, 2, 1, 2), 0);
    ck_assert_int_eq(xup_cap_budget_slot(&b, 0), 0);
    /* and a monitor that never sends stays on slot 0 */
    ck_assert_int_eq(xup_cap_budget_slot(&b, 1), 0);
}
END_TEST

START_TEST(test_cap_budget_ack_everything_retires_all)
{
    struct xup_cap_budget b;
    int mon;

    /* rect_id_ack == INT_MAX is the "ack everything" the client region
     * message may send; every monitor's ring must empty */
    xup_cap_budget_reset(&b);
    for (mon = 0; mon < 3; ++mon)
    {
        ck_assert_int_eq(xup_cap_budget_record_send(&b, mon, mon * 2 + 1,
                         0, 2), 0);
        ck_assert_int_eq(xup_cap_budget_record_send(&b, mon, mon * 2 + 2,
                         0, 2), 0);
        ck_assert_int_eq(xup_cap_budget_retire(&b, mon, 0), 2);
    }
    for (mon = 0; mon < 3; ++mon)
    {
        ck_assert_int_eq(xup_cap_budget_retire(&b, mon, INT_MAX), 0);
        ck_assert_int_eq(xup_cap_budget_has_capacity(&b, mon, INT_MAX, 2),
                         1);
    }
}
END_TEST

/******************************************************************************/
/* capture || encode: the JOINT flow control, both halves at once.
 *
 * The two halves live in different processes and are separately tested
 * above (xup_cap_budget) and by construction (xrdp_gfx_ack_window_open).
 * Neither alone decides whether capture of frame N+1 overlaps encode of
 * frame N — a capture happens only when BOTH admit it, and the coupling
 * is that xorgxrdp's rect_id_ack advances ONLY when the xrdp window lets
 * mod_frame_ack through. This models that loop.
 *
 * PRD "Concurrency state of the encode pipeline" requires capture ||
 * encode at m = 1, and it had no CI assertion until BACKLOG #64 — where
 * a 180 s remote run was spent on a property that a unit test settles.
 * The wall-clock claim is NOT what is asserted here (a unit test cannot
 * measure wall clock); what is asserted is that the shipped predicates
 * ADMIT the overlap at m = 1 and refuse it when either half is closed.
 * That is the part a code change can regress.
 *
 * drive_pipeline() runs the loop for `frames` sends and returns the
 * maximum number of captures outstanding at any instant — the pipeline
 * depth. Depth >= 2 is overlap; depth 1 is a serial pipeline.
 */
#define DRIVE_MAX_PENDING 64

static int
drive_pipeline(int monitors, int cap, int fif, int enc_ticks, int ticks)
{
    struct xup_cap_budget b;
    int frame_id_server = 0;    /* frames xrdp has sent */
    int frame_id_client = 0;    /* frames the client has acknowledged */
    int rect_id = 0;            /* xorgxrdp's cumulative capture id */
    int rect_id_ack = 0;        /* how far mod_frame_ack has released it */
    /* frames sent but not yet acknowledged, oldest first: the round trip
     * is what creates the overlap, so it must be modelled explicitly */
    int pend_frame[DRIVE_MAX_PENDING];
    int pend_rect[DRIVE_MAX_PENDING];
    int pend_due[DRIVE_MAX_PENDING];
    int pend_n = 0;
    int max_depth = 0;
    int i;
    int mon;

    xup_cap_budget_reset(&b);
    for (i = 0; i < ticks; ++i)
    {
        /* CAPTURE STAGE. A capture happens only when BOTH halves admit
         * it: xorgxrdp's per-monitor slot budget and xrdp's global ack
         * window. This is the coupling under test. */
        for (mon = 0; mon < monitors; ++mon)
        {
            int depth;

            if (!xrdp_gfx_ack_window_open(frame_id_client,
                                          frame_id_server, fif))
            {
                break;
            }
            if (!xup_cap_budget_has_capacity(&b, mon, rect_id_ack, cap))
            {
                continue;
            }
            ++rect_id;
            if (xup_cap_budget_record_send(&b, mon, rect_id,
                                           rect_id_ack, cap) != 0)
            {
                continue;
            }
            ++frame_id_server;
            if (pend_n < DRIVE_MAX_PENDING)
            {
                pend_frame[pend_n] = frame_id_server;
                pend_rect[pend_n] = rect_id;
                pend_due[pend_n] = i + enc_ticks;
                ++pend_n;
            }
            /* outstanding captures for THIS monitor right now; >= 2 means
             * frame N+1 was captured while N was still unretired */
            depth = xup_cap_budget_retire(&b, mon, rect_id_ack);
            if (depth > max_depth)
            {
                max_depth = depth;
            }
        }

        /* COMPLETION STAGE: a frame captured at tick T is acknowledged at
         * T + enc_ticks, and that ack is the ONLY thing that advances
         * rect_id_ack. The latency is the whole point -- two capture
         * slots exist because the encode outlasts the capture, so with
         * enc_ticks = 1 nothing could ever run ahead and every
         * configuration would trivially read depth 1. */
        while (pend_n > 0 && pend_due[0] <= i)
        {
            int k;

            frame_id_client = pend_frame[0];
            rect_id_ack = pend_rect[0];
            for (k = 1; k < pend_n; ++k)
            {
                pend_frame[k - 1] = pend_frame[k];
                pend_rect[k - 1] = pend_rect[k];
                pend_due[k - 1] = pend_due[k];
            }
            --pend_n;
        }
    }
    return max_depth;
}

START_TEST(test_overlap_m1_capture_and_encode_do_overlap)
{
    /* THE REQUIREMENT: one monitor, shipped cap of 2 and shipped window
     * of 2 (DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT). Frame N+1's capture must
     * be admitted while frame N is still outstanding. */
    ck_assert_int_ge(drive_pipeline(1, 2, 2, 2, 32), 2);
}
END_TEST

START_TEST(test_overlap_m1_single_slot_is_serial)
{
    /* Control: the SAME loop with one capture slot must NOT overlap. If
     * this ever reaches 2 the model is admitting captures the budget did
     * not grant, and the test above proves nothing. */
    ck_assert_int_eq(drive_pipeline(1, 1, 2, 2, 32), 1);
}
END_TEST

START_TEST(test_overlap_m1_closed_ack_window_is_serial)
{
    /* Control: slots are available (cap 2) but the xrdp window is 1, so
     * rect_id_ack never advances far enough to free the second slot.
     * This is the coupling — the capture side cannot overlap on its own,
     * which is why BOTH halves belong in one test. */
    ck_assert_int_eq(drive_pipeline(1, 2, 1, 2, 32), 1);
}
END_TEST

/* --- adversarial: the model must RED on constructs that cannot happen ---
 *
 * drive_pipeline() is a model, and a model that cannot be wrong proves
 * nothing. Two silent modelling errors already slipped through while
 * writing the tests above -- acking in the same tick as the send, and a
 * 1-tick encode -- and BOTH made every configuration read depth 1, so a
 * "depth == 1" assertion passed for the wrong reason. These pin the
 * model's own invariants so the next such error fails loudly instead of
 * quietly agreeing with whatever was expected.
 */
START_TEST(test_overlap_model_never_exceeds_its_own_bounds)
{
    int cap;
    int fif;
    int mon;

    /* A capture that is outstanding occupies a slot AND a window entry,
     * so depth can never exceed either bound. A model that reports more
     * has invented capacity: it would let a future "the fix works" claim
     * pass on arithmetic that no shipped code can produce. */
    for (mon = 1; mon <= 3; ++mon)
    {
        for (cap = 1; cap <= 3; ++cap)
        {
            for (fif = 1; fif <= 6; ++fif)
            {
                int d = drive_pipeline(mon, cap, fif, 2, 32);

                ck_assert_int_le(d, cap);
                ck_assert_int_le(d, fif);
                ck_assert_int_ge(d, 0);
            }
        }
    }
}
END_TEST

START_TEST(test_overlap_model_zero_capacity_yields_no_captures)
{
    /* A window shut to nothing must produce NO captures: fif = 0 makes
     * frame_id_client + 0 > frame_id_server false from the first frame.
     * A model that still reports depth here is admitting work through a
     * closed gate, which is the exact failure mode that would make a
     * broken flow-control change look healthy. */
    ck_assert_int_eq(drive_pipeline(1, 2, 0, 2, 32), 0);
    ck_assert_int_eq(drive_pipeline(2, 2, 0, 2, 32), 0);

    /* cap = 0 does NOT mean "no captures": xup_cap_budget_clamp_cap()
     * floors the cap at 1 by design, so a degenerate or unset cap
     * degrades to a serial pipeline rather than wedging the session
     * entirely. Asserting 0 here was this test's own first draft and it
     * failed against the shipped floor -- pinned so the floor cannot be
     * removed silently. */
    ck_assert_int_eq(drive_pipeline(1, 0, 2, 2, 32), 1);
    ck_assert_int_eq(drive_pipeline(1, -5, 2, 2, 32), 1);
    /* and the ceiling: a cap above the slot count cannot buy depth the
     * shmem layout has no slots for */
    ck_assert_int_le(drive_pipeline(1, 99, 8, 4, 48),
                     XUP_CAP_AVC444_SLOT_COUNT);
}
END_TEST

START_TEST(test_overlap_model_instant_ack_cannot_overlap)
{
    /* THE IMPOSSIBLE CONSTRUCT, pinned. enc_ticks = 1 means the frame is
     * acknowledged in the same tick it was sent, so nothing can ever be
     * outstanding while the next capture is admitted. Depth MUST be 1 at
     * every window size -- if this ever reads >= 2 the model is
     * overlapping frames that were already retired, and every positive
     * result above becomes meaningless.
     *
     * This is not hypothetical: the first version of drive_pipeline()
     * acked inside the capture loop and reported depth 1 for m=1 fif=2
     * (no overlap) and depth 2 for m=1 fif=1 (overlap with a SMALLER
     * window) -- an inversion that only showed up because the controls
     * disagreed with each other.
     *
     * enc_ticks = 0, not 1: one tick of latency still leaves the frame
     * outstanding across the next tick's capture attempt, so it legally
     * reaches depth 2. Asserting this at enc_ticks = 1 was this test's
     * own first draft and it failed -- which is the point of writing it. */
    int fif;

    for (fif = 1; fif <= 8; ++fif)
    {
        ck_assert_int_eq(drive_pipeline(1, 2, fif, 0, 32), 1);
        ck_assert_int_eq(drive_pipeline(2, 2, fif, 0, 32), 1);
    }
    /* one tick of latency is the SHALLOWEST setting that can overlap,
     * and it must -- otherwise the positive m=1 result above is riding
     * on the latency parameter rather than on the gates */
    ck_assert_int_ge(drive_pipeline(1, 2, 2, 1, 32), 2);
}
END_TEST

START_TEST(test_cap_budget_two_gates_agree)
{
    /* xup_cap_budget has TWO gates on the same rule: has_capacity()
     * predicts, record_send() enforces. They must agree exactly.
     *
     * Found by mutation testing 2026-07-31: deleting the has_capacity()
     * call from the pipeline model changed NOTHING, because record_send()
     * refuses over-cap sends on its own. That is good defence in depth
     * and it was entirely unasserted -- so a change that broke the
     * PREDICTOR while leaving the enforcer intact (or vice versa) would
     * pass CI, and callers that branch on has_capacity() without checking
     * record_send()'s return would silently drop frames. */
    struct xup_cap_budget b;
    int cap;
    int i;
    int mon;

    for (cap = 1; cap <= XUP_CAP_AVC444_SLOT_COUNT; ++cap)
    {
        xup_cap_budget_reset(&b);
        for (mon = 0; mon < 2; ++mon)
        {
            int rect_id = 0;

            for (i = 0; i < 8; ++i)
            {
                int predicted = xup_cap_budget_has_capacity(&b, mon, 0,
                                cap);
                int refused;

                ++rect_id;
                refused = xup_cap_budget_record_send(&b, mon, rect_id, 0,
                                                     cap) != 0;
                /* predicted capacity <=> the send was accepted */
                ck_assert_int_eq(predicted, refused ? 0 : 1);
            }
        }
    }
}
END_TEST

START_TEST(test_overlap_model_widening_window_never_reduces_depth)
{
    /* Monotonicity. Relaxing a gate cannot make the pipeline shallower;
     * if it does, the model has a bookkeeping bug (an off-by-one in the
     * pending queue, or retiring against the wrong id). This is the
     * invariant that the fif=4 probe SHOULD have been checked against
     * before it was read as evidence (BACKLOG #64). */
    int fif;
    int prev = 0;

    for (fif = 1; fif <= 8; ++fif)
    {
        int d = drive_pipeline(1, 4, fif, 3, 48);

        ck_assert_int_ge(d, prev);
        prev = d;
    }
}
END_TEST

START_TEST(test_overlap_m2_global_window_pins_each_monitor_to_one)
{
    /* PRD, measured and stated: at m >= 2 the overlap is "partial and
     * accidental" — cross-monitor interleaving, while the two-slot
     * mechanism is inert PER MONITOR, because the global window of 2 is
     * consumed by 2 monitors at one frame each. Pinned here so that a
     * change making the window per-monitor shows up as a deliberate
     * test update rather than a silent behaviour change. */
    ck_assert_int_eq(drive_pipeline(2, 2, 2, 2, 32), 1);
    /* and the fix, when it comes: a window of 2 PER monitor restores
     * per-monitor depth 2 without a global pool (which the PRD forbids
     * for bufferbloat) */
    ck_assert_int_ge(drive_pipeline(2, 2, 2 * 2, 2, 32), 2);
}
END_TEST

/******************************************************************************/
/* PRD FR-ACK-1 / BACKLOG #64: the ack protocol itself.
 *
 * drive_pipeline() above models a LOSSLESS consumer -- every paint msg
 * it receives becomes an encoded frame on the wire. The AVC444 backend
 * is not lossless in that sense: the warmup PENDING return, an encoder
 * error and the ship-the-pair-or-nothing drop all CONSUME a msg and
 * produce no output frame. The protocol question is what the producer
 * hears about those, and it is a different question from "does the
 * shipped pair of predicates admit overlap", so it gets its own model
 * rather than an edit to that one.
 *
 * The two protocols under comparison:
 *
 *   ACK_INHERITED  the ack value is the CONSUMER'S OWN COUNT of frames
 *                  it has sent, and a consumed-without-output msg
 *                  produces no ack at all. The producer compares that
 *                  count against its rect_id, so after L such events
 *                  the two differ by L for the rest of the session --
 *                  the drift is monotonically nondecreasing, and once
 *                  it reaches the per-monitor cap the producer can
 *                  never admit another capture.
 *
 *   ACK_ON_CONSUME FR-ACK-1: EVERY consumed msg is acked exactly once,
 *                  when it reaches a terminal state, with the id
 *                  COPIED from that msg -- displayed=1 when the last
 *                  EGFX byte went out, displayed=0 otherwise. No
 *                  quantity this side maintains enters the ack value,
 *                  so no drift term can exist.
 *
 * What is asserted below is taken from the FR's own invariants and
 * prose, not from running this model: Invariant I (every sent r is
 * eventually covered, so the pipeline keeps making progress),
 * Invariant II (the bound is unchanged at cap), Invariant III (a
 * displayed=0 frame's region comes back exactly once), and the FR's
 * statement that a single unexpressed event pins the inherited
 * protocol at depth cap-1 and cap of them wedge it outright.
 */
#define ACK_INHERITED  0
#define ACK_ON_CONSUME 1
#define ACK_MAX_PENDING 64

struct ack_run
{
    int captures;       /* paint msgs the producer sent */
    int tail_captures;  /* of those, ones sent in the final quarter */
    int tail_depth;     /* deepest per-monitor RING count, final quarter */
    /* deepest per-monitor count of frames CAPTURED AND NOT YET
     * TERMINATED, final quarter. This, not tail_depth, is the quantity
     * "capture || encode" is a claim about, and the two differ exactly
     * where this FR bites: under the inherited protocol a ring entry
     * can belong to a frame that is long since on the wire but whose
     * drifting ack never retires it, so the ring reads 2 while at most
     * one capture is ever concurrent with an encode. A metric that
     * cannot tell those apart cannot test the claim. */
    int tail_inflight;
    int acks;           /* xup acks the consumer emitted */
    int max_drift;      /* max (terminating id - acked value) seen */
    int losses;         /* msgs consumed with no output frame */
    int returned;       /* of those, ones whose region was identified */
    int lost_unacked;   /* of those, ones that produced no ack at all */
};

/* One tick of the joint loop, with a consumer that can consume without
 * producing. loss_every > 0 loses every loss_every'th rect_id; with
 * loss_once set, only that one rect_id is lost and the rest of the run
 * is clean. */
static void
drive_consumer(int monitors, int cap, int fif, int enc_ticks,
               int client_ticks, int ticks, int loss_every, int loss_once,
               int protocol, struct ack_run *out)
{
    struct xup_cap_budget b;
    struct xup_cap_sent sent;
    int frame_id_server = 0;    /* last frame id the consumer put on the wire */
    int frame_id_client = 0;    /* last frame id the client acknowledged */
    int rect_id = 0;            /* the producer's cumulative capture id */
    int rect_id_ack = 0;        /* how far the xup ack has released it */
    int consumer_sends = 0;     /* the inherited protocol's ack value */
    int owed = 0;               /* xup ack deferred by a closed window */
    int enc_id[ACK_MAX_PENDING];
    int enc_mon[ACK_MAX_PENDING];
    int enc_due[ACK_MAX_PENDING];
    int enc_lost[ACK_MAX_PENDING];
    int enc_n = 0;
    int cli_id[ACK_MAX_PENDING];
    int cli_due[ACK_MAX_PENDING];
    int cli_n = 0;
    int tail_from = ticks - ticks / 4;
    int i;
    int k;
    int mon;

    xup_cap_budget_reset(&b);
    xup_cap_sent_reset(&sent);
    g_memset(out, 0, sizeof(*out));

    for (i = 0; i < ticks; ++i)
    {
        /* CAPTURE. Both halves must admit it, exactly as in the
         * lossless model: the producer's per-monitor slot budget and
         * the consumer's egfx ack window. */
        for (mon = 0; mon < monitors; ++mon)
        {
            int depth;

            if (!xrdp_gfx_ack_window_open(frame_id_client,
                                          frame_id_server, fif))
            {
                break;
            }
            if (!xup_cap_budget_has_capacity(&b, mon, rect_id_ack, cap))
            {
                continue;
            }
            ++rect_id;
            if (xup_cap_budget_record_send(&b, mon, rect_id,
                                           rect_id_ack, cap) != 0)
            {
                continue;
            }
            /* the region this capture took out of the dirty region is
             * held against the frame's own id until its ack says what
             * became of it */
            xup_cap_sent_take(&sent, mon, rect_id);
            if (enc_n < ACK_MAX_PENDING)
            {
                enc_id[enc_n] = rect_id;
                enc_mon[enc_n] = mon;
                enc_due[enc_n] = i + enc_ticks;
                enc_lost[enc_n] = loss_every > 0 &&
                                  (loss_once ? rect_id == loss_every
                                   : rect_id % loss_every == 0);
                ++enc_n;
            }
            ++out->captures;
            depth = xup_cap_budget_retire(&b, mon, rect_id_ack);
            if (i >= tail_from)
            {
                int inflight = 0;

                for (k = 0; k < enc_n; ++k)
                {
                    if (enc_mon[k] == mon)
                    {
                        ++inflight;
                    }
                }
                ++out->tail_captures;
                if (depth > out->tail_depth)
                {
                    out->tail_depth = depth;
                }
                if (inflight > out->tail_inflight)
                {
                    out->tail_inflight = inflight;
                }
            }
        }

        /* TERMINAL STATES, in dequeue order: the single consumer worker
         * reaches them one at a time, which is why acks are emitted in
         * nondecreasing id and a cumulative apply is sufficient. */
        while (enc_n > 0 && enc_due[0] <= i)
        {
            int id = enc_id[0];
            int lost = enc_lost[0];
            int ack_val = -1;
            int displayed = 1;

            for (k = 1; k < enc_n; ++k)
            {
                enc_id[k - 1] = enc_id[k];
                enc_mon[k - 1] = enc_mon[k];
                enc_due[k - 1] = enc_due[k];
                enc_lost[k - 1] = enc_lost[k];
            }
            --enc_n;

            if (lost)
            {
                ++out->losses;
                displayed = 0;
                if (protocol == ACK_ON_CONSUME)
                {
                    /* rule 2(b): acked immediately with the echoed id
                     * and ungated -- no frame of that id reached the
                     * client, so no client credit is owed for it */
                    ack_val = id;
                }
                else
                {
                    /* the inherited protocol cannot express this state */
                    ++out->lost_unacked;
                }
            }
            else
            {
                ++consumer_sends;
                frame_id_server = id;
                ack_val = (protocol == ACK_ON_CONSUME) ? id
                          : consumer_sends;
                if (cli_n < ACK_MAX_PENDING)
                {
                    cli_id[cli_n] = id;
                    cli_due[cli_n] = i + client_ticks;
                    ++cli_n;
                }
                if (!xrdp_gfx_ack_window_open(frame_id_client,
                                              frame_id_server, fif))
                {
                    /* held until the client frees credit, exactly as
                     * xrdp_mm_update_module_frame_ack holds it */
                    owed = ack_val;
                    ack_val = -1;
                }
            }

            if (ack_val >= 0)
            {
                int amon = 0;
                int aslot = 0;

                ++out->acks;
                if (id - ack_val > out->max_drift)
                {
                    out->max_drift = id - ack_val;
                }
                if (!displayed &&
                        xup_cap_sent_find(&sent, ack_val, &amon, &aslot))
                {
                    /* Invariant III: those pixels are on no wire, so
                     * the region goes back to the dirty region */
                    ++out->returned;
                    xup_cap_sent_clear(&sent, amon, aslot);
                }
                if (ack_val > rect_id_ack)
                {
                    rect_id_ack = ack_val;
                }
                xup_cap_sent_retire(&sent, rect_id_ack);
            }
        }

        /* CLIENT ACKS: they free egfx credit and flush a held xup ack */
        while (cli_n > 0 && cli_due[0] <= i)
        {
            frame_id_client = cli_id[0];
            for (k = 1; k < cli_n; ++k)
            {
                cli_id[k - 1] = cli_id[k];
                cli_due[k - 1] = cli_due[k];
            }
            --cli_n;
            if (owed > 0 &&
                    xrdp_gfx_ack_window_open(frame_id_client,
                                             frame_id_server, fif))
            {
                ++out->acks;
                if (owed > rect_id_ack)
                {
                    rect_id_ack = owed;
                }
                xup_cap_sent_retire(&sent, rect_id_ack);
                owed = 0;
            }
        }
    }
}

START_TEST(test_overlap_lossy_encode_wedges_without_consume_ack)
{
    struct ack_run r;

    /* FR-ACK-1, "Contrast, old protocol": the ack value is the
     * consumer's own send count, so r - ack is the number of
     * consumed-without-output events and never decreases. ONE such
     * event therefore costs one of the producer's two slots for the
     * rest of the session -- the pipeline is pinned at depth cap - 1,
     * which at the shipped cap of 2 is a SERIAL pipeline. */
    drive_consumer(1, 2, 2, 2, 1, 64, 8, 1, ACK_INHERITED, &r);
    ck_assert_int_eq(r.losses, 1);
    ck_assert_int_eq(r.lost_unacked, 1);
    ck_assert_int_eq(r.tail_inflight, 2 - 1);
    /* the ring still reads full: the slot is held by a frame that was
     * encoded and sent, and no drifting cumulative ack will ever
     * retire it. That is the ghost, and it is why the producer-side
     * budget counter alone showed nothing wrong on the live box. */
    ck_assert_int_eq(r.tail_depth, 2);

    /* and cap such events wedge it outright: the drift reaches the cap,
     * has_capacity is false forever, and the producer never captures
     * again. This is the measured "outstanding=0 in 0 of 1344 samples"
     * reproduced in logic. */
    drive_consumer(1, 2, 2, 2, 1, 64, 2, 0, ACK_INHERITED, &r);
    ck_assert_int_ge(r.losses, 2);
    ck_assert_int_eq(r.tail_captures, 0);
    ck_assert_int_eq(r.tail_inflight, 0);
}
END_TEST

START_TEST(test_overlap_consume_ack_restores_depth)
{
    struct ack_run r;
    int loss_every;

    /* Invariants I + II: every sent id is eventually covered, so
     * outstanding strictly decreases after every consumption and
     * capture admission recurs -- under ANY loss mask the producer
     * still reaches, and keeps reaching, the full cap of 2. The bound
     * itself is unchanged: it never exceeds the cap. */
    for (loss_every = 2; loss_every <= 7; ++loss_every)
    {
        drive_consumer(1, 2, 2, 2, 1, 64, loss_every, 0,
                       ACK_ON_CONSUME, &r);
        ck_assert_int_gt(r.losses, 0);
        ck_assert_int_eq(r.lost_unacked, 0);
        ck_assert_int_eq(r.tail_inflight, 2);
        /* Invariant II is untouched: the bound is still the cap */
        ck_assert_int_eq(r.tail_depth, 2);
        ck_assert_int_gt(r.tail_captures, 0);
    }
    /* the single-event case that pins the inherited protocol forever */
    drive_consumer(1, 2, 2, 2, 1, 64, 8, 1, ACK_ON_CONSUME, &r);
    ck_assert_int_eq(r.losses, 1);
    ck_assert_int_eq(r.tail_inflight, 2);
}
END_TEST

START_TEST(test_ack_value_is_echoed_identity)
{
    struct ack_run r;
    int loss_every;

    /* Rule 1: the acked value is copied from the received msg, so for
     * every ack the difference between the terminating id and the value
     * acked is ZERO -- there is no term that could accumulate. */
    for (loss_every = 2; loss_every <= 9; ++loss_every)
    {
        drive_consumer(1, 2, 2, 2, 1, 64, loss_every, 0,
                       ACK_ON_CONSUME, &r);
        ck_assert_int_eq(r.max_drift, 0);
        drive_consumer(2, 2, 4, 2, 1, 64, loss_every, 0,
                       ACK_ON_CONSUME, &r);
        ck_assert_int_eq(r.max_drift, 0);
    }
    /* a clean run cannot tell the two protocols apart -- which is why
     * the inherited one survived this long */
    drive_consumer(1, 2, 2, 2, 1, 64, 0, 0, ACK_INHERITED, &r);
    ck_assert_int_eq(r.losses, 0);
    ck_assert_int_eq(r.max_drift, 0);
    /* one unexpressed event and the counter-based value is already
     * behind, permanently */
    drive_consumer(1, 2, 2, 2, 1, 64, 8, 1, ACK_INHERITED, &r);
    ck_assert_int_ge(r.max_drift, 1);
}
END_TEST

START_TEST(test_dropped_frame_region_returns_to_dirty)
{
    struct xup_cap_sent sent;
    struct ack_run r;
    int mon = -1;
    int slot = -1;

    /* Invariant III at the unit level: the producer must be able to
     * name the region a displayed=0 ack refers to, from the id alone. */
    xup_cap_sent_reset(&sent);
    ck_assert_int_eq(xup_cap_sent_find(&sent, 7, &mon, &slot), 0);
    ck_assert_int_eq(xup_cap_sent_take(&sent, 0, 7), 0);
    ck_assert_int_eq(xup_cap_sent_take(&sent, 0, 8), 1);
    ck_assert_int_eq(xup_cap_sent_find(&sent, 7, &mon, &slot), 1);
    ck_assert_int_eq(mon, 0);
    ck_assert_int_eq(slot, 0);
    ck_assert_int_eq(xup_cap_sent_find(&sent, 8, &mon, &slot), 1);
    ck_assert_int_eq(slot, 1);
    /* a region is returned ONCE: after it is taken back, the same ack
     * arriving again (duplicate, or a cumulative ack covering it) must
     * find nothing to return */
    xup_cap_sent_clear(&sent, 0, 0);
    ck_assert_int_eq(xup_cap_sent_find(&sent, 7, &mon, &slot), 0);
    /* the cumulative ack retires whatever it covers and nothing above */
    ck_assert_int_eq(xup_cap_sent_take(&sent, 1, 9), 0);
    ck_assert_int_eq(xup_cap_sent_retire(&sent, 8), 1);
    ck_assert_int_eq(xup_cap_sent_find(&sent, 8, &mon, &slot), 0);
    ck_assert_int_eq(xup_cap_sent_find(&sent, 9, &mon, &slot), 1);
    /* an id that was never sent is not addressable at all */
    ck_assert_int_eq(xup_cap_sent_find(&sent, 4242, &mon, &slot), 0);
    /* zero is the empty marker, never a frame */
    xup_cap_sent_reset(&sent);
    ck_assert_int_eq(xup_cap_sent_find(&sent, 0, &mon, &slot), 0);

    /* and over a whole run: EVERY frame consumed without output has its
     * region identified and returned, exactly once each -- no pixel is
     * silently dropped, and no region comes back twice */
    drive_consumer(1, 2, 2, 2, 1, 64, 3, 0, ACK_ON_CONSUME, &r);
    ck_assert_int_gt(r.losses, 0);
    ck_assert_int_eq(r.returned, r.losses);
    drive_consumer(2, 2, 4, 2, 1, 64, 2, 0, ACK_ON_CONSUME, &r);
    ck_assert_int_gt(r.losses, 0);
    ck_assert_int_eq(r.returned, r.losses);
}
END_TEST

START_TEST(test_xup_ack_displayed_bit_serialization)
{
    struct stream *s;
    int flags;
    int frame_id;

    /* The bit rides the flags word the paint-rect-ex ack already
     * carries, and displayed=1 encodes to the value xrdp sends today --
     * that is the whole compatibility claim: happy-path bytes unchanged,
     * old peers ignore what they do not know. */
    ck_assert_int_eq(xup_ack_flags_make(1), 0);
    ck_assert_int_eq(xup_ack_flags_make(0), XUP_ACK_FLAGS_NOT_DISPLAYED);
    ck_assert_int_eq(xup_ack_flags_displayed(xup_ack_flags_make(1)), 1);
    ck_assert_int_eq(xup_ack_flags_displayed(xup_ack_flags_make(0)), 0);
    /* an old peer's zero flags must read as displayed, or every frame
     * from an unchanged xrdp would be re-dirtied forever */
    ck_assert_int_eq(xup_ack_flags_displayed(0), 1);

    /* the two fields the consumer writes and the producer reads, in
     * that order, over the message body */
    make_stream(s);
    init_stream(s, 64);
    out_uint32_le(s, xup_ack_flags_make(0));
    out_uint32_le(s, 123456);
    s_mark_end(s);
    ck_assert_int_eq((int)(s->end - s->data), 8);
    s->p = s->data;
    in_uint32_le(s, flags);
    in_uint32_le(s, frame_id);
    ck_assert_int_eq(xup_ack_flags_displayed(flags), 0);
    ck_assert_int_eq(frame_id, 123456);
    free_stream(s);

    make_stream(s);
    init_stream(s, 64);
    out_uint32_le(s, xup_ack_flags_make(1));
    out_uint32_le(s, INT_MAX);
    s_mark_end(s);
    s->p = s->data;
    in_uint32_le(s, flags);
    in_uint32_le(s, frame_id);
    ck_assert_int_eq(xup_ack_flags_displayed(flags), 1);
    ck_assert_int_eq(frame_id, INT_MAX);
    free_stream(s);
}
END_TEST

/******************************************************************************/
/* BACKLOG #70: the eager slot-release ack.
 *
 * The two frontier rules first, then a four-resource pipeline model.
 *
 * What #70 changes is WHEN the producer's capture slot is released. The
 * shipped ack rides the frame's last transport write, so capture,
 * encode, rewrite and egress each wait for all the others. The eager
 * ack fires for frame N when
 *
 *   (a) the encoder children have absorbed N's input, and
 *   (b) frame N-1's last PDU has been handed to the transport,
 *
 * and it is SLOT_ONLY: it frees the slot and says nothing about the
 * frame, whose region must stay held because its tail can still fail.
 *
 * The numbers asserted below are derived from that rule and from the
 * resource layout of the model, never read off a run: a serial worker
 * that spends absorb+rewrite per frame cannot beat one frame per
 * absorb+rewrite however the acks are paced; a protocol that admits the
 * next capture only after the whole chain cannot hold more than one
 * frame between capture and termination; one that admits it after
 * absorb holds exactly two; and one that drops condition (b) admits
 * captures at the worker's rate against a slower egress, so its backlog
 * is a function of how long the run is -- which is the bufferbloat the
 * PRD forbids, and the reason (b) exists.
 */
START_TEST(test_ack_frontier_slot_only_holds_the_region_frontier)
{
    struct xup_ack_frontier f;

    xup_ack_frontier_reset(&f);
    ck_assert_int_eq(f.slot, 0);
    ck_assert_int_eq(f.shown, 0);

    /* a slot-only ack says the consumer has finished READING frame 5.
     * Nothing is known about frame 5's fate, so no region may be
     * forgotten -- the whole point of the flag. */
    xup_ack_frontier_apply(&f, XUP_ACK_FLAGS_SLOT_ONLY, 5);
    ck_assert_int_eq(f.slot, 5);
    ck_assert_int_eq(f.shown, 0);

    /* the ordinary ack for the same frame disposes of it */
    xup_ack_frontier_apply(&f, 0, 5);
    ck_assert_int_eq(f.slot, 5);
    ck_assert_int_eq(f.shown, 5);

    /* displayed=0 is a disposal too: it is the ack that hands the
     * region back, so it must move the shown frontier */
    xup_ack_frontier_apply(&f, XUP_ACK_FLAGS_NOT_DISPLAYED, 6);
    ck_assert_int_eq(f.slot, 6);
    ck_assert_int_eq(f.shown, 6);
}
END_TEST

START_TEST(test_ack_frontier_is_cumulative_and_monotonic)
{
    struct xup_ack_frontier f;

    xup_ack_frontier_reset(&f);
    xup_ack_frontier_apply(&f, 0, 9);
    /* a duplicated or reordered ack must not resurrect a retired slot
     * or a forgotten region (rule 4) */
    xup_ack_frontier_apply(&f, 0, 3);
    ck_assert_int_eq(f.slot, 9);
    ck_assert_int_eq(f.shown, 9);
    xup_ack_frontier_apply(&f, XUP_ACK_FLAGS_SLOT_ONLY, 2);
    ck_assert_int_eq(f.slot, 9);
    ck_assert_int_eq(f.shown, 9);
    /* shown never exceeds slot: nothing is disposed of before the
     * consumer has finished reading it */
    xup_ack_frontier_apply(&f, XUP_ACK_FLAGS_SLOT_ONLY, 12);
    ck_assert_int_eq(f.slot, 12);
    ck_assert_int_eq(f.shown, 9);
    ck_assert_int_le(f.shown, f.slot);
    xup_ack_frontier_apply(&f, 0, 12);
    ck_assert_int_le(f.shown, f.slot);
    /* a NULL frontier is a no-op, not a crash */
    xup_ack_frontier_apply(NULL, 0, 1);
    xup_ack_frontier_reset(NULL);
}
END_TEST

#define EAGER_OFF   0   /* shipped: the ack rides the last write */
#define EAGER_ON    1   /* #70: ack at max(absorb N, egress N-1) */
#define EAGER_NO_BP 2   /* adversarial: absorb only, condition (b) dropped */
#define EAGER_MAXQ  256

struct eager_run
{
    int captures;        /* frames the producer put on the wire */
    int terminated;      /* frames that reached a terminal state */
    int tail_inflight;   /* max captured-and-not-terminated, tail quarter */
    int max_egress_q;    /* deepest queue waiting for the transport */
    int slot_acks;
    int slot_clobber;    /* captures into a slot whose region is held */
    int region_lost;     /* failed frames whose region was forgotten */
    int returned;        /* failed frames whose region came back */
    int losses;
    int bad_absorb;      /* slot acks naming an unabsorbed frame */
    int bad_backpressure;/* slot acks emitted before egress(id-1) */
    int frontier_broken; /* shown > slot, ever */
};

/* One m=1 pipeline over four resources, one frame deep each:
 *
 *   producer  cap_ticks       the X capture and pack
 *   worker    absorb_ticks    the children read the vmsplice'd input
 *             rewrite_ticks   then the LTR rewrite and EGFX assembly
 *   main      egress_ticks    the transport writes
 *
 * The producer is admitted by the xup ack alone (the real budget helpers
 * are the ones consulted), and the client's egfx window is the outer
 * gate. Only the ack emission rule differs between protocols. */
static void
drive_eager(int protocol, int ticks, int cap_ticks, int absorb_ticks,
            int rewrite_ticks, int egress_ticks, int fif, int cap,
            int client_ticks, int loss_every, struct eager_run *out)
{
    struct xup_cap_budget b;
    struct xup_cap_sent sent;
    struct xup_ack_frontier front;
    int work_q[EAGER_MAXQ];
    int work_n = 0;
    int egress_q[EAGER_MAXQ];
    int egress_n = 0;
    int rect_id = 0;
    int absorbed = 0;           /* highest id whose input is drained */
    int frame_id_server = 0;    /* highest id fully handed to transport */
    int frame_id_client = 0;
    int acked = 0;              /* highest value any ack carried */
    int region_sent = 0;        /* highest value an ordinary ack carried */
    int cap_busy = 0;
    int cap_at = 0;
    int worker_busy = 0;
    int worker_id = 0;
    int worker_absorb_at = 0;
    int worker_done_at = 0;
    int main_busy = 0;
    int main_id = 0;
    int main_done_at = 0;
    int cli_id[EAGER_MAXQ];
    int cli_due[EAGER_MAXQ];
    int cli_n = 0;
    int tail_from = ticks - ticks / 4;
    int inflight = 0;
    int i;
    int k;

    xup_cap_budget_reset(&b);
    xup_cap_sent_reset(&sent);
    xup_ack_frontier_reset(&front);
    g_memset(out, 0, sizeof(*out));

    for (i = 0; i < ticks; ++i)
    {
        int emit = 0;

        /* --- completions, oldest stage first --- */
        if (main_busy && main_done_at == i)
        {
            int lost = loss_every > 0 && (main_id % loss_every) == 0;

            main_busy = 0;
            --inflight;
            ++out->terminated;
            if (lost)
            {
                int amon = 0;
                int aslot = 0;

                ++out->losses;
                /* the tail failed AFTER the slot was already released:
                 * the region must still be findable, or those pixels
                 * are gone with no event anywhere */
                if (xup_cap_sent_find(&sent, main_id, &amon, &aslot))
                {
                    ++out->returned;
                    xup_cap_sent_clear(&sent, amon, aslot);
                }
                else
                {
                    ++out->region_lost;
                }
                if (main_id > acked)
                {
                    acked = main_id;
                }
                if (main_id > region_sent)
                {
                    region_sent = main_id;
                }
                xup_ack_frontier_apply(&front, XUP_ACK_FLAGS_NOT_DISPLAYED,
                                       main_id);
                xup_cap_sent_retire(&sent, front.shown);
            }
            else
            {
                frame_id_server = main_id;
                if (cli_n < EAGER_MAXQ)
                {
                    cli_id[cli_n] = main_id;
                    cli_due[cli_n] = i + client_ticks;
                    ++cli_n;
                }
            }
            emit = 1;
        }
        if (worker_busy && worker_done_at == i)
        {
            worker_busy = 0;
            if (egress_n < EAGER_MAXQ)
            {
                egress_q[egress_n++] = worker_id;
            }
            if (egress_n > out->max_egress_q)
            {
                out->max_egress_q = egress_n;
            }
        }
        else if (worker_busy && worker_absorb_at == i)
        {
            absorbed = worker_id;
            emit = 1;
        }
        if (cap_busy && cap_at == i)
        {
            cap_busy = 0;
            ++rect_id;
            xup_cap_budget_record_send(&b, 0, rect_id, front.slot, cap);
            /* every captured frame must get an entry to hold its region
             * in. A full map means more undisposed frames than the map
             * can name, and the next failure would have no pixels to
             * give back -- the corner the +1 entry exists for. */
            if (xup_cap_sent_take(&sent, 0, rect_id) < 0)
            {
                ++out->slot_clobber;
            }
            if (work_n < EAGER_MAXQ)
            {
                work_q[work_n++] = rect_id;
            }
            ++out->captures;
            ++inflight;
            if (i >= tail_from && inflight > out->tail_inflight)
            {
                out->tail_inflight = inflight;
            }
        }

        /* --- client acks free egfx credit --- */
        while (cli_n > 0 && cli_due[0] <= i)
        {
            frame_id_client = cli_id[0];
            for (k = 1; k < cli_n; ++k)
            {
                cli_id[k - 1] = cli_id[k];
                cli_due[k - 1] = cli_due[k];
            }
            --cli_n;
            emit = 1;
        }

        /* --- ack emission --- */
        if (emit && xrdp_gfx_ack_window_open(frame_id_client,
                                             frame_id_server, fif))
        {
            if (frame_id_server > region_sent)
            {
                region_sent = frame_id_server;
                if (region_sent > acked)
                {
                    acked = region_sent;
                }
                xup_ack_frontier_apply(&front, 0, region_sent);
                xup_cap_sent_retire(&sent, front.shown);
            }
            if (protocol != EAGER_OFF)
            {
                int target = absorbed;

                if (protocol == EAGER_ON && target > frame_id_server + 1)
                {
                    target = frame_id_server + 1;
                }
                if (target > acked)
                {
                    if (target > absorbed)
                    {
                        ++out->bad_absorb;
                    }
                    if (target > frame_id_server + 1)
                    {
                        ++out->bad_backpressure;
                    }
                    acked = target;
                    ++out->slot_acks;
                    xup_ack_frontier_apply(&front, XUP_ACK_FLAGS_SLOT_ONLY,
                                           target);
                }
            }
        }
        if (front.shown > front.slot)
        {
            ++out->frontier_broken;
        }

        /* --- starts, downstream first so a freed stage is refilled --- */
        if (!main_busy && egress_n > 0)
        {
            main_id = egress_q[0];
            for (k = 1; k < egress_n; ++k)
            {
                egress_q[k - 1] = egress_q[k];
            }
            --egress_n;
            main_busy = 1;
            main_done_at = i + egress_ticks;
        }
        if (!worker_busy && work_n > 0)
        {
            worker_id = work_q[0];
            for (k = 1; k < work_n; ++k)
            {
                work_q[k - 1] = work_q[k];
            }
            --work_n;
            worker_busy = 1;
            worker_absorb_at = i + absorb_ticks;
            worker_done_at = i + absorb_ticks + rewrite_ticks;
        }
        if (!cap_busy &&
                xrdp_gfx_ack_window_open(frame_id_client, frame_id_server,
                                         fif) &&
                xup_cap_budget_has_capacity(&b, 0, front.slot, cap))
        {
            cap_busy = 1;
            cap_at = i + cap_ticks;
        }
    }
}

/* The parameters the two throughput tests share. The worker is the
 * heaviest resource, exactly as measured on the T4 (absorb+rewrite
 * 24.1+44.9 ms against 30.4 ms of egress), so the eager protocol's
 * ceiling is the worker and the shipped protocol's is the whole chain. */
#define EG_TICKS   400
#define EG_CAP_T   1
#define EG_ABS_T   2
#define EG_REW_T   4
#define EG_EGR_T   3
#define EG_CHAIN   (EG_CAP_T + EG_ABS_T + EG_REW_T + EG_EGR_T)
#define EG_WORKER  (EG_ABS_T + EG_REW_T)

START_TEST(test_eager_ack_buys_nothing_when_the_budget_already_admits_two)
{
    struct eager_run off;
    struct eager_run on;

    drive_eager(EAGER_OFF, EG_TICKS, EG_CAP_T, EG_ABS_T, EG_REW_T,
                EG_EGR_T, 2, 2, 1, 0, &off);
    drive_eager(EAGER_ON, EG_TICKS, EG_CAP_T, EG_ABS_T, EG_REW_T,
                EG_EGR_T, 2, 2, 1, 0, &on);

    /* The shipped ack already admits a SECOND capture: the producer's
     * budget is two slots and the ack retires the first at egress, so a
     * frame is captured while its predecessor is still in the tail.
     * That is the state this model is in, and it is NOT the state the
     * T4 was measured in -- see the next test. */
    ck_assert_int_eq(off.tail_inflight, 2);
    ck_assert_int_eq(off.slot_acks, 0);

    /* the eager ack adds exactly one more frame between capture and
     * termination, bounded there by condition (b) */
    ck_assert_int_eq(on.tail_inflight, 3);
    ck_assert_int_gt(on.slot_acks, 0);

    /* ...and buys no throughput, because with two captures already in
     * hand the serial worker is the floor and no ack can move it. A
     * deeper pipeline against the same bottleneck is not a faster one.
     * This assertion exists to keep #70 from being sold as a speedup in
     * a regime where it is not one. */
    ck_assert_int_le(off.captures, EG_TICKS / EG_WORKER + 2);
    ck_assert_int_le(on.captures, EG_TICKS / EG_WORKER + 2);
    ck_assert_int_le(on.captures - off.captures, 2);

    ck_assert_int_eq(on.bad_absorb, 0);
    ck_assert_int_eq(on.bad_backpressure, 0);
    ck_assert_int_eq(on.frontier_broken, 0);
    ck_assert_int_eq(off.frontier_broken, 0);
}
END_TEST

START_TEST(test_eager_ack_restores_the_floor_at_effective_depth_one)
{
    struct eager_run off;
    struct eager_run on;

    /* cap=1: the producer may hold ONE outstanding frame. This is the
     * regime the deployed T4 was measured in -- period 113.6 ms against
     * a 69 ms worker (absorb 24.1 + rewrite/assembly 44.9), i.e. the
     * whole chain rather than its busiest resource (BACKLOG #70,
     * captures i55_t4_cond*). Whether the deployment is here because of
     * the budget or because a slot is dead is #70 step 0's question;
     * what this test pins is what the ack rule is worth once it is. */
    drive_eager(EAGER_OFF, EG_TICKS, EG_CAP_T, EG_ABS_T, EG_REW_T,
                EG_EGR_T, 2, 1, 1, 0, &off);
    drive_eager(EAGER_ON, EG_TICKS, EG_CAP_T, EG_ABS_T, EG_REW_T,
                EG_EGR_T, 2, 1, 1, 0, &on);

    /* one frame at a time: capture, encode, rewrite and egress each
     * wait for all the others, so the period is their SUM */
    ck_assert_int_eq(off.tail_inflight, 1);
    ck_assert_int_le(off.captures, EG_TICKS / EG_CHAIN + 2);

    /* the eager ack admits the next capture at absorb, so the period
     * collapses to the busiest single resource -- the worker */
    ck_assert_int_eq(on.tail_inflight, 2);
    ck_assert_int_ge(on.captures, EG_TICKS / EG_WORKER - 1);

    /* the gain is the chain-to-worker ratio, 10 ticks against 6 here;
     * anything at or below 1.5x means the ack did not move the pacer */
    ck_assert_int_gt(on.captures * 2, off.captures * 3);

    /* and one capture slot still holds one region: nothing overflows */
    ck_assert_int_eq(on.slot_clobber, 0);
    ck_assert_int_eq(on.bad_backpressure, 0);
    ck_assert_int_eq(on.frontier_broken, 0);
}
END_TEST

START_TEST(test_eager_ack_never_drops_a_region_a_failing_tail_owes_back)
{
    struct eager_run on;
    struct eager_run off;

    /* every third frame fails in its TAIL -- after its slot ack has
     * already gone out, which is the corner SLOT_ONLY exists for. Its
     * region must still be there to hand back. */
    drive_eager(EAGER_ON, EG_TICKS, EG_CAP_T, EG_ABS_T, EG_REW_T,
                EG_EGR_T, 2, 2, 1, 3, &on);
    drive_eager(EAGER_OFF, EG_TICKS, EG_CAP_T, EG_ABS_T, EG_REW_T,
                EG_EGR_T, 2, 2, 1, 3, &off);

    ck_assert_int_gt(on.losses, 0);
    ck_assert_int_eq(on.region_lost, 0);
    ck_assert_int_eq(on.returned, on.losses);
    /* every captured frame found an entry to hold its region in: this
     * is what the +1 in XUP_CAP_SENT_SLOTS is for, and sizing the map
     * at the slot count instead FAILS here (measured while writing it:
     * 45 overflows, 21 regions lost) */
    ck_assert_int_eq(on.slot_clobber, 0);
    /* and the shipped protocol is untouched by any of it */
    ck_assert_int_gt(off.losses, 0);
    ck_assert_int_eq(off.region_lost, 0);
    ck_assert_int_eq(off.returned, off.losses);
    ck_assert_int_eq(off.slot_clobber, 0);
}
END_TEST

START_TEST(test_absorb_only_ack_bufferbloats_and_the_metric_shows_it)
{
    struct eager_run on_short;
    struct eager_run on_long;
    struct eager_run nobp_short;
    struct eager_run nobp_long;

    /* egress SLOWER than the worker: absorb 1 + rewrite 1 against 6 of
     * transport. Dropping condition (b) admits captures at the worker's
     * rate against a queue that drains at a third of it. */
    drive_eager(EAGER_NO_BP, 200, 1, 1, 1, 6, 2, 2, 1, 0, &nobp_short);
    drive_eager(EAGER_NO_BP, 400, 1, 1, 1, 6, 2, 2, 1, 0, &nobp_long);
    drive_eager(EAGER_ON, 200, 1, 1, 1, 6, 2, 2, 1, 0, &on_short);
    drive_eager(EAGER_ON, 400, 1, 1, 1, 6, 2, 2, 1, 0, &on_long);

    /* a backlog that is a function of run length is the bufferbloat the
     * PRD forbids; it is what condition (b) removes */
    ck_assert_int_gt(nobp_long.max_egress_q, nobp_short.max_egress_q);
    ck_assert_int_gt(nobp_long.bad_backpressure, 0);

    /* with (b) the queue is bounded by the token, not by the run */
    ck_assert_int_eq(on_long.max_egress_q, on_short.max_egress_q);
    ck_assert_int_le(on_long.max_egress_q, 2);
    ck_assert_int_eq(on_long.bad_backpressure, 0);
    /* and the slow transport, not the ack, is what paces it */
    ck_assert_int_le(on_long.captures, 400 / 6 + 2);
}
END_TEST

START_TEST(test_capture_contract_rejects_a_previous_resize_layout)
{
    struct display_size_description initial;
    struct display_size_description same_coded_size;
    struct display_size_description grown;
    struct xup_avc444_capture_layout layout;
    struct xup_avc444_capture_layout retained;
    uint32_t monitor;
    uint32_t slot;
    uint32_t flags;

    g_memset(&initial, 0, sizeof(initial));
    initial.monitorCount = 1;
    initial.session_width = 2196;
    initial.session_height = 1250;
    set_monitor_cap(&initial, 0, 0, 0, 2195, 1249);

    same_coded_size = initial;
    same_coded_size.session_width = 2198;
    same_coded_size.minfo[0].right = 2197;
    grown = same_coded_size;
    grown.session_width = 2412;
    grown.session_height = 1344;
    grown.minfo[0].right = 2411;
    grown.minfo[0].bottom = 1343;

    ck_assert_int_eq(xup_avc444_layout_build(
                         &initial, XRDP_yuv444_v2_stream_709fr, 32,
                         &layout), 0);
    ck_assert_uint_eq(layout.total_bytes, 16760832U);
    ck_assert_int_eq(xup_avc444_layout_matches(
                         &same_coded_size, XRDP_yuv444_v2_stream_709fr,
                         32, &layout), 0);
    ck_assert_int_eq(xup_avc444_layout_matches(
                         &grown, XRDP_yuv444_v2_stream_709fr,
                         32, &layout), 0);
    ck_assert_int_eq(xup_avc444_layout_refresh(
                         &grown, XRDP_yuv444_v2_stream_709fr, 32,
                         &layout), 0);
    ck_assert_uint_eq(layout.total_bytes, 19611648U);
    ck_assert_int_eq(xup_avc444_layout_matches(
                         &grown, XRDP_yuv444_v2_stream_709fr,
                         32, &layout), 1);

    retained = layout;
    grown.minfo[0].right = -1;
    ck_assert_int_eq(xup_avc444_layout_refresh(
                         &grown, XRDP_yuv444_v2_stream_709fr, 32,
                         &layout), 1);
    ck_assert_mem_eq(&layout, &retained, sizeof(layout));

    flags = xup_avc444_capture_flags(0, 0, 1);
    ck_assert_int_eq(xup_avc444_capture_identity(flags, &monitor, &slot), 0);
    ck_assert_uint_eq(monitor, 0U);
    ck_assert_uint_eq(slot, 1U);
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
    tcase_add_test(tc, test_cap_layout_two_slot_strides);
    tcase_add_test(tc, test_cap_budget_starts_empty);
    tcase_add_test(tc, test_cap_budget_cap1_is_serial);
    tcase_add_test(tc, test_cap_budget_cap2_reaches_depth_two);
    tcase_add_test(tc, test_cap_budget_third_capture_is_signalled);
    tcase_add_test(tc, test_cap_budget_two_monitors_never_reach_three);
    tcase_add_test(tc, test_cap_budget_slot_alternates_per_monitor);
    tcase_add_test(tc, test_cap_budget_ack_everything_retires_all);
    tcase_add_test(tc, test_overlap_m1_capture_and_encode_do_overlap);
    tcase_add_test(tc, test_overlap_m1_single_slot_is_serial);
    tcase_add_test(tc, test_overlap_m1_closed_ack_window_is_serial);
    tcase_add_test(tc, test_overlap_m2_global_window_pins_each_monitor_to_one);
    tcase_add_test(tc, test_overlap_model_never_exceeds_its_own_bounds);
    tcase_add_test(tc, test_overlap_model_zero_capacity_yields_no_captures);
    tcase_add_test(tc, test_overlap_model_instant_ack_cannot_overlap);
    tcase_add_test(tc, test_overlap_model_widening_window_never_reduces_depth);
    tcase_add_test(tc, test_cap_budget_two_gates_agree);
    /* PRD FR-ACK-1 / BACKLOG #64 */
    tcase_add_test(tc, test_overlap_lossy_encode_wedges_without_consume_ack);
    tcase_add_test(tc, test_overlap_consume_ack_restores_depth);
    tcase_add_test(tc, test_ack_value_is_echoed_identity);
    tcase_add_test(tc, test_dropped_frame_region_returns_to_dirty);
    tcase_add_test(tc, test_xup_ack_displayed_bit_serialization);
    tcase_add_test(tc, test_ack_frontier_slot_only_holds_the_region_frontier);
    tcase_add_test(tc, test_ack_frontier_is_cumulative_and_monotonic);
    tcase_add_test(tc,
                   test_eager_ack_buys_nothing_when_the_budget_already_admits_two);
    tcase_add_test(tc, test_eager_ack_restores_the_floor_at_effective_depth_one);
    tcase_add_test(tc,
                   test_eager_ack_never_drops_a_region_a_failing_tail_owes_back);
    tcase_add_test(tc, test_absorb_only_ack_bufferbloats_and_the_metric_shows_it);
    tcase_add_test(tc,
                   test_capture_contract_rejects_a_previous_resize_layout);
    suite_add_tcase(s, tc);
    return s;
}
