#!/usr/bin/env python3
"""BACKLOG #80 -- how late does the CLIENT acknowledge, and did the credit
window actually fill?

WHY THIS EXISTS. Every fleet measurement of the credit frontier used the
oracle client, which acknowledges a frame BEFORE decoding it. The
frontier is

    credit = min(frame_id_consumed, frame_id_server + 1, frame_id_client + C)

and the third term -- how far ahead of the CLIENT we may run -- is
therefore the one term the oracle client cannot exercise. A human at a
real client cannot see it either: they can judge whether the picture
lagged, not whether the window was ever full. This script reads the
server's own ring and answers the half the eye cannot.

WHAT IT REPORTS.

1. ACK LATENCY -- from a frame leaving the server (`egress`) to the
   client's acknowledgement covering it (`cliack`). Acks are CUMULATIVE:
   an ack for id N covers every id <= N, so each egressed frame is
   matched to the FIRST ack whose id reaches it. Paired by frame id,
   never by time window (the 2c gate).

2. DISTANCE AT EGRESS -- for each frame as it left, its id minus the
   client's acknowledged id. This is the occupancy of the window, and it
   is the unambiguous evidence that the client term was exercised: if
   the histogram sits at 1-2 the client was simply keeping up and the
   walk proved nothing about the frontier.

3. THE BOUND. PRD FR-FLOW-1: distance <= wire_window + 2 * monitors, the
   2 being the per-monitor capture slots. Pass --monitors to check it.
   THE MONITOR COUNT IS NOT IN THE RING -- read it from the session log
   (`xrdp_egfx_reset_graphics: ... monitorcount N`) and pass it. Getting
   it wrong turns a correct run into an apparent bound violation, or
   hides a real one.

4. WHICH TERM WAS SMALLEST at the moment credit was emitted, from
   `ackslot`. Reported for completeness and NOT load-bearing: at the
   emission instant the three terms frequently tie, so this cannot
   attribute the binding term. #80 already recorded that limitation.
   The distance histogram is what settles it.

Usage:
    i80_ack_latency.py <perf-ring-file> [--monitors N] [--window C] [label]
"""

import sys


def load(path):
    out = []
    for line in open(path):
        if line.startswith('#'):
            continue
        f = line.split()
        if len(f) < 9:
            continue
        try:
            t = int(f[0])
        except ValueError:
            continue
        out.append((t, f[2], [int(x) for x in f[3:9]]))
    return out


def pct(v, k):
    v = sorted(v)
    return v[int(k * (len(v) - 1))] if v else float('nan')


def report(path, label, monitors, window):
    recs = load(path)

    # egress: a = frame_id, b = displayed, c = pending KiB,
    #         d = frame_id_client   (xrdp_mm.c)
    # cliack: a = frame_id, b = queue depth, c = frames decoded,
    #         d = frame_id_server
    egress = {}
    for t, name, a in recs:
        if name == 'egress' and a[0] not in egress:
            egress[a[0]] = (t, a[3])

    first_ack = {}
    for t, name, a in recs:
        if name != 'cliack':
            continue
        fid = a[0]
        for k in egress:
            if k <= fid and k not in first_ack:
                first_ack[k] = t

    lat = [(first_ack[k] - te) / 1e6
           for k, (te, _c) in egress.items() if k in first_ack]
    dist = [k - cli for k, (_t, cli) in egress.items()]
    outstanding = len(egress) - len(lat)

    print("=== %s" % label)
    print("  frames egressed %d, acknowledged %d, still outstanding at the "
          "end of the ring %d" % (len(egress), len(lat), outstanding))
    if lat:
        print("  ACK LATENCY (frame leaves -> client acknowledges it), ms:")
        print("    p10 %7.2f   p50 %7.2f   p90 %7.2f   max %7.2f"
              % (pct(lat, .1), pct(lat, .5), pct(lat, .9), max(lat)))
    if dist:
        hist = {}
        for d in dist:
            hist[d] = hist.get(d, 0) + 1
        print("  DISTANCE at egress (frames this one is ahead of the "
              "client's acknowledged position):")
        for d in sorted(hist):
            print("    %d: %6d  (%5.1f %%)"
                  % (d, hist[d], 100.0 * hist[d] / len(dist)))
        if monitors is not None and window is not None:
            bound = window + 2 * monitors
            worst = max(dist)
            verdict = "HELD" if worst <= bound else "*** EXCEEDED ***"
            print("  BOUND wire_window + 2 x monitors = %d + 2 x %d = %d;"
                  " worst observed %d -- %s"
                  % (window, monitors, bound, worst, verdict))

    slots = [a for _t, n, a in recs if n == 'ackslot']
    if slots:
        terms = {}
        for a in slots:
            _slot, server, consumed, client, C = a[0], a[1], a[2], a[3], a[4]
            v = {'consumed': consumed, 'server+1': server + 1,
                 'client+C': client + C}
            lo = min(v.values())
            key = ','.join(sorted(k for k in v if v[k] == lo))
            terms[key] = terms.get(key, 0) + 1
        print("  smallest term at emission (%d emissions; ties are common,"
              " so this cannot attribute -- see the header):" % len(slots))
        for k, c in sorted(terms.items(), key=lambda kv: -kv[1]):
            print("    %s: %d (%.1f %%)" % (k, c, 100.0 * c / len(slots)))


if __name__ == '__main__':
    args = [a for a in sys.argv[1:]]
    mon = None
    win = None
    rest = []
    i = 0
    while i < len(args):
        if args[i] == '--monitors' and i + 1 < len(args):
            mon = int(args[i + 1])
            i += 2
        elif args[i] == '--window' and i + 1 < len(args):
            win = int(args[i + 1])
            i += 2
        else:
            rest.append(args[i])
            i += 1
    if not rest:
        sys.exit(__doc__)
    report(rest[0], rest[1] if len(rest) > 1 else rest[0], mon, win)
