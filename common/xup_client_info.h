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
 * page-aligned (XUP_CAP_REGION_ALIGN 64 -> XUP_CAP_PAGE_ALIGN 4096). */
#define XUP_CLIENT_INFO_CURRENT_VERSION 20260726

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

/* Fill offsets[] with each monitor's capture region offset and return
 * the total shmem bytes required. With no monitors (single screen) the
 * session dimensions get one region at offset 0. */
static inline int
xup_cap_h264_shmem_layout(const struct display_size_description *displays,
                          enum xrdp_capture_code capture_code,
                          int capture_format, int chroma_align,
                          int session_width, int session_height,
                          int offsets[CLIENT_MONITOR_DATA_MAXIMUM_MONITORS])
{
    int index;
    int count;
    int total;
    int mwidth;
    int mheight;

    for (index = 0; index < CLIENT_MONITOR_DATA_MAXIMUM_MONITORS; ++index)
    {
        offsets[index] = 0;
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
        return xup_cap_h264_mon_bytes(capture_code, capture_format,
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
        total += xup_cap_page_align(
                     xup_cap_h264_mon_bytes(capture_code, capture_format,
                                            chroma_align,
                                            mwidth, mheight));
    }
    return total;
}

#endif // XUP_CLIENT_INFO_H
