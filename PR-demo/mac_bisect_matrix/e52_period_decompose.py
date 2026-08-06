#!/usr/bin/env python3
"""Decompose the E5-2 send period into the segments the stop rule asks for.

The E5 gate reports one number — mean send-to-send interval — and when it
comes back short, BACKLOG #52's stop rule says the remainder has to be
ATTRIBUTED before anything ships. This reads the same `GFX_TRACE` lines the
gate reads and splits each cycle into:

    batch arm -> first enc submit     capture + AVC444 pack (xorgxrdp)
    first enc submit -> last=1        encode + LTR rewrite + EGFX assembly
    last=1 -> next batch arm          idle: waiting for the next damage

The first two are OUR pipeline; the third is the payload's (or the
capture's) ability to produce new damage. A run in which the first two
dominate is measuring us; a run in which the third dominates is measuring
the payload, and its ratio is not a statement about the pipeline.

Usage:  e52_period_decompose.py <gfx_trace.txt|.gz> [...]
"""
import gzip
import re
import statistics as st
import sys
from datetime import datetime

TS = re.compile(r'^\[(\d{4}-\d\d-\d\dT\d\d:\d\d:\d\d\.\d+)')


def _open(path):
    if path.endswith(".gz"):
        return gzip.open(path, "rt", errors="replace")
    return open(path, errors="replace")


def stamp(line):
    m = TS.match(line)
    if not m:
        return None
    return datetime.fromisoformat(m.group(1)).timestamp()


def summarize(name, xs):
    if not xs:
        print("  %-36s (none)" % name)
        return
    xs = sorted(xs)
    n = len(xs)
    print("  %-36s n=%5d  mean %6.1f  p50 %6.1f  p90 %6.1f ms"
          % (name, n, 1000 * st.mean(xs), 1000 * xs[n // 2],
             1000 * xs[int(n * 0.9)]))


def decompose(path):
    batch, enc, last_send = [], [], []
    # per-frame timeline keyed by id_server: "first" = first send of that
    # frame's EGFX payload, "last" = its last=1 close
    frames = {}
    for ln in _open(path):
        ts = stamp(ln)
        if ts is None:
            continue
        if "GFX_TRACE batch " in ln:
            k = re.search(r'kids_armed=(\d+)', ln)
            batch.append((ts, int(k.group(1)) if k else 0))
        elif "GFX_TRACE enc submitted_seq" in ln:
            enc.append(ts)
        elif "GFX_TRACE send " in ln:
            m = re.search(r'last=(\d+) frame_id=\d+ id_server=(\d+)', ln)
            if m:
                fid = int(m.group(2))
                f = frames.setdefault(fid, {"arm": None, "last": None})
                if f["arm"] is None:
                    # the batch arm and encode submit that produced this
                    # frame are the most recent ones seen before its first
                    # send -- that is what makes the pairing frame-identity
                    # based rather than cycle-window based
                    f["arm"] = batch[-1][0] if batch else None
                if m.group(1) == "1":
                    f["last"] = ts
            if "last=1" in ln:
                last_send.append(ts)

    if len(batch) < 2:
        print("%s: only %d batch cycles — nothing to decompose "
              "(is XRDP_GFX_TRACE=1 set, and is this a step-7 build?)"
              % (path, len(batch)))
        return

    bt = [b[0] for b in batch]
    period, cap, service, idle = [], [], [], []
    ei = 0
    for i in range(len(bt) - 1):
        b, nb = bt[i], bt[i + 1]
        period.append(nb - b)
        while ei < len(enc) and enc[ei] < b:
            ei += 1
        if ei < len(enc) and enc[ei] < nb:
            cap.append(enc[ei] - b)
        ls = None
        for s in last_send:
            if b <= s < nb:
                ls = s
        if ls is not None:
            service.append(ls - b)
            idle.append(nb - ls)

    print("=== %s ===" % path)
    summarize("batch -> batch (full period)", period)
    summarize("batch arm -> first enc submit", cap)
    summarize("batch arm -> last=1 (service)", service)
    summarize("last=1 -> next batch arm (IDLE)", idle)

    if period and service and cap:
        p = st.mean(period)
        s = st.mean(service)
        c = st.mean(cap)
        print()
        print("  capture + pack        %5.1f ms = %4.1f %% of the period"
              % (1000 * c, 100 * c / p))
        print("  encode + assembly     %5.1f ms = %4.1f %% of the period"
              % (1000 * (s - c), 100 * (s - c) / p))
        print("  idle (awaiting damage)%5.1f ms = %4.1f %% of the period"
              % (1000 * (p - s), 100 * (p - s) / p))
        print("  ---------------------------------------------------")
        print("  OUR PIPELINE          %5.1f ms = %4.1f %% of the period"
              % (1000 * s, 100 * s / p))

    counts = {}
    for _, k in batch:
        counts[k] = counts.get(k, 0) + 1
    print("\n  kids_armed: %s" % ", ".join(
        "%d in %d cycles (%.0f%%)" % (k, c, 100.0 * c / len(batch))
        for k, c in sorted(counts.items())))

    # --- does capture EVER overlap encode? -------------------------------
    #
    # Read this and not `inflight`. `inflight` in the GFX_TRACE enc line is
    # pairs_submitted - pairs_returned INSIDE one ffmpeg child, logged on
    # the same call that submitted the frame. The shipped args are
    # `-tune zerolatency` (libx264) / `-async_depth 1` (VAAPI), which by
    # design return the coded picture on the submitting call — so
    # inflight==0 and rv==READY on every sample, always, at any monitor
    # count and any ack-window size. It is a statement about the encoder
    # child's internal queue depth, NOT about whether xorgxrdp's capture
    # runs concurrently with xrdp's encode. Reading it as the latter is
    # what made BACKLOG #64's first draft wrong.
    #
    # The overlap question is a cross-cycle timestamp question: does cycle
    # N+1's capture begin before cycle N's final send completes? If the two
    # stages pipeline, that gap goes negative. If it is positive on every
    # cycle, the stages are strictly serial and the gap is dead time.
    # Pair by FRAME IDENTITY (id_server), never by cycle window. A cycle
    # window mis-attributes: the `last=1` that falls inside cycle N is the
    # close of the frame captured in cycle N-1, because the next cycle arms
    # before the previous frame's final send goes out. Windowing produced a
    # NEGATIVE "encode + assembly" segment on the 1-monitor run (service
    # 8.2 ms < capture 11.5 ms), which is the arithmetic telling us the
    # pairing is wrong — not a measurement.
    gaps = []
    ids = sorted(frames)
    for a, b in zip(ids, ids[1:]):
        fa, fb = frames[a], frames[b]
        if fa.get("last") is None or fb.get("arm") is None:
            continue
        gaps.append(fb["arm"] - fa["last"])
    if gaps:
        g = sorted(1000.0 * x for x in gaps)
        n = len(g)
        over = sum(1 for x in g if x < 0)
        print()
        print("  OVERLAP CHECK (capture arm of frame N+1 - last=1 of N):")
        print("    n=%d  min %.1f  p10 %.1f  p50 %.1f  mean %.1f  max %.1f ms"
              % (n, g[0], g[n // 10], g[n // 2], st.mean(g), g[-1]))
        print("    cycles where the next capture started BEFORE the previous"
              " send finished: %d (%.1f %%)" % (over, 100.0 * over / n))
        if over == 0:
            print("    -> STRICTLY SERIAL: capture and encode never overlap."
                  " The %.1f ms mean gap is dead time between stages."
                  % st.mean(g))
        else:
            print("    -> pipelined in %.1f %% of cycles."
                  % (100.0 * over / n))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(2)
    for p in sys.argv[1:]:
        decompose(p)
        print()
