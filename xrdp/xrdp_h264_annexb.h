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
 *
 * This is deliberately NOT a full H.264 parser (PRD FR-H264-5): it splits
 * bounded Annex-B start codes and inspects only the one-byte NAL header's
 * nal_unit_type. It never parses slice syntax or decodes pixels. All
 * scanning is bounded; the input is treated as untrusted (PRD FR-NUT-7).
 */

#ifndef _XRDP_H264_ANNEXB_H
#define _XRDP_H264_ANNEXB_H

struct xrdp_h264_nal_summary
{
    int valid;      /* well-formed Annex-B with at least one start code    */
    int nal_count;
    int has_sps;    /* nal_unit_type 7                                     */
    int has_pps;    /* nal_unit_type 8                                     */
    int has_idr;    /* nal_unit_type 5 (IDR VCL)                           */
    int has_vcl;    /* nal_unit_type 1 or 5                                */
    int forbidden_bit_set; /* any NAL with forbidden_zero_bit == 1         */
    int sps_count;  /* reset packets must carry EXACTLY ONE SPS: strict    */
    int pps_count;  /* decoders (macOS VideoToolbox) black out on          */
    /*                 duplicated parameter sets (PRD FR-PROBE-6)          */
};

/**
 * Scan an Annex-B access unit. Returns 0 and fills *out on a structurally
 * valid byte stream; returns non-zero (out->valid == 0) on malformed input
 * (no start code, truncation). Bounded: at most 65536 NAL units inspected.
 */
int
xrdp_h264_scan_annexb(const unsigned char *data, int len,
                      struct xrdp_h264_nal_summary *out);

/**
 * First-packet reset requirement (PRD FR-H264-5): at least one SPS, one
 * PPS and one IDR, valid framing, no forbidden bit. Returns 1 if satisfied.
 */
int
xrdp_h264_main_reset_ok(const unsigned char *data, int len);

/**
 * Auxiliary packet requirement (PRD FR-H264-5): at least one VCL NAL unit
 * (type 1 or 5), valid framing. Returns 1 if satisfied.
 */
int
xrdp_h264_aux_ok(const unsigned char *data, int len);

#endif /* _XRDP_H264_ANNEXB_H */
