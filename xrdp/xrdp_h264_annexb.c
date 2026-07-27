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

#include <stdlib.h>
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

/*
 * SPS HRD removal (macOS interop, BACKLOG 2026-07-27).
 *
 * The bisect matrix convicted nal_hrd_parameters() in the SPS VUI: the
 * macOS Windows App's in-RDP VideoToolbox path blacks out on any stream
 * whose SPS carries HRD, with every other field (including timing_info)
 * bit-identical to a rendering stream. This rewrite is deliberately a
 * MINIMAL bit-exact splice, not a re-serialization: everything before
 * the nal_hrd flag and everything from pic_struct_present_flag onwards
 * is copied verbatim, so no other field can change as a side effect.
 * Input is untrusted (client-independent but encoder-supplied): every
 * read is bounds-checked and any anomaly fails the whole call.
 */

#define SPS_RBSP_MAX 1024

struct sps_bits
{
    const unsigned char *buf;
    int nbits;
    int pos;
    int err;
};

/*****************************************************************************/
static unsigned int
bits_u(struct sps_bits *b, int n)
{
    unsigned int v;

    v = 0;
    while (n > 0)
    {
        if (b->err || b->pos >= b->nbits)
        {
            b->err = 1;
            return 0;
        }
        v = (v << 1) | ((b->buf[b->pos >> 3] >> (7 - (b->pos & 7))) & 1);
        b->pos++;
        n--;
    }
    return v;
}

/*****************************************************************************/
static unsigned int
bits_ue(struct sps_bits *b)
{
    int zeros;

    zeros = 0;
    while (bits_u(b, 1) == 0 && !b->err)
    {
        zeros++;
        if (zeros > 31)
        {
            b->err = 1;
            return 0;
        }
    }
    if (b->err)
    {
        return 0;
    }
    return (1u << zeros) - 1 + bits_u(b, zeros);
}

/*****************************************************************************/
static int
bits_se(struct sps_bits *b)
{
    unsigned int k;

    k = bits_ue(b);
    return (k & 1) ? (int)((k + 1) / 2) : -(int)(k / 2);
}

/*****************************************************************************/
static void
skip_scaling_list(struct sps_bits *b, int size)
{
    int j;
    int last_scale;
    int next_scale;

    last_scale = 8;
    next_scale = 8;
    for (j = 0; j < size && !b->err; j++)
    {
        if (next_scale != 0)
        {
            next_scale = (last_scale + bits_se(b) + 256) % 256;
        }
        last_scale = (next_scale == 0) ? last_scale : next_scale;
    }
}

/*****************************************************************************/
static void
skip_hrd_parameters(struct sps_bits *b)
{
    unsigned int cpb_cnt;
    unsigned int i;

    cpb_cnt = bits_ue(b); /* cpb_cnt_minus1 */
    if (cpb_cnt > 31)
    {
        b->err = 1;
        return;
    }
    bits_u(b, 8); /* bit_rate_scale + cpb_size_scale */
    for (i = 0; i <= cpb_cnt && !b->err; i++)
    {
        bits_ue(b); /* bit_rate_value_minus1 */
        bits_ue(b); /* cpb_size_value_minus1 */
        bits_u(b, 1); /* cbr_flag */
    }
    bits_u(b, 20); /* 4 x u(5) removal/output delay + time_offset lengths */
}

/*****************************************************************************/
/* parse an unescaped SPS RBSP far enough to locate the HRD region.        */
/* returns 0 and sets [*hrd_start, *hrd_end) = bit range spanning          */
/* nal_hrd flag .. low_delay_hrd_flag inclusive, with *have_hrd = 1, when  */
/* either hrd flag is set; *have_hrd = 0 when the SPS carries no HRD.      */
/* when the SPS has no VUI at all, *hrd_start / *hrd_end stay -1.          */
/* pic_struct_present_flag is always the bit AT *hrd_end (first VUI bit    */
/* after the HRD region, present or not).                                  */
/* returns non-zero on any parse failure.                                  */
static int
sps_locate_hrd(const unsigned char *rbsp, int nbits,
               int *hrd_start, int *hrd_end, int *have_hrd)
{
    struct sps_bits b;
    unsigned int profile_idc;
    unsigned int chroma_format_idc;
    unsigned int poc_type;
    unsigned int n;
    unsigned int i;
    int nal_hrd;
    int vcl_hrd;

    b.buf = rbsp;
    b.nbits = nbits;
    b.pos = 0;
    b.err = 0;
    *have_hrd = 0;
    *hrd_start = -1;
    *hrd_end = -1;

    profile_idc = bits_u(&b, 8);
    bits_u(&b, 16);          /* constraint flags + level_idc */
    bits_ue(&b);             /* seq_parameter_set_id */
    chroma_format_idc = 1;
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
            profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
            profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ||
            profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
            profile_idc == 135)
    {
        chroma_format_idc = bits_ue(&b);
        if (chroma_format_idc == 3)
        {
            bits_u(&b, 1);   /* separate_colour_plane_flag */
        }
        bits_ue(&b);         /* bit_depth_luma_minus8 */
        bits_ue(&b);         /* bit_depth_chroma_minus8 */
        bits_u(&b, 1);       /* qpprime_y_zero_transform_bypass_flag */
        if (bits_u(&b, 1))   /* seq_scaling_matrix_present_flag */
        {
            n = (chroma_format_idc != 3) ? 8 : 12;
            for (i = 0; i < n && !b.err; i++)
            {
                if (bits_u(&b, 1))
                {
                    skip_scaling_list(&b, (i < 6) ? 16 : 64);
                }
            }
        }
    }
    bits_ue(&b);             /* log2_max_frame_num_minus4 */
    poc_type = bits_ue(&b);
    if (poc_type == 0)
    {
        bits_ue(&b);         /* log2_max_pic_order_cnt_lsb_minus4 */
    }
    else if (poc_type == 1)
    {
        bits_u(&b, 1);       /* delta_pic_order_always_zero_flag */
        bits_se(&b);         /* offset_for_non_ref_pic */
        bits_se(&b);         /* offset_for_top_to_bottom_field */
        n = bits_ue(&b);     /* num_ref_frames_in_pic_order_cnt_cycle */
        if (n > 255)
        {
            return 1;
        }
        for (i = 0; i < n && !b.err; i++)
        {
            bits_se(&b);     /* offset_for_ref_frame */
        }
    }
    bits_ue(&b);             /* max_num_ref_frames */
    bits_u(&b, 1);           /* gaps_in_frame_num_value_allowed_flag */
    bits_ue(&b);             /* pic_width_in_mbs_minus1 */
    bits_ue(&b);             /* pic_height_in_map_units_minus1 */
    if (bits_u(&b, 1) == 0)  /* frame_mbs_only_flag */
    {
        bits_u(&b, 1);       /* mb_adaptive_frame_field_flag */
    }
    bits_u(&b, 1);           /* direct_8x8_inference_flag */
    if (bits_u(&b, 1))       /* frame_cropping_flag */
    {
        bits_ue(&b);
        bits_ue(&b);
        bits_ue(&b);
        bits_ue(&b);
    }
    if (bits_u(&b, 1) == 0)  /* vui_parameters_present_flag */
    {
        return b.err;        /* no VUI -> no HRD */
    }
    if (bits_u(&b, 1))       /* aspect_ratio_info_present_flag */
    {
        if (bits_u(&b, 8) == 255) /* aspect_ratio_idc == Extended_SAR */
        {
            bits_u(&b, 32);  /* sar_width + sar_height */
        }
    }
    if (bits_u(&b, 1))       /* overscan_info_present_flag */
    {
        bits_u(&b, 1);
    }
    if (bits_u(&b, 1))       /* video_signal_type_present_flag */
    {
        bits_u(&b, 4);       /* video_format + video_full_range_flag */
        if (bits_u(&b, 1))   /* colour_description_present_flag */
        {
            bits_u(&b, 24);
        }
    }
    if (bits_u(&b, 1))       /* chroma_loc_info_present_flag */
    {
        bits_ue(&b);
        bits_ue(&b);
    }
    if (bits_u(&b, 1))       /* timing_info_present_flag */
    {
        bits_u(&b, 32);      /* num_units_in_tick */
        bits_u(&b, 32);      /* time_scale */
        bits_u(&b, 1);       /* fixed_frame_rate_flag */
    }
    if (b.err)
    {
        return 1;
    }
    *hrd_start = b.pos;
    nal_hrd = bits_u(&b, 1);
    if (nal_hrd)
    {
        skip_hrd_parameters(&b);
    }
    vcl_hrd = bits_u(&b, 1);
    if (vcl_hrd)
    {
        skip_hrd_parameters(&b);
    }
    if (nal_hrd || vcl_hrd)
    {
        bits_u(&b, 1);       /* low_delay_hrd_flag */
    }
    if (b.err)
    {
        return 1;
    }
    *hrd_end = b.pos;
    *have_hrd = (nal_hrd || vcl_hrd);
    return 0;
}

/*****************************************************************************/
static void
put_bit(unsigned char *out, int *pos, int cap_bits, int bit, int *err)
{
    if (*pos >= cap_bits)
    {
        *err = 1;
        return;
    }
    if (bit)
    {
        out[*pos >> 3] |= 0x80 >> (*pos & 7);
    }
    (*pos)++;
}

/*****************************************************************************/
/* rewrite one SPS NAL (header byte + escaped payload): drop the VUI HRD   */
/* (strip_hrd) and/or clear pic_struct_present_flag (strip_ps).            */
/* returns the new NAL length, 0 if nothing needed changing (out           */
/* untouched), or -1 on failure. out must hold at least nal_len bytes.     */
static int
sps_rewrite_nal(const unsigned char *nal, int nal_len,
                unsigned char *out, int strip_hrd, int strip_ps)
{
    unsigned char rbsp[SPS_RBSP_MAX];
    unsigned char newr[SPS_RBSP_MAX];
    int rlen;
    int i;
    int zeros;
    int nbits;
    int hrd_start;
    int hrd_end;
    int have_hrd;
    int need_hrd;
    int need_ps;
    int last_one;
    int opos;
    int oerr;
    int olen;
    int bit;

    if (nal_len < 4 || nal_len > SPS_RBSP_MAX)
    {
        return -1;
    }
    /* unescape: drop the 0x03 of every 00 00 03 emulation prevention */
    rlen = 0;
    zeros = 0;
    for (i = 1; i < nal_len; i++)
    {
        if (zeros == 2 && nal[i] == 3)
        {
            zeros = 0;
            continue;
        }
        zeros = (nal[i] == 0) ? zeros + 1 : 0;
        rbsp[rlen++] = nal[i];
    }
    nbits = rlen * 8;
    if (sps_locate_hrd(rbsp, nbits, &hrd_start, &hrd_end, &have_hrd) != 0)
    {
        return -1;
    }
    if (hrd_end < 0 || hrd_end >= nbits)
    {
        return 0;            /* no VUI -> neither HRD nor pic_struct */
    }
    need_hrd = strip_hrd && have_hrd;
    need_ps = strip_ps &&
              ((rbsp[hrd_end >> 3] >> (7 - (hrd_end & 7))) & 1);
    if (!need_hrd && !need_ps)
    {
        return 0;
    }
    /* the rbsp_stop_one_bit is the last set bit; copy through it and let
     * byte padding re-create the alignment zeros */
    last_one = -1;
    for (i = nbits - 1; i >= 0; i--)
    {
        if ((rbsp[i >> 3] >> (7 - (i & 7))) & 1)
        {
            last_one = i;
            break;
        }
    }
    if (last_one <= hrd_end)
    {
        return -1;
    }
    memset(newr, 0, sizeof(newr));
    opos = 0;
    oerr = 0;
    for (i = 0; i < hrd_start && !oerr; i++)
    {
        bit = (rbsp[i >> 3] >> (7 - (i & 7))) & 1;
        put_bit(newr, &opos, SPS_RBSP_MAX * 8, bit, &oerr);
    }
    if (need_hrd)
    {
        put_bit(newr, &opos, SPS_RBSP_MAX * 8, 0, &oerr); /* nal_hrd = 0 */
        put_bit(newr, &opos, SPS_RBSP_MAX * 8, 0, &oerr); /* vcl_hrd = 0 */
    }
    else
    {
        for (i = hrd_start; i < hrd_end && !oerr; i++)
        {
            bit = (rbsp[i >> 3] >> (7 - (i & 7))) & 1;
            put_bit(newr, &opos, SPS_RBSP_MAX * 8, bit, &oerr);
        }
    }
    /* pic_struct_present_flag is the bit at hrd_end */
    bit = need_ps ? 0 : ((rbsp[hrd_end >> 3] >> (7 - (hrd_end & 7))) & 1);
    put_bit(newr, &opos, SPS_RBSP_MAX * 8, bit, &oerr);
    for (i = hrd_end + 1; i <= last_one && !oerr; i++)
    {
        bit = (rbsp[i >> 3] >> (7 - (i & 7))) & 1;
        put_bit(newr, &opos, SPS_RBSP_MAX * 8, bit, &oerr);
    }
    if (oerr)
    {
        return -1;
    }
    rlen = (opos + 7) / 8; /* zero padding is already in newr */
    /* re-escape and prepend the untouched NAL header byte */
    out[0] = nal[0];
    olen = 1;
    zeros = 0;
    for (i = 0; i < rlen; i++)
    {
        if (zeros == 2 && newr[i] <= 3)
        {
            if (olen >= nal_len)
            {
                return -1;
            }
            out[olen++] = 3;
            zeros = 0;
        }
        if (olen >= nal_len)
        {
            return -1;
        }
        zeros = (newr[i] == 0) ? zeros + 1 : 0;
        out[olen++] = newr[i];
    }
    return olen;
}

/*****************************************************************************/
/* walk every SPS NAL in the access unit and apply the requested VUI
 * rewrites (drop HRD / clear pic_struct_present_flag) */
static int
sanitize_walk(unsigned char *data, int *len, int strip_hrd, int strip_ps)
{
    unsigned char out[SPS_RBSP_MAX];
    int pos;
    int nal_start;
    int sc_prefix;
    int nal_count;

    if (data == NULL || len == NULL || *len < 4)
    {
        return 1;
    }
    if (!find_start_code(data, *len, 0, &nal_start, &sc_prefix))
    {
        return 1;
    }
    pos = nal_start;
    nal_count = 0;
    while (pos < *len && nal_count < XRDP_H264_MAX_NALS)
    {
        int next_start;
        int next_prefix;
        int nal_end;
        int nal_len;
        int new_len;

        nal_count++;
        if (find_start_code(data, *len, pos + 1, &next_start, &next_prefix))
        {
            nal_end = next_start - next_prefix;
        }
        else
        {
            nal_end = *len;
            next_start = -1;
        }
        nal_len = nal_end - pos;
        if ((data[pos] & 0x1f) == 7)
        {
            new_len = sps_rewrite_nal(data + pos, nal_len, out,
                                      strip_hrd, strip_ps);
            if (new_len < 0)
            {
                return 1;
            }
            if (new_len > 0 && new_len != nal_len)
            {
                memcpy(data + pos, out, new_len);
                memmove(data + pos + new_len, data + nal_end,
                        *len - nal_end);
                *len -= nal_len - new_len;
                if (next_start > 0)
                {
                    next_start -= nal_len - new_len;
                }
            }
            else if (new_len > 0)
            {
                memcpy(data + pos, out, new_len);
            }
        }
        if (next_start < 0)
        {
            break;
        }
        pos = next_start;
    }
    return 0;
}

/*****************************************************************************/
int
xrdp_h264_sanitize_hrd(unsigned char *data, int *len)
{
    return sanitize_walk(data, len, 1, 0);
}

/*****************************************************************************/
int
xrdp_h264_strip_pic_struct(unsigned char *data, int *len)
{
    return sanitize_walk(data, len, 0, 1);
}


/*
 * MMCO stripping (DIAGNOSTIC, 2026-07-27 Mac k=1 bisect).
 *
 * The Mac-clean VAAPI wire marks references with explicit MMCO ops in
 * every P slice; the Mac-broken nvenc wire uses sliding-window marking.
 * With max_num_ref_frames = 1 the two are semantically identical, so
 * converting the clean stream to sliding window is a single-delta arm:
 * if the Mac starts mis-pairing on it, reference marking is convicted.
 * The rewrite replaces dec_ref_pic_marking's adaptive op list with
 * adaptive_ref_pic_marking_mode_flag = 0 and re-pads the CABAC
 * alignment so the entropy-coded payload is copied byte-verbatim.
 * Fail-loud contract: any slice shape outside what our own encoders
 * emit (list modification, weighted prediction, poc_type 1, slice
 * groups, fields) fails the whole call.
 */

/*****************************************************************************/
static void
cache_sps(struct xrdp_h264_param_cache *c, const unsigned char *nal,
          int nal_len)
{
    unsigned char rbsp[SPS_RBSP_MAX];
    struct sps_bits b;
    unsigned int profile_idc;
    unsigned int cfi;
    unsigned int n;
    unsigned int i;
    int rlen;
    int zeros;
    int j;

    rlen = 0;
    zeros = 0;
    for (j = 1; j < nal_len && rlen < SPS_RBSP_MAX; j++)
    {
        if (zeros == 2 && nal[j] == 3)
        {
            zeros = 0;
            continue;
        }
        zeros = (nal[j] == 0) ? zeros + 1 : 0;
        rbsp[rlen++] = nal[j];
    }
    b.buf = rbsp;
    b.nbits = rlen * 8;
    b.pos = 0;
    b.err = 0;
    profile_idc = bits_u(&b, 8);
    bits_u(&b, 16);
    bits_ue(&b);             /* sps id */
    if (profile_idc == 100 || profile_idc == 110 || profile_idc == 122 ||
            profile_idc == 244 || profile_idc == 44 || profile_idc == 83 ||
            profile_idc == 86 || profile_idc == 118 || profile_idc == 128)
    {
        cfi = bits_ue(&b);
        if (cfi == 3)
        {
            bits_u(&b, 1);
        }
        bits_ue(&b);
        bits_ue(&b);
        bits_u(&b, 1);
        if (bits_u(&b, 1))
        {
            n = (cfi != 3) ? 8 : 12;
            for (i = 0; i < n && !b.err; i++)
            {
                if (bits_u(&b, 1))
                {
                    skip_scaling_list(&b, (i < 6) ? 16 : 64);
                }
            }
        }
    }
    c->log2_max_frame_num = bits_ue(&b) + 4;
    c->poc_type = bits_ue(&b);
    if (c->poc_type == 0)
    {
        c->log2_max_poc_lsb = bits_ue(&b) + 4;
    }
    bits_ue(&b);             /* max_num_ref_frames */
    bits_u(&b, 1);           /* gaps allowed */
    bits_ue(&b);             /* width */
    bits_ue(&b);             /* height */
    c->frame_mbs_only = bits_u(&b, 1);
    c->have_sps = !b.err;
}

/*****************************************************************************/
static void
cache_pps(struct xrdp_h264_param_cache *c, const unsigned char *nal,
          int nal_len)
{
    unsigned char rbsp[SPS_RBSP_MAX];
    struct sps_bits b;
    int rlen;
    int zeros;
    int j;

    rlen = 0;
    zeros = 0;
    for (j = 1; j < nal_len && rlen < SPS_RBSP_MAX; j++)
    {
        if (zeros == 2 && nal[j] == 3)
        {
            zeros = 0;
            continue;
        }
        zeros = (nal[j] == 0) ? zeros + 1 : 0;
        rbsp[rlen++] = nal[j];
    }
    b.buf = rbsp;
    b.nbits = rlen * 8;
    b.pos = 0;
    b.err = 0;
    bits_ue(&b);                          /* pps id */
    bits_ue(&b);                          /* sps id */
    c->entropy_cabac = bits_u(&b, 1);
    bits_u(&b, 1);                        /* bottom_field_pic_order */
    c->slice_groups = bits_ue(&b);        /* num_slice_groups_minus1 */
    bits_ue(&b);                          /* num_ref_idx_l0_default */
    bits_ue(&b);                          /* num_ref_idx_l1_default */
    c->weighted_pred = bits_u(&b, 1);
    bits_u(&b, 2);                        /* weighted_bipred_idc */
    bits_se(&b);                          /* pic_init_qp */
    bits_se(&b);                          /* pic_init_qs */
    bits_se(&b);                          /* chroma_qp_offset */
    c->deblock_present = bits_u(&b, 1);
    bits_u(&b, 1);                        /* constrained_intra */
    c->redundant_present = bits_u(&b, 1);
    c->have_pps = !b.err;
}

/*****************************************************************************/
/* rewrite one non-IDR ref slice NAL: adaptive MMCO list -> sliding      */
/* window. returns new length, 0 = untouched, -1 = failure.              */
static int
slice_strip_mmco(const unsigned char *nal, int nal_len, unsigned char *out,
                 int out_cap, const struct xrdp_h264_param_cache *c)
{
    unsigned char *rbsp;
    unsigned char *newr;
    struct sps_bits b;
    unsigned int stype;
    unsigned int op;
    int rlen;
    int zeros;
    int i;
    int mark_start;
    int mark_end;
    int hdr_end;
    int pay_byte;
    int opos;
    int oerr;
    int olen;
    int bit;
    int nbytes;

    if (nal_len < 4)
    {
        return -1;
    }
    rbsp = (unsigned char *)malloc(nal_len);
    newr = (unsigned char *)malloc(nal_len + 8);
    if (rbsp == NULL || newr == NULL)
    {
        free(rbsp);
        free(newr);
        return -1;
    }
    rlen = 0;
    zeros = 0;
    for (i = 1; i < nal_len; i++)
    {
        if (zeros == 2 && nal[i] == 3)
        {
            zeros = 0;
            continue;
        }
        zeros = (nal[i] == 0) ? zeros + 1 : 0;
        rbsp[rlen++] = nal[i];
    }
    b.buf = rbsp;
    b.nbits = rlen * 8;
    b.pos = 0;
    b.err = 0;
    bits_ue(&b);                          /* first_mb_in_slice */
    stype = bits_ue(&b) % 5;
    bits_ue(&b);                          /* pps id */
    bits_u(&b, c->log2_max_frame_num);    /* frame_num */
    if (!c->frame_mbs_only || c->slice_groups != 0 || stype == 1)
    {
        goto unsupported;                 /* fields/slice groups/B */
    }
    if (c->poc_type == 0)
    {
        bits_u(&b, c->log2_max_poc_lsb);
    }
    else if (c->poc_type == 1)
    {
        goto unsupported;
    }
    if (c->redundant_present)
    {
        bits_ue(&b);
    }
    if (stype == 0)                       /* P */
    {
        if (bits_u(&b, 1))                /* num_ref_idx override */
        {
            bits_ue(&b);
        }
        if (bits_u(&b, 1))                /* ref_pic_list_modification */
        {
            goto unsupported;
        }
        if (c->weighted_pred)
        {
            goto unsupported;
        }
    }
    mark_start = b.pos;
    if (bits_u(&b, 1) == 0)               /* adaptive marking flag */
    {
        free(rbsp);
        free(newr);
        return 0;                         /* already sliding window */
    }
    do
    {
        op = bits_ue(&b);
        switch (op)
        {
            case 0:
                break;
            case 1:
            case 2:
            case 4:
            case 6:
                bits_ue(&b);
                break;
            case 3:
                bits_ue(&b);
                bits_ue(&b);
                break;
            case 5:
                break;
            default:
                goto unsupported;
        }
    }
    while (op != 0 && !b.err);
    mark_end = b.pos;
    if (stype == 0 && c->entropy_cabac)
    {
        bits_ue(&b);                      /* cabac_init_idc */
    }
    bits_se(&b);                          /* slice_qp_delta */
    if (c->deblock_present)
    {
        if (bits_ue(&b) != 1)             /* disable_deblocking_idc */
        {
            bits_se(&b);
            bits_se(&b);
        }
    }
    hdr_end = b.pos;
    if (b.err || !c->entropy_cabac)
    {
        /* CAVLC data is not byte-aligned; only CABAC is supported */
        goto unsupported;
    }
    pay_byte = (hdr_end + 7) / 8;         /* CABAC payload after alignment */
    if (pay_byte >= rlen)
    {
        goto unsupported;
    }
    memset(newr, 0, nal_len + 8);
    opos = 0;
    oerr = 0;
    for (i = 0; i < mark_start && !oerr; i++)
    {
        bit = (rbsp[i >> 3] >> (7 - (i & 7))) & 1;
        put_bit(newr, &opos, (nal_len + 8) * 8, bit, &oerr);
    }
    put_bit(newr, &opos, (nal_len + 8) * 8, 0, &oerr); /* sliding window */
    for (i = mark_end; i < hdr_end && !oerr; i++)
    {
        bit = (rbsp[i >> 3] >> (7 - (i & 7))) & 1;
        put_bit(newr, &opos, (nal_len + 8) * 8, bit, &oerr);
    }
    while ((opos & 7) != 0 && !oerr)      /* cabac_alignment_one_bit */
    {
        put_bit(newr, &opos, (nal_len + 8) * 8, 1, &oerr);
    }
    nbytes = opos / 8;
    if (oerr || nbytes + (rlen - pay_byte) > nal_len + 8)
    {
        goto unsupported;
    }
    memcpy(newr + nbytes, rbsp + pay_byte, rlen - pay_byte);
    nbytes += rlen - pay_byte;
    /* re-escape */
    out[0] = nal[0];
    olen = 1;
    zeros = 0;
    for (i = 0; i < nbytes; i++)
    {
        if (zeros == 2 && newr[i] <= 3)
        {
            if (olen >= out_cap)
            {
                goto unsupported;
            }
            out[olen++] = 3;
            zeros = 0;
        }
        if (olen >= out_cap)
        {
            goto unsupported;
        }
        zeros = (newr[i] == 0) ? zeros + 1 : 0;
        out[olen++] = newr[i];
    }
    free(rbsp);
    free(newr);
    return olen;
unsupported:
    free(rbsp);
    free(newr);
    return -1;
}

/*****************************************************************************/
int
xrdp_h264_strip_mmco(unsigned char *data, int *len,
                     struct xrdp_h264_param_cache *cache)
{
    unsigned char *out;
    int pos;
    int nal_start;
    int sc_prefix;
    int nal_count;
    int rv;

    if (data == NULL || len == NULL || cache == NULL || *len < 4)
    {
        return 1;
    }
    if (!find_start_code(data, *len, 0, &nal_start, &sc_prefix))
    {
        return 1;
    }
    out = (unsigned char *)malloc(*len + 16);
    if (out == NULL)
    {
        return 1;
    }
    rv = 0;
    pos = nal_start;
    nal_count = 0;
    while (pos < *len && nal_count < XRDP_H264_MAX_NALS)
    {
        int next_start;
        int next_prefix;
        int nal_end;
        int nal_len;
        int new_len;
        int ntype;

        nal_count++;
        if (find_start_code(data, *len, pos + 1, &next_start, &next_prefix))
        {
            nal_end = next_start - next_prefix;
        }
        else
        {
            nal_end = *len;
            next_start = -1;
        }
        nal_len = nal_end - pos;
        ntype = data[pos] & 0x1f;
        if (ntype == 7)
        {
            cache_sps(cache, data + pos, nal_len);
        }
        else if (ntype == 8)
        {
            cache_pps(cache, data + pos, nal_len);
        }
        else if (ntype == 1 && ((data[pos] >> 5) & 3) != 0)
        {
            if (!cache->have_sps || !cache->have_pps)
            {
                rv = 1;
                break;
            }
            new_len = slice_strip_mmco(data + pos, nal_len, out,
                                       *len + 16, cache);
            if (new_len < 0)
            {
                rv = 1;
                break;
            }
            if (new_len > 0 && new_len != nal_len)
            {
                if (new_len > nal_len)
                {
                    /* never grow the caller buffer; diagnostic knob */
                    rv = 1;
                    break;
                }
                memcpy(data + pos, out, new_len);
                memmove(data + pos + new_len, data + nal_end,
                        *len - nal_end);
                *len -= nal_len - new_len;
                if (next_start > 0)
                {
                    next_start -= nal_len - new_len;
                }
            }
            else if (new_len > 0)
            {
                memcpy(data + pos, out, new_len);
            }
        }
        if (next_start < 0)
        {
            break;
        }
        pos = next_start;
    }
    free(out);
    return rv;
}
