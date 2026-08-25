#!/usr/bin/env python3
"""Decode an oracle capture and prove there is NO black frame mid-stream.

Owner gate (2026-07-29): the session is not handed over onscreen until
the oracle capture decodes clean, with no black frame in the middle.
This is that gate -- offline, deterministic, no screen and no client.

WHY THE OBVIOUS APPROACHES ARE WRONG. Three separate false alarms came
out of getting this wrong, each of which looked like a catastrophic
product failure on a capture that was in fact fine:

 1. Splitting into <stem>_main.h264 / <stem>_aux.h264 and decoding each
    alone. Under the FR-H264-8 LTR aux-chain the two views share ONE
    frame_num chain (main takes N, aux takes N+1), so each view ALONE
    steps frame_num by 2 -- read as gaps and dropped -- and the
    parameter sets ship once, in the main view, because this is one
    chain and not two streams. On the KNOWN-GOOD arm-n capture that
    reports main 66.8/167 frames and aux 0 ("non-existing PPS 0").
    The splitter is a debugging convenience, not a model of the client.

 2. Letting ffmpeg pick its default frame-rate mode. These pictures
    carry no timestamps, so ffmpeg synthesises them and DROPS pictures
    whose timestamps collide: 334 pictures in, 131 frames out. Hence
    -vsync 0.

 3. Hand-rolled byte arithmetic over rawvideo with an assumed
    width*height. Main and aux views are NOT the same size, so a fixed
    stride misreads frame boundaries -- it reported "619.7 frames" for
    334 pictures. Hence ffmpeg's own blackframe filter.

So: concatenate every record's NALs in WIRE ORDER into one chain (then
frame_num is contiguous and the parameter sets are present), decode with
-vsync 0, and let blackframe judge. Output frames alternate main, aux,
main, aux; the MAIN frames are what the user sees, so those are the ones
judged. The aux view carries packed chroma -- its luminance is not a
picture and means nothing here.

Validated against the known-good arm-n baseline: 334/334 frames, zero
black. A checker that cannot pass a good capture cannot condemn a bad
one.

Usage:
  oracle_black_frame_check.py <oracle_avc_s*.bin> ...
  oracle_black_frame_check.py <capture_dir>/       # every dump in it
  oracle_black_frame_check.py --single-view <oracle_avc_s*.bin>
"""
import os
import re
import struct
import subprocess
import sys


def avc420_nals(buf):
    """Strip the AVC420 metablock, return the Annex-B NAL bytes."""
    (nrects,) = struct.unpack_from('<I', buf, 0)
    return buf[4 + nrects * 8 + nrects * 2:]


def interleaved_stream(path, single_view=False):
    """Every record's NALs, in wire order -> one contiguous chain."""
    data = open(path, 'rb').read()
    out = bytearray()
    views = []
    pos = 0
    while pos + 4 <= len(data):
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) != ln or ln < 8:
            break
        try:
            if single_view:
                out += avc420_nals(rec)
                views.append('main')
                continue
            (w,) = struct.unpack_from('<I', rec, 0)
            avc1len = w & 0x3FFFFFFF
            lc = (w >> 30) & 0x3
            avc1 = rec[4:4 + avc1len]
            if lc in (0, 1):
                out += avc420_nals(avc1)
                views.append('main')
            if lc == 0:
                out += rec[4 + avc1len:]
                views.append('aux')
            elif lc == 2:
                out += avc420_nals(avc1 if avc1len > 0 else rec[4:])
                views.append('aux')
        except struct.error:
            break
    return bytes(out), views


def decode_black_frames(stream):
    """(n_frames, black_frame_indices, stderr) for the whole chain."""
    proc = subprocess.Popen(
        ['ffmpeg', '-hide_banner', '-f', 'h264', '-i', 'pipe:0',
         '-vsync', '0', '-vf', 'blackframe=amount=98:threshold=32',
         '-f', 'null', '-'],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE)
    _out, err = proc.communicate(stream)
    err = err.decode('utf-8', 'replace')
    black = [int(m.group(1))
             for m in re.finditer(r'Parsed_blackframe.*?frame:(\d+)', err)]
    counts = re.findall(r'frame=\s*(\d+)', err)
    return (int(counts[-1]) if counts else 0), black, err


def check(path, single_view=False):
    stream, views = interleaved_stream(path, single_view)
    n_frames, black, err = decode_black_frames(stream)
    n_expected = len(views)
    print('%s' % os.path.basename(path))
    print('  pictures in capture     : %d' % n_expected)
    print('  frames decoded          : %d' % n_frames)
    shortfall = n_expected - n_frames
    if shortfall > 0:
        print('  DECODE SHORTFALL        : %d' % shortfall)
    main_black = [i for i in black if i < len(views) and views[i] == 'main']
    interior = [i for i in main_black if 0 < i < n_frames - 2]
    print('  black frames (any view) : %d %s' % (len(black), black[:20]))
    print('  black MAIN-view frames  : %d %s'
          % (len(main_black), main_black[:20]))
    print('  ... mid-stream          : %d %s' % (len(interior), interior[:20]))
    hard = [ln.strip() for ln in err.splitlines()
            if ('error' in ln.lower() or 'no frame' in ln.lower()
                or 'non-existing' in ln.lower())][:6]
    if hard:
        print('  decoder errors          :')
        for ln in hard:
            print('      %s' % ln)
    ok = not interior and shortfall <= 0 and not hard
    print('  VERDICT: %s' % ('PASS' if ok else 'FAIL'))
    return ok


def main():
    args = sys.argv[1:]
    if not args:
        print(__doc__)
        return 2
    single_view = False
    if '--single-view' in args:
        single_view = True
        args.remove('--single-view')
    targets = []
    for a in args:
        if os.path.isdir(a):
            for name in sorted(os.listdir(a)):
                if name.startswith('oracle_avc_s') and name.endswith('.bin'):
                    targets.append(os.path.join(a, name))
        else:
            targets.append(a)
    ok = True
    for t in targets:
        ok = check(t, single_view) and ok
        print()
    print('NO MID-STREAM BLACK FRAME: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
