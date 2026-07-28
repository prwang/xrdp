#!/usr/bin/env python3
"""avc444_roundtrip_psnr.py — PROTOTYPE of the FR-H264-8 semantic PSNR
harness (PRD FR-H264-8 "Semantic roundtrip PSNR harness"; not yet a CI
gate — this is the Tier-B wire mode, demonstrating that the metric
BITES the known chroma-bleed corruption class before we build the full
source-roundtrip tier).

Tier-B: given an oracle AVC444 wire capture, decode it two ways —
  1-context: the full interleave through one decoder (MSTSC shape);
             per-view frames extracted from the interleaved decode are
             the ALIGNED GROUND TRUTH;
  2-context: each view through its own decoder (macOS shape).
For each view, per-frame per-plane PSNR (Y/U/V) between the two modes
via ffmpeg's psnr filter (no raw dumps). A DPB/reference corruption
shows up as a CHROMA-plane PSNR collapse (the cyan-bleed signature)
and/or monotonic decay (drift). The worst aux frame is exported as a
side-by-side PNG — the "cyan bleed pic" the number is biting.

Verdict rules (prototype):
  - RED if min chroma PSNR (U or V) < CHROMA_MIN_DB on either view;
  - RED if last-third mean chroma PSNR < first-third mean - DRIFT_DB
    (accumulating reference drift);
  - GREEN otherwise ('inf' = bit-identical frames).

Usage: avc444_roundtrip_psnr.py <capture.bin> [outdir]
Requires ffmpeg. Prototype limitation: assumes the wire strictly
alternates M,A,M,A (asserted; true for all current 1:1-cadence
captures).
"""
import os
import re
import struct
import subprocess
import sys

CHROMA_MIN_DB = 40.0
DRIFT_DB = 3.0
FFMPEG = os.environ.get('FFMPEG', 'ffmpeg')


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


def main():
    cap = sys.argv[1]
    outdir = sys.argv[2] if len(sys.argv) > 2 else '/tmp/roundtrip_psnr'
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


if __name__ == '__main__':
    main()
