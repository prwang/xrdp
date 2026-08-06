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
 * Flags of the xup paint-rect-ex ack (message 106), PRD FR-ACK-1.
 *
 * The ack value is the ECHOED rect_id of a specific received paint msg,
 * never a count the consumer maintains itself, and EVERY received paint
 * msg is acked exactly once when it reaches a terminal state:
 *
 *   displayed=1 (no flag) - encoded and last EGFX byte sent;
 *   displayed=0 (this flag) - consumed with no output frame, i.e. the
 *       AVC444 warmup PENDING return, an encoder error, or a dropped
 *       pair. The producer frees the slot AND returns that frame's
 *       captured region to its dirty region (Invariant III).
 *
 * Wire-compatible: the flags word already existed and old peers ignore
 * unknown bits, so an old xorgxrdp under a new xrdp keeps today's
 * behaviour and a new xorgxrdp under an old xrdp simply never sees the
 * bit set.
 */
#define XUP_ACK_FLAGS_NOT_DISPLAYED 0x00000001

/**
 * BACKLOG #70: this ack releases the capture SLOT only -- it says
 * nothing about the frame's disposition, and the region that frame took
 * out of the dirty region MUST stay held.
 *
 * The eager slot-release ack fires when the encoder children have
 * absorbed the frame's input (its borrowed capture pages are free) and
 * the previous frame has left for the transport. That is early enough
 * to admit the next capture while this frame's tail -- LTR rewrite,
 * EGFX assembly, egress -- is still running, which is the whole point;
 * but the tail can still fail, and a failure owes the producer its
 * pixels back. So the two things one ack used to do are split: this
 * flag frees the slot, and the frame's ordinary terminal ack (which
 * still arrives, with displayed=1 or 0) is what disposes of the region.
 *
 * NOT wire-compatible with an old producer, and that is why the
 * contract version below moves: an old xorgxrdp ignores the bit,
 * retires the region on the early ack, and silently loses those pixels
 * if the tail then fails -- a stale rectangle on screen with no event
 * anywhere. Mixed pairs are refused at connect instead.
 */
#define XUP_ACK_FLAGS_SLOT_ONLY 0x00000002

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
 * for EXACT equality, so the number below does NOT move for it.
 *
 * 20260731 (BACKLOG #70): XUP_ACK_FLAGS_SLOT_ONLY. A producer that does
 * not know the bit would retire a held region on an ack that only means
 * "the slot is free", so the pair must match exactly. */
#define XUP_CLIENT_INFO_CURRENT_VERSION 20260731

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

/*
 * PRD FR-ACK-1, the ack's flags word.
 *
 * Written by xrdp (xup send_paint_rect_ex_ack) and read by xorgxrdp.
 * Kept here, as two one-line functions, so BOTH ends encode and decode
 * the same bit rather than each open-coding it: displayed is the only
 * thing the flags word carries on this message today, and a reader that
 * disagreed with the writer would silently re-dirty (or silently lose)
 * every frame.
 *
 * xup_ack_flags_make(1) is 0 -- the exact value xrdp already sends -- so
 * the happy-path byte stream is unchanged, which is what makes the
 * upgrade wire-compatible with an old peer in either direction.
 */
static inline int
xup_ack_flags_make(int displayed)
{
    return displayed ? 0 : XUP_ACK_FLAGS_NOT_DISPLAYED;
}

static inline int
xup_ack_flags_displayed(int flags)
{
    return (flags & XUP_ACK_FLAGS_NOT_DISPLAYED) == 0;
}

/* BACKLOG #70: does this ack dispose of the frame's captured region, or
 * does it only free the capture slot? */
static inline int
xup_ack_flags_slot_only(int flags)
{
    return (flags & XUP_ACK_FLAGS_SLOT_ONLY) != 0;
}

/*
 * BACKLOG #70: the producer's two ack frontiers.
 *
 *   slot   how far the consumer has finished READING. A capture may
 *          overwrite the pages of any frame at or below it.
 *   shown  how far the consumer has DISPOSED of frames. A held region
 *          may be forgotten only at or below it.
 *
 * They are the same number until an eager slot-release ack runs ahead,
 * and `shown <= slot` always: nothing disposes of a frame the consumer
 * has not finished with. The rule lives here, in the one header both
 * daemons and the unit tests compile, so there is a single copy of the
 * decision that keeps captured pixels from being dropped on the floor.
 */
struct xup_ack_frontier
{
    int slot;
    int shown;
};

static inline void
xup_ack_frontier_reset(struct xup_ack_frontier *f)
{
    if (f != NULL)
    {
        f->slot = 0;
        f->shown = 0;
    }
}

/* apply one ack. rect_id_ack must already be resolved (a producer turns
 * INT_MAX into its own rect_id before calling). Cumulative and
 * monotonic: a reordered or duplicated ack can never walk a frontier
 * backwards and resurrect a retired slot or a forgotten region. */
static inline void
xup_ack_frontier_apply(struct xup_ack_frontier *f, int flags,
                       int rect_id_ack)
{
    if (f == NULL)
    {
        return;
    }
    if (rect_id_ack > f->slot)
    {
        f->slot = rect_id_ack;
    }
    if (!xup_ack_flags_slot_only(flags) && rect_id_ack > f->shown)
    {
        f->shown = rect_id_ack;
    }
}

/*
 * PRD FR-ACK-1 Invariant III: which outstanding frame owns which capture
 * slot.
 *
 * A capture takes a region OUT of the producer's dirty region, and until
 * that frame reaches a terminal state no wire carries those pixels. If
 * the terminal state is displayed=0 the region must come back, so the
 * producer has to answer "which region did rect_id r take?" from the id
 * alone -- the ack carries nothing else.
 *
 * The pixels themselves are the X server's (a RegionPtr per slot); this
 * header owns only the identity map, which is the part with an
 * invariant to test: one entry per capture slot is exactly enough,
 * because a monitor may have at most XUP_CAP_AVC444_SLOT_COUNT frames
 * outstanding and each occupies its own slot. Id 0 means empty
 * (rect_id is 1-based: xorgxrdp pre-increments before its first send).
 */
/*
 * BACKLOG #70: one MORE entry than there are capture slots.
 *
 * Before the eager slot-release ack the two counts were the same: a
 * frame held its capture slot until the ack that disposed of it, so a
 * monitor could never hold more regions than slots. The eager ack frees
 * the slot at absorb and leaves the region held until the frame's tail
 * finishes, so at the instant the tail of N is running, N holds a
 * region with no slot while N+1 and N+2 hold the two slots. Condition
 * (b) -- ack(N) waits for egress(N-1) -- bounds that lag at exactly one
 * frame, which is where the +1 comes from and why it is not +2.
 *
 * Sizing this at the slot count is not a smaller ring, it is LOST
 * PIXELS: a capture would overwrite the entry of a frame whose tail can
 * still fail, and the failure would then find no region to give back.
 */
#define XUP_CAP_SENT_SLOTS (XUP_CAP_AVC444_SLOT_COUNT + 1)

struct xup_cap_sent
{
    int ids[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS][XUP_CAP_SENT_SLOTS];
};

static inline void
xup_cap_sent_reset(struct xup_cap_sent *sent)
{
    int mon;
    int slot;

    if (sent == NULL)
    {
        return;
    }
    for (mon = 0; mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; ++mon)
    {
        for (slot = 0; slot < XUP_CAP_SENT_SLOTS; ++slot)
        {
            sent->ids[mon][slot] = 0;
        }
    }
}

static inline int
xup_cap_sent_ok(const struct xup_cap_sent *sent, int mon, int slot)
{
    return sent != NULL && mon >= 0 &&
           mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS &&
           slot >= 0 && slot < XUP_CAP_SENT_SLOTS;
}

/* Take a FREE entry for rect_id and return its index, or -1 when the
 * monitor already holds XUP_CAP_SENT_SLOTS undisposed frames. There is
 * no overwrite path on purpose: the caller must treat -1 as the loud
 * invariant violation it is (and hand the region straight back), never
 * as a reason to evict a region someone may still ask for. */
static inline int
xup_cap_sent_take(struct xup_cap_sent *sent, int mon, int rect_id)
{
    int slot;

    if (!xup_cap_sent_ok(sent, mon, 0) || rect_id == 0)
    {
        return -1;
    }
    for (slot = 0; slot < XUP_CAP_SENT_SLOTS; ++slot)
    {
        if (sent->ids[mon][slot] == 0)
        {
            sent->ids[mon][slot] = rect_id;
            return slot;
        }
    }
    return -1;
}

/* which slot holds rect_id? Returns 1 and fills the amon/aslot outputs
 * when found, 0 otherwise -- a late or duplicated ack for an
 * already-retired frame must find nothing, never an unrelated slot. */
static inline int
xup_cap_sent_find(const struct xup_cap_sent *sent, int rect_id,
                  int *amon, int *aslot)
{
    int mon;
    int slot;

    if (sent == NULL || rect_id == 0)
    {
        return 0;
    }
    for (mon = 0; mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; ++mon)
    {
        for (slot = 0; slot < XUP_CAP_SENT_SLOTS; ++slot)
        {
            if (sent->ids[mon][slot] == rect_id)
            {
                if (amon != NULL)
                {
                    *amon = mon;
                }
                if (aslot != NULL)
                {
                    *aslot = slot;
                }
                return 1;
            }
        }
    }
    return 0;
}

static inline void
xup_cap_sent_clear(struct xup_cap_sent *sent, int mon, int slot)
{
    if (!xup_cap_sent_ok(sent, mon, slot))
    {
        return;
    }
    sent->ids[mon][slot] = 0;
}

/* the ack is cumulative, so every entry at or below it belongs to a
 * frame that reached the wire. Returns how many entries were dropped. */
static inline int
xup_cap_sent_retire(struct xup_cap_sent *sent, int rect_id_ack)
{
    int mon;
    int slot;
    int dropped;

    if (sent == NULL)
    {
        return 0;
    }
    dropped = 0;
    for (mon = 0; mon < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; ++mon)
    {
        for (slot = 0; slot < XUP_CAP_SENT_SLOTS; ++slot)
        {
            if (sent->ids[mon][slot] != 0 &&
                    sent->ids[mon][slot] <= rect_id_ack)
            {
                sent->ids[mon][slot] = 0;
                ++dropped;
            }
        }
    }
    return dropped;
}

#endif // XUP_CLIENT_INFO_H
