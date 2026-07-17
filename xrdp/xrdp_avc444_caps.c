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
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include "xrdp_avc444_caps.h"
#include "xrdp_egfx.h"

/*****************************************************************************/
enum xrdp_gfx_avc_mode
xrdp_avc444_classify_caps(int version, int flags)
{
    switch (version)
    {
        case XR_RDPGFX_CAPVERSION_8:
            return XRDP_GFX_AVC_NONE;

        case XR_RDPGFX_CAPVERSION_81:
            if (flags & XR_RDPGFX_CAPS_FLAG_AVC420_ENABLED)
            {
                return XRDP_GFX_AVC420;
            }
            return XRDP_GFX_AVC_NONE;

        case XR_RDPGFX_CAPVERSION_101:
            /* reserved-only capset; carries no AVC flag field and is treated
             * as AVC444v2 territory. Not eligible for v1-only output. */
            return XRDP_GFX_AVC_NONE;

        case XR_RDPGFX_CAPVERSION_10:
        case XR_RDPGFX_CAPVERSION_102:
        case XR_RDPGFX_CAPVERSION_103:
        case XR_RDPGFX_CAPVERSION_104:
        case XR_RDPGFX_CAPVERSION_105:
        case XR_RDPGFX_CAPVERSION_106:
        case XR_RDPGFX_CAPVERSION_107:
            if (flags & XR_RDPGFX_CAPS_FLAG_AVC_DISABLED)
            {
                return XRDP_GFX_AVC_NONE;
            }
            return XRDP_GFX_AVC444;

        default:
            return XRDP_GFX_AVC_NONE;
    }
}

/*****************************************************************************/
int
xrdp_avc444_caps_supports_v2(int version, int flags)
{
    switch (version)
    {
        case XR_RDPGFX_CAPVERSION_101:
            /* the reserved-only v10.1 capset exists specifically to advertise
             * AVC444 v2 (ChromaV2); its mere presence signals v2 support */
            return 1;

        case XR_RDPGFX_CAPVERSION_102:
        case XR_RDPGFX_CAPVERSION_103:
        case XR_RDPGFX_CAPVERSION_104:
        case XR_RDPGFX_CAPVERSION_105:
        case XR_RDPGFX_CAPVERSION_106:
        case XR_RDPGFX_CAPVERSION_107:
            /* v10.2+ supersede v10.1 and retain v2 support unless the client
             * disables AVC entirely on that capset */
            return (flags & XR_RDPGFX_CAPS_FLAG_AVC_DISABLED) ? 0 : 1;

        default:
            return 0;
    }
}
