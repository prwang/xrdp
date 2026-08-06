#!/usr/bin/env python3
"""i79_ack_delay_analyze.py -- read an ack-delay sweep and decide whether
#78's mechanism survives it. BACKLOG #79, test layer 1.

THE CLAIM UNDER TEST (#78 / #79). At fif = 1 the producer's slot-recycle
credit is emitted only while xrdp_gfx_ack_window_open(client, server,
fif) is true, so it waits for the CLIENT's frame ack even though the
in-tree safety condition (the children have absorbed frame k-2's input)
was met earlier. The delay between those two instants is `withheld`.

THE PREDICTIONS, stated before the run (a metric chosen after seeing the
data proves nothing):

  P1  withheld p50 tracks D — the credit is waiting on the delayed ack,
      so delaying that ack by D delays the credit by ~D.
  P2  the stall fraction (withheld > 10 ms) goes from ~31 % at D = 0 to
      ~100 % once D exceeds the slack in the race (D >= ~10 ms).
  P3  the send period rises with D — the withheld credit is on the
      critical path, not merely correlated with it.
  P0  the injected delay actually applied, measured INSIDE the server:
      egress -> cliack latency rises by ~D. (Quality gate 2: a knob that
      was set is not a knob that applied. This check does not trust the
      proxy's own telemetry.)

Falsifiers, equally explicit: if withheld stays flat while the period
rises, the period is paced by something else and #78 named the wrong
mechanism. If withheld rises but the period does not, the withholding is
real but off the critical path (the fix would be pointless). If D = 0
through the proxy does not reproduce the no-proxy leg, the instrument is
a confound and NO leg is readable.

Pairing is by identity everywhere (never by time window — the 2c gate).

Usage: i79_ack_delay_analyze.py <sweep-capture-dir>
"""
import glob
import os
import statistics as st
import sys

WARMUP_S = 1.0          # dropped from every leg alike, and reported
STALL_MS = 10.0         # the >10 ms threshold #78 quoted


def load(legdir):
    """merge every ring file, on the wall clock, inside this leg's window"""
    win = {}
    for line in open(os.path.join(legdir, "window.txt")):
        k, v = line.split()
        win[k] = v
    t0 = int(win["t0"]) * 10 ** 9
    t1 = int(win["t1"]) * 10 ** 9
    evs = []
    for path in glob.glob(os.path.join(legdir, "perf", "enc.*")):
        base_mono = base_real = None
        for line in open(path, errors="replace"):
            if line.startswith("# perfbase"):
                f = line.split()
                base_mono, base_real = int(f[3]), int(f[5])
                continue
            f = line.split()
            if len(f) != 9 or base_mono is None:
                continue
            try:
                ns = int(f[0])
                a = [int(x) for x in f[3:9]]
            except ValueError:
                continue
            real = base_real + (ns - base_mono)
            if t0 <= real <= t1:
                evs.append((real, f[1], f[2], a))
    evs.sort(key=lambda e: e[0])
    return evs, win


def pct(v, p):
    if not v:
        return float("nan")
    s = sorted(v)
    return s[min(len(s) - 1, int(p * len(s)))]


def summarize(evs, delay_ms):
    """every number below is per-frame-identity, not per time window"""
    msgin, absorb, egress, cliack, take = {}, {}, {}, {}, {}
    acks = []
    waits, pumps = [], []
    wtid = next((e[1] for e in evs if e[2] == "pump_beg"), None)
    wb = pb = None
    for ts, tid, name, a in evs:
        if name == "msgin":
            msgin.setdefault(a[0], ts)
        elif name == "absorb":
            absorb.setdefault(a[0], ts)
        elif name == "egress":
            egress.setdefault(a[0], ts)
        elif name == "cliack":
            cliack.setdefault(a[0], ts)
        elif name == "take":
            take.setdefault(a[0], ts)
        elif name == "ackslot":
            acks.append((ts, a[0], "slot"))
        elif name == "ackregion":
            acks.append((ts, a[0], "region"))
        elif tid == wtid and name == "wait_beg":
            wb = ts
        elif tid == wtid and name == "wait_end" and wb is not None:
            waits.append((wb, (ts - wb) / 1e6))
            wb = None
        elif tid == wtid and name == "pump_beg":
            pb = ts
        elif tid == wtid and name == "pump_end" and pb is not None:
            pumps.append((ts - pb) / 1e6)
            pb = None
    acks.sort()

    # warm-up: the first second after the session's first capture is the
    # surface-creation transient. Dropped from every leg identically.
    if msgin:
        cut = min(msgin.values()) + int(WARMUP_S * 1e9)
    else:
        cut = 0

    def first_cred_geq(val):
        for t, v, kind in acks:
            if v >= val:
                return (t, v, kind)
        return None

    withheld, execu, kinds = [], [], []
    for k in sorted(msgin):
        if msgin[k] < cut or (k - 2) not in absorb:
            continue
        cred = first_cred_geq(k - 2)
        if cred is None or cred[0] > msgin[k]:
            continue
        withheld.append((cred[0] - absorb[k - 2]) / 1e6)
        execu.append((msgin[k] - cred[0]) / 1e6)
        kinds.append(cred[2])

    # P0: did the delay apply, measured inside the server?
    lag = [(cliack[i] - egress[i]) / 1e6 for i in cliack
           if i in egress and egress[i] >= cut and cliack[i] > egress[i]]
    # P3: the period, egress to egress (one per cycle, id-keyed)
    eg = sorted(t for t in egress.values() if t >= cut)
    period = [(b - a) / 1e6 for a, b in zip(eg, eg[1:])]
    w = [d for t, d in waits if t >= cut]
    p = pumps[int(len(pumps) * WARMUP_S / 60):] if pumps else []

    neg = sum(1 for x in withheld if x < -0.001)
    return {
        "delay": delay_ms,
        "cycles": len(eg),
        "withheld_n": len(withheld),
        "withheld_neg": neg,
        "withheld_p50": st.median(withheld) if withheld else float("nan"),
        "withheld_p90": pct(withheld, 0.9),
        "withheld_mean": st.mean(withheld) if withheld else float("nan"),
        "stall_frac": (sum(1 for x in withheld if x > STALL_MS)
                       / len(withheld)) if withheld else float("nan"),
        "stall_n": sum(1 for x in withheld if x > STALL_MS),
        "exec_p50": st.median(execu) if execu else float("nan"),
        "solo_slot": sum(1 for k in kinds if k == "slot"),
        "lag_n": len(lag),
        "lag_p50": st.median(lag) if lag else float("nan"),
        "lag_p90": pct(lag, 0.9),
        "period_mean": st.mean(period) if period else float("nan"),
        "period_p50": st.median(period) if period else float("nan"),
        "period_p90": pct(period, 0.9),
        "wait_mean": st.mean(w) if w else float("nan"),
        "wait_p90": pct(w, 0.9),
        "pump_p50": st.median(p) if p else float("nan"),
    }


def discriminate(evs):
    """separate #78's mechanism from the obvious rival.

    Delaying the client's acks would ALSO slow the server if egress were
    gated by the same window -- xrdp refusing to SEND until the client
    acknowledged. That rival predicts the same period curve, so the
    sweep cannot tell them apart on the period alone. Two things do:

      * what the server sends while unacked. `send` carries id_server
        and id_client; if frames go out with id_server - id_client >= 1
        the send path is not waiting for the client.
      * which cycles are the slow ones. If the withheld credit is on the
        critical path, the cycles whose capture waited are the slow
        cycles and the rest run at the ungated period. If instead
        everything is uniformly later, both classes slow down together.
    """
    egress, msgin, absorb = {}, {}, {}
    acks = []
    outstanding = {}
    for ts, tid, name, a in evs:
        if name == "send":
            outstanding[a[3] - a[4]] = outstanding.get(a[3] - a[4], 0) + 1
        elif name == "egress":
            egress.setdefault(a[0], ts)
        elif name == "msgin":
            msgin.setdefault(a[0], ts)
        elif name == "absorb":
            absorb.setdefault(a[0], ts)
        elif name == "ackslot":
            acks.append((ts, a[0]))
        elif name == "ackregion":
            acks.append((ts, a[0]))
    acks.sort()
    if msgin:
        cut = min(msgin.values()) + int(WARMUP_S * 1e9)
    else:
        cut = 0

    def cred(val):
        for t, v in acks:
            if v >= val:
                return t
        return None

    # period of the cycle that ENDS at egress(k), split by whether the
    # capture that started it had its credit withheld
    ids = sorted(i for i in egress if egress[i] >= cut)
    gated, free = [], []
    for prev, k in zip(ids, ids[1:]):
        if k not in msgin or (k - 2) not in absorb:
            continue
        c = cred(k - 2)
        if c is None or c > msgin[k]:
            continue
        per = (egress[k] - egress[prev]) / 1e6
        if (c - absorb[k - 2]) / 1e6 > STALL_MS:
            gated.append(per)
        else:
            free.append(per)
    return outstanding, gated, free


def main():
    root = sys.argv[1]
    legs = []
    for name in ["direct"] + ["d%s" % d for d in (0, 10, 20, 40)]:
        legdir = os.path.join(root, "leg_" + name)
        if not os.path.isdir(legdir):
            continue
        evs, win = load(legdir)
        dly = win.get("delay_ms", "none")
        r = summarize(evs, dly)
        r["leg"] = name
        r["events"] = len(evs)
        r["outstanding"], r["gated"], r["free"] = discriminate(evs)
        legs.append(r)

    print("=== #79 layer 1: ack-delay sweep on the UNMODIFIED build ===")
    print("warm-up dropped: first %.1f s of each leg; "
          "stall threshold: withheld > %.0f ms\n" % (WARMUP_S, STALL_MS))
    hdr = ("leg      D   cyc   egress->cliack     withheld (ms)        "
           "stall   period (ms)          wait")
    print(hdr)
    print("                    p50    p90     p50    p90   mean      "
          "  >10ms  mean   p50    p90    mean")
    for r in legs:
        print("%-7s %-4s %4d  %6.1f %6.1f  %6.2f %6.1f %6.2f   %5.1f%%  "
              "%5.1f %5.1f %6.1f  %5.1f"
              % (r["leg"], r["delay"], r["cycles"], r["lag_p50"],
                 r["lag_p90"], r["withheld_p50"], r["withheld_p90"],
                 r["withheld_mean"], 100 * r["stall_frac"],
                 r["period_mean"], r["period_p50"], r["period_p90"],
                 r["wait_mean"]))
    print()
    for r in legs:
        print("  %-7s n_withheld=%d (neg %d) stalls=%d/%d  "
              "solo-slot credits=%d  capture exec p50=%.2f ms  pump p50=%.2f"
              % (r["leg"], r["withheld_n"], r["withheld_neg"], r["stall_n"],
                 r["withheld_n"], r["solo_slot"], r["exec_p50"],
                 r["pump_p50"]))

    print("\n=== is it the SLOT credit, or is egress ack-gated? ===")
    print("leg      D    sends by (id_server - id_client) at send time"
          "        cycle period, ms")
    print("                                                          "
          "     credit withheld   credit prompt")
    for r in legs:
        hist = " ".join("%d:%d" % (k, v)
                        for k, v in sorted(r["outstanding"].items()))
        g, f = r["gated"], r["free"]
        print("%-7s %-4s %-42s %6.1f (n=%3d)  %6.1f (n=%3d)"
              % (r["leg"], r["delay"], hist,
                 st.mean(g) if g else float("nan"), len(g),
                 st.mean(f) if f else float("nan"), len(f)))

    by = {r["leg"]: r for r in legs}
    print("\n=== the four predictions ===")
    ok = {}
    if "direct" in by and "d0" in by:
        a, b = by["direct"], by["d0"]
        d = abs(a["period_mean"] - b["period_mean"])
        ok["control"] = d < 0.15 * a["period_mean"]
        print("CONTROL  proxy at D=0 vs no proxy: period %.1f vs %.1f ms "
              "(%.1f%% apart), withheld p90 %.1f vs %.1f  -> %s"
              % (a["period_mean"], b["period_mean"],
                 100 * d / a["period_mean"], a["withheld_p90"],
                 b["withheld_p90"],
                 "OK" if ok["control"] else "PROXY IS A CONFOUND"))
    doses = [by[k] for k in ("d0", "d10", "d20", "d40") if k in by]
    if len(doses) >= 2:
        base = doses[0]
        p0 = all(abs((r["lag_p50"] - base["lag_p50"])
                     - float(r["delay"])) < 5.0 for r in doses[1:])
        print("P0  delay applied in-server (egress->cliack rises by D): "
              + ", ".join("D=%s:+%.1f" % (r["delay"],
                                          r["lag_p50"] - base["lag_p50"])
                          for r in doses) + "  -> %s"
              % ("OK" if p0 else "THE KNOB DID NOT APPLY"))
        p1 = all(abs((r["withheld_p50"] - base["withheld_p50"])
                     - float(r["delay"])) < 0.5 * float(r["delay"]) + 3.0
                 for r in doses[1:])
        print("P1  withheld p50 tracks D: "
              + ", ".join("D=%s:%.2f" % (r["delay"], r["withheld_p50"])
                          for r in doses) + "  -> %s"
              % ("CONFIRMED" if p1 else "FALSIFIED"))
        p2 = all(r["stall_frac"] > 0.9 for r in doses[1:])
        print("P2  stall fraction -> ~100%% for D>=10: "
              + ", ".join("D=%s:%.0f%%" % (r["delay"], 100 * r["stall_frac"])
                          for r in doses) + "  -> %s"
              % ("CONFIRMED" if p2 else "FALSIFIED"))
        p3 = all(doses[i + 1]["period_mean"] > doses[i]["period_mean"]
                 for i in range(len(doses) - 1))
        print("P3  period rises with D: "
              + ", ".join("D=%s:%.1f" % (r["delay"], r["period_mean"])
                          for r in doses) + "  -> %s"
              % ("CONFIRMED" if p3 else "FALSIFIED"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
