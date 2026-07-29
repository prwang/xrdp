/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2025
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * @file    common/xup_client_info.h
 * @brief   Data shared with xorgxrdp
 */

#if !defined(XUP_CLIENT_INFO_H)
#define XUP_CLIENT_INFO_H

#include "xrdp_client_info.h"

/**
 * Information about the xrdp client which is passed to xorgxrdp
 *
 * This is a subset of 'struct xrdp_client_info'
 *
 * @note If you change this structure, you MUST bump the
 *       XUP_CLIENT_INFO_CURRENT_VERSION number so that the mismatch
 *       can be detected.
 */
struct xup_client_info
{
    int size; /* bytes for this structure */
    int version; /* Should be XUP_CLIENT_INFO_CURRENT_VERSION */
    int bpp;
    int jpeg; /* non standard bitmap cache v2 cap */
    int offscreen_support_level;
    int offscreen_cache_size;
    int offscreen_cache_entries;

    char orders[XR_PRIMARY_ORDER_COUNT];
    int order_flags_ex;
    int pointer_flags; /* 0 color, 1 new, 2 no new */
    int large_pointer_support_flags;

    struct display_size_description display_sizes;

    enum xrdp_capture_code capture_code;
    int capture_format;

    /* X11 keyboard layout - inferred from keyboard type/subtype */
    char model[CI_KBD_MODEL_SIZE];
    char layout[CI_KBD_LAYOUT_SIZE];
    char variant[CI_KBD_VARIANT_SIZE];
    char options[CI_KBD_OPTIONS_SIZE];
    char xkb_rules[CI_KBD_XKB_RULES_SIZE];
    // A few x11 keycodes are needed by the xup module
    int x11_keycode_caps_lock;
    int x11_keycode_num_lock;
    int x11_keycode_scroll_lock;

    /* xorgxrdp: frame capture interval (milliseconds) */
    int rfx_frame_interval;
    int h264_frame_interval;
    int normal_frame_interval;

    /* CC_GFX_AVC444: coded-WIDTH alignment of the packed views (16 for
     * FreeRDP-derived clients, 32 for mstsc); the capture packs at the
     * FINAL coded geometry so the shmem is splicable (PRD FR-CAPTURE-6) */
    int avc444_chroma_align;
};

/* yyyymmdd of last incompatible change to xup_client_info OR to the
 * xup wire protocol / shared-memory capture contract.
 * 20260725: GFX H.264 multimon shmem split — WIRETOSURFACE_1 (msg 62)
 * gained a trailing per-monitor capture shmem offset field, and the
 * capture shmem is laid out per xup_cap_h264_shmem_layout() below.
 * 20260726: CC_GFX_AVC444 capture carries the FINAL wire-format views
 * ([main NV12][aux NV12] at the coded geometry, page-aligned, variant
 * selected by capture_format: yuv444_v2_stream / yuv444_v1_stream /
 * nv12_709fr for main-only) instead of planar YUV444, so xrdp can
 * vmsplice the shmem straight to the encoder (PRD FR-CAPTURE-6 /
 * FR-PROC-6); xup_client_info gained avc444_chroma_align; regions are
 * page-aligned (XUP_CAP_REGION_ALIGN 64 -> XUP_CAP_PAGE_ALIGN 4096).
 * 20260727: CC_GFX_AVC444 per-monitor regions hold TWO slots (PRD
 * FR-CAPTURE-8 two-slot pipelined capture); the slot for a frame is
 * carried in the existing per-frame shmem_offset field, so capture of
 * frame N+1 can overlap the synchronous encode of frame N. Single-slot
 * modes unchanged.
 * The slot is chosen by the capture side alone and is NEVER derived
 * from rect_id: it advances per monitor on that monitor's own send
 * (xup_cap_budget below). The rect_id-parity rule this entry
 * originally described is dead — with m monitors rect_id advances by m
 * between one monitor's consecutive sends, so parity pinned each
 * monitor to a single slot (measured: the same slot on 1079 of 1079
 * full-pass sends). That is a capture-side rule only, shmem_offset was
 * always explicit on the wire, and both daemons compare this version
 * for EXACT equality, so the number below does NOT move for it. */
#define XUP_CLIENT_INFO_CURRENT_VERSION 20260727

/*
 * Shared-memory layout for the GFX H.264 capture family
 * (CC_GFX_A2 NV12, CC_GFX_AVC444 planar YUV444).
 *
 * In a multimon session every monitor historically wrote its capture
 * planes at offset 0 of the one shared shmem, each with its own
 * geometry. The H.264 encoders consume the full plane every frame and
 * rely on the bytes outside the current damage rects persisting from
 * that monitor's previous frames, so each monitor's frame corrupted the
 * other monitors' plane bytes (1-2px ghost lines at damage-rect
 * boundaries once the client blits the metablock fringe). Both daemons
 * therefore agree on one DISJOINT per-monitor layout: xorgxrdp sizes
 * the shmem and places each monitor's planes with this helper, and
 * additionally sends the frame's offset in the WIRETOSURFACE_1 message
 * (authoritative for xrdp). Modes whose reader never depends on shmem
 * persistence (RFX tile capture, legacy session-canvas CC_SUF_A2) keep
 * the whole-shmem layout and offset 0.
 */

/* each per-monitor region AND each view within it starts page aligned:
 * whole pages are what vmsplice moves by reference (PRD FR-PROC-6) */
#define XUP_CAP_PAGE_ALIGN 4096

static inline int
xup_cap_page_align(int v)
{
    return (v + (XUP_CAP_PAGE_ALIGN - 1)) & ~(XUP_CAP_PAGE_ALIGN - 1);
}

/* one NV12 view's bytes at the FINAL coded geometry: width aligned to
 * chroma_align (16 or 32; anything else is treated as 16, matching the
 * converter), height 16-aligned */
static inline int
xup_cap_avc444_nv12_bytes(int width, int height, int chroma_align)
{
    int cw;
    int ch;

    if (width < 1 || height < 1)
    {
        return 0;
    }
    if (chroma_align != 32)
    {
        chroma_align = 16;
    }
    cw = (width + chroma_align - 1) & ~(chroma_align - 1);
    ch = (height + 15) & ~15;
    return cw * ch + cw * (ch / 2);
}

/* offset of the auxiliary view within a monitor's CC_GFX_AVC444 region
 * (the main view is at 0; the aux view starts on the next page) */
static inline int
xup_cap_avc444_aux_offset(int width, int height, int chroma_align)
{
    return xup_cap_page_align(
               xup_cap_avc444_nv12_bytes(width, height, chroma_align));
}

/* capture bytes one monitor needs.
 * CC_GFX_AVC444 (PRD FR-CAPTURE-6): the packed wire views —
 *   [main NV12][aux NV12], each at the final coded geometry, aux view
 *   page-aligned; capture_format XRDP_nv12_709fr means main-only (the
 *   external AVC420 mode), yuv444_v1/v2_stream carry an aux view.
 * CC_GFX_A2: NV12 needs 1.5 B/px; 2 B/px is kept for slack, matching
 * the historical session-level formula (16-aligned dims). */
static inline int
xup_cap_h264_mon_bytes(enum xrdp_capture_code capture_code,
                       int capture_format, int chroma_align,
                       int width, int height)
{
    int awidth;
    int aheight;
    int nv12;

    if (width < 1 || height < 1)
    {
        return 0;
    }
    if (capture_code == CC_GFX_AVC444)
    {
        nv12 = xup_cap_avc444_nv12_bytes(width, height, chroma_align);
        if (capture_format == XRDP_nv12_709fr)
        {
            return nv12; /* main view only (external AVC420) */
        }
        return xup_cap_avc444_aux_offset(width, height, chroma_align)
               + nv12;
    }
    awidth = (width + 15) & ~15;
    aheight = (height + 15) & ~15;
    return awidth * aheight * 2;
}

/* CC_GFX_AVC444 per-monitor regions hold exactly this many capture
 * slots (PRD FR-CAPTURE-8): two, so capture of frame N+1 can overlap
 * the synchronous encode of frame N. FIXED at 2 in the versioned
 * contract — every extra slot adds one frame of raw inventory and one
 * frame of backpressure lag, so changing it is a contract change
 * requiring owner sign-off, never a tuning knob. */
#define XUP_CAP_AVC444_SLOT_COUNT 2

/* one CC_GFX_AVC444 slot: the packed [main NV12][aux NV12] views for
 * one monitor, padded to a page boundary so both slots and the views
 * inside them stay page-aligned (whole pages are what vmsplice moves
 * by reference) */
static inline int
xup_cap_avc444_slot_bytes(int capture_format, int chroma_align,
                          int width, int height)
{
    return xup_cap_page_align(
               xup_cap_h264_mon_bytes(CC_GFX_AVC444, capture_format,
                                      chroma_align, width, height));
}

/* capture bytes one monitor's whole region needs: the slot bytes times
 * the slot count for CC_GFX_AVC444 (FR-CAPTURE-8), one slot's bytes
 * for every single-slot mode */
static inline int
xup_cap_h264_mon_region_bytes(enum xrdp_capture_code capture_code,
                              int capture_format, int chroma_align,
                              int width, int height)
{
    if (capture_code == CC_GFX_AVC444)
    {
        return XUP_CAP_AVC444_SLOT_COUNT *
               xup_cap_avc444_slot_bytes(capture_format, chroma_align,
                                         width, height);
    }
    return xup_cap_h264_mon_bytes(capture_code, capture_format,
                                  chroma_align, width, height);
}

/* Fill offsets[] with each monitor's capture region offset and return
 * the total shmem bytes required. With no monitors (single screen) the
 * session dimensions get one region at offset 0. For CC_GFX_AVC444
 * each region is XUP_CAP_AVC444_SLOT_COUNT slots and slot_bytes[]
 * (optional, may be NULL) receives each monitor's slot stride: a
 * frame's slot lives at offsets[mon] + slot_index * slot_bytes[mon]. */
static inline int
xup_cap_h264_shmem_layout(const struct display_size_description *displays,
                          enum xrdp_capture_code capture_code,
                          int capture_format, int chroma_align,
                          int session_width, int session_height,
                          int offsets[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS],
                          int slot_bytes[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS])
{
    int index;
    int count;
    int total;
    int mwidth;
    int mheight;

    for (index = 0; index < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; ++index)
    {
        offsets[index] = 0;
        if (slot_bytes != NULL)
        {
            slot_bytes[index] = 0;
        }
    }
    count = 0;
    if (displays != NULL)
    {
        count = (int)displays->monitorCount;
        if (count > CLIENT_MONITOR_DATA_MAXIMUM_MONITORS)
        {
            count = CLIENT_MONITOR_DATA_MAXIMUM_MONITORS;
        }
    }
    if (count < 1)
    {
        if (slot_bytes != NULL && capture_code == CC_GFX_AVC444)
        {
            slot_bytes[0] = xup_cap_avc444_slot_bytes(capture_format,
                            chroma_align,
                            session_width,
                            session_height);
        }
        return xup_cap_h264_mon_region_bytes(capture_code, capture_format,
                                             chroma_align,
                                             session_width, session_height);
    }
    total = 0;
    for (index = 0; index < count; ++index)
    {
        offsets[index] = total;
        mwidth = displays->minfo[index].right
                 - displays->minfo[index].left + 1;
        mheight = displays->minfo[index].bottom
                  - displays->minfo[index].top + 1;
        if (slot_bytes != NULL && capture_code == CC_GFX_AVC444)
        {
            slot_bytes[index] = xup_cap_avc444_slot_bytes(capture_format,
                                chroma_align,
                                mwidth, mheight);
        }
        total += xup_cap_page_align(
                     xup_cap_h264_mon_region_bytes(capture_code,
                                                   capture_format,
                                                   chroma_align,
                                                   mwidth, mheight));
    }
    return total;
}

/*
 * Per-monitor outstanding-capture accounting for CC_GFX_AVC444
 * (PRD FR-CAPTURE-8, restated per monitor).
 *
 * The capture budget is m INDEPENDENT caps of at most
 * XUP_CAP_AVC444_SLOT_COUNT outstanding frames each, never a global
 * pool: a pool lets one damaged monitor take all of it, which is extra
 * raw inventory and slot aliasing rather than pipelining. The aggregate
 * is a consequence of the m caps and is never a quantity to gate on.
 *
 * Per monitor this carries a ring of the rect_ids it has sent and not
 * yet seen acked, plus the slot the monitor's NEXT capture writes.
 * rect_id_ack is CUMULATIVE (every rect_id at or below it is acked),
 * which is exactly why a ring of XUP_CAP_AVC444_SLOT_COUNT ascending
 * entries is sufficient: retiring is "drop every entry the ack covers",
 * never a per-id match.
 *
 * The slot advances in xup_cap_budget_record_send() only, i.e. exactly
 * once per send and strictly after that send is committed. The slot of
 * a frame in flight is read several times before the send (it goes on
 * the wire as the frame's shmem_offset, and it selects which slot's
 * missing region a capture refreshed), so an earlier advance would
 * point the encoder at the sibling of the slot just captured.
 *
 * All-zero is the valid initial state: nothing outstanding, slot 0.
 */
struct xup_cap_budget
{
    /* ascending rect_ids sent and not yet retired, per monitor */
    int ids[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS][XUP_CAP_AVC444_SLOT_COUNT];
    /* how many of ids[mon][] are live */
    int count[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
    /* slot the monitor's next capture writes */
    int slot[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS];
};

static inline void
xup_cap_budget_reset(struct xup_cap_budget *budget)
{
    int mon;
    int index;

    if (budget == NULL)
    {
        return;
    }
    for (mon = 0; mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; ++mon)
    {
        for (index = 0; index < XUP_CAP_AVC444_SLOT_COUNT; ++index)
        {
            budget->ids[mon][index] = 0;
        }
        budget->count[mon] = 0;
        budget->slot[mon] = 0;
    }
}

/* a monitor index outside the contract's range is not addressable */
static inline int
xup_cap_budget_mon_ok(const struct xup_cap_budget *budget, int mon)
{
    return budget != NULL && mon >= 0 &&
           mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS;
}

/* the depth the layout can actually hold; single-slot modes pass 1 */
static inline int
xup_cap_budget_clamp_cap(int cap)
{
    if (cap < 1)
    {
        return 1;
    }
    if (cap > XUP_CAP_AVC444_SLOT_COUNT)
    {
        return XUP_CAP_AVC444_SLOT_COUNT;
    }
    return cap;
}

/* drop every entry the cumulative ack covers and return the monitor's
 * surviving outstanding count */
static inline int
xup_cap_budget_retire(struct xup_cap_budget *budget, int mon,
                      int rect_id_ack)
{
    int index;
    int live;
    int kept;

    if (!xup_cap_budget_mon_ok(budget, mon))
    {
        return 0;
    }
    live = budget->count[mon];
    if (live < 0)
    {
        live = 0;
    }
    if (live > XUP_CAP_AVC444_SLOT_COUNT)
    {
        live = XUP_CAP_AVC444_SLOT_COUNT;
    }
    kept = 0;
    for (index = 0; index < live; ++index)
    {
        if (budget->ids[mon][index] > rect_id_ack)
        {
            budget->ids[mon][kept] = budget->ids[mon][index];
            ++kept;
        }
    }
    budget->count[mon] = kept;
    return kept;
}

/* can this monitor take one more outstanding frame? Retires first, so
 * a stale ring never denies capacity. An unaddressable monitor fails
 * closed. */
static inline int
xup_cap_budget_has_capacity(struct xup_cap_budget *budget, int mon,
                            int rect_id_ack, int cap)
{
    if (!xup_cap_budget_mon_ok(budget, mon))
    {
        return 0;
    }
    return xup_cap_budget_retire(budget, mon, rect_id_ack) <
           xup_cap_budget_clamp_cap(cap);
}

/* count a send of rect_id for mon and advance that monitor's slot.
 * Returns 0 normally, non-zero when the monitor was ALREADY at cap:
 * that send is a third outstanding capture for a two-slot layout, which
 * the caller must report loudly. The ring then keeps the newest ids —
 * the oldest is the first a cumulative ack retires anyway. */
static inline int
xup_cap_budget_record_send(struct xup_cap_budget *budget, int mon,
                           int rect_id, int rect_id_ack, int cap)
{
    int limit;
    int index;
    int over;

    if (!xup_cap_budget_mon_ok(budget, mon))
    {
        return 1;
    }
    limit = xup_cap_budget_clamp_cap(cap);
    over = xup_cap_budget_retire(budget, mon, rect_id_ack) >= limit;
    while (budget->count[mon] >= limit && budget->count[mon] > 0)
    {
        for (index = 1; index < budget->count[mon]; ++index)
        {
            budget->ids[mon][index - 1] = budget->ids[mon][index];
        }
        --budget->count[mon];
    }
    budget->ids[mon][budget->count[mon]] = rect_id;
    ++budget->count[mon];
    budget->slot[mon] = (budget->slot[mon] + 1) % limit;
    return over;
}

/* slot the monitor's next capture must write */
static inline int
xup_cap_budget_slot(const struct xup_cap_budget *budget, int mon)
{
    if (!xup_cap_budget_mon_ok(budget, mon))
    {
        return 0;
    }
    if (budget->slot[mon] < 0 ||
            budget->slot[mon] >= XUP_CAP_AVC444_SLOT_COUNT)
    {
        return 0;
    }
    return budget->slot[mon];
}

#endif // XUP_CLIENT_INFO_H
