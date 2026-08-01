#!/usr/bin/env python3
"""i61f_delivery_chain.py -- who clocks the capture, and where a stall lives.

BACKLOG #61f. Reads one gate capture directory (xrdp.log with ACK_TRACE,
gfx_trace.txt with GFX_TRACE, session-xorg.log with the xorgxrdp-side
ACK_TRACE cap lines) and prints the delivery-chain forensics that
root-caused the ~33 ms enqueue stalls on 2026-08-01:

  1. frame-send holes and the fif window state at each resume
     (id_server - id_client read off the server's own send lines --
     never a reconstructed occupancy; identity pairing, the 2c gate)
  2. the capture trigger: capbegin(N) - slotack_sent(N-2), which must
     stay locked under perturbation if the slot ack is the clock
  3. the xup transit: rect sent_us -> msgin us, i.e. how long a
     captured frame waits for the xrdp main thread to read it
  4. stall echo spacing: a slotack(N-2) feedback loop repeats a stall
     at two-frame spacing

xrdp `us=` and Xorg `begin_us` are both CLOCK_MONOTONIC microseconds,
so the two processes align with no clock fitting.

Usage: i61f_delivery_chain.py <capture-dir> [hole-threshold-ms]
"""
import sys
import os
import re
import datetime
from collections import defaultdict, Counter


def wall(line):
    m = re.match(r"^\[(\d+-\d+-\d+T\d+:\d+:\d+\.\d+)", line)
    if not m:
        return None
    return datetime.datetime.strptime(
        m.group(1), "%Y-%m-%dT%H:%M:%S.%f").timestamp()


def pctl(v, q):
    s = sorted(v)
    return s[min(len(s) - 1, int(q * len(s)))]


def dist(vals, label):
    v = sorted(x for x in vals if x is not None)
    if not v:
        print("   %-36s (no data)" % label)
        return
    n = len(v)
    print("   %-36s n=%4d p10 %7.1f p50 %7.1f p90 %7.1f IQR %6.1f"
          % (label, n, v[n // 10], v[n // 2], v[9 * n // 10],
             v[3 * n // 4] - v[n // 4]))


def main():
    cdir = sys.argv[1]
    thresh = float(sys.argv[2]) if len(sys.argv) > 2 else 50.0

    # --- GFX_TRACE: per-frame header sends + window state ------------
    hdr = []
    seen = set()
    for line in open(os.path.join(cdir, "gfx_trace.txt"), errors="replace"):
        if " GFX_TRACE send bytes=18 " not in line:
            continue
        m = re.search(r"id_server=(\d+) id_client=(\d+) fif=(\d+)", line)
        if not m:
            continue
        i = int(m.group(1))
        if i in seen:
            continue
        seen.add(i)
        hdr.append((wall(line), i, int(m.group(2))))
    hdr = hdr[10:]
    gaps = [(hdr[k + 1][0] - hdr[k][0]) * 1000 for k in range(len(hdr) - 1)]
    holes = [k for k in range(len(gaps)) if gaps[k] > thresh]
    print("== 1. frame sends: %d, holes > %.0f ms: %d" %
          (len(hdr), thresh, len(holes)))
    resume_if = Counter(hdr[k + 1][1] - hdr[k + 1][2] for k in holes)
    print("   fif window (id_server - id_client) at each RESUME:",
          dict(sorted(resume_if.items())),
          " -- 0 means the window sat OPEN through the stall")

    # --- ACK_TRACE, xrdp side ----------------------------------------
    ev = defaultdict(dict)
    for line in open(os.path.join(cdir, "xrdp.log"), errors="replace"):
        if "ACK_TRACE" not in line:
            continue
        m = re.search(r"ACK_TRACE (\w+) id=(\d+)(?: kind=(\w+))?.* us=(\d+)",
                      line)
        if not m:
            continue
        kind = m.group(1) if m.group(1) != "ack" else "ack_" + m.group(3)
        i = int(m.group(2))
        if kind not in ev[i]:
            ev[i][kind] = int(m.group(4))

    # --- ACK_TRACE cap, xorgxrdp side --------------------------------
    cap = {}
    xorg = os.path.join(cdir, "session-xorg.log")
    if os.path.exists(xorg):
        for line in open(xorg, errors="replace"):
            m = re.search(r"ACK_TRACE cap id=(\d+) mon=0 begin_us=(\d+) "
                          r"packed_us=(\d+) sent_us=(\d+) ack=(-?\d+) "
                          r"shown=(-?\d+)", line)
            if m:
                cap[int(m.group(1))] = tuple(int(m.group(g))
                                             for g in range(2, 7))

    msgin = {i: ev[i]["msgin"] for i in ev if "msgin" in ev[i]}
    mids = sorted(msgin)[10:]
    stall = set()
    for k in range(len(mids) - 1):
        if (msgin[mids[k + 1]] - msgin[mids[k]]) / 1000 > thresh:
            stall.add(mids[k + 1])

    if cap:
        front = Counter((n - cap[n][3], n - cap[n][4])
                        for n in cap if n in msgin)
        print("== 2. capture trigger (xorgxrdp frontier at capture:"
              " (N-ack, N-shown) = %s)" % dict(front.most_common(3)))
        for sel, tag in [([n for n in mids if n not in stall], "normal"),
                         ([n for n in mids if n in stall], "stalled")]:
            good = [n for n in sel if n in cap and "ack_slot" in ev.get(n - 2, {})]
            dist([(cap[n][0] - ev[n - 2]["ack_slot"]) / 1000 for n in good],
                 "capbegin(N) - slotack_sent(N-2), " + tag)

    print("== 3. xup transit: captured rect -> main thread reads it")
    for sel, tag in [([n for n in mids if n not in stall], "normal"),
                     ([n for n in mids if n in stall], "stalled")]:
        good = [n for n in sel if n in cap]
        dist([(msgin[n] - cap[n][2]) / 1000 for n in good],
             "cap sent_us -> msgin, " + tag)

    st = sorted(stall)
    sp = [b - a for a, b in zip(st, st[1:])]
    print("== 4. stall echo: consecutive-stall spacing histogram:",
          dict(sorted(Counter(sp).items())[:8]),
          " -- modal 2 = the slotack(N-2) loop echoing")


main()
