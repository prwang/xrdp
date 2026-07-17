/**
 * xrdp: A Remote Desktop Protocol server.
 *
 * Copyright (C) Jay Sorg 2004-2026
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
 *
 * MS-RDPEGFX AVC capability classification.
 *
 * Pure input->output logic mapping (capability version + flags) to the
 * AVC wire mode this backend selects for that single advertised capset.
 * The external ffmpeg backend supplies AVC444 v1 only; AVC420 and
 * AVC444v2 classification are provided for codec-order interaction and
 * future backends.
 */

#ifndef _XRDP_AVC444_CAPS_H
#define _XRDP_AVC444_CAPS_H

enum xrdp_gfx_avc_mode
{
    XRDP_GFX_AVC_NONE = 0,
    XRDP_GFX_AVC420,
    XRDP_GFX_AVC444,     /* AVC444 v1, codec id 0x000E */
    XRDP_GFX_AVC444V2    /* AVC444 v2, codec id 0x000F (deferred) */
};

/**
 * Classify one advertised RDPGFX capability set.
 *
 * version - one of XR_RDPGFX_CAPVERSION_* (from xrdp_egfx.h)
 * flags   - the capset flags word (XR_RDPGFX_CAPS_FLAG_*)
 *
 * Returns the AVC mode candidate this capset supports per the MS-RDPEGFX AVC capability rules:
 *   v8            -> NONE
 *   v8.1 + AVC420_ENABLED -> AVC420 ; v8.1 without it -> NONE
 *   v10.0 with AVC_DISABLED clear -> AVC444 (v1)
 *   v10.1 (reserved-only capset)  -> NONE (not eligible for v1)
 *   v10.2..10.7 with AVC_DISABLED clear -> AVC444 (v1)
 *   any v10.x with AVC_DISABLED set -> NONE
 * AVC_THINCLIENT is a preference, never a prerequisite.
 */
enum xrdp_gfx_avc_mode
xrdp_avc444_classify_caps(int version, int flags);

/**
 * Whether a single advertised capset indicates AVC444 v2 (ChromaV2) support.
 *
 * v2 is advertised by the reserved v10.1 capset (RDPGFX_CAPVERSION_101) and is
 * retained by the later v10.2..10.7 capsets. Returns 1 if this capset signals
 * v2 support (and AVC is not disabled on it), 0 otherwise. The caller ORs this
 * across every advertised capset to decide whether the client can decode
 * codec id 0x000F; a client without any v2 capset falls back to v1 (0x000E).
 */
int
xrdp_avc444_caps_supports_v2(int version, int flags);

#endif /* _XRDP_AVC444_CAPS_H */
