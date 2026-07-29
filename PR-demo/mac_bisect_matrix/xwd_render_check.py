#!/usr/bin/env python3
"""Did the CLIENT keep rendering across the re-key boundaries?

BACKLOG #48 acceptance has a byte half and a pixel half. The byte half
(rekey_boundary_audit.py) proves the SERVER emitted DELETE/CREATE/MAP
ahead of a full-surface IDR. It cannot prove a client survived it: a
decoder that wedges at the first boundary still leaves a perfectly
well-formed capture behind it, because the capture is the server's
output.

This reads the xwd frames sampled from the oracle client's X display
during the same connection and answers two questions per frame:

  black?    mean luminance ~0 -- a wedged/torn-down decoder that never
            got a new surface renders nothing.
  frozen?   identical to the previous frame -- a decoder that stopped
            consuming leaves the last good picture on screen forever,
            which is NOT black and would pass a naive check.

The workload (SESSION_KIND=code) scrolls continuously at 10 fps, so
consecutive samples taken seconds apart MUST differ. A run that stays
non-black and keeps changing across N boundaries is a real client
decoding through N re-keys.

Usage: xwd_render_check.py <shot.xwd> [shot.xwd ...]
"""
import struct
import sys

XWD_HEADER_INTS = 25


def read_xwd(path):
    """Return (width, height, bits_per_pixel, pixel_bytes)."""
    with open(path, 'rb') as fp:
        blob = fp.read()
    if len(blob) < XWD_HEADER_INTS * 4:
        raise ValueError('%s: too short to be xwd' % path)
    h = struct.unpack('>%dI' % XWD_HEADER_INTS, blob[:XWD_HEADER_INTS * 4])
    header_size, version = h[0], h[1]
    if version != 7:
        raise ValueError('%s: xwd version %d, expected 7' % (path, version))
    width, height, bpp = h[4], h[5], h[11]
    ncolors = h[19]
    # window name follows the fixed header, then the colormap (12 bytes
    # per XWDColor entry), then the raster
    off = header_size + ncolors * 12
    return width, height, bpp, blob[off:]


def frame_stats(path):
    width, height, bpp, px = read_xwd(path)
    step = max(1, (bpp // 8))
    # sample every 97th pixel: enough for a luminance/aliveness verdict,
    # cheap enough to run over a whole capture
    stride = step * 97
    vals = px[::stride]
    if not vals:
        return width, height, 0.0, 0
    mean = sum(vals) / float(len(vals))
    # cheap content fingerprint, order-sensitive
    sig = 0
    for i, v in enumerate(vals):
        sig = (sig * 131 + v + i) & 0xFFFFFFFF
    return width, height, mean, sig


def single(path):
    """One frame, one line: '<mean> <sig> <black:0|1>'.

    Used by capture_arm_boundary.sh to judge each sample AS IT IS TAKEN,
    so a client that wedges at the first boundary aborts the run in
    ~20 s instead of after the full capture window. A black screen is
    the answer, not something to keep collecting evidence about.
    """
    try:
        _w, _h, mean, sig = frame_stats(path)
    except (ValueError, IOError):
        print('0 0 1')
        return 1
    is_black = 1 if mean < 2.0 else 0
    print('%.4f %d %d' % (mean, sig, is_black))
    return is_black


def main():
    if len(sys.argv) > 2 and sys.argv[1] == '--single':
        return single(sys.argv[2])
    shots = sys.argv[1:]
    if not shots:
        print('usage: xwd_render_check.py [--single] <shot.xwd> ...')
        return 2
    prev_sig = None
    black = 0
    frozen = 0
    print('%-28s %-12s %-10s %-12s %s'
          % ('frame', 'geometry', 'mean', 'changed', 'verdict'))
    for path in shots:
        try:
            w, h, mean, sig = frame_stats(path)
        except ValueError as exc:
            print('%-28s UNREADABLE (%s)' % (path.rsplit('/', 1)[-1], exc))
            black += 1
            continue
        is_black = mean < 2.0
        changed = prev_sig is None or sig != prev_sig
        if is_black:
            black += 1
        if not changed:
            frozen += 1
        verdict = 'BLACK' if is_black else ('FROZEN' if not changed else 'ok')
        print('%-28s %-12s %-10.2f %-12s %s'
              % (path.rsplit('/', 1)[-1], '%dx%d' % (w, h), mean,
                 'yes' if changed else 'NO', verdict))
        prev_sig = sig
    print()
    print('frames: %d   black: %d   frozen: %d' % (len(shots), black, frozen))
    ok = black == 0 and frozen == 0
    print('RENDER ACROSS BOUNDARIES: %s' % ('PASS' if ok else 'FAIL'))
    return 0 if ok else 1


if __name__ == '__main__':
    sys.exit(main())
