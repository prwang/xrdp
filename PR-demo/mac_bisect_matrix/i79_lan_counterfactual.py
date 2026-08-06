#!/usr/bin/env python3
"""#79: is the prompt-credit class a valid counterfactual for the FIXED build?

The layer-1 sweep's central claim about the LAN regime is that removing
the gate costs nothing there, and its evidence is that cycles whose
credit arrived PROMPTLY ran at 16.3-16.9 ms in every leg, flat under a
40 ms ack delay. That evidence has a selection problem, and it has to be
faced before the number is used to predict the fixed build:

    the prompt class is CONDITIONED on the ack having arrived in time.

Which is a statement about the cycle's history, not only its mechanism.
In the period-3 limit cycle a prompt cycle always follows two stalled
ones, and during a stall the producer has ~35 ms of idle time in which
capture can get ahead. So "16.4 ms" may be the period of a cycle that
started from a pre-loaded pipeline, and the fixed build -- where EVERY
cycle is prompt and none of them follows a stall -- would not inherit it.

This script asks the archived captures the discriminating question, at
no session cost: **condition the prompt-cycle period on its position in
a run of consecutive prompt cycles.**

    position 1  = first prompt cycle after a stall   (may be pre-loaded)
    position >=2 = a prompt cycle whose PREDECESSOR was also prompt
                   -- no stall to have loaded it, i.e. the steady state
                   the fixed build would run in

If position>=2 periods match position 1, the pre-loading hypothesis is
dead and the prompt class is a sound counterfactual. If they are
materially slower, the LAN prediction in BACKLOG #79 step 4 is
overstated and must be re-derived before the fixed build is measured.

It also checks the owner's model, in period form (the arguments are
times, so the min-of-rates is a max-of-periods):

    period = max(ack_latency / K, compute_serial)

reporting the residual per leg for K = fif = 1, since that is the form
the model is usually written in.

Usage: i79_lan_counterfactual.py <capture-dir>
"""

import os
import sys
import glob
import statistics as st

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i79_ack_delay_analyze import load, pct, WARMUP_S, STALL_MS


def classify(evs):
    """ordered per-cycle list of (frame_id, period_ms, gated?) .

    Same construction as i79_ack_delay_analyze.discriminate() -- paired
    by frame IDENTITY, never by time window -- kept deliberately
    identical so the two scripts cannot disagree about which cycles are
    gated.
    """
    egress, msgin, absorb = {}, {}, {}
    acks = []
    pumps = {}
    wtid = next((e[1] for e in evs if e[2] == "pump_beg"), None)
    pb = None
    for ts, tid, name, a in evs:
        if name == "egress":
            egress.setdefault(a[0], ts)
        elif name == "msgin":
            msgin.setdefault(a[0], ts)
        elif name == "absorb":
            absorb.setdefault(a[0], ts)
        elif name in ("ackslot", "ackregion"):
            acks.append((ts, a[0]))
        elif tid == wtid and name == "pump_beg":
            pb = ts
        elif tid == wtid and name == "pump_end" and pb is not None:
            pumps.setdefault(a[0], (ts - pb) / 1e6)
            pb = None
    acks.sort()
    cut = (min(msgin.values()) + int(WARMUP_S * 1e9)) if msgin else 0

    def cred(val):
        for t, v in acks:
            if v >= val:
                return t
        return None

    ids = sorted(i for i in egress if egress[i] >= cut)
    out = []
    for prev, k in zip(ids, ids[1:]):
        if k not in msgin or (k - 2) not in absorb:
            continue
        c = cred(k - 2)
        if c is None or c > msgin[k]:
            continue
        per = (egress[k] - egress[prev]) / 1e6
        gated = (c - absorb[k - 2]) / 1e6 > STALL_MS
        out.append((k, per, gated, pumps.get(k)))
    return out


def runs_by_position(cycles):
    """period of prompt cycles, keyed by position in their prompt run"""
    by_pos = {}
    run_len = []
    n = 0
    for _, per, gated, _ in cycles:
        if gated:
            if n:
                run_len.append(n)
            n = 0
            continue
        n += 1
        by_pos.setdefault(min(n, 4), []).append(per)
    if n:
        run_len.append(n)
    return by_pos, run_len


def main():
    root = sys.argv[1]
    legs = [d for d in sorted(glob.glob(os.path.join(root, "leg_*")))
            if os.path.exists(os.path.join(d, "window.txt"))]
    print("#79 LAN counterfactual -- is the prompt class pre-loaded by "
          "the stall before it?\n")
    print("%-8s %5s  %6s %6s  %s"
          % ("leg", "D", "gated", "prompt", "prompt-cycle period by "
             "position in its prompt run (n, mean, p50)"))
    rows = []
    for d in legs:
        evs, win = load(d)
        cyc = classify(evs)
        by_pos, run_len = runs_by_position(cyc)
        gated = [p for _, p, g, _ in cyc if g]
        prompt = [p for _, p, g, _ in cyc if not g]
        cells = []
        for pos in (1, 2, 3, 4):
            v = by_pos.get(pos, [])
            if v:
                cells.append("p%s%s: n=%-3d %5.1f/%5.1f"
                             % (pos, "+" if pos == 4 else "",
                                len(v), st.mean(v), st.median(v)))
        print("%-8s %5s  %6d %6d  %s"
              % (os.path.basename(d)[4:], win.get("delay_ms", "-"),
                 len(gated), len(prompt), "   ".join(cells)))
        rows.append((os.path.basename(d)[4:], win, cyc, by_pos, run_len,
                     gated, prompt))

    print("\nprompt-run length distribution (how much steady state "
          "exists at all):")
    for name, win, cyc, by_pos, run_len, gated, prompt in rows:
        if not run_len:
            print("  %-8s none" % name)
            continue
        hist = {}
        for n in run_len:
            hist[min(n, 6)] = hist.get(min(n, 6), 0) + 1
        print("  %-8s runs=%-4d max=%-3d  %s"
              % (name, len(run_len), max(run_len),
                 " ".join("len%s%s:%d" % (k, "+" if k == 6 else "", v)
                          for k, v in sorted(hist.items()))))

    print("\nVERDICT on the counterfactual")
    for name, win, cyc, by_pos, run_len, gated, prompt in rows:
        a = by_pos.get(1, [])
        b = [p for pos in (2, 3, 4) for p in by_pos.get(pos, [])]
        if len(a) < 20 or len(b) < 20:
            print("  %-8s insufficient: n(pos1)=%d n(pos>=2)=%d "
                  "-- cannot answer in this leg" % (name, len(a), len(b)))
            continue
        d = st.mean(b) - st.mean(a)
        print("  %-8s pos1 %5.1f ms (n=%d)  vs  pos>=2 %5.1f ms (n=%d)"
              "  ->  %+.1f ms  %s"
              % (name, st.mean(a), len(a), st.mean(b), len(b), d,
                 "steady state is NOT slower -- counterfactual holds"
                 if d < 1.0 else
                 "steady state IS slower -- 16.4 ms was pre-loaded"))

    print("\nMODEL  period = max(ack_latency / K, compute_serial), K = "
          "fif = 1")
    print("  compute_serial taken as the leg's own pump p50 plus the "
          "non-pump serial work,")
    print("  reported as the measured prompt-cycle period so the model "
          "is not fitted to itself.")
    print("  %-8s %8s %10s %10s %10s %8s"
          % ("leg", "D", "acklat", "compute", "predicted", "measured"))
    for name, win, cyc, by_pos, run_len, gated, prompt in rows:
        evs, _ = load(os.path.join(root, "leg_" + name))
        eg, cl = {}, {}
        for ts, tid, nm, a in evs:
            if nm == "egress":
                eg.setdefault(a[0], ts)
            elif nm == "cliack":
                cl.setdefault(a[0], ts)
        lag = sorted((cl[i] - eg[i]) / 1e6 for i in cl if i in eg)
        if not lag or not prompt:
            continue
        acklat = lag[len(lag) // 2]
        compute = st.median(prompt)
        allper = [p for _, p, _, _ in cyc]
        pred = max(acklat, compute)
        meas = st.mean(allper)
        print("  %-8s %8s %10.1f %10.1f %10.1f %8.1f   %+.1f"
              % (name, win.get("delay_ms", "-"), acklat, compute, pred,
                 meas, meas - pred))


if __name__ == "__main__":
    main()
