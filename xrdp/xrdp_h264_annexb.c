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

    /* #75: memchr for the leading zero rather than a byte-at-a-time
     * triple compare. This runs over the whole coded picture once per
     * NAL boundary, so at 4K it is a pass over ~1.7 MB per view; the
     * bytes it skips are entropy-coded and almost never zero. Same
     * result as the naive loop -- first i >= pos with 00 00 01 and
     * i + 3 <= len. */
    i = pos;
    while (i + 3 <= len)
    {
        const unsigned char *hit;

        hit = (const unsigned char *)memchr(data + i, 0, len - 2 - i);
        if (hit == NULL)
        {
            return 0;
        }
        i = (int)(hit - data);
        if (data[i + 1] == 0 && data[i + 2] == 1)
        {
            /* a preceding zero makes it a 4-byte start code, but either way
             * the NAL header follows the 01 byte */
            *sc_prefix = (i > pos && data[i - 1] == 0) ? 4 : 3;
            *nal_start = i + 3;
            return 1;
        }
        i++;
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

/*
 * #75: escaped bytes of a VCL NAL unescaped up front to reach the end of
 * the slice header. A slice header is tens of bytes -- first_mb,
 * slice_type, pps_id, frame_num, the child's marking ops, qp delta and
 * the deblocking fields -- so this is roughly an order of magnitude of
 * slack. It is a performance bound, not a correctness one: a header that
 * does not fit re-runs over the whole NAL.
 */
#define XRDP_H264_LTR_HDR_SCAN 512

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

    /* keep the bytes for the D18 drop guard (0 length = too big to
     * prove identical, so the set keeps passing through) */
    c->sps_raw_len = 0;
    if (nal_len > 0 && nal_len <= XRDP_H264_PS_RAW_MAX)
    {
        memcpy(c->sps_raw, nal, nal_len);
        c->sps_raw_len = nal_len;
    }
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
            profile_idc == 86 || profile_idc == 118 || profile_idc == 128 ||
            profile_idc == 138 || profile_idc == 139 || profile_idc == 134 ||
            profile_idc == 135)
    {
        cfi = bits_ue(&b);
        if (cfi == 3)
        {
            bits_u(&b, 1);
        }
        bits_ue(&b);
        bits_ue(&b);
        bits_u(&b, 1);
        c->scaling_present = bits_u(&b, 1);
        if (c->scaling_present)
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
/* bit index just past the last data bit of an RBSP (i.e. the position of  */
/* the rbsp_stop_one_bit), or -1 if none found                             */
static int
rbsp_data_bits(const unsigned char *rbsp, int rlen)
{
    int i;
    int bit;

    for (i = rlen - 1; i >= 0; i--)
    {
        if (rbsp[i] != 0)
        {
            for (bit = 7; bit >= 0; bit--)
            {
                if ((rbsp[i] >> (7 - bit)) & 1)
                {
                    return i * 8 + bit;
                }
            }
        }
    }
    return -1;
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

    c->pps_raw_len = 0;              /* see cache_sps: D18 drop guard */
    if (nal_len > 0 && nal_len <= XRDP_H264_PS_RAW_MAX)
    {
        memcpy(c->pps_raw, nal, nal_len);
        c->pps_raw_len = nal_len;
    }
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
    c->num_ref_idx_l0_default = bits_ue(&b);
    bits_ue(&b);                          /* num_ref_idx_l1_default */
    c->weighted_pred = bits_u(&b, 1);
    bits_u(&b, 2);                        /* weighted_bipred_idc */
    c->pic_init_qp = bits_se(&b) + 26;
    bits_se(&b);                          /* pic_init_qs */
    c->chroma_qp_offset = bits_se(&b);
    c->deblock_present = bits_u(&b, 1);
    bits_u(&b, 1);                        /* constrained_intra */
    c->redundant_present = bits_u(&b, 1);
    /* optional High-profile extension (transform_8x8 etc.) */
    c->transform_8x8 = 0;
    c->pps_scaling_present = 0;
    c->second_chroma_qp_offset = c->chroma_qp_offset;
    if (!b.err && b.pos < rbsp_data_bits(rbsp, rlen))
    {
        c->transform_8x8 = bits_u(&b, 1);
        c->pps_scaling_present = bits_u(&b, 1);
        if (c->pps_scaling_present)
        {
            /* payload interpretation would depend on the lists; the
             * compat check rejects this shape, no need to parse them */
            b.err = 1;
        }
        else
        {
            c->second_chroma_qp_offset = bits_se(&b);
        }
    }
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

/*
 * AVC444 reference partitioning (BACKLOG 2026-07-27).
 *
 * Mechanism proof (PR-demo/mac_bisect_matrix/CROSS_VIEW_REFERENCE_PROOF
 * .md): with one encoder context interleaving main/aux, inter MBs
 * reference the OTHER view; any client that deviates from strict
 * in-order single-decoder feeding resolves them against same-view
 * frames instead and the wrong-reference error compounds through the
 * DPB (the Mac chroma corruption). Ground truth (real Win2022) keeps
 * ONE chain but never predicts across views. This rewrite reproduces
 * that contract with two stock-ffmpeg children: the aux child encodes
 * every frame as IDR, and each aux packet is rewritten into a
 * non-reference, non-IDR I leaf spliced into the main child's chain.
 * Leaves never enter the DPB, so the main chain self-references at
 * ANY aux cadence (owner directive: no cadence-dependent correctness).
 */

/*****************************************************************************/
static void
put_ue(unsigned char *out, int *pos, int cap_bits, unsigned int v, int *err)
{
    unsigned int vv;
    unsigned int t;
    int n;
    int i;

    vv = v + 1;
    n = 0;
    t = vv;
    while (t > 1)
    {
        t >>= 1;
        n++;
    }
    for (i = 0; i < n; i++)
    {
        put_bit(out, pos, cap_bits, 0, err);
    }
    for (i = n; i >= 0; i--)
    {
        put_bit(out, pos, cap_bits, (vv >> i) & 1, err);
    }
}

/*****************************************************************************/
/* every parse-relevant SPS/PPS field must match between the two encoder   */
/* children, or the leaf slice bits would be reinterpreted under the main  */
/* parameter sets; anything outside the shapes our encoders emit fails     */
static int
leaf_caches_compatible(const struct xrdp_h264_param_cache *mc,
                       const struct xrdp_h264_param_cache *ac)
{
    return mc->have_sps && mc->have_pps && ac->have_sps && ac->have_pps &&
           mc->log2_max_frame_num == ac->log2_max_frame_num &&
           mc->poc_type == 2 && ac->poc_type == 2 &&
           mc->frame_mbs_only == 1 && ac->frame_mbs_only == 1 &&
           mc->scaling_present == 0 && ac->scaling_present == 0 &&
           mc->entropy_cabac == 1 && ac->entropy_cabac == 1 &&
           mc->slice_groups == 0 && ac->slice_groups == 0 &&
           mc->deblock_present == ac->deblock_present &&
           mc->redundant_present == 0 && ac->redundant_present == 0 &&
           mc->pic_init_qp == ac->pic_init_qp &&
           mc->chroma_qp_offset == ac->chroma_qp_offset &&
           mc->second_chroma_qp_offset == ac->second_chroma_qp_offset &&
           mc->transform_8x8 == ac->transform_8x8 &&
           mc->pps_scaling_present == 0 && ac->pps_scaling_present == 0;
}

/*****************************************************************************/
/* rewrite one IDR slice NAL into a non-reference, non-IDR I leaf slice:   */
/* nal_ref_idc 0, type 1, idr_pic_id and dec_ref_pic_marking removed,      */
/* frame_num = new_fn, CABAC payload copied byte-verbatim after re-padding */
/* the alignment. returns the new NAL length or -1 on failure.             */
static int
slice_idr_to_leaf(const unsigned char *nal, int nal_len, unsigned char *out,
                  int out_cap, const struct xrdp_h264_param_cache *ac,
                  const struct xrdp_h264_param_cache *mc, int new_fn)
{
    unsigned char *rbsp;
    unsigned char *newr;
    struct sps_bits b;
    unsigned int first_mb;
    unsigned int stype;
    unsigned int pps_id;
    int rlen;
    int zeros;
    int i;
    int hdr2_start;
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
    first_mb = bits_ue(&b);
    stype = bits_ue(&b);
    pps_id = bits_ue(&b);
    bits_u(&b, ac->log2_max_frame_num);   /* frame_num (discarded) */
    if (stype % 5 != 2)
    {
        goto unsupported;                 /* IDR must carry I slices */
    }
    bits_ue(&b);                          /* idr_pic_id (dropped) */
    /* poc_type == 2 (enforced by leaf_caches_compatible): no POC fields */
    bits_u(&b, 2);                        /* IDR dec_ref_pic_marking */
    hdr2_start = b.pos;
    bits_se(&b);                          /* slice_qp_delta */
    if (ac->deblock_present)
    {
        if (bits_ue(&b) != 1)             /* disable_deblocking_idc */
        {
            bits_se(&b);
            bits_se(&b);
        }
    }
    hdr_end = b.pos;
    if (b.err || !ac->entropy_cabac)
    {
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
    put_ue(newr, &opos, (nal_len + 8) * 8, first_mb, &oerr);
    put_ue(newr, &opos, (nal_len + 8) * 8, stype, &oerr);
    put_ue(newr, &opos, (nal_len + 8) * 8, pps_id, &oerr);
    for (i = mc->log2_max_frame_num - 1; i >= 0 && !oerr; i--)
    {
        put_bit(newr, &opos, (nal_len + 8) * 8, (new_fn >> i) & 1, &oerr);
    }
    for (i = hdr2_start; i < hdr_end && !oerr; i++)
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
    /* re-escape; leaf NAL header: forbidden 0, nal_ref_idc 0, type 1 */
    out[0] = 0x01;
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
/* cache SPS/PPS from the main packet and return the frame_num of its last */
/* reference VCL NAL, or -1 on failure                                     */
static int
main_ref_frame_num(const unsigned char *data, int len,
                   struct xrdp_h264_param_cache *mc)
{
    struct sps_bits b;
    unsigned char rbsp[SPS_RBSP_MAX];
    int pos;
    int nal_start;
    int sc_prefix;
    int nal_count;
    int fn;
    int rlen;
    int zeros;
    int i;

    if (!find_start_code(data, len, 0, &nal_start, &sc_prefix))
    {
        return -1;
    }
    fn = -1;
    pos = nal_start;
    nal_count = 0;
    while (pos < len && nal_count < XRDP_H264_MAX_NALS)
    {
        int next_start;
        int next_prefix;
        int nal_end;
        int nal_len;
        int ntype;

        nal_count++;
        if (find_start_code(data, len, pos + 1, &next_start, &next_prefix))
        {
            nal_end = next_start - next_prefix;
        }
        else
        {
            nal_end = len;
            next_start = -1;
        }
        nal_len = nal_end - pos;
        ntype = data[pos] & 0x1f;
        if (ntype == 7)
        {
            cache_sps(mc, data + pos, nal_len);
        }
        else if (ntype == 8)
        {
            cache_pps(mc, data + pos, nal_len);
        }
        else if ((ntype == 1 || ntype == 5) && ((data[pos] >> 5) & 3) != 0)
        {
            if (!mc->have_sps)
            {
                return -1;
            }
            rlen = 0;
            zeros = 0;
            for (i = pos + 1; i < nal_end && rlen < SPS_RBSP_MAX; i++)
            {
                if (zeros == 2 && data[i] == 3)
                {
                    zeros = 0;
                    continue;
                }
                zeros = (data[i] == 0) ? zeros + 1 : 0;
                rbsp[rlen++] = data[i];
            }
            b.buf = rbsp;
            b.nbits = rlen * 8;
            b.pos = 0;
            b.err = 0;
            bits_ue(&b);                  /* first_mb_in_slice */
            bits_ue(&b);                  /* slice_type */
            bits_ue(&b);                  /* pps id */
            i = bits_u(&b, mc->log2_max_frame_num);
            if (b.err)
            {
                return -1;
            }
            fn = i;
        }
        if (next_start < 0)
        {
            break;
        }
        pos = next_start;
    }
    return fn;
}

/*****************************************************************************/
int
xrdp_h264_aux_to_leaf(unsigned char *aux, int *aux_len,
                      const unsigned char *main_data, int main_len,
                      struct xrdp_h264_param_cache *main_cache,
                      struct xrdp_h264_param_cache *aux_cache)
{
    unsigned char *out;
    int out_len;
    int main_fn;
    int leaf_fn;
    int pos;
    int nal_start;
    int sc_prefix;
    int nal_count;
    int leaves;
    int rv;

    if (aux == NULL || aux_len == NULL || main_data == NULL ||
            main_cache == NULL || aux_cache == NULL || *aux_len < 4)
    {
        return 1;
    }
    main_fn = main_ref_frame_num(main_data, main_len, main_cache);
    if (main_fn < 0)
    {
        /* no reference VCL NAL in the main packet */
        return 1;
    }
    if (!find_start_code(aux, *aux_len, 0, &nal_start, &sc_prefix))
    {
        return 1;
    }
    out = (unsigned char *)malloc(*aux_len + 16);
    if (out == NULL)
    {
        return 1;
    }
    rv = 0;
    out_len = 0;
    leaves = 0;
    pos = nal_start;
    nal_count = 0;
    while (pos < *aux_len && nal_count < XRDP_H264_MAX_NALS)
    {
        int next_start;
        int next_prefix;
        int nal_end;
        int nal_len;
        int new_len;
        int ntype;

        nal_count++;
        if (find_start_code(aux, *aux_len, pos + 1, &next_start,
                            &next_prefix))
        {
            nal_end = next_start - next_prefix;
        }
        else
        {
            nal_end = *aux_len;
            next_start = -1;
        }
        nal_len = nal_end - pos;
        ntype = aux[pos] & 0x1f;
        if (ntype == 7)
        {
            cache_sps(aux_cache, aux + pos, nal_len);
        }
        else if (ntype == 8)
        {
            cache_pps(aux_cache, aux + pos, nal_len);
        }
        else if (ntype == 6 || ntype == 9)
        {
            /* SEI / AUD: dropped with the parameter sets */
        }
        else if (ntype == 5)
        {
            if (!leaf_caches_compatible(main_cache, aux_cache))
            {
                /* main/aux SPS-PPS parse fields differ or unsupported */
                rv = 1;
                break;
            }
            leaf_fn = (main_fn + 1) &
                      ((1 << main_cache->log2_max_frame_num) - 1);
            if (out_len + 4 + nal_len + 8 > *aux_len + 16)
            {
                rv = 1;
                break;
            }
            out[out_len++] = 0;
            out[out_len++] = 0;
            out[out_len++] = 0;
            out[out_len++] = 1;
            new_len = slice_idr_to_leaf(aux + pos, nal_len, out + out_len,
                                        *aux_len + 16 - out_len,
                                        aux_cache, main_cache, leaf_fn);
            if (new_len < 0)
            {
                rv = 1;
                break;
            }
            out_len += new_len;
            leaves++;
        }
        else
        {
            /* unexpected NAL type in the all-IDR aux stream */
            rv = 1;
            break;
        }
        if (next_start < 0)
        {
            break;
        }
        pos = next_start;
    }
    if (rv == 0 && leaves == 0)
    {
        rv = 1;
    }
    if (rv == 0)
    {
        memcpy(aux, out, out_len);
        *aux_len = out_len;
    }
    free(out);
    return rv;
}

/*
 * FR-H264-8 (EXPERIMENTAL): aux-refs-aux via Windows-style long-term
 * reference slots. See the contract comment in xrdp_h264_annexb.h and
 * PRD FR-H264-8. Everything below follows the file's one rule for
 * modifying slice bits: full-NAL unescape -> bit-copy with the edits
 * -> re-escape. No in-place patching of escaped bytes (a changed byte
 * can create or destroy 00 00 03 emulation runs).
 */

/*****************************************************************************/
/* MaxDpbMbs by level_idc (H.264 table A-1). 0 = unknown level.          */
/* level 1b (level_idc 11 + constraint_set3_flag) has MaxDpbMbs 396,     */
/* not 900: fail loud rather than price it as level 1.1.                 */
static int
ltr_max_dpb_mbs(int level_idc, int constraint_flags)
{
    switch (level_idc)
    {
        case 10:
            return 396;
        case 11:
            return (constraint_flags & 0x10) ? 0 : 900;
        case 12:
        case 13:
        case 20:
            return 2376;
        case 21:
            return 4752;
        case 22:
        case 30:
            return 8100;
        case 31:
            return 18000;
        case 32:
            return 20480;
        case 40:
        case 41:
            return 32768;
        case 42:
            return 34816;
        case 50:
            return 110400;
        case 51:
        case 52:
            return 184320;
        case 60:
        case 61:
        case 62:
            return 696320;
        default:
            return 0;
    }
}

/*****************************************************************************/
/* the compat guard for the LTR splice: every shape our rewrite depends  */
/* on, verified from the ACTUAL SPS/PPS, never assumed                   */
static int
ltr_cache_ok(const struct xrdp_h264_param_cache *c)
{
    /* narrow child frame_num fields (x264 emits log2 = 4) are
     * WIDENED to XRDP_H264_LTR_LOG2_MAX_FRAME_NUM (16) by the
     * SPS/slice rewrites -- sparse aux cadences (FR-PROC-7) would
     * alias a narrow per-view frame_num, and cadence-dependent
     * correctness is forbidden */
    return c->have_sps && c->have_pps &&
           c->log2_max_frame_num >= 4 &&
           c->log2_max_frame_num <= XRDP_H264_LTR_LOG2_MAX_FRAME_NUM &&
           c->poc_type == 2 &&
           c->frame_mbs_only == 1 &&
           c->scaling_present == 0 &&
           c->entropy_cabac == 1 &&
           c->slice_groups == 0 &&
           c->weighted_pred == 0 &&
           c->num_ref_idx_l0_default == 0 &&
           c->redundant_present == 0 &&
           c->pps_scaling_present == 0;
}

/*****************************************************************************/
static void
copy_bit_range(const unsigned char *rbsp, int from, int to,
               unsigned char *out, int *opos, int cap_bits, int *err)
{
    int i;
    int bit;

    for (i = from; i < to && !*err; i++)
    {
        bit = (rbsp[i >> 3] >> (7 - (i & 7))) & 1;
        put_bit(out, opos, cap_bits, bit, err);
    }
}

/*****************************************************************************/
/* rewrite one SPS NAL for the LTR chain: max_num_ref_frames -> 3 and,   */
/* when a VUI bitstream_restriction is present, max_dec_frame_buffering  */
/* -> 3 (decoders that size the DPB from the VUI would otherwise evict   */
/* LT1 -- the exact corruption class of the Mac bug). Verifies           */
/* gaps_in_frame_num_allowed == 0 and the level's DPB budget >= 3.       */
/* returns the new NAL length or -1 on any check/parse failure.          */
static int
sps_ltr_rewrite_nal(const unsigned char *nal, int nal_len,
                    unsigned char *out, int out_cap)
{
    unsigned char rbsp[SPS_RBSP_MAX];
    unsigned char newr[SPS_RBSP_MAX];
    struct sps_bits b;
    unsigned int profile_idc;
    unsigned int cflags;
    unsigned int level_idc;
    unsigned int chroma_format_idc;
    unsigned int poc_type;
    unsigned int mnrf;
    unsigned int mdfb;
    unsigned int w_mbs;
    unsigned int h_mbs;
    int log2_mfn;
    int lmfn_start;
    int lmfn_end;
    int mnrf_start;
    int mnrf_end;
    int mdfb_start;
    int mdfb_end;
    int nal_hrd;
    int vcl_hrd;
    int last_one;
    int rlen;
    int zeros;
    int j;
    int opos;
    int oerr;
    int olen;
    int nbits;
    int dpb;

    if (nal_len < 4 || nal_len > SPS_RBSP_MAX)
    {
        return -1;
    }
    rlen = 0;
    zeros = 0;
    for (j = 1; j < nal_len; j++)
    {
        if (zeros == 2 && nal[j] == 3)
        {
            zeros = 0;
            continue;
        }
        zeros = (nal[j] == 0) ? zeros + 1 : 0;
        rbsp[rlen++] = nal[j];
    }
    nbits = rlen * 8;
    b.buf = rbsp;
    b.nbits = nbits;
    b.pos = 0;
    b.err = 0;
    mdfb_start = -1;
    mdfb_end = -1;
    mdfb = 0;

    profile_idc = bits_u(&b, 8);
    cflags = bits_u(&b, 8);
    level_idc = bits_u(&b, 8);
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
            bits_u(&b, 1);
        }
        bits_ue(&b);         /* bit_depth_luma_minus8 */
        bits_ue(&b);         /* bit_depth_chroma_minus8 */
        bits_u(&b, 1);       /* qpprime_y_zero_transform_bypass_flag */
        if (bits_u(&b, 1))   /* seq_scaling_matrix_present_flag */
        {
            return -1;       /* guard: scaling lists unsupported */
        }
    }
    lmfn_start = b.pos;
    log2_mfn = bits_ue(&b) + 4;
    lmfn_end = b.pos;
    if (log2_mfn < 4 || log2_mfn > XRDP_H264_LTR_LOG2_MAX_FRAME_NUM)
    {
        return -1;
    }
    poc_type = bits_ue(&b);
    if (poc_type != 2)
    {
        return -1;           /* guard: only poc_type 2 slice headers */
    }
    mnrf_start = b.pos;
    mnrf = bits_ue(&b);
    mnrf_end = b.pos;
    if (mnrf < 1 || mnrf > 3)
    {
        return -1;
    }
    if (bits_u(&b, 1) != 0)  /* gaps_in_frame_num_value_allowed_flag */
    {
        return -1;
    }
    w_mbs = bits_ue(&b) + 1;
    h_mbs = bits_ue(&b) + 1;
    if (bits_u(&b, 1) == 0)  /* frame_mbs_only_flag */
    {
        return -1;
    }
    bits_u(&b, 1);           /* direct_8x8_inference_flag */
    if (bits_u(&b, 1))       /* frame_cropping_flag */
    {
        bits_ue(&b);
        bits_ue(&b);
        bits_ue(&b);
        bits_ue(&b);
    }
    if (b.err || w_mbs == 0 || h_mbs == 0 || w_mbs > 16384 ||
            h_mbs > 16384)
    {
        return -1;
    }
    /* level DPB budget: raising the reference count to 3 must fit */
    dpb = ltr_max_dpb_mbs((int)level_idc, (int)cflags);
    if (dpb <= 0)
    {
        return -1;           /* unknown level: fail loud, never guess */
    }
    dpb = dpb / (int)(w_mbs * h_mbs);
    if (dpb > 16)
    {
        dpb = 16;
    }
    if (dpb < 3)
    {
        return -1;           /* level cannot hold LT0+LT1+current */
    }
    if (bits_u(&b, 1))       /* vui_parameters_present_flag */
    {
        if (bits_u(&b, 1))   /* aspect_ratio_info_present_flag */
        {
            if (bits_u(&b, 8) == 255)
            {
                bits_u(&b, 32);
            }
        }
        if (bits_u(&b, 1))   /* overscan_info_present_flag */
        {
            bits_u(&b, 1);
        }
        if (bits_u(&b, 1))   /* video_signal_type_present_flag */
        {
            bits_u(&b, 4);
            if (bits_u(&b, 1))
            {
                bits_u(&b, 24);
            }
        }
        if (bits_u(&b, 1))   /* chroma_loc_info_present_flag */
        {
            bits_ue(&b);
            bits_ue(&b);
        }
        if (bits_u(&b, 1))   /* timing_info_present_flag */
        {
            bits_u(&b, 64);
            bits_u(&b, 1);
        }
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
            bits_u(&b, 1);   /* low_delay_hrd_flag */
        }
        bits_u(&b, 1);       /* pic_struct_present_flag */
        if (bits_u(&b, 1))   /* bitstream_restriction_flag */
        {
            bits_u(&b, 1);   /* motion_vectors_over_pic_boundaries */
            bits_ue(&b);     /* max_bytes_per_pic_denom */
            bits_ue(&b);     /* max_bits_per_mb_denom */
            bits_ue(&b);     /* log2_max_mv_length_horizontal */
            bits_ue(&b);     /* log2_max_mv_length_vertical */
            bits_ue(&b);     /* max_num_reorder_frames (kept) */
            mdfb_start = b.pos;
            mdfb = bits_ue(&b);
            mdfb_end = b.pos;
        }
    }
    if (b.err)
    {
        return -1;
    }
    last_one = rbsp_data_bits(rbsp, rlen);
    if (last_one < mnrf_end || (mdfb_end > 0 && last_one < mdfb_end))
    {
        return -1;
    }
    memset(newr, 0, sizeof(newr));
    opos = 0;
    oerr = 0;
    /* widen the frame_num field to the LTR output width: narrow
     * fields alias under sparse aux cadences, and per-view feeds
     * DIE at the wrap (see XRDP_H264_LTR_LOG2_MAX_FRAME_NUM) */
    copy_bit_range(rbsp, 0, lmfn_start, newr, &opos, SPS_RBSP_MAX * 8,
                   &oerr);
    put_ue(newr, &opos, SPS_RBSP_MAX * 8,
           (unsigned int)(XRDP_H264_LTR_LOG2_MAX_FRAME_NUM - 4), &oerr);
    copy_bit_range(rbsp, lmfn_end, mnrf_start, newr, &opos,
                   SPS_RBSP_MAX * 8, &oerr);
    put_ue(newr, &opos, SPS_RBSP_MAX * 8, 3, &oerr);
    if (mdfb_start >= 0)
    {
        copy_bit_range(rbsp, mnrf_end, mdfb_start, newr, &opos,
                       SPS_RBSP_MAX * 8, &oerr);
        put_ue(newr, &opos, SPS_RBSP_MAX * 8, (mdfb > 3) ? mdfb : 3,
               &oerr);
        copy_bit_range(rbsp, mdfb_end, last_one + 1, newr, &opos,
                       SPS_RBSP_MAX * 8, &oerr);
    }
    else
    {
        copy_bit_range(rbsp, mnrf_end, last_one + 1, newr, &opos,
                       SPS_RBSP_MAX * 8, &oerr);
    }
    if (oerr)
    {
        return -1;
    }
    rlen = (opos + 7) / 8;
    out[0] = nal[0];
    olen = 1;
    zeros = 0;
    for (j = 0; j < rlen; j++)
    {
        if (zeros == 2 && newr[j] <= 3)
        {
            if (olen >= out_cap)
            {
                return -1;
            }
            out[olen++] = 3;
            zeros = 0;
        }
        if (olen >= out_cap)
        {
            return -1;
        }
        zeros = (newr[j] == 0) ? zeros + 1 : 0;
        out[olen++] = newr[j];
    }
    return olen;
}

/*****************************************************************************/
/* parse just first_mb_in_slice and slice_type from a slice NAL          */
static int
slice_peek(const unsigned char *nal, int nal_len, unsigned int *first_mb,
           unsigned int *stype)
{
    unsigned char rbsp[64];
    struct sps_bits b;
    int rlen;
    int zeros;
    int i;

    if (nal_len < 3)
    {
        return 1;
    }
    rlen = 0;
    zeros = 0;
    for (i = 1; i < nal_len && rlen < (int)sizeof(rbsp); i++)
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
    *first_mb = bits_ue(&b);
    *stype = bits_ue(&b);
    return b.err;
}

/*****************************************************************************/
/* dec_ref_pic_marking() of a non-IDR reference child slice: sliding
 * window (flag 0) or an adaptive mmco chain. Parsed only to be
 * discarded and replaced by the constant LTR self-mark -- see the
 * reasoning at the call sites. Returns 0 on success, 1 on mmco5 or an
 * invalid op (which the replacement cannot represent). */
static int
parse_child_marking(struct sps_bits *b)
{
    unsigned int op;

    if (bits_u(b, 1))                     /* adaptive marking */
    {
        do
        {
            op = bits_ue(b);
            switch (op)
            {
                case 0:
                    break;
                case 1:
                case 2:
                case 4:
                case 6:
                    bits_ue(b);
                    break;
                case 3:
                    bits_ue(b);
                    bits_ue(b);
                    break;
                default:
                    return 1;              /* mmco5 / invalid ops */
            }
        }
        while (op != 0 && !b->err);
    }
    return 0;
}

/*****************************************************************************/
/* Emulation-prevention state after emitting the first `len` RBSP bytes:  */
/* the number of consecutive trailing 0x00 since the last escape byte was */
/* inserted, which is exactly what decides whether the NEXT byte needs    */
/* one. Mirrors the escape loop in slice_ltr_rewrite; keep them together. */
static int
escape_state(const unsigned char *rbsp, int len)
{
    int zeros;
    int i;

    zeros = 0;
    for (i = 0; i < len; i++)
    {
        if (zeros == 2 && rbsp[i] <= 3)
        {
            zeros = 0;
        }
        zeros = (rbsp[i] == 0) ? zeros + 1 : 0;
    }
    return zeros;
}

/*****************************************************************************/
/* Offset in the ESCAPED NAL at which the emission of RBSP byte `want`     */
/* begins -- the byte itself, or the inserted 0x03 in front of it. Walks   */
/* only as far as `want`, which for the CABAC payload boundary is tens of  */
/* bytes. Returns -1 if the NAL ends first.                                */
static int
esc_offset_of_rbsp(const unsigned char *nal, int nal_len, int want)
{
    int zeros;
    int r;
    int i;

    zeros = 0;
    r = 0;
    for (i = 1; i < nal_len; i++)
    {
        if (zeros == 2 && nal[i] == 3)
        {
            if (r == want)
            {
                return i;        /* the escape belongs to rbsp[want] */
            }
            zeros = 0;
            continue;
        }
        if (r == want)
        {
            return i;
        }
        zeros = (nal[i] == 0) ? zeros + 1 : 0;
        r++;
    }
    return (r == want) ? nal_len : -1;
}

/*****************************************************************************/
/* rewrite one VCL slice NAL into its LTR-chain form. view: 0 main,      */
/* 1 aux. to_intra_i: emit this intra picture as the self-contained      */
/* non-IDR I that self-marks the view's own long-term slot -- always     */
/* for the aux view, and for the main view at a scheduled refresh once   */
/* the chain has started (FR-H264-6; an IDR there would flush the DPB).  */
/* fn: the shared frame_num to write.                                    */
/* Accepts three input shapes: IDR-carrying-I, non-IDR I (h264_nvenc at  */
/* a forced key frame without -forced-idr) and P.                        */
/* returns the new NAL length or -1 on failure.                          */
/*                                                                       */
/* scan_len is how much of the NAL is unescaped up front. The slice      */
/* header is tens of bytes and the CABAC payload behind it is copied     */
/* verbatim, so the caller asks for a bounded prefix first (#75) and     */
/* only re-runs over the whole NAL if that was not enough.               */
static int
slice_ltr_rewrite_sz(const unsigned char *nal, int nal_len, int scan_len,
                     unsigned char *out, int out_cap,
                     const struct xrdp_h264_param_cache *c,
                     int view, int to_intra_i, int fn)
{
    unsigned char *rbsp;
    unsigned char *newr;
    struct sps_bits b;
    unsigned int first_mb;
    unsigned int stype;
    unsigned int pps_id;
    unsigned int idr_pic_id;
    unsigned int no_output;
    unsigned int old_fn;
    int is_idr;
    int is_p;
    int is_i;
    int out_log2;
    int rlen;
    int zeros;
    int i;
    int hdr2_start;
    int hdr_end;
    int pay_byte;
    int opos;
    int oerr;
    int olen;
    int nbytes;
    int cap_bits;
    int zbytes;
    int prefix_mode;
    int esc_pay_off;
    int zero_state;
    unsigned char hdr_rbsp[XRDP_H264_LTR_HDR_SCAN];
    unsigned char hdr_newr[XRDP_H264_LTR_HDR_SCAN + 64];

    is_idr = (nal[0] & 0x1f) == 5;
    if (nal_len < 4)
    {
        return -1;
    }
    prefix_mode = scan_len < nal_len;
    if (prefix_mode)
    {
        /* the header path: both buffers are stack-sized and no
         * allocation happens at all */
        rbsp = hdr_rbsp;
        newr = hdr_newr;
        cap_bits = (int)sizeof(hdr_newr) * 8;
    }
    else
    {
        rbsp = (unsigned char *)malloc(nal_len);
        newr = (unsigned char *)malloc(nal_len + 16);
        if (rbsp == NULL || newr == NULL)
        {
            free(rbsp);
            free(newr);
            return -1;
        }
        cap_bits = (nal_len + 16) * 8;
    }
    rlen = 0;
    zeros = 0;
    for (i = 1; i < scan_len; i++)
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
    first_mb = bits_ue(&b);
    stype = bits_ue(&b);
    pps_id = bits_ue(&b);
    old_fn = bits_u(&b, c->log2_max_frame_num);  /* replaced below */
    is_p = (stype % 5 == 0);
    is_i = (stype % 5 == 2);
    if (is_idr)
    {
        if (stype % 5 != 2)
        {
            goto unsupported;             /* IDR must carry I slices */
        }
        if (old_fn != 0)
        {
            goto unsupported;             /* 7.4.3: IDR frame_num = 0 */
        }
        idr_pic_id = bits_ue(&b);
        no_output = bits_u(&b, 1);
        bits_u(&b, 1);                    /* long_term_reference_flag */
        hdr2_start = b.pos;
    }
    else if (is_p)
    {
        idr_pic_id = 0;
        no_output = 0;
        if (bits_u(&b, 1))                /* num_ref_idx override */
        {
            goto unsupported;
        }
        if (bits_u(&b, 1))                /* ref_pic_list_modification */
        {
            goto unsupported;             /* must not already exist */
        }
        /* dec_ref_pic_marking (nri != 0 enforced by caller) */
        /* benign child marking -- sliding window or short-term mmco
         * chains (Mesa emits [mmco1 diff=0, mmco0] on every P) -- is
         * parsed and REPLACED by the constant LTR self-mark: the
         * output chain holds no short-term references, so dropped
         * mmco1/2/3 have nothing to act on, and a dropped mmco4 is
         * safe because the child chain contains no long-term
         * pictures for a lowered bound to unmark. mmco5 (full
         * reference reset) changes decoder state the replacement
         * cannot represent: hard-reject, matching the reference
         * splicer. */
        if (parse_child_marking(&b) != 0)
        {
            goto unsupported;
        }
        hdr2_start = b.pos;               /* cabac_init_idc onwards */
    }
    else if (is_i)
    {
        /* non-IDR I: the scheduled-refresh shape h264_nvenc emits at a
         * forced key frame without -forced-idr. Between frame_num and
         * dec_ref_pic_marking there is nothing to skip under the guard
         * ltr_cache_ok() enforces (poc_type 2, frame_mbs_only, no
         * redundant_pic_cnt), and an I slice carries neither a
         * num_ref_idx override nor a ref_pic_list_modification. The
         * child's marking is parsed and discarded exactly as on the P
         * path. */
        idr_pic_id = 0;
        no_output = 0;
        if (parse_child_marking(&b) != 0)
        {
            goto unsupported;
        }
        hdr2_start = b.pos;               /* slice_qp_delta onwards */
    }
    else
    {
        goto unsupported;                 /* B/SP/SI */
    }
    /* remaining header: [P: cabac_init_idc ue], slice_qp_delta se,
     * [deblock fields when PPS declares them] -- copied verbatim */
    if (is_p)
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
        goto unsupported;
    }
    pay_byte = (hdr_end + 7) / 8;         /* CABAC payload after align */
    if (pay_byte >= rlen)
    {
        goto unsupported;
    }
    /* #75: put_bit ORs, so only the bytes the new header will occupy
     * have to start at zero. The old form zeroed the whole picture
     * buffer -- 1.7 MB per view at 4K -- and every byte past the header
     * is overwritten by the payload copy below anyway. The bound is the
     * original header rounded up, plus the widest insertion this
     * function makes (list modification + mmco, under 24 bits) and a
     * byte of alignment slack. */
    zbytes = pay_byte + 8;
    if (zbytes > (prefix_mode ? (int)sizeof(hdr_newr) : nal_len + 16))
    {
        goto unsupported;
    }
    memset(newr, 0, zbytes);
    opos = 0;
    oerr = 0;
    put_ue(newr, &opos, cap_bits, first_mb, &oerr);
    put_ue(newr, &opos, cap_bits, stype, &oerr);
    put_ue(newr, &opos, cap_bits, pps_id, &oerr);
    out_log2 = XRDP_H264_LTR_LOG2_MAX_FRAME_NUM;
    for (i = out_log2 - 1; i >= 0 && !oerr; i--)
    {
        put_bit(newr, &opos, cap_bits, (fn >> i) & 1, &oerr);
    }
    if (is_idr && !to_intra_i)
    {
        /* main IDR stays IDR: keep idr_pic_id and no_output, force
         * long_term_reference_flag = 1 (seeds LT0) */
        put_ue(newr, &opos, cap_bits, idr_pic_id, &oerr);
        put_bit(newr, &opos, cap_bits, (int)no_output, &oerr);
        put_bit(newr, &opos, cap_bits, 1, &oerr);
    }
    else
    {
        if (is_p)
        {
            /* P: override = 0, then the constant LTR selection */
            put_bit(newr, &opos, cap_bits, 0, &oerr);
            put_bit(newr, &opos, cap_bits, 1, &oerr);  /* rplm_l0 */
            put_ue(newr, &opos, cap_bits, 2, &oerr);   /* idc: LT */
            put_ue(newr, &opos, cap_bits, view, &oerr);
            put_ue(newr, &opos, cap_bits, 3, &oerr);   /* idc: end */
        }
        /* constant self-mark into the view's slot (aux seed I and
         * every P): adaptive = 1, mmco 6, ltfi = view, mmco 0 */
        put_bit(newr, &opos, cap_bits, 1, &oerr);
        put_ue(newr, &opos, cap_bits, 6, &oerr);
        put_ue(newr, &opos, cap_bits, view, &oerr);
        put_ue(newr, &opos, cap_bits, 0, &oerr);
    }
    copy_bit_range(rbsp, hdr2_start, hdr_end, newr, &opos, cap_bits,
                   &oerr);
    while ((opos & 7) != 0 && !oerr)      /* cabac_alignment_one_bit */
    {
        put_bit(newr, &opos, cap_bits, 1, &oerr);
    }
    nbytes = opos / 8;
    if (oerr || nbytes > zbytes)
    {
        goto unsupported;
    }
    if (!prefix_mode)
    {
        if (nbytes + (rlen - pay_byte) > nal_len + 16)
        {
            goto unsupported;
        }
        memcpy(newr + nbytes, rbsp + pay_byte, rlen - pay_byte);
        nbytes += rlen - pay_byte;
    }
    /* NAL header: nri = 3 always (Windows shape); type: IDR stays 5
     * on main, everything else is 1 */
    out[0] = (is_idr && !to_intra_i) ? 0x65 : 0x61;
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
    if (prefix_mode)
    {
        /* #75: the CABAC payload is byte-aligned in both the child's NAL
         * and ours (cabac_alignment_one_bit pads to a byte in front of
         * it) and its RBSP bytes are copied through unchanged, so the
         * ESCAPED payload bytes are invariant -- provided the escaper
         * enters the payload in the same state in both. `zeros` is that
         * state for the header we just emitted; escape_state() computes
         * it for the child's. When they agree the child's own escaped
         * payload is byte-for-byte what we would produce, so it is
         * copied verbatim: no unescape pass, no re-escape pass, and no
         * picture-sized intermediate.
         *
         * When they disagree the payload's first bytes could escape
         * differently, so this returns -1 and the caller re-runs over
         * the whole NAL on the original path. The trailing
         * cabac_zero_words escape below is not re-applied here for the
         * same reason it is not needed: the child's NAL already carries
         * it, and it is inside the range copied. */
        zero_state = escape_state(rbsp, pay_byte);
        if (zero_state != zeros)
        {
            goto unsupported;
        }
        esc_pay_off = esc_offset_of_rbsp(nal, nal_len, pay_byte);
        if (esc_pay_off < 0 || esc_pay_off >= nal_len)
        {
            goto unsupported;
        }
        if (olen + (nal_len - esc_pay_off) > out_cap)
        {
            goto unsupported;
        }
        memcpy(out + olen, nal + esc_pay_off, nal_len - esc_pay_off);
        olen += nal_len - esc_pay_off;
        return olen;
    }
    if (zeros >= 2)
    {
        /* trailing cabac_zero_words: the child's wire ended 00 00 03;
         * re-emit the escape so the NAL never ends in a zero byte
         * (7.4.1) and the zeros cannot merge into the next start code */
        if (olen >= out_cap)
        {
            goto unsupported;
        }
        out[olen++] = 3;
    }
    free(rbsp);
    free(newr);
    return olen;
unsupported:
    if (!prefix_mode)
    {
        free(rbsp);
        free(newr);
    }
    return -1;
}

/*****************************************************************************/
/* #75: try the bounded-prefix path first -- the slice header is tens of  */
/* bytes, so the whole rewrite is a small stack buffer plus one memcpy of */
/* the child's already-escaped payload. Anything that path cannot express */
/* (a header longer than the scan, or a payload whose escaping would      */
/* change) re-runs over the whole NAL on the original code, so no packet  */
/* the old form accepted is refused by the new one.                       */
static int
slice_ltr_rewrite(const unsigned char *nal, int nal_len,
                  unsigned char *out, int out_cap,
                  const struct xrdp_h264_param_cache *c,
                  int view, int to_intra_i, int fn)
{
    if (nal_len > XRDP_H264_LTR_HDR_SCAN)
    {
        int rv;

        rv = slice_ltr_rewrite_sz(nal, nal_len, XRDP_H264_LTR_HDR_SCAN,
                                  out, out_cap, c, view, to_intra_i, fn);
        if (rv >= 0)
        {
            return rv;
        }
    }
    return slice_ltr_rewrite_sz(nal, nal_len, nal_len, out, out_cap, c,
                                view, to_intra_i, fn);
}

/*****************************************************************************/
int
xrdp_h264_ltr_growth_budget(const unsigned char *data, int len)
{
    int pos;
    int nal_start;
    int sc_prefix;
    int nals;

    if (data == NULL || len < 4 ||
            !find_start_code(data, len, 0, &nal_start, &sc_prefix))
    {
        return 64;
    }
    nals = 0;
    pos = nal_start;
    while (pos < len && nals < XRDP_H264_MAX_NALS)
    {
        int next_start;
        int next_prefix;

        nals++;
        if (!find_start_code(data, len, pos + 1, &next_start,
                             &next_prefix))
        {
            break;
        }
        pos = next_start;
    }
    return 64 + 24 * nals;
}

/*****************************************************************************/
/* Bounded pre-scan: will this packet's coded picture be emitted as the
 * converted self-contained intra (and therefore have the child's
 * repeated parameter sets dropped, D18)? Reads the first VCL NAL with
 * first_mb_in_slice == 0 and nothing else; an unreadable or
 * non-intra picture answers 0, and the slice loop then re-derives the
 * same answer and refuses the packet on any disagreement. */
static int
packet_intra_is_converted(const unsigned char *data, int len,
                          const struct xrdp_h264_ltr_state *st, int view)
{
    unsigned int first_mb;
    unsigned int stype;
    int pos;
    int nal_start;
    int sc_prefix;
    int nal_count;

    if (!find_start_code(data, len, 0, &nal_start, &sc_prefix))
    {
        return 0;
    }
    pos = nal_start;
    nal_count = 0;
    while (pos < len && nal_count < XRDP_H264_MAX_NALS)
    {
        int next_start;
        int next_prefix;
        int nal_end;
        int ntype;

        nal_count++;
        if (find_start_code(data, len, pos + 1, &next_start, &next_prefix))
        {
            nal_end = next_start - next_prefix;
        }
        else
        {
            nal_end = len;
            next_start = -1;
        }
        ntype = data[pos] & 0x1f;
        if (ntype == 5 || ntype == 1)
        {
            if (slice_peek(data + pos, nal_end - pos, &first_mb,
                           &stype) != 0)
            {
                return 0;
            }
            if (first_mb != 0)
            {
                return 0;     /* not the start of the picture */
            }
            if (ntype != 5 && stype % 5 != 2)
            {
                return 0;     /* P: nothing to convert */
            }
            return (view == 1) || st->started;
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
/* shared walker for both views.                                         */
/* view 0: at the stream/epoch entry the SPS is rewritten, PPS/SEI/AUD    */
/* copied and the IDR stays an IDR (LT0 seed); at a SCHEDULED REFRESH     */
/* once the chain has started the intra picture becomes the              */
/* self-contained non-IDR I that re-marks LT0 and the child's repeated   */
/* SPS/PPS/SEI are dropped (D18: not a decoder entry point), while a     */
/* CHANGED parameter set fails the packet instead of being swallowed;    */
/* P -> LTR/LT0.                                                         */
/* view 1: SPS/PPS cached + dropped, SEI/AUD dropped, any intra picture  */
/* (IDR or non-IDR I) -> seed I (LT1), P -> LTR/LT1.                     */
/* Both views accept the two child intra shapes: IDR-carrying-I          */
/* (h264_vaapi) and non-IDR I (h264_nvenc without -forced-idr).          */
static int
ltr_rewrite_walk(unsigned char *data, int *len, int cap,
                 struct xrdp_h264_ltr_state *st, int view)
{
    struct xrdp_h264_param_cache *own;
    unsigned char *out;
    unsigned int first_mb;
    unsigned int stype;
    int out_cap;
    int out_len;
    int pos;
    int nal_start;
    int sc_prefix;
    int nal_count;
    int vcl_pics;
    int intra_seen;
    int convert_intra;
    int cur_fn;
    int rv;
    int mfn_mask;

    if (data == NULL || len == NULL || st == NULL || *len < 4)
    {
        return 1;
    }
    if (!find_start_code(data, *len, 0, &nal_start, &sc_prefix))
    {
        return 1;
    }
    own = (view == 0) ? &st->main_cache : &st->aux_cache;
    out_cap = *len + xrdp_h264_ltr_growth_budget(data, *len);
    /* #75: a picture-sized buffer is far above glibc's 128 KB
     * M_MMAP_THRESHOLD, so malloc/free here was an mmap+munmap pair per
     * view per frame and every page of it faulted on first touch --
     * 4228 minor faults per pair measured at 4K. Held on the state and
     * reused instead; xrdp_h264_ltr_state_free() releases it. */
    if (st->scratch_cap < out_cap)
    {
        free(st->scratch);
        st->scratch = (unsigned char *)malloc(out_cap);
        st->scratch_cap = (st->scratch == NULL) ? 0 : out_cap;
    }
    out = st->scratch;
    if (out == NULL)
    {
        return 1;
    }
    rv = 0;
    out_len = 0;
    vcl_pics = 0;
    intra_seen = 0;
    cur_fn = 0;
    /* The packet's coded picture decides whether the child's parameter
     * sets are dropped, and that decision has to be made BEFORE the
     * first NAL is emitted -- the sets precede the slice in the access
     * unit. So the picture's kind is read in a bounded pre-scan, and
     * the slice loop below re-derives the same value and refuses the
     * packet if the two disagree. */
    convert_intra = packet_intra_is_converted(data, *len, st, view);
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
        int nri;

        nal_count++;
        if (find_start_code(data, *len, pos + 1, &next_start,
                            &next_prefix))
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
        nri = (data[pos] >> 5) & 3;
        if (ntype == 7)
        {
            int drop_ps;

            /* D18: at a converted cut the child's repeat of the
             * parameter sets is dropped -- the cut is not a decoder
             * entry point, so it costs bytes at every refresh and buys
             * nothing. Only a set PROVEN identical to the one already
             * on the wire may be dropped; a CHANGED set fails the
             * packet, because swallowing it would be silent
             * whole-picture corruption. */
            drop_ps = 0;
            if (convert_intra && own->sps_raw_len > 0)
            {
                if (nal_len == own->sps_raw_len &&
                        memcmp(own->sps_raw, data + pos, nal_len) == 0)
                {
                    drop_ps = 1;
                }
                else
                {
                    rv = 1;
                    break;
                }
            }
            cache_sps(own, data + pos, nal_len);
            if (view == 0 && !drop_ps)
            {
                if (out_len + 4 + nal_len + 8 > out_cap)
                {
                    rv = 1;
                    break;
                }
                out[out_len++] = 0;
                out[out_len++] = 0;
                out[out_len++] = 0;
                out[out_len++] = 1;
                new_len = sps_ltr_rewrite_nal(data + pos, nal_len,
                                              out + out_len,
                                              out_cap - out_len);
                if (new_len < 0)
                {
                    rv = 1;
                    break;
                }
                out_len += new_len;
            }
        }
        else if (ntype == 8)
        {
            int drop_ps;

            drop_ps = 0;                       /* see the SPS case */
            if (convert_intra && own->pps_raw_len > 0)
            {
                if (nal_len == own->pps_raw_len &&
                        memcmp(own->pps_raw, data + pos, nal_len) == 0)
                {
                    drop_ps = 1;
                }
                else
                {
                    rv = 1;
                    break;
                }
            }
            cache_pps(own, data + pos, nal_len);
            if (view == 0 && !drop_ps)
            {
                if (out_len + 4 + nal_len > out_cap)
                {
                    rv = 1;
                    break;
                }
                out[out_len++] = 0;
                out[out_len++] = 0;
                out[out_len++] = 0;
                out[out_len++] = 1;
                memcpy(out + out_len, data + pos, nal_len);
                out_len += nal_len;
            }
        }
        else if (ntype == 6 || ntype == 9)
        {
            /* D18: the SEI a child repeats with its cut parameter sets
             * goes with them (173 B of the 206 B measured per cut on
             * the VAAPI shape). The AUD is not a parameter set and the
             * Windows wire carries one on every frame, so it stays. */
            if (view == 0 && !(convert_intra && ntype == 6))
            {
                if (out_len + 4 + nal_len > out_cap)
                {
                    rv = 1;
                    break;
                }
                out[out_len++] = 0;
                out[out_len++] = 0;
                out[out_len++] = 0;
                out[out_len++] = 1;
                memcpy(out + out_len, data + pos, nal_len);
                out_len += nal_len;
            }
        }
        else if (ntype == 5 || ntype == 1)
        {
            if (nri == 0)
            {
                rv = 1;         /* non-reference VCL: not our shape */
                break;
            }
            if (!ltr_cache_ok(own))
            {
                rv = 1;
                break;
            }
            if (view == 1 &&
                    !leaf_caches_compatible(&st->main_cache,
                                            &st->aux_cache))
            {
                rv = 1;
                break;
            }
            if (slice_peek(data + pos, nal_len, &first_mb, &stype) != 0)
            {
                rv = 1;
                break;
            }
            mfn_mask = (1 << XRDP_H264_LTR_LOG2_MAX_FRAME_NUM) - 1;
            if (first_mb == 0)
            {
                if (vcl_pics > 0)
                {
                    /* one coded picture per packet is the child
                     * contract (encode_pair pops per picture) */
                    rv = 1;
                    break;
                }
                vcl_pics = 1;
                intra_seen = (ntype == 5) || (stype % 5 == 2);
                /* the pre-scan decided the picture's fate; every slice
                 * of a multi-slice picture must agree with it */
                if (convert_intra !=
                        (intra_seen && ((view == 1) || st->started)))
                {
                    rv = 1;
                    break;
                }
                if (st->refresh_period > 0 && st->started)
                {
                    /* OBSERVED vs REQUESTED (FR-H264-6): the child was
                     * spawned with a frame-indexed -force_key_frames
                     * schedule, so an intra picture is due exactly on
                     * the scheduled ordinals of this view. A picture
                     * that parses P where intra was scheduled means the
                     * encoder silently skipped the refresh -- fail the
                     * pair rather than ship a stream whose prediction
                     * chain is longer than the wire claims. An intra
                     * picture arriving OFF schedule is equally a
                     * mismatch: it is either an unscheduled GOP IDR
                     * (which D7 makes unreachable) or a de-phased
                     * child, and both invalidate the depth bound. */
                    int expect_intra;

                    expect_intra = (st->pic_index[view] %
                                    st->refresh_period) == 0;
                    if (expect_intra != intra_seen)
                    {
                        rv = 1;
                        break;
                    }
                }
                if (view == 0 && !st->started && ntype != 5)
                {
                    /* the main chain must START with a real IDR: it is
                     * the stream's only decoder entry point. A non-IDR
                     * I here (nvenc's first picture without
                     * -forced-idr) is refused, not silently accepted as
                     * an entry point it cannot be. */
                    rv = 1;
                    break;
                }
                if (intra_seen)
                {
                    cur_fn = convert_intra ? st->frame_num : 0;
                }
                else
                {
                    if (view == 1 && !st->aux_seeded)
                    {
                        /* aux P with LT1 unseeded: the pair fails --
                         * only an aux INTRA picture can seed LT1 */
                        rv = 1;
                        break;
                    }
                    cur_fn = st->frame_num;
                }
            }
            else if (vcl_pics == 0)
            {
                rv = 1;           /* slices before the picture start */
                break;
            }
            if (out_len + 4 + nal_len + 24 > out_cap)
            {
                rv = 1;
                break;
            }
            out[out_len++] = 0;
            out[out_len++] = 0;
            out[out_len++] = 0;
            out[out_len++] = 1;
            new_len = slice_ltr_rewrite(data + pos, nal_len,
                                        out + out_len,
                                        out_cap - out_len, own, view,
                                        convert_intra,
                                        cur_fn & mfn_mask);
            if (new_len < 0)
            {
                rv = 1;
                break;
            }
            out_len += new_len;
        }
        else
        {
            rv = 1;               /* unexpected NAL type */
            break;
        }
        if (next_start < 0)
        {
            break;
        }
        pos = next_start;
    }
    if (rv == 0 && vcl_pics == 0)
    {
        rv = 1;
    }
    if (rv == 0 && out_len > cap)
    {
        rv = 1;                   /* caller buffer too small */
    }
    if (rv == 0)
    {
        mfn_mask = (1 << XRDP_H264_LTR_LOG2_MAX_FRAME_NUM) - 1;
        memcpy(data, out, out_len);
        *len = out_len;
        st->frame_num = (cur_fn + 1) & mfn_mask;
        if (view == 0 && intra_seen)
        {
            st->started = 1;
            if (!convert_intra)
            {
                /* a REAL IDR shipped: it emptied the DPB and LT1 with
                 * it. A converted refresh does not -- its mmco6
                 * REPLACES the occupant of LT0 and never touches LT1,
                 * which is the whole point of FR-H264-6 */
                st->aux_seeded = 0;
            }
        }
        if (view == 1 && intra_seen)
        {
            st->aux_seeded = 1;   /* the seed I now occupies LT1 */
        }
        /* the schedule ordinal advances only for a packet that SHIPPED,
         * and only here in the commit epilogue: a rejected packet must
         * leave the state untouched */
        if (view == 0 && intra_seen && !convert_intra)
        {
            st->pic_index[0] = 0; /* epoch entry restarts the schedule */
            st->pic_index[1] = 0;
        }
        st->pic_index[view]++;
    }
    return rv;                    /* out is st->scratch, kept for reuse */
}

/*****************************************************************************/
void
xrdp_h264_ltr_state_free(struct xrdp_h264_ltr_state *st)
{
    if (st == NULL)
    {
        return;
    }
    free(st->scratch);
    st->scratch = NULL;
    st->scratch_cap = 0;
}

/*****************************************************************************/
int
xrdp_h264_ltr_rewrite_main(unsigned char *data, int *len, int cap,
                           struct xrdp_h264_ltr_state *st)
{
    return ltr_rewrite_walk(data, len, cap, st, 0);
}

/*****************************************************************************/
int
xrdp_h264_ltr_rewrite_aux(unsigned char *data, int *len, int cap,
                          struct xrdp_h264_ltr_state *st)
{
    return ltr_rewrite_walk(data, len, cap, st, 1);
}
