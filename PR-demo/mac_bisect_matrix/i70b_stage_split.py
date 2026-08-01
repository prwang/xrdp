#!/usr/bin/env python3
"""i70b_stage_split.py -- decompose the encoder worker's cycle from the
dedicated perf-trace sink (common/perf_trace.c), armed by
XRDP_PERF_TRACE.

BACKLOG #70's follow-up could only difference the ACK_TRACE stamps and
concluded "7.9 ms of worker CPU per frame between absorb(N) and
submit(N+1)" without being able to name the stage. This reads the
bracket pairs directly:

  drain   drain_beg -> drain_end     take items off the fifo (mutex held)
  subm    subm_beg  -> subm_end      parse + hand each pair to its child
  pump    pump_beg  -> pump_end      ONE poll set over every armed child
  coll    coll_beg  -> coll_end      NUT pop + LTR rewrite of BOTH views
  emit    emit_beg  -> emit_end      build the EGFX PDUs, queue to main
  gap     emit_end  -> next drain_beg   loop overhead, and any blocking
                                        wait for the next frame

Records are "<monotonic_ns> <tid> <tag> <a> <b>". Stages are paired
within one thread, in order -- a beg with no matching end is dropped
rather than paired across a cycle boundary, and a NEGATIVE duration is
reported as a hard error rather than averaged in (quality gate 2c).

Usage: i70b_stage_split.py <capture-dir> [<capture-dir> ...]
"""
import os
import sys

STAGES = ["drain", "subm", "pump", "coll", "emit"]


def pct(vals, p):
    if not vals:
        return float("nan")
    s = sorted(vals)
    return s[min(len(s) - 1, int(p * len(s)))]


def load(path):
    evs = []
    for line in open(path, errors="replace"):
        f = line.split()
        if len(f) != 5:
            continue
        try:
            evs.append((int(f[0]), f[1], f[2], int(f[3]), int(f[4])))
        except ValueError:
            continue
    return evs


def report(d):
    path = os.path.join(d, "perf_enc.trace")
    if not os.path.exists(path):
        print("%s: no perf_enc.trace" % os.path.basename(d))
        return
    evs = load(path)
    print("=" * 68)
    print("%s   %d records" % (os.path.basename(d), len(evs)))

    # --- pair each stage within its own thread, in order ---
    durs = {s: [] for s in STAGES}
    open_at = {}
    negative = 0
    for ns, tid, tag, a, b in evs:
        if "_" not in tag:
            continue
        stage, half = tag.rsplit("_", 1)
        if stage not in durs:
            continue
        key = (tid, stage)
        if half == "beg":
            open_at[key] = ns
        elif half == "end" and key in open_at:
            dt = ns - open_at.pop(key)
            if dt < 0:
                negative += 1
            else:
                durs[stage].append(dt / 1e6)
    if negative:
        print("  ERROR: %d NEGATIVE stage durations -- the pairing is "
              "broken, do not read the table below" % negative)

    # --- the loop gap: emit_end -> the next drain_beg on that thread ---
    gaps = []
    last_emit_end = {}
    for ns, tid, tag, a, b in evs:
        if tag == "emit_end":
            last_emit_end[tid] = ns
        elif tag == "drain_beg" and tid in last_emit_end:
            gaps.append((ns - last_emit_end.pop(tid)) / 1e6)

    # --- the whole cycle, drain_beg to drain_beg ---
    cycle = []
    last_drain = {}
    for ns, tid, tag, a, b in evs:
        if tag == "drain_beg":
            if tid in last_drain:
                cycle.append((ns - last_drain[tid]) / 1e6)
            last_drain[tid] = ns

    print("  %-10s %6s %8s %8s %8s %8s %9s" %
          ("stage", "n", "mean", "p50", "p90", "max", "sum/frame"))
    n_cycles = max(1, len(cycle) + 1)
    total = 0.0
    for s in STAGES:
        v = durs[s]
        if not v:
            continue
        per_frame = sum(v) / n_cycles
        total += per_frame
        print("  %-10s %6d %7.2f%s %7.2f %7.2f %7.2f %8.2f ms"
              % (s, len(v), sum(v) / len(v), "ms", pct(v, .5), pct(v, .9),
                 max(v), per_frame))
    if gaps:
        per_frame = sum(gaps) / n_cycles
        total += per_frame
        print("  %-10s %6d %7.2f%s %7.2f %7.2f %7.2f %8.2f ms"
              % ("gap", len(gaps), sum(gaps) / len(gaps), "ms",
                 pct(gaps, .5), pct(gaps, .9), max(gaps), per_frame))
    print("  %-10s %41s %8.2f ms" % ("SUM", "", total))
    if cycle:
        print("  %-10s %6d %7.2f%s %7.2f %7.2f %7.2f"
              % ("CYCLE", len(cycle), sum(cycle) / len(cycle), "ms",
                 pct(cycle, .5), pct(cycle, .9), max(cycle)))
        # gate 1: the parts must add up to the whole
        obs = sum(cycle) / len(cycle)
        print("  closure: stages sum to %.2f ms against a %.2f ms cycle "
              "(%.2f ms unattributed)" % (total, obs, obs - total))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for d in sys.argv[1:]:
        report(d)
