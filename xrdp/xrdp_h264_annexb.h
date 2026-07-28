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

/**
 * macOS interop (BACKLOG 2026-07-27): remove HRD parameters from every
 * SPS in an Annex-B access unit, in place. Clears
 * nal_hrd_parameters_present_flag and vcl_hrd_parameters_present_flag,
 * drops their hrd_parameters() structures and low_delay_hrd_flag, and
 * preserves every other SPS field bit-exactly (the stream can only
 * shrink; *len is updated). SPS NALs without HRD pass through untouched.
 * Returns 0 on success, non-zero on any parse or bounds failure — the
 * caller must treat that as a validation failure and never ship a
 * half-rewritten stream.
 */
int
xrdp_h264_sanitize_hrd(unsigned char *data, int *len);

/*
 * Clear pic_struct_present_flag in the VUI of every SPS in an Annex-B
 * access unit, in place (macOS interop: the flag declares per-frame
 * picture-timing SEI which xrdp strips, and a declared-but-absent
 * timing feed changes VideoToolbox output pacing). Every other SPS
 * field is preserved bit-exactly; SPS NALs without VUI or with the
 * flag already 0 pass through untouched. Same failure contract as
 * xrdp_h264_sanitize_hrd().
 */
int
xrdp_h264_strip_pic_struct(unsigned char *data, int *len);


/* cached SPS/PPS fields needed to parse slice headers (strip_mmco,
 * aux_to_leaf) */
struct xrdp_h264_param_cache
{
    int have_sps;
    int log2_max_frame_num;
    int log2_max_poc_lsb;
    int poc_type;
    int frame_mbs_only;
    int scaling_present;    /* SPS seq_scaling_matrix_present_flag       */
    int have_pps;
    int entropy_cabac;
    int slice_groups;
    int weighted_pred;
    int deblock_present;
    int redundant_present;
    int pic_init_qp;        /* pic_init_qp_minus26 + 26                  */
    int chroma_qp_offset;
    int second_chroma_qp_offset;
    int transform_8x8;
    int pps_scaling_present;
};

/*
 * DIAGNOSTIC (Mac k=1 bisect): rewrite every non-IDR reference slice so
 * dec_ref_pic_marking uses sliding-window instead of an explicit MMCO
 * op list, re-padding the CABAC alignment (payload copied verbatim).
 * SPS/PPS NALs encountered in the stream update *cache. Fails loudly on
 * any slice shape our encoders do not emit.
 */
int
xrdp_h264_strip_mmco(unsigned char *data, int *len,
                     struct xrdp_h264_param_cache *cache);

/*
 * AVC444 reference partitioning (BACKLOG 2026-07-27): rewrite an
 * all-IDR auxiliary packet into non-reference, non-IDR I "leaf"
 * frames so the aux view never enters the shared DPB and the main
 * chain self-references only main frames at any aux cadence.
 * Walks the MAIN packet first (read-only) to cache its SPS/PPS in
 * *main_cache and read the frame_num of its last reference VCL NAL;
 * then rewrites the AUX packet in place: SPS/PPS are cached into
 * *aux_cache and dropped, SEI/AUD are dropped, and every IDR slice
 * becomes a type-1 slice with nal_ref_idc 0, idr_pic_id and
 * dec_ref_pic_marking removed, and frame_num = main frame_num + 1
 * (the non-reference-picture rule). The CABAC payload is copied
 * byte-verbatim after re-padding the alignment. Fails loudly (and
 * leaves the caller to drop the packet) on any stream shape outside
 * what our encoders emit, or if any parse-relevant SPS/PPS field
 * differs between the two encoder children.
 */
int
xrdp_h264_aux_to_leaf(unsigned char *aux, int *aux_len,
                      const unsigned char *main_data, int main_len,
                      struct xrdp_h264_param_cache *main_cache,
                      struct xrdp_h264_param_cache *aux_cache);

#endif /* _XRDP_H264_ANNEXB_H */

