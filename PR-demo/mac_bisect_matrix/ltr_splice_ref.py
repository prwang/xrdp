#!/usr/bin/env python3
"""FR-H264-8 reference splicer (SYNTAX-ratchet golden-vector generator).

Bit-exact python reference of the planned C rewriter (PRD FR-H264-8,
lines 1330-1366): splice two child H.264 elementary streams (main +
aux, each refs=1/CABAC/poc_type=2) into ONE frame_num chain using the
measured Win2022 long-term-reference recipe:

  - one shared frame_num counter, +1 per PICTURE, main then aux per
    pair, wrap mod 2^OUT_LOG2 (child log2 accepted in 4..16 and
    widened to OUT_LOG2 = 16

# H.264 Table A-1 MaxDpbMbs by level_idc (matches the C rewriter's
# ltr_max_dpb_mbs; unknown level = fail loud)
MAX_DPB_MBS = {10: 396, 11: 900, 12: 2376, 13: 2376, 20: 2376,
               21: 4752, 22: 8100, 30: 8100, 31: 18000, 32: 20480,
               40: 32768, 41: 32768, 42: 34816, 50: 110400,
               51: 184320, 52: 184320, 60: 696320, 61: 696320,
               62: 696320});
  - every VCL NAL nal_ref_idc=3;
  - main SPS ships once with max_num_ref_frames=3 (and, if a VUI
    bitstream_restriction is present, max_dec_frame_buffering=3);
  - main IDR: long_term_reference_flag 0->1 (seeds LT0);
  - main P: rplm [idc=2 ltpn=0, idc=3]; marking replaced by
    [adaptive=1, mmco6 ltfi=0, mmco0];
  - aux IDR -> self-contained non-IDR type-1 I slice (idr_pic_id
    dropped, marking replaced by [adaptive=1, mmco6 ltfi=1, mmco0]) --
    deliberate deviation from the Windows first-aux-refs-LT0 quirk;
  - aux P: rplm [idc=2 ltpn=1, idc=3]; marking replaced by
    [adaptive=1, mmco6 ltfi=1, mmco0]; only legal once LT1 is seeded;
  - aux SPS/PPS/SEI/AUD dropped (SPS/PPS cached for compat check);
  - NO mmco4 anywhere; CABAC payloads byte-verbatim after re-padding
    cabac_alignment_one_bit; full unescape -> bit-copy -> re-escape.

Usage:
  ltr_splice_ref.py main.h264 aux.h264 outdir
      [--aux-cadence K] [--fault-retarget]
      [--emit-header PATH --win2022 WIN_ANNEXB]

Outputs in outdir: interleaved.h264, main_only.h264, aux_only.h264,
order.json, pkt_main_%03d.bin, pkt_aux_%03d.bin.

--fault-retarget: sensitivity-validation fault; retargets the FIRST
aux P slice's long_term_pic_num from 1 to 0 (must turn the decode
identity checks RED).

FAIL LOUD (nonzero exit, no output written) on any stream shape
outside the guard; see die() call sites.
"""
import argparse
import json
import os
import sys


def die(msg):
    sys.stderr.write('FATAL: %s\n' % msg)
    sys.exit(1)


def unescape(b):
    out = bytearray()
    i = 0
    while i < len(b):
        if i + 2 < len(b) and b[i] == 0 and b[i + 1] == 0 and b[i + 2] == 3:
            out += b[i:i + 2]
            i += 3
        else:
            out.append(b[i])
            i += 1
    return bytes(out)


def escape(b):
    out = bytearray()
    zeros = 0
    for byte in b:
        if zeros >= 2 and byte <= 3:
            out.append(3)
            zeros = 0
        out.append(byte)
        zeros = zeros + 1 if byte == 0 else 0
    return bytes(out)


class R:
    def __init__(self, buf):
        self.b = buf
        self.p = 0

    def u(self, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((self.b[self.p >> 3] >> (7 - (self.p & 7))) & 1)
            self.p += 1
        return v

    def ue(self):
        z = 0
        while self.u(1) == 0:
            z += 1
            assert z < 32
        return (1 << z) - 1 + (self.u(z) if z else 0)

    def se(self):
        k = self.ue()
        return (k + 1) // 2 if k % 2 else -(k // 2)

    def more_data(self):
        """data bits remain before the rbsp_stop_one_bit (last set
        bit of the buffer)"""
        for i in range(len(self.b) * 8 - 1, -1, -1):
            if (self.b[i >> 3] >> (7 - (i & 7))) & 1:
                return self.p < i
        return False


class W:
    def __init__(self):
        self.bits = []

    def u(self, v, n):
        for i in range(n - 1, -1, -1):
            self.bits.append((v >> i) & 1)

    def ue(self, v):
        v += 1
        n = v.bit_length()
        self.u(0, n - 1)
        self.u(v, n)

    def se(self, v):
        self.ue(2 * v - 1 if v > 0 else -2 * v)

    def copy_bits(self, r, n):
        for _ in range(n):
            self.bits.append(r.u(1))

    def tobytes(self):
        by = bytearray()
        for i in range(0, len(self.bits), 8):
            chunk = self.bits[i:i + 8]
            chunk += [0] * (8 - len(chunk))
            v = 0
            for bit in chunk:
                v = (v << 1) | bit
            by.append(v)
        return bytes(by)


SC4 = b'\x00\x00\x00\x01'

# The LTR chain's OUTPUT frame_num width (must equal the C rewriter's
# XRDP_H264_LTR_LOG2_MAX_FRAME_NUM): child fields are widened to 16
# bits, the legal maximum. MEASURED (2026-07-28, ffmpeg 7.1.5): a
# per-view feed with +2 frame_num gaps silently stops decoding at the
# frame_num wrap (8-bit field, 300 aux pictures: 129/300 output, no
# warnings), so the wire must never let a decoder see a wrap in any
# topology; the runner re-keys before the 16-bit counter can wrap.
OUT_LOG2 = 16

# H.264 Table A-1 MaxDpbMbs by level_idc (matches the C rewriter's
# ltr_max_dpb_mbs; unknown level = fail loud)
MAX_DPB_MBS = {10: 396, 11: 900, 12: 2376, 13: 2376, 20: 2376,
               21: 4752, 22: 8100, 30: 8100, 31: 18000, 32: 20480,
               40: 32768, 41: 32768, 42: 34816, 50: 110400,
               51: 184320, 52: 184320, 60: 696320, 61: 696320,
               62: 696320}


def nal_units(buf):
    out = []
    i = buf.find(b'\x00\x00\x01')
    while i >= 0:
        j = buf.find(b'\x00\x00\x01', i + 3)
        e = j if j >= 0 else len(buf)
        raw = buf[i + 3:e]
        if raw.endswith(b'\x00'):
            raw = raw[:-1]
        if raw:
            out.append(raw)
        i = j
    return out


def first_mb_of(nal):
    return R(unescape(nal[1:10])).ue()


def access_units(nals):
    """Group NALs into AUs (pictures). A new picture starts at a VCL
    NAL with first_mb_in_slice == 0; prefix non-VCL NALs attach to the
    following picture."""
    aus = []
    cur = []
    pend = []
    for n in nals:
        t = n[0] & 0x1F
        if t in (1, 5):
            if first_mb_of(n) == 0 and any(
                    (x[0] & 0x1F) in (1, 5) for x in cur):
                aus.append(cur)
                cur = []
            cur += pend
            pend = []
            cur.append(n)
        else:
            pend.append(n)
    if pend:
        die('trailing non-VCL NAL units')
    if cur:
        aus.append(cur)
    return aus


HIGH_PROFILES = (100, 110, 122, 244, 44, 83, 86, 118, 128,
                 138, 139, 134, 135)


def parse_sps(rbsp, rewrite, new_log2=None):
    """Parse SPS; if rewrite, echo bit-exactly with max_num_ref_frames
    := 3 and (when a VUI bitstream_restriction exists)
    max_dec_frame_buffering := 3. new_log2 additionally rewrites
    log2_max_frame_num (conditioning mode). Returns
    (fields, rewritten_rbsp)."""
    r = R(rbsp)
    w = W()

    def u(n):
        v = r.u(n)
        w.u(v, n)
        return v

    def ue():
        v = r.ue()
        w.ue(v)
        return v

    def hrd():
        cnt = ue() + 1
        u(8)                       # bit_rate_scale, cpb_size_scale
        for _ in range(cnt):
            ue()
            ue()
            u(1)
        u(20)                      # 4 x 5-bit lengths

    f = {}
    f['profile_idc'] = u(8)
    f['constraint_flags'] = u(8)
    f['level_idc'] = u(8)
    f['sps_id'] = ue()
    if f['profile_idc'] in HIGH_PROFILES:
        f['chroma_format_idc'] = ue()
        if f['chroma_format_idc'] == 3:
            f['separate_colour'] = u(1)
        f['bit_depth_luma'] = ue() + 8
        f['bit_depth_chroma'] = ue() + 8
        f['qpprime'] = u(1)
        if u(1):
            die('seq_scaling_matrix_present unsupported')
    f['log2_max_frame_num'] = r.ue() + 4
    w.ue((new_log2 if new_log2 else f['log2_max_frame_num']) - 4)
    f['poc_type'] = ue()
    if f['poc_type'] == 0:
        f['log2_max_poc_lsb'] = ue() + 4
    elif f['poc_type'] == 1:
        die('poc_type 1 unsupported')
    f['max_num_ref_frames'] = r.ue()
    w.ue(3 if rewrite else f['max_num_ref_frames'])
    f['gaps_in_frame_num'] = u(1)
    f['width_mbs'] = ue() + 1
    f['height_map_units'] = ue() + 1
    f['frame_mbs_only'] = u(1)
    if not f['frame_mbs_only']:
        die('frame_mbs_only_flag == 0 unsupported')
    f['direct_8x8'] = u(1)
    if u(1):                       # frame_cropping_flag
        ue()
        ue()
        ue()
        ue()
    f['vui'] = u(1)
    f['bitstream_restriction'] = 0
    if f['vui']:
        if u(1):                   # aspect_ratio_info_present
            if u(8) == 255:
                u(32)
        if u(1):                   # overscan_info_present
            u(1)
        if u(1):                   # video_signal_type_present
            u(4)
            if u(1):               # colour_description_present
                u(24)
        if u(1):                   # chroma_loc_info_present
            ue()
            ue()
        if u(1):                   # timing_info_present
            u(65)
        nal_hrd = u(1)
        if nal_hrd:
            hrd()
        vcl_hrd = u(1)
        if vcl_hrd:
            hrd()
        if nal_hrd or vcl_hrd:
            u(1)                   # low_delay_hrd
        u(1)                       # pic_struct_present
        f['bitstream_restriction'] = u(1)
        if f['bitstream_restriction']:
            u(1)                   # mv_over_pic_boundaries
            ue()                   # max_bytes_per_pic_denom
            ue()                   # max_bits_per_mb_denom
            ue()                   # log2_max_mv_length_horizontal
            ue()                   # log2_max_mv_length_vertical
            f['max_num_reorder_frames'] = ue()
            f['max_dec_frame_buffering'] = r.ue()
            w.ue(max(3, f['max_dec_frame_buffering'])
                 if rewrite else f['max_dec_frame_buffering'])
    if r.u(1) != 1:                # rbsp_stop_one_bit
        die('SPS rbsp trailing: missing stop bit')
    w.u(1, 1)
    while r.p & 7:
        if r.u(1) != 0:
            die('SPS rbsp trailing: nonzero alignment bit')
    while len(w.bits) & 7:
        w.u(0, 1)
    if (r.p >> 3) != len(rbsp):
        die('SPS: %d trailing bytes' % (len(rbsp) - (r.p >> 3)))
    return f, w.tobytes()


def parse_pps(rbsp):
    r = R(rbsp)
    f = {}
    f['pps_id'] = r.ue()
    f['sps_id'] = r.ue()
    f['cabac'] = r.u(1)
    f['bottom_field_poc'] = r.u(1)
    f['num_slice_groups'] = r.ue() + 1
    if f['num_slice_groups'] != 1:
        die('num_slice_groups != 1 unsupported')
    f['num_ref_idx_l0_default'] = r.ue() + 1
    f['num_ref_idx_l1_default'] = r.ue() + 1
    f['weighted_pred'] = r.u(1)
    f['weighted_bipred_idc'] = r.u(2)
    f['pic_init_qp'] = r.se() + 26
    f['pic_init_qs'] = r.se() + 26
    f['chroma_qp_index_offset'] = r.se()
    f['deblocking_filter_control'] = r.u(1)
    f['constrained_intra_pred'] = r.u(1)
    f['redundant_pic_cnt_present'] = r.u(1)
    # optional High-profile extension: parse-relevant for the verbatim
    # CABAC payload, so it joins the main/aux compat equality check
    f['transform_8x8'] = 0
    f['pps_scaling'] = 0
    f['second_chroma_qp_offset'] = f['chroma_qp_index_offset']
    if r.more_data():
        f['transform_8x8'] = r.u(1)
        f['pps_scaling'] = r.u(1)
        if f['pps_scaling']:
            die('pic_scaling_matrix_present unsupported')
        f['second_chroma_qp_offset'] = r.se()
    return f


def guard_stream(name, sps, pps):
    if sps['poc_type'] != 2:
        die('%s: poc_type %d != 2' % (name, sps['poc_type']))
    if not 4 <= sps['log2_max_frame_num'] <= OUT_LOG2:
        die('%s: log2_max_frame_num %d outside 4..%d'
            % (name, sps['log2_max_frame_num'], OUT_LOG2))
    if sps['gaps_in_frame_num']:
        die('%s: gaps_in_frame_num_allowed set' % name)
    if not 1 <= sps['max_num_ref_frames'] <= 3:
        die('%s: max_num_ref_frames %d outside 1..3'
            % (name, sps['max_num_ref_frames']))
    dpb = MAX_DPB_MBS.get(sps['level_idc'], 0)
    if dpb <= 0:
        die('%s: unknown level_idc %d' % (name, sps['level_idc']))
    dpb = min(16, dpb // (sps['width_mbs'] * sps['height_map_units']))
    if dpb < 3:
        die('%s: level %d DPB budget %d < 3 refs'
            % (name, sps['level_idc'], dpb))
    if not pps['cabac']:
        die('%s: not CABAC' % name)
    if pps['weighted_pred']:
        die('%s: weighted_pred enabled' % name)
    if pps['redundant_pic_cnt_present']:
        die('%s: redundant_pic_cnt_present unsupported' % name)
    if pps['num_ref_idx_l0_default'] != 1:
        die('%s: num_ref_idx_l0_default_active %d != 1 (need refs=1)'
            % (name, pps['num_ref_idx_l0_default']))


def parse_child_marking(r):
    """dec_ref_pic_marking of a non-IDR reference child slice: sliding
    window (flag 0) or an adaptive chain (e.g. Mesa's [mmco1 diff=0,
    mmco0]). Parsed only to be discarded and replaced."""
    if r.u(1):                     # adaptive_ref_pic_marking_mode_flag
        while True:
            op = r.ue()
            if op == 0:
                break
            if op in (1, 3):
                r.ue()             # difference_of_pic_nums_minus1
            if op == 2:
                r.ue()             # long_term_pic_num
            if op == 3 or op == 6:
                r.ue()             # long_term_frame_idx
            if op == 4:
                r.ue()             # max_long_term_frame_idx_plus1
            if op == 5:
                die('mmco5 in child stream unsupported')
            if op > 6:
                die('invalid mmco %d in child stream' % op)


def rewrite_slice(nal, new_fn, log2, pps, view, lt1_seeded,
                  fault_ltpn=None):
    """Rewrite one child VCL NAL per the FR-H264-8 recipe. Returns the
    rewritten NAL (escaped, with header byte, no start code)."""
    ntype = nal[0] & 0x1F
    if ntype not in (1, 5):
        die('unexpected VCL NAL type %d' % ntype)
    if (nal[0] >> 5) == 0:
        die('%s child VCL is non-reference (nri=0)' % view)
    rbsp = unescape(nal[1:])
    r = R(rbsp)
    w = W()
    first_mb = r.ue()
    stype = r.ue()
    pps_id = r.ue()
    old_fn = r.u(log2)
    st = stype % 5
    w.ue(first_mb)
    w.ue(stype)
    w.ue(pps_id)
    w.u(new_fn, OUT_LOG2)
    if ntype == 5:
        if st != 2:
            die('IDR with slice_type %d (not I)' % stype)
        idr_pic_id = r.ue()
        nopp = r.u(1)              # no_output_of_prior_pics_flag
        r.u(1)                     # long_term_reference_flag
        if view == 'M':
            if old_fn != 0:
                die('main IDR frame_num %d != 0' % old_fn)
            w.ue(idr_pic_id)       # idr_pic_id preserved (C parity)
            w.u(nopp, 1)
            w.u(1, 1)              # long_term_reference_flag 0 -> 1
            out_type = 5
        else:
            # aux IDR -> self-contained non-IDR I, self-marks LT1
            w.u(1, 1)              # adaptive_ref_pic_marking_mode_flag
            w.ue(6)                # mmco 6
            w.ue(1)                # long_term_frame_idx = 1
            w.ue(0)                # mmco 0
            out_type = 1
    elif st == 0:
        if r.u(1):                 # num_ref_idx_active_override_flag
            die('%s P: num_ref_idx_active_override set' % view)
        if r.u(1):                 # ref_pic_list_modification_flag_l0
            die('%s P: ref_pic_list_modification already present'
                % view)
        parse_child_marking(r)
        if view == 'A' and not lt1_seeded:
            die('aux P before LT1 is seeded')
        ltpn = 0 if view == 'M' else 1
        if fault_ltpn is not None:
            ltpn = fault_ltpn
        ltfi = 0 if view == 'M' else 1
        w.u(0, 1)                  # num_ref_idx_active_override_flag
        w.u(1, 1)                  # ref_pic_list_modification_flag_l0
        w.ue(2)                    # modification_of_pic_nums_idc = 2
        w.ue(ltpn)                 # long_term_pic_num
        w.ue(3)                    # idc = 3 (end)
        w.u(1, 1)                  # adaptive_ref_pic_marking_mode_flag
        w.ue(6)                    # mmco 6
        w.ue(ltfi)                 # long_term_frame_idx
        w.ue(0)                    # mmco 0
        out_type = 1
    else:
        die('%s: unexpected non-IDR slice_type %d' % (view, stype))
    # tail: [P: cabac_init_idc ue], slice_qp_delta se,
    #       [pps deblock control: idc ue (+2 se if idc != 1)]
    t0 = r.p
    if st == 0:
        r.ue()                     # cabac_init_idc
    r.se()                         # slice_qp_delta
    if pps['deblocking_filter_control']:
        if r.ue() != 1:            # disable_deblocking_filter_idc
            r.se()
            r.se()
    t1 = r.p
    r.p = t0
    w.copy_bits(r, t1 - t0)
    while r.p & 7:
        if r.u(1) != 1:
            die('bad cabac_alignment_one_bit')
    while len(w.bits) & 7:
        w.u(1, 1)
    return bytes([0x60 | out_type]) + escape(w.tobytes()
                                             + rbsp[r.p >> 3:])


def condition_slice(nal, old_log2, new_log2, pps):
    """Identity slice rewrite except frame_num is re-serialized with
    new_log2 bits (value preserved). Used to condition x264 children,
    whose SPS sizes log2_max_frame_num from DPB+1 (== 4 at refs=1)
    with no knob to reach the >= 8 the frozen recipe requires."""
    ntype = nal[0] & 0x1F
    rbsp = unescape(nal[1:])
    r = R(rbsp)
    w = W()
    w.ue(r.ue())                   # first_mb_in_slice
    stype = r.ue()
    w.ue(stype)
    st = stype % 5
    w.ue(r.ue())                   # pps_id
    fn = r.u(old_log2)
    if fn >= (1 << new_log2):
        die('condition: frame_num %d overflows log2 %d'
            % (fn, new_log2))
    w.u(fn, new_log2)
    # walk the rest of the header to find its end, then bit-copy
    t0 = r.p
    if ntype == 5:
        r.ue()                     # idr_pic_id
        r.u(2)                     # no_output_of_prior_pics, ltrf
    else:
        if st == 0:
            if r.u(1):             # num_ref_idx_active_override
                die('condition: num_ref_idx override set')
            if r.u(1):             # rplm_l0
                die('condition: rplm present')
        if (nal[0] >> 5) != 0:
            parse_child_marking(r)
    if st == 0:
        r.ue()                     # cabac_init_idc
    r.se()                         # slice_qp_delta
    if pps['deblocking_filter_control']:
        if r.ue() != 1:
            r.se()
            r.se()
    t1 = r.p
    r.p = t0
    w.copy_bits(r, t1 - t0)
    while r.p & 7:
        if r.u(1) != 1:
            die('condition: bad cabac_alignment_one_bit')
    while len(w.bits) & 7:
        w.u(1, 1)
    return bytes([nal[0]]) + escape(w.tobytes() + rbsp[r.p >> 3:])


def condition_stream(inp, outp, target_log2):
    nals = nal_units(open(inp, 'rb').read())
    sps_nal = [n for n in nals if (n[0] & 0x1F) == 7]
    pps_nal = [n for n in nals if (n[0] & 0x1F) == 8]
    if len(sps_nal) != 1 or len(pps_nal) != 1:
        die('condition: want exactly 1 SPS and 1 PPS')
    sps_f, sps_rw = parse_sps(unescape(sps_nal[0][1:]), rewrite=False,
                              new_log2=target_log2)
    pps_f = parse_pps(unescape(pps_nal[0][1:]))
    old_log2 = sps_f['log2_max_frame_num']
    if old_log2 > target_log2:
        die('condition: log2 %d already > target %d'
            % (old_log2, target_log2))
    out = bytearray()
    for n in nals:
        t = n[0] & 0x1F
        if t == 7:
            out += SC4 + bytes([n[0]]) + escape(sps_rw)
        elif t in (1, 5):
            out += SC4 + condition_slice(n, old_log2, target_log2,
                                         pps_f)
        else:
            out += SC4 + n
    open(outp, 'wb').write(bytes(out))
    print('conditioned %s -> %s: log2_max_frame_num %d -> %d'
          % (inp, outp, old_log2, target_log2))


def load_child(path, name):
    buf = open(path, 'rb').read()
    if not buf:
        die('%s: empty stream' % name)
    nals = nal_units(buf)
    sps = [n for n in nals if (n[0] & 0x1F) == 7]
    pps = [n for n in nals if (n[0] & 0x1F) == 8]
    if len(sps) != 1 or len(pps) != 1:
        die('%s: want exactly 1 SPS and 1 PPS, got %d/%d'
            % (name, len(sps), len(pps)))
    sps_f, _ = parse_sps(unescape(sps[0][1:]), rewrite=False)
    pps_f = parse_pps(unescape(pps[0][1:]))
    guard_stream(name, sps_f, pps_f)
    for n in nals:
        if (n[0] & 0x1F) not in (1, 5, 6, 7, 8, 9):
            die('%s: unexpected NAL type %d' % (name, n[0] & 0x1F))
    return access_units(nals), sps_f, pps_f, sps[0], pps[0]


def splice(main_aus, aux_aus, sps_f, pps_f, sps_nal, pps_nal,
           cadence, fault):
    log2 = sps_f['log2_max_frame_num']
    mod = 1 << OUT_LOG2
    _, sps_rw_rbsp = parse_sps(unescape(sps_nal[1:]), rewrite=True,
                               new_log2=OUT_LOG2)
    sps_rw = bytes([sps_nal[0]]) + escape(sps_rw_rbsp)
    counter = None
    lt1 = False
    fault_pending = fault
    main_pkts = []
    aux_pkts = []
    order = []
    aux_i = 0
    for k, au in enumerate(main_aus):
        vcl = [n for n in au if (n[0] & 0x1F) in (1, 5)]
        if not vcl:
            die('main AU %d has no VCL NAL' % k)
        is_idr = (vcl[0][0] & 0x1F) == 5
        if is_idr:
            fn = 0
        else:
            if counter is None:
                die('main stream does not start with an IDR')
            fn = counter
        pkt = bytearray()
        for n in au:
            t = n[0] & 0x1F
            if t == 7:
                pkt += SC4 + sps_rw
            elif t in (6, 8, 9):
                pkt += SC4 + n     # main PPS/SEI/AUD pass through
            else:
                pkt += SC4 + rewrite_slice(n, fn, log2, pps_f, 'M',
                                           lt1)
        if is_idr:
            counter = 1 % mod
            lt1 = False            # DPB flushed: LT1 unseeded
        else:
            counter = (counter + 1) % mod
        main_pkts.append(bytes(pkt))
        ent = {'v': 'M', 'f': len(main_pkts) - 1, 'frame_num': fn}
        if is_idr and len(main_pkts) > 1:
            ent['idr'] = 1     # re-key: 2-ctx aux context restarts here
        order.append(ent)
        if k % cadence == 0 and aux_i < len(aux_aus):
            au2 = aux_aus[aux_i]
            aux_i += 1
            vcl2 = [n for n in au2 if (n[0] & 0x1F) in (1, 5)]
            if not vcl2:
                die('aux AU %d has no VCL NAL' % (aux_i - 1))
            fn2 = counter
            pkt2 = bytearray()
            for n in au2:
                t = n[0] & 0x1F
                if t in (6, 7, 8, 9):
                    continue       # aux SPS/PPS/SEI/AUD dropped
                fl = None
                if fault_pending and t == 1:
                    fl = 0         # deliberate fault: ltpn 1 -> 0
                    fault_pending = False
                pkt2 += SC4 + rewrite_slice(n, fn2, log2, pps_f, 'A',
                                            lt1, fault_ltpn=fl)
            counter = (counter + 1) % mod
            if (vcl2[0][0] & 0x1F) == 5:
                lt1 = True
            aux_pkts.append(bytes(pkt2))
            order.append({'v': 'A', 'f': len(aux_pkts) - 1,
                          'frame_num': fn2})
    if fault and fault_pending:
        die('--fault-retarget: no aux P slice found to retarget')
    if aux_i != len(aux_aus):
        die('aux AU count mismatch: schedule consumed %d of %d'
            % (aux_i, len(aux_aus)))
    aux_prefix = SC4 + sps_rw + SC4 + pps_nal
    return main_pkts, aux_pkts, order, aux_prefix, log2


# ---------------------------------------------------------------- #
# Win2022 ground-truth constants (measured capture cross-check)     #
# ---------------------------------------------------------------- #

WIN_SPS_HEX = ('674d40209590050065bffa000a0006c8'
               '00001f400007530078e15240')
WIN_AUX_P_HEX = '61e1ab449d6257fe1ac66c9146f779ecf6eef7b85e89c95e'
WIN_FIRST_AUX_P_HEX = ('61e10b9275895ffe1ad1e07b4b0a7343'
                       '9bc296bf9b3d389f')


def parse_win_p_header(nal, log2, pps):
    """Parse a Win2022 P slice header of the LTR shape; None if the
    NAL is not a first_mb==0 P slice of that shape."""
    rbsp = unescape(nal[1:40])
    r = R(rbsp)
    try:
        if r.ue() != 0:            # first_mb_in_slice
            return None
        if r.ue() % 5 != 0:        # slice_type P
            return None
        r.ue()                     # pps_id
        f = {'frame_num': r.u(log2)}
        f['override'] = r.u(1)
        if f['override']:
            r.ue()
        if r.u(1) != 1:            # rplm flag
            return None
        if r.ue() != 2:            # modification idc
            return None
        f['ltpn'] = r.ue()
        if r.ue() != 3:            # idc end
            return None
        if r.u(1) != 1:            # adaptive marking
            return None
        if r.ue() != 6:            # mmco 6
            return None
        f['ltfi'] = r.ue()
        if r.ue() != 0:            # mmco 0
            return None
        if pps['cabac']:
            f['cabac_init_idc'] = r.ue()
        f['qp_delta'] = r.se()
        if pps['deblocking_filter_control']:
            f['deblock_idc'] = r.ue()
        return f
    except (AssertionError, IndexError):
        return None


def win_constants(path):
    nals = nal_units(open(path, 'rb').read())
    sps_nal = next(n for n in nals if (n[0] & 0x1F) == 7)
    pps_nal = next(n for n in nals if (n[0] & 0x1F) == 8)
    sps_f, _ = parse_sps(unescape(sps_nal[1:]), rewrite=False)
    pps_f = parse_pps(unescape(pps_nal[1:]))
    log2 = sps_f['log2_max_frame_num']
    if log2 != 8:
        die('win2022 SPS log2_max_frame_num %d != 8' % log2)
    if sps_f['max_num_ref_frames'] != 3 or sps_f['gaps_in_frame_num']:
        die('win2022 SPS ref/gaps fields unexpected')
    if sps_nal.hex() != WIN_SPS_HEX:
        die('win2022 SPS bytes differ from recorded ground truth:\n'
            '  got  %s\n  want %s' % (sps_nal.hex(), WIN_SPS_HEX))
    first_aux = None
    aux_p = None
    for n in nals:
        if (n[0] & 0x1F) != 1:
            continue
        h = parse_win_p_header(n, log2, pps_f)
        if h is None or h['ltfi'] != 1:
            continue               # aux slices self-mark LT1
        if h['ltpn'] == 0 and first_aux is None:
            first_aux = (n, h)
        if h['ltpn'] == 1 and aux_p is None:
            aux_p = (n, h)
        if first_aux and aux_p:
            break
    if first_aux is None or aux_p is None:
        die('win2022: aux P slices not found')
    n, h = aux_p
    if (h['frame_num'] != 13 or h['ltpn'] != 1
            or h['cabac_init_idc'] != 0 or h['qp_delta'] != -4
            or h['deblock_idc'] != 1):
        die('win2022 aux P parse mismatch: %r' % h)
    if n[:24].hex() != WIN_AUX_P_HEX:
        die('win2022 aux P bytes differ:\n  got  %s\n  want %s'
            % (n[:24].hex(), WIN_AUX_P_HEX))
    n2, h2 = first_aux
    if h2['frame_num'] != 8 or h2['ltpn'] != 0:
        die('win2022 first-aux parse mismatch: %r' % h2)
    if n2[:24].hex() != WIN_FIRST_AUX_P_HEX:
        die('win2022 first-aux bytes differ:\n  got  %s\n  want %s'
            % (n2[:24].hex(), WIN_FIRST_AUX_P_HEX))
    return sps_nal, n[:24], n2[:24]


# ---------------------------------------------------------------- #
# C header emission                                                 #
# ---------------------------------------------------------------- #

def c_array(name, data):
    out = ['/* %d bytes */' % len(data),
           'static const unsigned char %s[] =' % name, '{']
    for i in range(0, len(data), 12):
        out.append('    ' + ' '.join('0x%02x,' % b
                                     for b in data[i:i + 12]))
    out.append('};')
    out.append('#define %s_LEN %d' % (name.upper(), len(data)))
    out.append('')
    return out


def emit_header(path, main_name, aux_name, main_aus, aux_aus,
                main_pkts, aux_pkts, win):
    lines = [
        '/*',
        ' * test_avc444_ltr_vectors.h -- FR-H264-8 LTR splice golden',
        ' * vectors.',
        ' *',
        ' * GENERATED by PR-demo/mac_bisect_matrix/ltr_splice_ref.py',
        ' * -- DO NOT hand-edit. Regenerate with:',
        ' *   ltr_splice_ref.py %s %s <outdir>' % (main_name,
                                                   aux_name),
        ' *     --emit-header tests/xrdp/test_avc444_ltr_vectors.h',
        ' *     --win2022 <assembled win2022 gfxwin_anim annex-b>',
        ' *',
        ' * ltr_*_in_k     : child AU k as fed to the splicer',
        ' *                  (annex-b, 4-byte start codes; AU 0',
        ' *                  includes SPS/PPS/SEI). RAW x264 children',
        ' *                  (log2_max_frame_num = 4: x264 sizes log2',
        ' *                  from DPB+1 with no knob) -- the splice',
        ' *                  widens the frame_num field to 16 bits',
        ' *                  (see OUT_LOG2 in the generator), so these',
        ' *                  vectors also pin the widening path.',
        ' * ltr_*_golden_k : the same AU rewritten by the FR-H264-8',
        ' *                  splice, exactly as the C rewriter under',
        ' *                  test must emit it.',
        ' * win2022_*      : measured Win2022 ground-truth bytes',
        ' *                  (PR-demo/win2022_ground_truth, capture',
        ' *                  gfxwin_anim via assemble_annexb.py).',
        ' */',
        '#ifndef TEST_AVC444_LTR_VECTORS_H',
        '#define TEST_AVC444_LTR_VECTORS_H',
        '',
    ]
    for k, au in enumerate(main_aus):
        lines += c_array('ltr_main_in_%d' % k,
                         b''.join(SC4 + n for n in au))
    for k, au in enumerate(aux_aus):
        lines += c_array('ltr_aux_in_%d' % k,
                         b''.join(SC4 + n for n in au))
    for k, p in enumerate(main_pkts):
        lines += c_array('ltr_main_golden_%d' % k, p)
    for k, p in enumerate(aux_pkts):
        lines += c_array('ltr_aux_golden_%d' % k, p)
    sps, auxp, firstaux = win
    lines += ['/* Win2022 SPS NAL (max_num_ref_frames=3, gaps=0,',
              ' * log2_max_frame_num=8) */']
    lines += c_array('win2022_sps', sps)
    lines += ['/* first 24 bytes of the Win2022 AU-13 aux P slice:',
              ' * nri=3 type=1, frame_num=13, rplm [idc=2 ltpn=1,',
              ' * idc=3], marking [adaptive=1, mmco6 ltfi=1, mmco0],',
              ' * cabac_init_idc=0, qp_delta=-4, deblock idc=1 */']
    lines += c_array('win2022_aux_p_hdr', auxp)
    lines += ['/* first 24 bytes of the Win2022 FIRST aux P slice',
              ' * (frame_num=8, ltpn=0 -- the Windows first-aux-',
              ' * refs-LT0 quirk our splice deliberately does NOT',
              ' * copy) */']
    lines += c_array('win2022_first_aux_p_hdr', firstaux)
    lines += ['#endif /* TEST_AVC444_LTR_VECTORS_H */', '']
    open(path, 'w').write('\n'.join(lines))


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--condition':
        if len(sys.argv) != 5:
            die('usage: --condition in.h264 out.h264 target_log2')
        condition_stream(sys.argv[2], sys.argv[3], int(sys.argv[4]))
        return
    ap = argparse.ArgumentParser()
    ap.add_argument('main')
    ap.add_argument('aux')
    ap.add_argument('outdir')
    ap.add_argument('--aux-cadence', type=int, default=1)
    ap.add_argument('--fault-retarget', action='store_true')
    ap.add_argument('--emit-header')
    ap.add_argument('--win2022')
    args = ap.parse_args()
    if args.aux_cadence < 1:
        die('--aux-cadence must be >= 1')
    if args.emit_header and (args.aux_cadence != 1
                             or args.fault_retarget):
        die('--emit-header requires cadence 1 and no fault')
    if args.emit_header and not args.win2022:
        die('--emit-header requires --win2022')

    main_aus, sps_m, pps_m, sps_nal, pps_nal = load_child(
        args.main, 'main')
    aux_aus, sps_a, pps_a, _, _ = load_child(args.aux, 'aux')
    if sps_m != sps_a:
        die('main/aux SPS parse fields differ:\n  main %r\n  aux  %r'
            % (sps_m, sps_a))
    if pps_m != pps_a:
        die('main/aux PPS parse fields differ:\n  main %r\n  aux  %r'
            % (pps_m, pps_a))

    main_pkts, aux_pkts, order, aux_prefix, log2 = splice(
        main_aus, aux_aus, sps_m, pps_m, sps_nal, pps_nal,
        args.aux_cadence, args.fault_retarget)

    win = win_constants(args.win2022) if args.win2022 else None

    os.makedirs(args.outdir, exist_ok=True)
    wire = bytearray()
    mi = ai = 0
    for e in order:
        if e['v'] == 'M':
            wire += main_pkts[mi]
            mi += 1
        else:
            wire += aux_pkts[ai]
            ai += 1
    open(os.path.join(args.outdir, 'interleaved.h264'),
         'wb').write(bytes(wire))
    open(os.path.join(args.outdir, 'main_only.h264'),
         'wb').write(b''.join(main_pkts))
    open(os.path.join(args.outdir, 'aux_only.h264'),
         'wb').write(aux_prefix + b''.join(aux_pkts))
    for k, p in enumerate(main_pkts):
        open(os.path.join(args.outdir, 'pkt_main_%03d.bin' % k),
             'wb').write(p)
    for k, p in enumerate(aux_pkts):
        open(os.path.join(args.outdir, 'pkt_aux_%03d.bin' % k),
             'wb').write(p)
    # order.json in the roundtrip-harness splice-contract format (a
    # bare list of {'v','f','frame_num'[,'idr']}); generator metadata
    # goes to meta.json
    open(os.path.join(args.outdir, 'order.json'),
         'w').write(json.dumps(order) + '\n')
    meta = {'child_log2_max_frame_num': log2,
            'out_log2_max_frame_num': OUT_LOG2,
            'aux_cadence': args.aux_cadence,
            'fault_retarget': args.fault_retarget}
    open(os.path.join(args.outdir, 'meta.json'),
         'w').write(json.dumps(meta, indent=1) + '\n')

    if args.emit_header:
        emit_header(args.emit_header, os.path.basename(args.main),
                    os.path.basename(args.aux), main_aus, aux_aus,
                    main_pkts, aux_pkts, win)

    print('spliced %d main + %d aux pictures (cadence %d, fault=%s)'
          % (len(main_pkts), len(aux_pkts), args.aux_cadence,
             args.fault_retarget))
    print('order: ' + ' '.join('%s%d' % (e['v'], e['frame_num'])
                               for e in order))


if __name__ == '__main__':
    main()
