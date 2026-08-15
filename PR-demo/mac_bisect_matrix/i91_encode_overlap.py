#!/usr/bin/env python3
"""BACKLOG #91 -- do two screens' ENCODES overlap, and what staggers them?

This script exists because the first #91 analysis answered the wrong
question. It counted the ORDER of the four `outfirst` instants -- did one
screen's two children both emit output before the other screen's did --
and that count (874 of 874 "strictly separated") was reported as though
it meant the encodes never ran at the same time. Completion order is not
concurrency: intervals can be ordered by their right edges and still
overlap throughout, which is what these do. See the 2026-08-08 addendum
in captures/i91_attribution_m2_20260807_211825_s20/README.md.

WHAT IT MEASURES INSTEAD. A child cannot begin encoding until its whole
raw picture has arrived -- ffmpeg's raw-video reader hands nothing to the
encoder until it holds a complete frame. So:

    encode window = [ feedend , outfirst ]

  feedend   the last byte of the picture entered the child's pipe
            (xrdp_encoder_ffmpeg.c, feed_vmsplice)
  outfirst  the first byte of the result came back
            (xrdp_encoder_ffmpeg.c, drain_stdout)

Two windows that overlap are two encodes running at once. Both records
carry the monitor index in field d (BACKLOG #91, landed in 1fed64c1),
which is what makes a four-child pump splittable by screen at all.

TWO BIASES IN THAT WINDOW, neither hidden:
  * feedend fires when the last byte enters the PIPE; the child may still
    have up to one pipe buffer (1 MiB, set with F_SETPIPE_SZ) unread, so
    the true encode start is under a millisecond later than the window's
    left edge at the transfer rates this prints.
  * on a VAAPI arm the window also contains the upload of the frame from
    system memory to the GPU, not only the encode.

Records are paired by IDENTITY (the monitor index carried in the record),
never by time window -- the 2c gate.

Usage:
    i91_encode_overlap.py <perf-ring-file> [label] [WxH,WxH,...]

The geometry list gives each monitor index its pixel size, in index
order, and defaults to this bisect fleet's two-screen layout. It is used
only for the per-megapixel columns; every overlap count is independent
of it.
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from perf_trace_records import read_records

DEFAULT_GEOM = "2560x1440,3840x2400"


def load(path):
    """Read the named text stream without positional payload recovery."""
    return [(r["mono_ns"], r["event"], r) for r in read_records(path)
            if r["event"] != "clock_base"]


def bits(mask):
    return [i for i in range(8) if mask >> i & 1]


def pct(v, k):
    v = sorted(v)
    return v[int(k * (len(v) - 1))] if v else float('nan')


def pumps_of(recs):
    """Split the ring into pumps, each carrying its per-child records.

    pump_beg field c is the mask of monitors in this poll set; pump_end
    field b is the armed child count (2 per monitor).
    """
    out = []
    cur = None
    for t, name, fields in recs:
        if name == 'pump_beg':
            cur = {'t0': t, 'mask': fields['monitor_mask'],
                   'feed': [], 'out': []}
        elif cur is None:
            continue
        elif name == 'feedend':
            cur['feed'].append((t, fields['monitor'], fields['main']))
        elif name == 'outfirst':
            cur['out'].append((t, fields['monitor'], fields['main']))
        elif name == 'pump_end':
            cur['t1'] = t
            cur['armed'] = fields['kids_armed']
            out.append(cur)
            cur = None
    return out


def report(path, label, geom):
    mpx = {}
    mbytes = {}
    for i, wh in enumerate(geom.split(',')):
        w, h = (int(x) for x in wh.lower().split('x'))
        mpx[i] = w * h / 1e6
        mbytes[i] = w * h * 3 / 2 / 1e6       # NV12

    pumps = pumps_of(load(path))
    both = [p for p in pumps
            if len(bits(p['mask'])) == 2 and p['armed'] == 4
            and len(p['feed']) == 4 and len(p['out']) == 4]
    print("\n===== %s : %d pumps, %d of them carrying both screens"
          % (label, len(pumps), len(both)))

    # ---- per-child encode window, split by how many screens shared the
    # pump. This is the contention test: the same screen, alone versus
    # sharing, inside one run.
    win = {}
    for p in pumps:
        nmon = len(bits(p['mask']))
        F = {(m, b): t for t, m, b in p['feed']}
        O = {(m, b): t for t, m, b in p['out']}
        for k in F:
            if k in O and O[k] > F[k] and k[0] in mpx:
                win.setdefault((nmon, k[0]), []).append((O[k] - F[k]) / 1e6)
    print("\n  encode window per child: input complete -> first output byte")
    print("  screens  monitor      n    ms p50   ms/Mpx p50")
    for k in sorted(win):
        v = win[k]
        print("     %d        %d      %5d   %7.2f     %8.3f"
              % (k[0], k[1], len(v), pct(v, .5), pct(v, .5) / mpx[k[1]]))

    # ---- do the two screens' encode windows overlap?
    ov = 0
    sib_ov = 0
    sib_n = 0
    kmin = []
    small = []
    big = []
    stagger = []
    lastfeed = []
    dur = []
    for p in both:
        F = {(m, b): t for t, m, b in p['feed']}
        O = {(m, b): t for t, m, b in p['out']}
        ms = sorted(set(m for m, b in F))
        if len(ms) != 2:
            continue
        a, b = ms
        fa = max(F[(a, x)] for x in (0, 1) if (a, x) in F)
        oa = max(O[(a, x)] for x in (0, 1) if (a, x) in O)
        fb = max(F[(b, x)] for x in (0, 1) if (b, x) in F)
        ob = max(O[(b, x)] for x in (0, 1) if (b, x) in O)
        if oa > fb and ob > fa:
            ov += 1
        ea = (oa - fa) / 1e6
        small.append(ea)
        big.append((ob - fb) / 1e6)
        stagger.append((fb - fa) / 1e6)
        if ea > 0:
            # scale every encode by k; the windows meet at
            # k > stagger / (first screen's encode)
            kmin.append((fb - fa) / 1e6 / ea)
        for m in ms:
            if all((m, x) in F and (m, x) in O for x in (0, 1)):
                sib_n += 1
                if O[(m, 0)] > F[(m, 1)] and O[(m, 1)] > F[(m, 0)]:
                    sib_ov += 1
        lastfeed.append((max(F.values()) - p['t0']) / 1e6)
        dur.append((p['t1'] - p['t0']) / 1e6)

    n = len(both)
    print("\n  THE TWO SCREENS' encode windows overlap : %d / %d" % (ov, n))
    print("  the two VIEWS of one screen overlap     : %d / %d"
          % (sib_ov, sib_n))
    print("  first screen's encode p50 %.2f ms | second screen's p50 %.2f ms"
          % (pct(small, .5), pct(big, .5)))
    print("  stagger (second screen's input lands this much later) p50"
          " %.2f ms" % pct(stagger, .5))

    print("\n  counterfactual: scale every encode by k")
    print("    median k at which the two windows meet: %.2f" % pct(kmin, .5))
    for k in (1.5, 2.0, 3.0):
        c = sum(1 for x in kmin if x < k)
        print("    k = %.1f -> %d / %d pumps overlap (%.0f %%)"
              % (k, c, len(kmin), 100.0 * c / len(kmin)))

    # ---- what the pump spends its time on before any encode can start
    tot = sum(2 * mbytes[m] for m in bits(both[0]['mask'])) if both else 0
    print("\n  raw input transfer: %.1f MB of NV12 into %d pipes"
          % (tot, 4))
    print("    last byte lands at p50 %.2f ms of a %.2f ms pump"
          "  => %.2f GB/s aggregate"
          % (pct(lastfeed, .5), pct(dur, .5), tot / pct(lastfeed, .5)))

    # ONE pump, the middle one in order -- not a per-instant median. A
    # timeline built from column medians is not a pump that happened;
    # this one is, so the two are printed as different things.
    print("\n  one representative pump (the middle one), ms after it starts:")
    for p in [both[len(both) // 2]] if both else []:
        rows = ([( (t - p['t0']) / 1e6, 'monitor %d input complete' % m)
                 for t, m, b in p['feed']]
                + [((t - p['t0']) / 1e6, 'monitor %d first output' % m)
                   for t, m, b in p['out']]
                + [((p['t1'] - p['t0']) / 1e6, 'pump ends')])
        for t, what in sorted(rows):
            print("    %6.2f  %s" % (t, what))


if __name__ == '__main__':
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    report(sys.argv[1],
           sys.argv[2] if len(sys.argv) > 2 else sys.argv[1],
           sys.argv[3] if len(sys.argv) > 3 else DEFAULT_GEOM)
