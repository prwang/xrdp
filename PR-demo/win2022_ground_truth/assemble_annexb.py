#!/usr/bin/env python3
"""Assemble a decode-order Annex-B stream from a gfxwin_* capture dir.

Each f%06d_c000F.bin is one RFX_AVC444_BITMAP_STREAM as delivered on the
wire (MS-RDPEGFX 2.2.4.5): UINT32 header (bits0-29 cbAvc420EncodedBitstream1,
bits30-31 LC), then one or two RFX_AVC420_BITMAP_STREAM structures. This
emits every view's H.264 payload in exact wire/decode order — the feed a
single in-order client decoder sees — and prints which AU ordinals are aux.

This is the assembly step behind the LTR reference-machinery measurement in
GROUND_TRUTH_win2022_avc444.md (2026-07-28 addendum). Reproduce with:

  ./assemble_annexb.py gfxwin_anim /tmp/win2022_anim.h264
  ffmpeg -i /tmp/win2022_anim.h264 -c copy -bsf:v trace_headers -f null - 2>&1 \
    | grep -E "ref_pic_list_modification|memory_management|long_term|..." \
    | awk '{f=$2; v=$NF; c[f" = "v]++} END{for (k in c) print c[k], k}'
"""
import glob
import struct
import sys


def h264_of(buf):
    """Skip numRegionRects + rects + quantQualityVals of an AVC420 stream."""
    n = struct.unpack_from('<I', buf, 0)[0]
    return buf[4 + n * 8 + n * 2:]


def main():
    capdir, outpath = sys.argv[1], sys.argv[2]
    out = bytearray()
    aux_aus = []
    au = 0
    for p in sorted(glob.glob(capdir + '/f*_c000F.bin')):
        data = open(p, 'rb').read()
        (w,) = struct.unpack_from('<I', data, 0)
        cb1 = w & 0x3FFFFFFF
        lc = (w >> 30) & 3
        if lc == 1:
            streams = [('M', data[4:])]
        elif lc == 2:
            streams = [('A', data[4:])]
        else:
            streams = [('M', data[4:4 + cb1]), ('A', data[4 + cb1:])]
        for kind, s in streams:
            h = h264_of(s)
            if not h:
                continue
            out += h
            if kind == 'A':
                aux_aus.append(au)
            au += 1
    open(outpath, 'wb').write(bytes(out))
    print('%d AUs (%d aux at %s) -> %s (%d bytes)'
          % (au, len(aux_aus), aux_aus, outpath, len(out)))


if __name__ == '__main__':
    main()
