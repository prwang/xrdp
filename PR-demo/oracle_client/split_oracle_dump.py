#!/usr/bin/env python3
"""Split an oracle dump into playable H.264 elementary streams.

Input: /tmp/oracle_avc_s<id>.bin — u32-length-prefixed records, each one
RDPGFX_SURFACE_COMMAND payload as captured by the oracle client:
  - AVC420 session: RFX_AVC420_BITMAP_STREAM
      = metablock (u32 numRegionRects, numRegionRects * RECT16(8B),
        numRegionRects * quantQualityVal(2B)) + Annex-B NALs
  - AVC444(v2) session: RFX_AVC444_BITMAP_STREAM
      = u32 avc1len (bits 0..29 = cbAvc420EncodedBitstream1, bits 30..31 =
        LC), avc1 = AVC420_BITMAP_STREAM, then (LC==0) avc2 NAL-only... —
        per MS-RDPEGFX; LC=1 main-only, LC=2 aux-only.

Output: <stem>_main.h264 and <stem>_aux.h264 (concatenated Annex-B).
Remux to mp4: ffmpeg -fflags +genpts -r 30 -i X_main.h264 -c copy X.mp4

Usage: split_oracle_dump.py /tmp/oracle_avc_s0.bin [AVC444|AVC420]
"""
import struct
import sys

path = sys.argv[1]
mode = sys.argv[2] if len(sys.argv) > 2 else 'AVC444'
stem = path.rsplit('.', 1)[0]
data = open(path, 'rb').read()

def avc420_nals(buf):
    """Strip the metablock, return the Annex-B NAL bytes."""
    (nrects,) = struct.unpack_from('<I', buf, 0)
    off = 4 + nrects * 8 + nrects * 2
    return buf[off:]

main = open(stem + '_main.h264', 'wb')
aux = open(stem + '_aux.h264', 'wb')
pos = 0
frames = mains = auxes = 0
while pos + 4 <= len(data):
    (ln,) = struct.unpack_from('<I', data, pos)
    pos += 4
    rec = data[pos:pos + ln]
    pos += ln
    if len(rec) != ln:
        print('truncated record at end, ignored', file=sys.stderr)
        break
    frames += 1
    if mode == 'AVC420':
        main.write(avc420_nals(rec))
        mains += 1
        continue
    (w,) = struct.unpack_from('<I', rec, 0)
    avc1len = w & 0x3FFFFFFF
    lc = (w >> 30) & 0x3
    avc1 = rec[4:4 + avc1len]
    if lc in (0, 1):
        main.write(avc420_nals(avc1))
        mains += 1
    if lc == 0:
        aux.write(rec[4 + avc1len:])
        auxes += 1
    elif lc == 2:
        main_view = avc420_nals(avc1)  # LC=2: the avc1 slot carries the aux
        aux.write(main_view)
        auxes += 1
main.close()
aux.close()
print('%d records -> %d main, %d aux (%s_main.h264 / %s_aux.h264)' %
      (frames, mains, auxes, stem, stem))
