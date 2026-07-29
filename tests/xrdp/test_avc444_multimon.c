#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <limits.h>

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
    suite_add_tcase(s, tc);
    return s;
}
