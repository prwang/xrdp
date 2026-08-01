#!/usr/bin/env python3
"""i61g_sched_attribute.py -- read the schedstat sample against the frame chain.

BACKLOG #61g Step 1.  Takes a gate capture that was run with E_SCHED=1 and
answers, for the oracle client's pauses, which of three things happened:

  waited for a CPU   run-delay (schedstat field 2) grows during the pause
  was running        on-CPU time (field 1) grows during the pause
  was asleep         neither grows -- the pause is internal to the client
                     (a lock, a blocking write, a decode, a page fault)

and, for the xrdp main thread, what fraction of the period it is on-CPU
(BACKLOG #61f: the captured rect waits ~16 ms for that thread, and a
thread that is on-CPU is a thread doing work we could move or cut, while
a sleeping one is waiting for something else).

Client pauses are located from the server's own record of when client
frame acks arrived -- `GFX_TRACE ack frame_id=` lines in xrdp.log, which
the main thread writes on receipt.  A gap between consecutive ack
arrivals is a window in which the client sent nothing back.

Usage: i61g_sched_attribute.py <capture-dir> [hole-threshold-ms]
"""
import sys
import os
import re
import datetime
import statistics
from collections import defaultdict


def wall(line):
    m = re.match(r"^\[(\d+-\d+-\d+T\d+:\d+:\d+\.\d+)", line)
    if not m:
        return None
    return datetime.datetime.strptime(
        m.group(1), "%Y-%m-%dT%H:%M:%S.%f").timestamp()


def pct(v, q):
    s = sorted(v)
    return s[min(len(s) - 1, int(q * len(s)))]


def nsec_placeholder(span, durs):
    """wall seconds NOT inside a hole"""
    return max(1e-6, span - sum(durs) / 1000.0)


def main():
    cdir = sys.argv[1]
    thresh = float(sys.argv[2]) if len(sys.argv) > 2 else 50.0

    # --- the wall <-> CLOCK_MONOTONIC offset, from records carrying both --
    offs = []
    acks = []
    sends = defaultdict(list)
    for line in open(os.path.join(cdir, "xrdp.log"), errors="replace"):
        if "ACK_TRACE" in line:
            m = re.search(r"ACK_TRACE \w+ id=\d+.* us=(\d+)", line)
            if m:
                offs.append(wall(line) - int(m.group(1)) / 1e6)
        elif "GFX_TRACE ack frame_id=" in line:
            acks.append(wall(line))
        elif "GFX_TRACE send bytes=" in line:
            m = re.search(r"bytes=(\d+) .*id_server=(\d+)", line)
            if m:
                sends[int(m.group(2))].append((wall(line), int(m.group(1))))
    off = statistics.median(offs)
    acks = sorted(set(a - off for a in acks if a))[5:]

    # --- the sample ------------------------------------------------------
    prev = {}
    series = defaultdict(list)     # (label, tid) -> [(us, dcpu, dwait, state)]
    comm = {}
    for line in open(os.path.join(cdir, "sched.csv")):
        f = line.rstrip("\n").split(",")
        if len(f) < 9 or f[0] == "us":
            continue
        us, label, tid, cm, st = int(f[0]), f[1], int(f[3]), f[4], f[5]
        cpu, wait = int(f[6]), int(f[7])
        comm[(label, tid)] = cm
        k = (label, tid)
        if k in prev:
            pus, pc, pw = prev[k]
            if us > pus:
                series[k].append((us, cpu - pc, wait - pw, st))
        prev[k] = (us, cpu, wait)
    t0 = min(s[0][0] for s in series.values() if s) / 1e6
    t1 = max(s[-1][0] for s in series.values() if s) / 1e6
    span = t1 - t0
    print("sample: %d threads over %.1f s" % (len(series), span))

    # --- who burns CPU ---------------------------------------------------
    tot = [(sum(d for _, d, _, _ in s) / 1e9, k) for k, s in series.items()]
    tot.sort(reverse=True)
    print("\n== busiest threads (CPU seconds over the sample) ==")
    for cpu_s, k in tot[:10]:
        w = sum(d for _, _, d, _ in series[k]) / 1e9
        print("   %-7s %-18s tid %-8d cpu %6.2f s (%4.0f%% of a core)  "
              "run-delay %5.2f s" % (k[0], comm[k], k[1], cpu_s,
                                     100 * cpu_s / span, w))

    # --- aggregate per label --------------------------------------------
    print("\n== per side ==")
    for label in sorted({k[0] for k in series}):
        c = sum(sum(d for _, d, _, _ in s) for k, s in series.items()
                if k[0] == label) / 1e9
        w = sum(sum(d for _, _, d, _ in s) for k, s in series.items()
                if k[0] == label) / 1e9
        print("   %-7s cpu %6.2f s = %.2f cores; run-delay %6.3f s = %.1f%% "
              "of its CPU time" % (label, c, c / span, w,
                                   100 * w / c if c else 0))

    # --- the client's pauses --------------------------------------------
    holes = [(acks[i], acks[i + 1]) for i in range(len(acks) - 1)
             if (acks[i + 1] - acks[i]) * 1000 > thresh]
    gaps = [(acks[i + 1] - acks[i]) * 1000 for i in range(len(acks) - 1)]
    print("\n== client ack arrivals: %d, gaps p50 %.1f p90 %.1f ms; "
          "holes > %.0f ms: %d" % (len(acks), pct(gaps, .5), pct(gaps, .9),
                                   thresh, len(holes)))
    if not holes:
        print("   no holes at this threshold -- nothing to attribute")
        return

    # Only the client threads that DO anything. Aggregating over all ~170
    # threads of the oracle client drowns the three working ones in idle
    # ones -- a state histogram taken over the lot reads "99 % asleep" no
    # matter what the busy threads are doing.
    cl = [k for k in series if k[0] == "client"
          and sum(d for _, d, _, _ in series[k]) / 1e9 > 0.05 * span]
    print("   working client threads: " +
          ", ".join("%s/%d" % (comm[k], k[1]) for k in cl))
    idx = {k: 0 for k in cl}
    rows = []
    for a, b in holes:
        cpu = wait = 0
        states = defaultdict(int)
        for k in cl:
            s = series[k]
            i = idx[k]
            while i < len(s) and s[i][0] / 1e6 < a:
                i += 1
            idx[k] = i
            while i < len(s) and s[i][0] / 1e6 <= b:
                cpu += s[i][1]
                wait += s[i][2]
                states[s[i][3]] += 1
                i += 1
        dur = (b - a) * 1000
        rows.append((dur, cpu / 1e6, wait / 1e6, dict(states)))
    durs = [r[0] for r in rows]
    print("   hole length ms: p50 %.0f p90 %.0f max %.0f"
          % (pct(durs, .5), pct(durs, .9), max(durs)))
    print("   during a hole, summed over ALL client threads:")
    print("     CPU consumed      p50 %6.1f ms  (%.0f%% of the hole)"
          % (pct([r[1] for r in rows], .5),
             100 * statistics.mean(r[1] for r in rows)
             / statistics.mean(durs)))
    print("     run-delay accrued p50 %6.1f ms  (%.0f%% of the hole)"
          % (pct([r[2] for r in rows], .5),
             100 * statistics.mean(r[2] for r in rows)
             / statistics.mean(durs)))
    st = defaultdict(int)
    for r in rows:
        for k, v in r[3].items():
            st[k] += v
    n = sum(st.values()) or 1
    print("     thread states    " +
          "  ".join("%s %.1f%%" % (k, 100 * v / n)
                    for k, v in sorted(st.items(), key=lambda x: -x[1])))
    print("     (R = runnable/running, S = interruptible sleep, "
          "D = uninterruptible sleep)")

    # per working thread, inside vs outside the holes: the shape that
    # separates "this thread is the pause" from "this thread does not care"
    print("   per working client thread, ms of CPU per second of wall:")
    for k in cl:
        ic = oc = 0
        istate = defaultdict(int)
        for us, d, w, s in series[k]:
            t = us / 1e6
            if any(a <= t <= b for a, b in holes):
                ic += d
                istate[s] += 1
            else:
                oc += d
        tot_i = sum(istate.values()) or 1
        print("     %-18s outside %5.0f   inside %5.0f   "
              "states in holes: %s" % (
                  comm[k], oc / 1e6 / nsec_placeholder(span, durs),
                  ic / 1e6 / (sum(durs) / 1000.0),
                  " ".join("%s %.0f%%" % (s, 100 * v / tot_i)
                           for s, v in sorted(istate.items(),
                                              key=lambda x: -x[1])[:3])))

    # baseline: the same quantities outside holes, per equal wall time
    hs = set()
    for a, b in holes:
        hs.add((a, b))

    def inhole(t):
        return any(a <= t <= b for a, b in hs)

    ncpu = nwait = nsec = 0
    for k in cl:
        for us, d, w, _ in series[k]:
            if not inhole(us / 1e6):
                ncpu += d
                nwait += w
    nsec = span - sum(durs) / 1000.0
    print("   outside holes, per second of wall time: client CPU %.0f ms/s, "
          "run-delay %.1f ms/s" % (ncpu / 1e6 / nsec, nwait / 1e6 / nsec))
    hsec = sum(durs) / 1000.0
    print("   inside  holes, per second of wall time: client CPU %.0f ms/s, "
          "run-delay %.1f ms/s"
          % (sum(r[1] for r in rows) / hsec, sum(r[2] for r in rows) / hsec))


main()
