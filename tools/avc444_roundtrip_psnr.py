#!/usr/bin/env python3
"""avc444_roundtrip_psnr.py — PRD FR-H264-8 "Semantic roundtrip PSNR
harness". Two tiers plus a packer selftest, one self-contained tool
(python3 + numpy + ffmpeg only).

Tier-A ("roundtrip"): SOURCE roundtrip. Generate a deterministic
synthetic RGB sequence (per-frame index markers in BOTH luma and an
iso-luminant chroma-only rendering — a pairing-skew detector — plus
chroma-rich moving content and smooth luma motion), convert with the
EXACT integer BT.709 full-range matrix of xrdp_avc444_convert.c, pack
into the two AVC444 v1 NV12 views with a numpy port of fill_main /
fill_aux (validated byte-exact against the in-tree C loops via
tools/avc444_pack_selftest.c — see the "selftest" subcommand), encode
each view with its own libx264 child, splice into one chain, decode in
BOTH client shapes and reconstruct 444:
  1-context: the interleaved feed through one decoder (MSTSC shape);
  2-context: main-only and aux-only feeds through separate decoders
             (macOS shape).
Per-frame per-plane PSNR vs the SOURCE YUV444 in both modes. PASS =
  (a) min per-plane PSNR >= band (band = leaf-baseline min - 2 dB,
      measured FIRST and recorded; pass it to later runs via --band);
  (b) per-frame |PSNR_1ctx - PSNR_2ctx| <= 0.01 dB (v1 + leaf splice
      should be pixel-identical across modes);
  (c) no monotonic decay: last-third mean >= first-third mean - 1 dB
      per chroma plane per mode (DPB drift signature; the first
      min(5, n/10) pairs are excluded — the crf IDR warm-up transient
      is +3..5 dB and would bias the first third);
  (d) complete frame counts in every feed (decoder starvation or
      interleave pairing skew is RED, not a crash). The 2-context aux
      decoder gets a fresh context per declared re-key ('idr' manifest
      mark): ffmpeg's h264 decoder permanently mutes output after the
      backwards frame_num jump a main-IDR restart imprints on the
      keyframe-less aux feed, while a real 2-context client re-keys
      its aux session there (and the FR-H264-8 aux chain re-seeds LT1
      there). Per-segment decode cannot mask in-segment starvation.
Under --aux-cadence K>1 the reference for a stale-aux pair is the
cadence-ideal reconstruction (main view of frame f + aux view of the
last aux'd frame), so PSNR measures encode+splice+decode fidelity, not
cadence-inherent staleness; at cadence 1 the reference IS the source
(the v1 packing is a lossless permutation, proven by selftest).
--faults is the sensitivity proof: three injected corruptions of the
spliced interleave (drop one aux AU with a stale manifest; swap two
adjacent main frame_num fields; corrupt 8 bytes mid-CABAC of one aux
slice — the latter a stand-in for an LTR retarget until the FR-H264-8
LTR splicer exists) must EACH turn the verdict RED.

Splice contract (--splice cmd "CMD"): the harness runs
    CMD main.h264 aux.h264 outdir
where main.h264/aux.h264 are the two child streams. CMD must write
into outdir: interleaved.h264 (the spliced single chain), main_only.h264
(feed for the 2-context main decoder), aux_only.h264 (feed for the
2-context aux decoder), and order.json — a JSON list, one entry per
VCL picture of interleaved.h264 in decode order, each {"v": "M"|"A",
"f": <source frame index>}; a mid-stream main IDR (re-key point) is
marked "idr": 1 on its main entry. If order.json is missing, strict
M,A alternation with f = pair index is assumed. This is how the FR-H264-8
LTR reference splicer (and later the real C tool) plugs in; --splice
leaf is the built-in FR-H264-7 all-intra leaf construction (the
validated aux_leaf_spike.py logic, fnrule prevref+1).

Tier-B ("wire", unchanged prototype): given an oracle AVC444 wire
capture, decode 1-context vs 2-context and report inter-mode per-frame
Y/U/V PSNR per view; RED on chroma collapse (< 40 dB), drift, or
per-view decode starvation; worst frame exported as a side-by-side
PNG. Assumes strict M,A alternation. Sensitivity proven on the
pre-fix t4_ps0 capture (RED) and the FR-H264-7 leaf wire (GREEN).

Usage:
  avc444_roundtrip_psnr.py wire <capture.bin> [outdir]
  avc444_roundtrip_psnr.py <capture.bin> [outdir]        (legacy Tier-B)
  avc444_roundtrip_psnr.py roundtrip --splice {leaf,cmd}
      [--splice-cmd 'CMD'] [--frames N] [--size WxH] [--aux-cadence K]
      [--idr-restart N] [--faults] [--band Y,U,V] [outdir]
  avc444_roundtrip_psnr.py selftest [--c-tool /path/to/pack_selftest]
"""
import argparse
import json
import math
import os
import shlex
import struct
import subprocess
import sys

CHROMA_MIN_DB = 40.0     # Tier-B chroma floor
DRIFT_DB = 3.0           # Tier-B drift allowance
EPS_DB = 0.01            # Tier-A max |1ctx - 2ctx| per frame/plane
TREND_DB = 1.0           # Tier-A chroma decay allowance
BAND_MARGIN_DB = 2.0     # Tier-A band = leaf baseline min - this
FFMPEG = os.environ.get('FFMPEG', 'ffmpeg')
SC = b'\x00\x00\x00\x01'


# ---------------------------------------------------------------- Tier-B ---

def nals_of(buf):
    out = []
    i = buf.find(b'\x00\x00\x01')
    while i >= 0:
        j = buf.find(b'\x00\x00\x01', i + 3)
        end = j if j >= 0 else len(buf)
        raw = buf[i + 3:end]
        if raw.endswith(b'\x00'):
            raw = raw[:-1]
        if raw:
            out.append(raw)
        i = j
    return out


def split_capture(path, work):
    """Oracle dump -> interleaved/main_only/aux_only annex-b + kinds."""
    data = open(path, 'rb').read()
    main_nals, aux_nals, kinds = [], [], []
    inter = bytearray()
    pos = 0
    while pos + 4 <= len(data):
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) != ln or ln < 8:
            sys.exit('bad record framing')
        (w,) = struct.unpack_from('<I', rec, 0)
        cb1 = w & 0x3FFFFFFF
        lc = (w >> 30) & 0x3
        (nrects,) = struct.unpack_from('<I', rec, 4)
        body = rec[8 + nrects * 10:]
        end1 = 4 + cb1 - (8 + nrects * 10)
        views = [('M', body)] if lc == 1 else [('A', body)] if lc == 2 \
            else [('M', body[:end1]), ('A', body[end1:])]
        for kind, v in views:
            for nal in nals_of(v):
                inter += b'\x00\x00\x00\x01' + nal
                t = nal[0] & 0x1F
                if kind == 'M':
                    main_nals.append(nal)
                else:
                    aux_nals.append(nal)
                if t in (1, 5):
                    kinds.append(kind)
    assert aux_nals, 'not an AVC444 interleave capture'
    assert all(k == 'MA'[i % 2] for i, k in enumerate(kinds)), \
        'prototype requires strict M,A alternation'
    sps = next(n for n in main_nals if (n[0] & 0x1F) == 7)
    pps = next(n for n in main_nals if (n[0] & 0x1F) == 8)
    with open(work + '/interleaved.h264', 'wb') as f:
        f.write(inter)
    with open(work + '/main_only.h264', 'wb') as f:
        for n in main_nals:
            f.write(b'\x00\x00\x00\x01' + n)
    with open(work + '/aux_only.h264', 'wb') as f:
        f.write(b'\x00\x00\x00\x01' + sps + b'\x00\x00\x00\x01' + pps)
        for n in aux_nals:
            if (n[0] & 0x1F) not in (7, 8):
                f.write(b'\x00\x00\x00\x01' + n)
    return kinds


def psnr_stats(work, view, parity):
    """psnr filter: interleave (every 2nd frame) vs per-view decode."""
    stats = '%s/psnr_%s.log' % (work, view)
    sel = 'not(mod(n\\,2))' if parity == 0 else 'mod(n\\,2)'
    cmd = [FFMPEG, '-hide_banner', '-loglevel', 'error',
           '-i', work + '/interleaved.h264',
           '-i', work + '/%s_only.h264' % view,
           '-lavfi',
           "[0:v]select='%s',setpts=N/25/TB[a];"
           "[1:v]setpts=N/25/TB[b];[a][b]psnr=f=%s" % (sel, stats),
           '-f', 'null', '-']
    subprocess.run(cmd, check=True, timeout=300)
    frames = []
    for line in open(stats):
        d = dict(kv.split(':') for kv in line.split())
        frames.append(tuple(
            float('inf') if d[k] == 'inf' else float(d[k])
            for k in ('psnr_y', 'psnr_u', 'psnr_v')))
    return frames


def fin(vals):
    v = [x for x in vals if x != float('inf')]
    return v


def report(view, frames):
    ys, us, vs = zip(*frames)
    red = []
    minu, minv = min(us), min(vs)
    for name, mn in (('U', minu), ('V', minv)):
        if mn < CHROMA_MIN_DB:
            red.append('min PSNR_%s %.2f dB < %.1f' % (name, mn,
                                                       CHROMA_MIN_DB))
    third = max(len(frames) // 3, 1)
    for name, series in (('U', us), ('V', vs)):
        head, tail = fin(series[:third]), fin(series[-third:])
        if head and tail:
            drop = (sum(head) / len(head)) - (sum(tail) / len(tail))
            if drop > DRIFT_DB:
                red.append('PSNR_%s drift -%.2f dB first->last third'
                           % (name, drop))
    fmt = lambda x: 'inf' if x == float('inf') else '%.2f' % x
    print('%s: %d frames  min Y/U/V = %s / %s / %s dB'
          % (view, len(frames), fmt(min(ys)), fmt(minu), fmt(minv)))
    worst = min(range(len(frames)), key=lambda i: min(frames[i][1],
                                                      frames[i][2]))
    return red, worst


def export_worst(work, outdir, view, parity, idx):
    n_inter = 2 * idx + parity
    out = '%s/worst_%s_f%03d_1ctx_vs_2ctx.png' % (outdir, view, idx)
    cmd = [FFMPEG, '-hide_banner', '-loglevel', 'error', '-y',
           '-i', work + '/interleaved.h264',
           '-i', work + '/%s_only.h264' % view,
           '-lavfi',
           "[0:v]select='eq(n\\,%d)'[a];[1:v]select='eq(n\\,%d)'[b];"
           "[a][b]hstack" % (n_inter, idx),
           '-frames:v', '1', out]
    subprocess.run(cmd, check=True, timeout=300)
    return out


def cmd_wire(argv):
    cap = argv[0]
    outdir = argv[1] if len(argv) > 1 else '/tmp/roundtrip_psnr'
    work = outdir + '/work'
    os.makedirs(work, exist_ok=True)
    kinds = split_capture(cap, work)
    pairs = kinds.count('M')
    print('%s: %d frame pairs' % (cap, pairs))
    fails = []
    for view, parity in (('main', 0), ('aux', 1)):
        frames = psnr_stats(work, view, parity)
        if len(frames) < pairs:
            # a per-view decoder that cannot even produce the frames
            # (no keyframe, missing references) is the strongest form
            # of 2-context corruption — report it, don't crash on it
            fails.append('%s: 2-context decode produced %d/%d frames '
                         '(decoder starved: missing keyframe/references)'
                         % (view, len(frames), pairs))
            print('%s: %d/%d frames decodable in 2-context mode'
                  % (view, len(frames), pairs))
            if not frames:
                continue
        red, worst = report(view, frames)
        fails += ['%s: %s' % (view, r) for r in red]
        if red:
            pic = export_worst(work, outdir, view, parity, worst)
            print('  evidence: %s (worst frame %d)' % (pic, worst))
    if fails:
        print('RED: semantic corruption detected')
        for f in fails:
            print('  ' + f)
        sys.exit(1)
    print('GREEN: both decode modes semantically equivalent '
          '(chroma >= %.1f dB, no drift)' % CHROMA_MIN_DB)


# --------------------------------------------------- bitstream primitives ---

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


def access_units(nals):
    """Group NALs into AUs: prefix non-VCL NALs attach to next VCL."""
    aus = []
    cur = []
    for n in nals:
        cur.append(n)
        if (n[0] & 0x1F) in (1, 5):
            aus.append(cur)
            cur = []
    assert not cur, 'trailing non-VCL NALs'
    return aus


def skip_scaling_list(r, size):
    last, nxt = 8, 8
    for _ in range(size):
        if nxt:
            nxt = (last + r.se() + 256) % 256
        if nxt:
            last = nxt


def parse_sps(nal):
    r = R(unescape(nal[1:]))
    s = {}
    s['profile_idc'] = r.u(8)
    r.u(8)
    s['level_idc'] = r.u(8)
    r.ue()  # sps_id
    s['chroma_format_idc'] = 1
    if s['profile_idc'] in (100, 110, 122, 244, 44, 83, 86,
                            118, 128, 138, 139, 134, 135):
        s['chroma_format_idc'] = r.ue()
        if s['chroma_format_idc'] == 3:
            r.u(1)
        r.ue()  # bit_depth_luma
        r.ue()  # bit_depth_chroma
        r.u(1)  # qpprime_y_zero
        if r.u(1):  # seq_scaling_matrix_present
            for i in range(8 if s['chroma_format_idc'] != 3 else 12):
                if r.u(1):
                    skip_scaling_list(r, 16 if i < 6 else 64)
    s['log2_max_frame_num'] = r.ue() + 4
    s['poc_type'] = r.ue()
    if s['poc_type'] == 0:
        r.ue()
    elif s['poc_type'] == 1:
        r.u(1)
        r.se()
        r.se()
        for _ in range(r.ue()):
            r.se()
    s['max_num_ref_frames'] = r.ue()
    s['gaps_in_frame_num_allowed'] = r.u(1)
    r.ue()  # pic_width_in_mbs
    r.ue()  # pic_height_in_map_units
    s['frame_mbs_only'] = r.u(1)
    return s


def parse_pps(nal):
    r = R(unescape(nal[1:]))
    p = {}
    r.ue()  # pps_id
    r.ue()  # sps_id
    p['entropy_cabac'] = r.u(1)
    r.u(1)  # bottom_field_pic_order_in_frame
    assert r.ue() == 0, 'num_slice_groups > 1 unsupported'
    r.ue()  # num_ref_idx_l0_default_active_minus1
    r.ue()  # num_ref_idx_l1_default_active_minus1
    p['weighted_pred'] = r.u(1)
    r.u(2)  # weighted_bipred_idc
    p['pic_init_qp'] = r.se() + 26
    r.se()  # pic_init_qs
    r.se()  # chroma_qp_index_offset
    p['deblock_ctrl'] = r.u(1)
    return p


def slice_fields(nal, log2):
    """first_mb, slice_type, frame_num of a VCL NAL."""
    r = R(unescape(nal[1:]))
    fmb = r.ue()
    st = r.ue()
    r.ue()  # pps_id
    return fmb, st, r.u(log2)


def set_frame_num(nal, log2, fn):
    """Bit-level frame_num rewrite (fixed width -> in-place on the RBSP)."""
    rbsp = bytearray(unescape(nal[1:]))
    r = R(rbsp)
    r.ue()
    r.ue()
    r.ue()
    for i in range(log2):
        bpos = r.p + i
        mask = 1 << (7 - (bpos & 7))
        if (fn >> (log2 - 1 - i)) & 1:
            rbsp[bpos >> 3] |= mask
        else:
            rbsp[bpos >> 3] &= 0xFF ^ mask
    return nal[:1] + escape(bytes(rbsp))


# --------------------------------------------- Tier-A source + v1 packing ---

MARK_BITS = 10                 # frame index marker width (mod 1024)
ISO_A = (128, 128, 128)        # Y=127 U=128 V=128 under the exact matrix
ISO_B = (44, 140, 255)         # Y=127 U=196 V=74: iso-luminant, chroma-only


def rgb_to_yuv444(rgb):
    """EXACT integer BT.709 full-range matrix of xrdp_avc444_convert.c
    (floor >>8 on negatives — numpy int32 >> is arithmetic == floor)."""
    import numpy as np
    r = rgb[..., 0].astype(np.int32)
    g = rgb[..., 1].astype(np.int32)
    b = rgb[..., 2].astype(np.int32)
    y = np.clip((54 * r + 183 * g + 18 * b) >> 8, 0, 255)
    u = np.clip(((-29 * r - 99 * g + 128 * b) >> 8) + 128, 0, 255)
    v = np.clip(((128 * r - 116 * g - 12 * b) >> 8) + 128, 0, 255)
    return (y.astype(np.uint8), u.astype(np.uint8), v.astype(np.uint8))


def gen_source(nframes, w, h):
    """Deterministic seeded frames: index markers (luma + iso-chroma),
    hue gradients + 1px checker (odd/even rows+cols), smooth luma blob
    (equal-RGB add is chroma-neutral under the matrix)."""
    import numpy as np
    xx, yy = np.meshgrid(np.arange(w), np.arange(h))
    rng = np.random.default_rng(0xA444)
    noise = rng.integers(0, 16, (h, w), dtype=np.int64)
    checker = ((xx ^ yy) & 1).astype(bool)
    bw = max(2, w // MARK_BITS)
    bh = max(2, h // 10)
    out = []
    for t in range(nframes):
        r = (xx * 5 + t * 7 + noise) % 256
        g = (yy * 3 + t * 5) % 256
        b = ((xx + yy) * 4 + t * 11) % 256
        b = np.where(checker, 255 - b, b)
        cx, cy = (t * 3) % w, (t * 2) % h
        blob = np.clip(120 - ((xx - cx) ** 2 + (yy - cy) ** 2) // 2, 0, 120)
        rgb = np.clip(np.stack([r, g, b], -1) + blob[..., None],
                      0, 255).astype(np.uint8)
        for i in range(MARK_BITS):
            if (i + 1) * bw > w:
                break
            bit = (t >> (MARK_BITS - 1 - i)) & 1
            rgb[0:bh, i * bw:(i + 1) * bw] = \
                (255, 255, 255) if bit else (0, 0, 0)
            rgb[bh:2 * bh, i * bw:(i + 1) * bw] = ISO_B if bit else ISO_A
        out.append(rgb_to_yuv444(rgb))
    return out


def coded_dims(w, h, walign=32):
    return (w + walign - 1) & ~(walign - 1), (h + 15) & ~15


def pack_v1(Y, U, V, cw, ch):
    """numpy port of fill_main/fill_aux (v1) — edge-replicated padding
    == sample_yuv clamping. Returns (mY, mUV, aY, aUV) uint8 arrays."""
    import numpy as np
    h, w = Y.shape
    pad = lambda p: np.pad(p, ((0, ch - h), (0, cw - w)), mode='edge')
    Yp, Up, Vp = pad(Y), pad(U), pad(V)
    mY = Yp.copy()
    mUV = np.empty((ch // 2, cw), np.uint8)
    mUV[:, 0::2] = Up[0::2, 0::2]      # v1: POINT sample at (even,even)
    mUV[:, 1::2] = Vp[0::2, 0::2]
    aY = np.zeros((ch, cw), np.uint8)
    uc = vc = 0
    for y in range(ch):                # 16-row bands: 8 U rows, 8 V rows
        use_u = (y % 16) < 8
        pos = 2 * (uc if use_u else vc) + 1
        if use_u:
            uc += 1
        else:
            vc += 1
        if pos >= h:                   # filler: replicate previous row
            if y > 0:
                aY[y] = aY[y - 1]
        else:
            aY[y] = (Up if use_u else Vp)[pos]
    aUV = np.empty((ch // 2, cw), np.uint8)
    aUV[:, 0::2] = Up[0::2, 1::2]      # (odd col, even row)
    aUV[:, 1::2] = Vp[0::2, 1::2]
    return mY, mUV, aY, aUV


def unpack_v1(mY, mUV, aY, aUV, w, h):
    """Inverse of pack_v1 on the actual region (lossless permutation)."""
    import numpy as np
    ch, cw = mY.shape
    U = np.zeros((ch, cw), np.uint8)
    V = np.zeros((ch, cw), np.uint8)
    U[0::2, 0::2] = mUV[:, 0::2]
    V[0::2, 0::2] = mUV[:, 1::2]
    U[0::2, 1::2] = aUV[:, 0::2]
    V[0::2, 1::2] = aUV[:, 1::2]
    uc = vc = 0
    for y in range(ch):
        use_u = (y % 16) < 8
        pos = 2 * (uc if use_u else vc) + 1
        if use_u:
            uc += 1
        else:
            vc += 1
        if pos < h:
            (U if use_u else V)[pos] = aY[y]
    return mY[:h, :w].copy(), U[:h, :w], V[:h, :w]


# ------------------------------------------------- Tier-A encode + splice ---

X264_PARAMS = ('cabac=1:weightp=0:scenecut=0:threads=1:'
               'sliced-threads=0:repeat-headers=0')


def sps_log2_bitspan(rbsp):
    """(start,end) bit positions of log2_max_frame_num_minus4 ue."""
    r = R(rbsp)
    profile = r.u(8)
    r.u(16)
    r.ue()  # sps_id
    if profile in (100, 110, 122, 244, 44, 83, 86,
                   118, 128, 138, 139, 134, 135):
        chroma = r.ue()
        if chroma == 3:
            r.u(1)
        r.ue()
        r.ue()
        r.u(1)
        if r.u(1):
            for i in range(8 if chroma != 3 else 12):
                if r.u(1):
                    skip_scaling_list(r, 16 if i < 6 else 64)
    p0 = r.p
    r.ue()
    return p0, r.p


def rewrite_sps_log2(nal, log2_new):
    """Full unescape -> bit-copy -> re-escape; the ue length change
    shifts all later bits (poc_type=2: nothing else references it)."""
    rbsp = unescape(nal[1:])
    p0, p1 = sps_log2_bitspan(rbsp)
    r = R(rbsp)
    w = W()
    w.copy_bits(r, p0)
    w.ue(log2_new - 4)
    r.p = p1
    w.copy_bits(r, len(rbsp) * 8 - p1)
    return nal[:1] + escape(w.tobytes())


def widen_slice_fn(nal, l2o, l2n, prev8, deblock_ctrl):
    """Rewrite one VCL slice header with frame_num re-numbered at the
    wider field (frozen field order, poc_type=2, CABAC); alignment
    re-padded, CABAC payload byte-verbatim. Fail-loud outside the
    guard."""
    idr = (nal[0] & 0x1F) == 5
    nri = (nal[0] >> 5) & 3
    assert idr or nri > 0, 'non-ref main slice unsupported'
    rbsp = unescape(nal[1:])
    r = R(rbsp)
    w = W()
    fmb = r.ue()
    st = r.ue()
    pps_id = r.ue()
    fn_old = r.u(l2o)
    fn8 = 0 if idr else (prev8 + 1) % (1 << l2n)
    assert fn8 % (1 << l2o) == fn_old, \
        'frame_num renumber mismatch: child %d vs %d' % (fn_old, fn8)
    w.ue(fmb)
    w.ue(st)
    w.ue(pps_id)
    w.u(fn8, l2n)
    hdr_start = r.p
    if idr:
        assert fn_old == 0, 'IDR frame_num != 0'
        r.ue()                 # idr_pic_id
        r.u(2)                 # no_output_of_prior_pics, long_term_ref
    else:
        assert st % 5 == 0, 'main slice not P: %d' % st
        assert r.u(1) == 0, 'num_ref_idx override unsupported'
        assert r.u(1) == 0, 'pre-existing list modification unsupported'
        assert r.u(1) == 0, 'adaptive marking unsupported (x264 emits ' \
            'sliding window)'
        r.ue()                 # cabac_init_idc
    r.se()                     # slice_qp_delta
    if deblock_ctrl:
        if r.ue() != 1:
            r.se()
            r.se()
    hdr_end = r.p
    r.p = hdr_start
    w.copy_bits(r, hdr_end - hdr_start)
    while r.p & 7:
        assert r.u(1) == 1, 'bad cabac alignment bit'
    while len(w.bits) & 7:
        w.u(1, 1)
    return nal[:1] + escape(w.tobytes() + rbsp[r.p >> 3:]), fn8


def widen_stream(data, l2o, l2n, deblock_ctrl):
    """x264 core 164 pins log2_max_frame_num=4 regardless of -g /
    keyint / stitchable (measured 2026-07-28); the frozen recipe
    requires >= 8, so widen SPS + all slice frame_nums bit-exactly.
    Caller must prove decode identity vs the original stream."""
    out = bytearray()
    prev8 = None
    for nal in nals_of(data):
        t = nal[0] & 0x1F
        if t == 7:
            out += SC + rewrite_sps_log2(nal, l2n)
        elif t in (1, 5):
            wn, prev8 = widen_slice_fn(nal, l2o, l2n, prev8,
                                       deblock_ctrl)
            out += SC + wn
        else:
            out += SC + nal
    return bytes(out)


def encode_child(raw_path, cw, ch, out_path, nframes, gop, keyexpr=None):
    """libx264 child per the frozen recipe; if x264 emits
    log2_max_frame_num < 8 the stream is widened (see widen_stream)
    and the widening is PROVEN decode-identical before use."""
    cmd = [FFMPEG, '-hide_banner', '-loglevel', 'error', '-y',
           '-f', 'rawvideo', '-pix_fmt', 'nv12',
           '-s', '%dx%d' % (cw, ch), '-r', '25', '-i', raw_path,
           '-c:v', 'libx264', '-preset', 'veryfast',
           '-tune', 'zerolatency', '-crf', '23', '-bf', '0',
           '-refs', '1', '-g', str(gop), '-x264-params', X264_PARAMS]
    if keyexpr:
        cmd += ['-force_key_frames', keyexpr]
    # repeat-headers=0 keeps SPS/PPS out of the packets (they ship
    # once per the recipe); without global_header the raw muxer would
    # then emit none at all, so put them in extradata and re-inject
    cmd += ['-flags:v', '+global_header',
            '-bsf:v', 'dump_extra=freq=keyframe', '-f', 'h264',
            out_path]
    subprocess.run(cmd, check=True, timeout=300)
    data = open(out_path, 'rb').read()
    nals = nals_of(data)
    sps = parse_sps(next(n for n in nals if (n[0] & 0x1F) == 7))
    pps = parse_pps(next(n for n in nals if (n[0] & 0x1F) == 8))
    n_vcl = sum(1 for n in nals if (n[0] & 0x1F) in (1, 5))
    if n_vcl != nframes:
        sys.exit('STOP: %s emitted %d VCL AUs for %d frames'
                 % (out_path, n_vcl, nframes))
    widened = False
    if sps['log2_max_frame_num'] < 8:
        wdata = widen_stream(data, sps['log2_max_frame_num'], 8,
                             pps['deblock_ctrl'])
        wpath = out_path + '.widened'
        open(wpath, 'wb').write(wdata)
        work = os.path.dirname(out_path)
        tag = os.path.basename(out_path).split('.')[0]
        oY, oUV = decode_feed(out_path, cw, ch, tag + '_orig', work)
        wY, wUV = decode_feed(wpath, cw, ch, tag + '_wide', work)
        if not (len(oY) == len(wY) == nframes
                and (oY == wY).all() and (oUV == wUV).all()):
            sys.exit('STOP: log2 widening of %s is NOT decode-'
                     'identical (%d vs %d frames)'
                     % (out_path, len(oY), len(wY)))
        data = wdata
        open(out_path, 'wb').write(data)
        sps = parse_sps(next(n for n in nals_of(data)
                             if (n[0] & 0x1F) == 7))
        widened = True
    if sps['log2_max_frame_num'] < 8:
        sys.exit('STOP: log2_max_frame_num=%d < 8 after widening'
                 % sps['log2_max_frame_num'])
    return data, sps, pps, widened


def assert_stream_shape(name, sps, pps):
    want = [('poc_type', sps['poc_type'] == 2, sps['poc_type']),
            ('log2_max_frame_num>=8', sps['log2_max_frame_num'] >= 8,
             sps['log2_max_frame_num']),
            ('gaps_in_frame_num_allowed==0',
             sps['gaps_in_frame_num_allowed'] == 0,
             sps['gaps_in_frame_num_allowed']),
            ('frame_mbs_only', sps['frame_mbs_only'] == 1,
             sps['frame_mbs_only']),
            ('chroma_format_idc==1', sps['chroma_format_idc'] == 1,
             sps['chroma_format_idc']),
            ('CABAC', pps['entropy_cabac'] == 1, pps['entropy_cabac']),
            ('weighted_pred==0', pps['weighted_pred'] == 0,
             pps['weighted_pred'])]
    bad = ['%s=%s' % (k, v) for k, ok, v in want if not ok]
    if bad:
        sys.exit('STOP: %s child stream has wrong shape: %s'
                 % (name, ', '.join(bad)))


def assert_children_compat(msps, mpps, asps, apps):
    keys = [('log2_max_frame_num', msps, asps),
            ('poc_type', msps, asps),
            ('entropy_cabac', mpps, apps),
            ('weighted_pred', mpps, apps),
            ('deblock_ctrl', mpps, apps),
            ('pic_init_qp', mpps, apps)]
    bad = ['%s %s!=%s' % (k, a[k], b[k]) for k, a, b in keys
           if a[k] != b[k]]
    if bad:
        sys.exit('STOP: child SPS/PPS mismatch: %s' % ', '.join(bad))


def idr_to_leaf(nal, new_fn, log2, deblock_ctrl):
    """FR-H264-7 leaf conversion (aux_leaf_spike.py logic, generalized):
    IDR -> non-ref non-IDR I slice; drop idr_pic_id + 2-bit marking; set
    frame_num; re-pad cabac alignment; CABAC payload byte-verbatim."""
    assert (nal[0] & 0x1F) == 5, 'not IDR'
    rbsp = unescape(nal[1:])
    r = R(rbsp)
    w = W()
    first_mb = r.ue()
    stype = r.ue()
    pps_id = r.ue()
    r.u(log2)                  # child frame_num, discarded
    assert stype % 5 == 2, 'aux slice not I: %d' % stype
    r.ue()                     # idr_pic_id, dropped
    r.u(2)                     # no_output_of_prior_pics, long_term_ref
    w.ue(first_mb)
    w.ue(stype)
    w.ue(pps_id)
    w.u(new_fn, log2)
    hdr_start = r.p
    r.se()                     # slice_qp_delta
    if deblock_ctrl:
        if r.ue() != 1:        # disable_deblocking_filter_idc
            r.se()
            r.se()
    hdr_end = r.p
    r.p = hdr_start
    w.copy_bits(r, hdr_end - hdr_start)
    while r.p & 7:
        assert r.u(1) == 1, 'bad cabac alignment bit'
    while len(w.bits) & 7:
        w.u(1, 1)
    return bytes([0x01]) + escape(w.tobytes() + rbsp[r.p >> 3:])


def splice_leaf(main_bytes, aux_bytes, work, cadence, log2, deblock_ctrl):
    """Build interleaved/main_only/aux_only + order.json (leaf mode)."""
    m_aus = access_units(nals_of(main_bytes))
    a_aus = access_units(nals_of(aux_bytes))
    assert len(m_aus) == len(a_aus), 'child AU counts differ: %d vs %d' \
        % (len(m_aus), len(a_aus))
    sps = next(n for au in m_aus for n in au if (n[0] & 0x1F) == 7)
    pps = next(n for au in m_aus for n in au if (n[0] & 0x1F) == 8)
    inter = bytearray()
    aux_only = bytearray(SC + sps + SC + pps)
    order = []
    prev_fn = None
    for k, au in enumerate(m_aus):
        is_idr = False
        for nal in au:
            if (nal[0] & 0x1F) in (1, 5):
                fmb, _, prev_fn = slice_fields(nal, log2)
                assert fmb == 0, 'multi-slice main picture'
                is_idr = (nal[0] & 0x1F) == 5
            inter += SC + nal
        ent = {'v': 'M', 'f': k}
        if is_idr and k > 0:
            ent['idr'] = 1     # re-key point: 2-ctx aux context restarts
        order.append(ent)
        if k % cadence == 0:
            idrs = [x for x in a_aus[k] if (x[0] & 0x1F) == 5]
            assert len(idrs) == 1, 'aux AU %d: %d IDR NALs' \
                % (k, len(idrs))
            leaf = idr_to_leaf(idrs[0], (prev_fn + 1) % (1 << log2),
                               log2, deblock_ctrl)
            inter += SC + leaf
            aux_only += SC + leaf
            order.append({'v': 'A', 'f': k})
    open(work + '/interleaved.h264', 'wb').write(bytes(inter))
    open(work + '/main_only.h264', 'wb').write(main_bytes)
    open(work + '/aux_only.h264', 'wb').write(bytes(aux_only))
    json.dump(order, open(work + '/order.json', 'w'))
    return order


def splice_external(cmdline, main_path, aux_path, work, npairs):
    """--splice cmd: run 'CMD main.h264 aux.h264 outdir' (see docstring)."""
    subprocess.run(shlex.split(cmdline) + [main_path, aux_path, work],
                   check=True, timeout=300)
    for f in ('interleaved.h264', 'main_only.h264', 'aux_only.h264'):
        if not os.path.exists(work + '/' + f):
            sys.exit('STOP: splice cmd did not write %s' % f)
    op = work + '/order.json'
    if os.path.exists(op):
        return json.load(open(op))
    order = []
    for k in range(npairs):        # contract fallback: strict M,A
        order.append({'v': 'M', 'f': k})
        order.append({'v': 'A', 'f': k})
    return order


# ------------------------------------------- Tier-A decode + PSNR verdict ---

def decode_feed(path, cw, ch, tag, work):
    """ffmpeg decode -> (Y[n,ch,cw], UV[n,ch/2,cw]); no check=True: a
    failed/partial decode surfaces as a frame-count RED, not a crash."""
    import numpy as np
    out = '%s/dec_%s.nv12' % (work, tag)
    p = subprocess.run([FFMPEG, '-hide_banner', '-loglevel', 'error',
                        '-y', '-i', path, '-f', 'rawvideo',
                        '-pix_fmt', 'nv12', out],
                       capture_output=True, timeout=300)
    if p.returncode:
        print('  [%s] decoder exit %d: %s'
              % (tag, p.returncode,
                 p.stderr.decode('utf-8', 'replace').strip()[-200:]))
    data = open(out, 'rb').read() if os.path.exists(out) else b''
    fsz = cw * ch * 3 // 2
    n = len(data) // fsz
    arr = np.frombuffer(data[:n * fsz], np.uint8) \
        .reshape(n, ch * 3 // 2, cw)
    return arr[:, :ch, :], arr[:, ch:, :]


def psnr(a, b):
    import numpy as np
    d = a.astype(np.float64) - b.astype(np.float64)
    mse = float(np.mean(d * d))
    return float('inf') if mse == 0 else 10 * math.log10(255.0 ** 2 / mse)


def decode_aux_segmented(path, order, cw, ch, work, label):
    """2-context aux decode with a FRESH decoder context per declared
    re-key ('idr':1 main manifest entries). Needed because ffmpeg's
    h264 decoder permanently stops emitting output after the backwards
    frame_num jump a main-IDR restart imprints on a keyframe-less
    all-non-ref aux feed (measured: 'Frame num gap 1 255', 29/60
    frames silently swallowed) even though every leaf is self-
    contained intra; a real 2-context client re-keys its aux session
    at these points, and the FR-H264-8 aux chain re-seeds LT1 there,
    so per-segment decode is architecturally faithful and cannot mask
    genuine reference starvation inside a segment."""
    import numpy as np
    auxs = [i for i, e in enumerate(order) if e['v'] == 'A']
    starts = set()
    prev = -1
    for j, pi in enumerate(auxs):
        if j == 0 or any(order[i].get('idr') for i in range(prev, pi)
                         if order[i]['v'] == 'M'):
            starts.add(j)
        prev = pi
    aus = access_units(nals_of(open(path, 'rb').read()))
    assert len(aus) == len(auxs), 'aux feed has %d pictures, manifest %d' \
        % (len(aus), len(auxs))
    prefix = b''.join(SC + n for n in aus[0][:-1]
                      if (n[0] & 0x1F) in (7, 8))
    segs = []
    for j in range(len(auxs)):
        if j in starts:
            segs.append([])
        segs[-1].append(j)
    if len(segs) > 1:
        print('  %s: aux-only decoded in %d segments (re-key at aux '
              'index %s)' % (label, len(segs),
                             sorted(starts - {0})))
    ys, uvs = [], []
    for si, seg in enumerate(segs):
        sp = '%s/aux_seg%d_%s.h264' % (work, si, label)
        with open(sp, 'wb') as f:
            if si > 0:
                f.write(prefix)
            for j in seg:
                f.write(b''.join(SC + n for n in aus[j]))
        y, uv = decode_feed(sp, cw, ch, 'aux_seg%d_%s' % (si, label),
                            work)
        ys.append(y)
        uvs.append(uv)
    return np.concatenate(ys), np.concatenate(uvs)


def evaluate(feeds, order, src, packs, w, h, cw, ch, band, label, outdir):
    """Decode all three feeds, reconstruct both modes, verdict.
    Returns (fails, band) — band computed+recorded if passed as None."""
    iY, iUV = decode_feed(feeds['inter'], cw, ch, label + '_i',
                          os.path.dirname(feeds['inter']))
    mY_d, mUV_d = decode_feed(feeds['main'], cw, ch, label + '_m',
                              os.path.dirname(feeds['main']))
    aY_d, aUV_d = decode_aux_segmented(feeds['aux'], order, cw, ch,
                                       os.path.dirname(feeds['aux']),
                                       label)
    mains = [i for i, e in enumerate(order) if e['v'] == 'M']
    auxs = [i for i, e in enumerate(order) if e['v'] == 'A']
    fails = []
    if len(iY) != len(order):
        fails.append('1-context decode %d/%d frames (interleave '
                     'incomplete / pairing skew)' % (len(iY), len(order)))
    if len(mY_d) != len(mains):
        fails.append('2-context main decode %d/%d frames (starved)'
                     % (len(mY_d), len(mains)))
    if len(aY_d) != len(auxs):
        fails.append('2-context aux decode %d/%d frames (starved)'
                     % (len(aY_d), len(auxs)))
    # per pair: aux partner = last aux'd source frame <= this frame
    refs = {}
    series = {'1ctx': [], '2ctx': []}
    rows = []
    for p, oi in enumerate(mains):
        f = order[oi]['f']
        ai = max((i for i in range(len(auxs))
                  if order[auxs[i]]['f'] <= f), default=None)
        if ai is None:
            continue
        af = order[auxs[ai]]['f']
        key = (f, af)
        if key not in refs:
            if af == f:
                refs[key] = src[f]      # cadence 1: reference == SOURCE
            else:                       # cadence-ideal reconstruction
                mp, ap_ = packs[f], packs[af]
                refs[key] = unpack_v1(mp[0], mp[1], ap_[2], ap_[3], w, h)
        ref = refs[key]
        row = [p, f]
        for mode in ('1ctx', '2ctx'):
            if mode == '1ctx':      # both views out of the one decoder
                MY, MUV, AY, AUV = iY, iUV, iY, iUV
                mi, xi = oi, auxs[ai]
            else:                   # separate main/aux decoders
                MY, MUV, AY, AUV = mY_d, mUV_d, aY_d, aUV_d
                mi, xi = p, ai
            if mi >= len(MY) or xi >= len(AY):
                row += [None] * 3
                continue
            ry, ru, rv = unpack_v1(MY[mi], MUV[mi], AY[xi], AUV[xi],
                                   w, h)
            tri = (psnr(ry, ref[0]), psnr(ru, ref[1]), psnr(rv, ref[2]))
            series[mode].append(tri)
            row += list(tri)
        rows.append(row)
    tsv = '%s/psnr_%s.tsv' % (outdir, label)
    with open(tsv, 'w') as fh:
        fh.write('pair\tframe\ty1\tu1\tv1\ty2\tu2\tv2\n')
        for row in rows:
            fh.write('\t'.join('' if x is None else str(x)
                               for x in row) + '\n')
    if not series['1ctx'] or not series['2ctx']:
        fails.append('no frames reconstructable in one or both modes')
        return fails, band
    mins = {}
    for pi, pl in enumerate('YUV'):
        mins[pl] = min(min(fr[pi] for fr in series[m])
                       for m in ('1ctx', '2ctx') if series[m])
    fmt = lambda x: 'inf' if x == float('inf') else '%.2f' % x
    for m in ('1ctx', '2ctx'):
        s = series[m]
        print('  %s [%s]: %d frames  min Y/U/V = %s / %s / %s dB'
              % (label, m, len(s), *(fmt(min(fr[i] for fr in s))
                                     for i in range(3))))
    if band is None:
        band = {pl: (mins[pl] - BAND_MARGIN_DB
                     if mins[pl] != float('inf') else 60.0)
                for pl in 'YUV'}
        json.dump(band, open(outdir + '/band.json', 'w'))
        print('  BAND recorded (leaf baseline min - %.1f dB): '
              'Y/U/V = %.2f / %.2f / %.2f  -> %s/band.json'
              % (BAND_MARGIN_DB, band['Y'], band['U'], band['V'], outdir))
    else:
        for pl in 'YUV':
            if mins[pl] < band[pl]:
                fails.append('min PSNR_%s %.2f dB < band %.2f'
                             % (pl, mins[pl], band[pl]))
    n = min(len(series['1ctx']), len(series['2ctx']))
    worst_d = 0.0
    for i in range(n):
        for pi, pl in enumerate('YUV'):
            a, b = series['1ctx'][i][pi], series['2ctx'][i][pi]
            d = 0.0 if a == b else abs(a - b)
            worst_d = max(worst_d, d)
            if d > EPS_DB:
                fails.append('frame %d PSNR_%s 1ctx/2ctx differ by '
                             '%s dB > %.2f' % (i, pl, fmt(d), EPS_DB))
                break
        else:
            continue
        break
    print('  %s: max |1ctx-2ctx| = %s dB' % (label, fmt(worst_d)))
    for m in ('1ctx', '2ctx'):
        # exclude the crf IDR warm-up (x264 gives the IDR-adjacent
        # frames a measured +3..5 dB transient which biases the first
        # third and false-flags decay); monotonic DPB drift spans the
        # whole sequence and still lands squarely in the comparison
        s = series[m][min(5, len(series[m]) // 10):]
        third = max(len(s) // 3, 1)
        for pi, pl in ((1, 'U'), (2, 'V')):
            head = fin([fr[pi] for fr in s[:third]])
            tail = fin([fr[pi] for fr in s[-third:]])
            if head and tail:
                drop = sum(head) / len(head) - sum(tail) / len(tail)
                if drop > TREND_DB:
                    fails.append('%s PSNR_%s decay -%.2f dB '
                                 'first->last third' % (m, pl, drop))
    return fails, band


# ------------------------------------------------- Tier-A fault injection ---

def build_faults(work, order, log2):
    """Three injected interleaves (sensitivity proof). Returns
    [(name, path, description)]."""
    data = open(work + '/interleaved.h264', 'rb').read()
    nals = nals_of(data)
    vcl = [i for i, n in enumerate(nals) if (n[0] & 0x1F) in (1, 5)]
    assert len(vcl) == len(order), 'interleave VCL count != manifest'
    auxs = [i for i, e in enumerate(order) if e['v'] == 'A']
    mains = [i for i, e in enumerate(order) if e['v'] == 'M']
    out = []

    def emit(name, nal_list, desc):
        p = '%s/interleaved_%s.h264' % (work, name)
        open(p, 'wb').write(b''.join(SC + n for n in nal_list))
        out.append((name, p, desc))

    # (i) drop one mid-stream aux AU; manifest deliberately NOT fixed
    k = auxs[len(auxs) // 2]
    drop = vcl[k]
    emit('dropaux', [n for i, n in enumerate(nals) if i != drop],
         'dropped aux AU of pair f=%d (manifest stale)' % order[k]['f'])
    # (ii) swap frame_num of two adjacent mid-stream main slices
    p1 = len(mains) // 2
    i1, i2 = vcl[mains[p1]], vcl[mains[p1 + 1]]
    assert (nals[i1][0] & 0x1F) == 1 and (nals[i2][0] & 0x1F) == 1, \
        'fault(ii) picked an IDR; choose another position'
    fn1 = slice_fields(nals[i1], log2)[2]
    fn2 = slice_fields(nals[i2], log2)[2]
    swapped = list(nals)
    swapped[i1] = set_frame_num(nals[i1], log2, fn2)
    swapped[i2] = set_frame_num(nals[i2], log2, fn1)
    emit('swapfn', swapped,
         'swapped frame_num %d<->%d of main pairs %d,%d'
         % (fn1, fn2, p1, p1 + 1))
    # (iii) corrupt 8 bytes mid-CABAC of one aux slice (stand-in for an
    # LTR retarget until the FR-H264-8 LTR splicer exists)
    k3 = vcl[auxs[len(auxs) // 2]]
    rbsp = bytearray(unescape(nals[k3][1:]))
    mid = max(16, len(rbsp) // 2)
    for i in range(mid, min(mid + 8, len(rbsp))):
        v = rbsp[i] ^ 0xAA
        rbsp[i] = v if v > 3 else 0xAA   # keep emulation-safe pre-escape
    corrupted = list(nals)
    corrupted[k3] = nals[k3][:1] + escape(bytes(rbsp))
    emit('corrupt', corrupted,
         'XORed 8 bytes at RBSP offset %d of aux pair f=%d'
         % (mid, order[auxs[len(auxs) // 2]]['f']))
    return out


# ------------------------------------------------------- Tier-A driver ---

def parse_band(s):
    v = [float(x) for x in s.split(',')]
    assert len(v) == 3, '--band wants Y,U,V'
    return dict(zip('YUV', v))


def cmd_roundtrip(argv):
    ap = argparse.ArgumentParser(
        prog='avc444_roundtrip_psnr.py roundtrip')
    ap.add_argument('--splice', choices=['leaf', 'cmd'], required=True)
    ap.add_argument('--splice-cmd')
    ap.add_argument('--frames', type=int, default=60)
    ap.add_argument('--size', default='64x64')
    ap.add_argument('--aux-cadence', type=int, default=1)
    ap.add_argument('--gop', type=int, default=240,
                    help='child GOP; > 256 lets leaf-mode frame_num '
                    'actually cross the mod-256 wrap')
    ap.add_argument('--idr-restart', type=int)
    ap.add_argument('--faults', action='store_true')
    ap.add_argument('--band', help='Y,U,V dB from the leaf baseline run')
    ap.add_argument('outdir', nargs='?', default='/tmp/roundtrip_semantic')
    a = ap.parse_args(argv)
    if a.splice == 'cmd' and not a.splice_cmd:
        ap.error('--splice cmd requires --splice-cmd')
    w, h = (int(x) for x in a.size.split('x'))
    cw, ch = coded_dims(w, h)
    work = a.outdir + '/work'
    os.makedirs(work, exist_ok=True)
    print('roundtrip: %dx%d (coded %dx%d) %d frames, splice=%s, '
          'cadence=%d%s' % (w, h, cw, ch, a.frames, a.splice,
                            a.aux_cadence,
                            ', idr-restart=%d' % a.idr_restart
                            if a.idr_restart is not None else ''))
    src = gen_source(a.frames, w, h)
    packs = [pack_v1(Y, U, V, cw, ch) for (Y, U, V) in src]
    with open(work + '/main_in.nv12', 'wb') as fm, \
            open(work + '/aux_in.nv12', 'wb') as fa:
        for mY, mUV, aY, aUV in packs:
            fm.write(mY.tobytes() + mUV.tobytes())
            fa.write(aY.tobytes() + aUV.tobytes())
    mkey = ('expr:eq(n,%d)' % a.idr_restart
            if a.idr_restart is not None else None)
    akey = 'expr:gte(t,0)' if a.splice == 'leaf' else None
    m_bytes, msps, mpps, mwide = encode_child(
        work + '/main_in.nv12', cw, ch, work + '/main.h264',
        a.frames, a.gop, mkey)
    a_bytes, asps, apps, awide = encode_child(
        work + '/aux_in.nv12', cw, ch, work + '/aux.h264',
        a.frames, a.gop, akey)
    assert_stream_shape('main', msps, mpps)
    assert_stream_shape('aux', asps, apps)
    assert_children_compat(msps, mpps, asps, apps)
    log2 = msps['log2_max_frame_num']
    print('children ok: -g %d, log2_max_frame_num=%d, poc_type=2, '
          'CABAC, weightp=0%s'
          % (a.gop, log2,
             ' [x264 emitted log2=4; SPS+frame_num widened to 8, '
             'proven decode-identical]' if (mwide or awide) else ''))
    if a.splice == 'leaf':
        order = splice_leaf(m_bytes, a_bytes, work, a.aux_cadence,
                            log2, mpps['deblock_ctrl'])
    else:
        order = splice_external(a.splice_cmd, work + '/main.h264',
                                work + '/aux.h264', work, a.frames)
    band = parse_band(a.band) if a.band else None
    feeds = {'inter': work + '/interleaved.h264',
             'main': work + '/main_only.h264',
             'aux': work + '/aux_only.h264'}
    fails, band = evaluate(feeds, order, src, packs, w, h, cw, ch,
                           band, 'clean', a.outdir)
    if fails:
        print('RED: semantic corruption detected')
        for f in fails:
            print('  ' + f)
        sys.exit(1)
    print('GREEN: roundtrip in band, modes equivalent, no decay')
    if not a.faults:
        return
    print('--- fault injection (sensitivity proof) ---')
    all_red = True
    for name, path, desc in build_faults(work, order, log2):
        print('fault %s: %s' % (name, desc))
        ffails, _ = evaluate({'inter': path, 'main': feeds['main'],
                              'aux': feeds['aux']}, order, src, packs,
                             w, h, cw, ch, band, name, a.outdir)
        if ffails:
            print('  RED as required — caught by:')
            for f in ffails:
                print('    ' + f)
        else:
            print('  *** STAYED GREEN — harness NOT sensitive to %s ***'
                  % name)
            all_red = False
    if not all_red:
        print('SENSITIVITY FAIL: at least one fault went undetected')
        sys.exit(1)
    print('SENSITIVITY OK: all 3 injected faults turned RED')


# ----------------------------------------------------------- selftest ---

def cmd_selftest(argv):
    ap = argparse.ArgumentParser(
        prog='avc444_roundtrip_psnr.py selftest')
    ap.add_argument('--c-tool', help='compiled avc444_pack_selftest '
                    'binary for byte-exact C comparison')
    a = ap.parse_args(argv)
    import numpy as np
    rng = np.random.default_rng(4444)
    ok = True
    for w, h in ((70, 34), (152, 90), (64, 64)):
        cw, ch = coded_dims(w, h)
        Y, U, V = (rng.integers(0, 256, (h, w), dtype=np.uint8)
                   for _ in range(3))
        mY, mUV, aY, aUV = pack_v1(Y, U, V, cw, ch)
        iy, iu, iv = unpack_v1(mY, mUV, aY, aUV, w, h)
        ident = (np.array_equal(iy, Y) and np.array_equal(iu, U)
                 and np.array_equal(iv, V))
        print('%3dx%-3d (coded %3dx%-3d) pack->unpack identity: %s'
              % (w, h, cw, ch, 'PASS' if ident else 'FAIL'))
        ok &= ident
        if not a.c_tool:
            continue
        d = '/tmp/pack_selftest_%dx%d' % (w, h)
        os.makedirs(d, exist_ok=True)
        open(d + '/in.yuv444', 'wb').write(
            Y.tobytes() + U.tobytes() + V.tobytes())
        subprocess.run([a.c_tool, str(w), str(h), d + '/in.yuv444',
                        d + '/c_main.nv12', d + '/c_aux.nv12'],
                       check=True, timeout=60)
        cm = open(d + '/c_main.nv12', 'rb').read()
        ca = open(d + '/c_aux.nv12', 'rb').read()
        em = mY.tobytes() + mUV.tobytes()
        ea = aY.tobytes() + aUV.tobytes()
        bm = 'PASS' if cm == em else 'FAIL (%d/%d bytes differ)' % (
            sum(x != y for x, y in zip(cm, em)), len(em))
        ba = 'PASS' if ca == ea else 'FAIL (%d/%d bytes differ)' % (
            sum(x != y for x, y in zip(ca, ea)), len(ea))
        print('%3dx%-3d C-vs-numpy byte-exact: main %s  aux %s'
              % (w, h, bm, ba))
        ok &= (cm == em) and (ca == ea)
    if not ok:
        print('SELFTEST RED')
        sys.exit(1)
    print('SELFTEST GREEN')


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    sub = sys.argv[1]
    if sub == 'wire':
        cmd_wire(sys.argv[2:])
    elif sub == 'roundtrip':
        cmd_roundtrip(sys.argv[2:])
    elif sub == 'selftest':
        cmd_selftest(sys.argv[2:])
    elif os.path.isfile(sub):
        cmd_wire(sys.argv[1:])   # legacy Tier-B CLI
    else:
        sys.exit('unknown subcommand %r\n\n%s' % (sub, __doc__))


if __name__ == '__main__':
    main()
