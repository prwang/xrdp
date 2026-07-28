#!/usr/bin/env python3
"""Wire bandwidth stats for an oracle AVC444 dump (probe444 capture).

Parses the length-prefixed record stream (<u32 LE len><RFX_AVC444_
BITMAP_STREAM>...), splits per-view bytes by LC, classifies each view
payload IDR vs non-IDR, and reports totals plus a steady-state segment
(records after the first quarter, i.e. past login/initial-IDR churn).
Sizes are H.264 GFX payload bytes as delivered on the wire — the codec
traffic a bandwidth comparison turns on (excludes TLS/transport framing,
which is identical across arms).

Usage: bandwidth_stats.py <dump.bin> [label]
Output: one parseable line per view + a human summary.
"""
import struct
import sys


def nal_types(buf):
    out = []
    i = buf.find(b'\x00\x00\x01')
    while i >= 0:
        j = buf.find(b'\x00\x00\x01', i + 3)
        end = j if j >= 0 else len(buf)
        if end > i + 3:
            out.append(buf[i + 3] & 0x1F)
        i = j
    return out


def views_of(rec):
    (w,) = struct.unpack_from('<I', rec, 0)
    cb1 = w & 0x3FFFFFFF
    lc = (w >> 30) & 0x3
    if lc == 1:
        return [('M', rec[4:])]
    if lc == 2:
        return [('A', rec[4:])]
    return [('M', rec[4:4 + cb1]), ('A', rec[4 + cb1:])]


def h264_len(view):
    """Payload minus the AVC420 region/quality preamble."""
    (n,) = struct.unpack_from('<I', view, 0)
    return len(view) - (4 + n * 8 + n * 2)


def main():
    data = open(sys.argv[1], 'rb').read()
    label = sys.argv[2] if len(sys.argv) > 2 else sys.argv[1]
    frames = []                 # (kind, is_idr, payload_bytes) wire order
    pos = 0
    while pos + 4 <= len(data):
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) != ln or ln < 8:
            sys.exit('bad record framing at offset %d' % pos)
        for kind, view in views_of(rec):
            if not view:
                continue
            frames.append((kind, 5 in nal_types(view[4:]), h264_len(view)))

    def stats(sel):
        n = len(sel)
        return n, sum(b for _, _, b in sel)

    def report(name, sel):
        n_all, b_all = stats(sel)
        idr = [f for f in sel if f[1]]
        n_idr, b_idr = stats(idr)
        n_p, b_p = n_all - n_idr, b_all - b_idr
        print('%s %s: frames=%d bytes=%d  idr=%d/%dB  nonidr=%d/%dB'
              '  avg_nonidr=%.0fB'
              % (label, name, n_all, b_all, n_idr, b_idr, n_p, b_p,
                 b_p / n_p if n_p else 0))

    m = [f for f in frames if f[0] == 'M']
    a = [f for f in frames if f[0] == 'A']
    report('main', m)
    report('aux ', a)
    ss = frames[len(frames) // 4:]
    report('main-steady', [f for f in ss if f[0] == 'M'])
    report('aux-steady ', [f for f in ss if f[0] == 'A'])
    report('TOTAL', frames)


if __name__ == '__main__':
    main()
