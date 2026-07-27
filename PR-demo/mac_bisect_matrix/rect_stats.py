#!/usr/bin/env python3
"""Rect-coverage statistics over an oracle AVC444 dump.

For the decode-state-corruption causal analysis we need, per record:
  - LC (1 = luma/main view, 2 = chroma/aux view)
  - NAL classes present (IDR / P / SPS / PPS / SEI)
  - rect count, union bounding box, summed rect area
  - whether the record's rect union covers the (observed) full surface

Surface size is taken as the max x2/y2 seen across the whole dump.

Usage: rect_stats.py /path/dump.bin [--per-record]
"""
import struct
import sys


def records(path):
    data = open(path, 'rb').read()
    pos = 0
    while pos + 4 <= len(data):
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) != ln:
            break
        yield rec


def parse_rec(rec):
    (w,) = struct.unpack_from('<I', rec, 0)
    avc1len = w & 0x3FFFFFFF
    lc = (w >> 30) & 0x3
    (nrects,) = struct.unpack_from('<I', rec, 4)
    rects = [struct.unpack_from('<4H', rec, 8 + k * 8)
             for k in range(nrects)]
    end = 4 + avc1len if lc != 2 else len(rec)
    stream = rec[8 + nrects * 10:end]
    nals = []
    i = stream.find(b'\x00\x00\x01')
    while i >= 0:
        j = stream.find(b'\x00\x00\x01', i + 3)
        e = j if j >= 0 else len(stream)
        raw = stream[i + 3:e]
        if raw.endswith(b'\x00'):
            raw = raw[:-1]
        if raw:
            nals.append(raw[0] & 0x1F)
        i = j
    return lc, rects, nals, len(stream)


def main():
    path = sys.argv[1]
    per_record = '--per-record' in sys.argv
    recs = [parse_rec(r) for r in records(path)]
    maxx = max((r[2] for _, rects, _, _ in recs for r in rects), default=0)
    maxy = max((r[3] for _, rects, _, _ in recs for r in rects), default=0)
    print('surface (max rect extent): %dx%d, records=%d'
          % (maxx, maxy, len(recs)))
    full = 0
    stats = {}
    for idx, (lc, rects, nals, slen) in enumerate(recs):
        ux1 = min((r[0] for r in rects), default=0)
        uy1 = min((r[1] for r in rects), default=0)
        ux2 = max((r[2] for r in rects), default=0)
        uy2 = max((r[3] for r in rects), default=0)
        area = sum((r[2] - r[0]) * (r[3] - r[1]) for r in rects)
        isfull = (ux1 == 0 and uy1 == 0 and ux2 == maxx and uy2 == maxy
                  and area >= 0.98 * maxx * maxy)
        full += isfull
        kind = ('IDR' if 5 in nals else 'P' if 1 in nals else '?')
        key = (lc, kind)
        s = stats.setdefault(key, [0, 0, 0])
        s[0] += 1
        s[1] += isfull
        s[2] += area
        if per_record or 5 in nals or idx < 8:
            print('rec=%03d lc=%d %s nals=%s rects=%d union=(%d,%d,%d,%d) '
                  'area=%d%%%s'
                  % (idx, lc, kind, nals, len(rects), ux1, uy1, ux2, uy2,
                     100 * area // (maxx * maxy) if maxx else 0,
                     ' FULL' if isfull else ''))
    print()
    for (lc, kind), (n, nf, area) in sorted(stats.items()):
        print('lc=%d %-3s: %3d records, %3d full-surface, mean area %d%%'
              % (lc, kind, n, nf, area // max(n, 1)
                 * 100 // max(maxx * maxy, 1)))


if __name__ == '__main__':
    main()
