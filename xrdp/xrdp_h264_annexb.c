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
 * H.264 Annex-B byte-stream validation for the AVC444 external backend.
 */

#if defined(HAVE_CONFIG_H)
#include "config_ac.h"
#endif

#include <string.h>

#include "xrdp_h264_annexb.h"

#define XRDP_H264_MAX_NALS 65536

/*****************************************************************************/
/* find the next Annex-B start code (00 00 01) at or after *pos.            */
/* returns 1 and sets *sc to the offset of the 01 byte + 1 (first NAL byte),*/
/* and *sclen to the start-code length (3 or 4). returns 0 if none found.   */
static int
find_start_code(const unsigned char *data, int len, int pos,
                int *nal_start, int *sc_prefix)
{
    int i;

    for (i = pos; i + 3 <= len; i++)
    {
        if (data[i] == 0 && data[i + 1] == 0 && data[i + 2] == 1)
        {
            /* a preceding zero makes it a 4-byte start code, but either way
             * the NAL header follows the 01 byte */
            *sc_prefix = (i > pos && data[i - 1] == 0) ? 4 : 3;
            *nal_start = i + 3;
            return 1;
        }
    }
    return 0;
}

/*****************************************************************************/
int
xrdp_h264_scan_annexb(const unsigned char *data, int len,
                      struct xrdp_h264_nal_summary *out)
{
    int pos;
    int nal_start;
    int sc_prefix;
    int type;
    unsigned char header;

    memset(out, 0, sizeof(*out));
    if (data == NULL || len < 4)
    {
        return 1;
    }
    /* locate the first start code */
    if (!find_start_code(data, len, 0, &nal_start, &sc_prefix))
    {
        return 1;
    }
    pos = nal_start;
    while (pos < len && out->nal_count < XRDP_H264_MAX_NALS)
    {
        int next_start;
        int next_prefix;

        header = data[pos];
        if ((header & 0x80) != 0)
        {
            out->forbidden_bit_set = 1;
        }
        type = header & 0x1f;
        out->nal_count++;
        switch (type)
        {
            case 7:
                out->has_sps = 1;
                out->sps_count++;
                break;
            case 8:
                out->has_pps = 1;
                out->pps_count++;
                break;
            case 5:
                out->has_idr = 1;
                out->has_vcl = 1;
                break;
            case 1:
                out->has_vcl = 1;
                break;
            default:
                break;
        }
        /* advance to the next start code (start searching after this NAL
         * header so a header byte of 0x01 is not mistaken for a start code) */
        if (!find_start_code(data, len, pos + 1, &next_start, &next_prefix))
        {
            break;
        }
        pos = next_start;
    }
    out->valid = (out->nal_count > 0 && !out->forbidden_bit_set);
    return out->valid ? 0 : 1;
}

/*****************************************************************************/
int
xrdp_h264_main_reset_ok(const unsigned char *data, int len)
{
    struct xrdp_h264_nal_summary s;

    if (xrdp_h264_scan_annexb(data, len, &s) != 0)
    {
        return 0;
    }
    return s.has_sps && s.has_pps && s.has_idr;
}

/*****************************************************************************/
int
xrdp_h264_aux_ok(const unsigned char *data, int len)
{
    struct xrdp_h264_nal_summary s;

    if (xrdp_h264_scan_annexb(data, len, &s) != 0)
    {
        return 0;
    }
    return s.has_vcl;
}
