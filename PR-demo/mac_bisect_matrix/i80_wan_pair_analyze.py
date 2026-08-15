#!/usr/bin/env python3
"""i80_wan_pair_analyze.py -- read the BACKLOG #80 step 4 WAN pair and
decide P1..P5, and put the LAN leg head to head with the OLD build.

WHY THIS IS A SEPARATE FILE FROM i79_ack_delay_analyze.py. That script
states #79's predictions and was written against the stall-era build; its
verdict lines mean what they meant on 2026-08-02 and editing them to fit
today's data would tidy a record (CLAUDE.md: records are superseded with
a dated note, never rewritten). What IS reused, by import rather than by
copy, is its measurement layer -- load(), summarize() and discriminate().
The head-to-head only means anything if `withheld`, `period` and the
outstanding histogram are computed by the SAME code on both builds, and
importing is the only way to guarantee that.

Usage:
  i80_wan_pair_analyze.py <label>=<legdir> [<label>=<legdir> ...]
"""
import os
import statistics as st
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i79_ack_delay_analyze import (load, summarize, discriminate,
                                   pct, WARMUP_S, STALL_MS)

WIRE_SLOTS = 2          # XUP_CAP_AVC444_SLOT_COUNT, per monitor


def frontier_facts(evs):
    """the #80-specific readings, none of which exist on the old build.

    Named ack records carry id, kind, egress, absorbed, client and window.
    Named egress records carry id, shown, pending_kib and client.
    """
    cs, queued = set(), []
    # Every emitted ack is classified by WHICH of the three terms was the
    # minimum. Ties get their own bucket and are not silently dropped: at
    # a short RTT all three terms are usually the SAME number, and a
    # classifier that counts only strict minima reports "no records" for
    # a leg that has 1512 of them. (It did, on this script's first run --
    # the tie bucket exists because that was wrong.)
    binder = {"consumed": 0, "server+1": 0, "client+C": 0, "tied": 0}
    n_slot = n_region = 0
    for ts, tid, name, fields in evs:
        if name == "ack" and fields.get("class") == "ACK_TRACE":
            cs.add(fields["window"])
            if fields["kind"] == "slot":
                n_slot += 1
            else:
                n_region += 1
            if fields["window"] == 0:
                # the OLD build's legacy record has no C field. Reading
                # its zero as a window would invent an answer out of a
                # missing field, and it did: every old-build ack came out
                # as "client+C binding, 100 %".
                continue
            terms = {"consumed": fields["absorbed"],
                     "server+1": fields["egress"] + 1,
                     "client+C": fields["client"] + fields["window"]}
            lo = min(terms.values())
            w = [k for k, v in terms.items() if v == lo]
            binder["tied" if len(w) > 1 else w[0]] += 1
        elif name == "egress":
            queued.append(fields["pending_kib"])
    return {
        "C_seen": sorted(cs),
        "is_frontier": any(c > 0 for c in cs),
        "n_slot": n_slot,
        "n_region": n_region,
        "binder": binder,
        "queued_kib_max": max(queued) if queued else 0,
        "queued_kib_p90": pct(queued, 0.9) if queued else 0,
        "queued_kib_mean": st.mean(queued) if queued else 0.0,
        "queued_n": len(queued),
    }


def stall_attribution(evs, stall_ms=STALL_MS):
    """for the cycles that DID wait, which term was holding the credit.

    Same pairing as summarize(): capture k's credit is the first ack
    whose value reaches k-2, and the wait runs from the absorb of k-2.
    The ack record carries the frontier's three inputs at the instant it
    was emitted, so the term that is the minimum THERE is the term that
    had been binding. Returns None for a build with no frontier records.
    """
    msgin, absorb = {}, {}
    acks = []
    for ts, tid, name, fields in evs:
        if name == "msgin":
            msgin.setdefault(fields["id"], ts)
        elif name == "absorb":
            absorb.setdefault(fields["id"], ts)
        elif name == "ack" and fields.get("class") == "ACK_TRACE":
            acks.append((ts, fields))
    acks.sort()
    if not msgin or not acks or all(a[1]["window"] == 0 for a in acks):
        return None
    cut = min(msgin.values()) + int(WARMUP_S * 1e9)
    out = {"consumed": 0, "server+1": 0, "client+C": 0, "tied": 0, "n": 0}
    for k in sorted(msgin):
        if msgin[k] < cut or (k - 2) not in absorb:
            continue
        cred = next((x for x in acks if x[1]["id"] >= k - 2), None)
        if cred is None or cred[0] > msgin[k]:
            continue
        if (cred[0] - absorb[k - 2]) / 1e6 <= stall_ms:
            continue
        fields = cred[1]
        terms = {"consumed": fields["absorbed"],
                 "server+1": fields["egress"] + 1,
                 "client+C": fields["client"] + fields["window"]}
        lo = min(terms.values())
        w = [t for t, v in terms.items() if v == lo]
        out["tied" if len(w) > 1 else w[0]] += 1
        out["n"] += 1
    return out


def period_tail(evs):
    """period distribution, id-keyed, same warm-up rule as summarize()"""
    egress, msgin = {}, {}
    for ts, tid, name, fields in evs:
        if name == "egress":
            egress.setdefault(fields["id"], ts)
        elif name == "msgin":
            msgin.setdefault(fields["id"], ts)
    cut = (min(msgin.values()) + int(WARMUP_S * 1e9)) if msgin else 0
    eg = sorted(t for t in egress.values() if t >= cut)
    per = [(b - a) / 1e6 for a, b in zip(eg, eg[1:])]
    if not per:
        return None
    return {
        "n": len(per),
        "p50": pct(per, 0.5),
        "p90": pct(per, 0.9),
        "p99": pct(per, 0.99),
        "mean": st.mean(per),
        "max": max(per),
    }


def main():
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    legs = []
    for arg in sys.argv[1:]:
        label, path = arg.split("=", 1)
        evs, win = load(path)
        r = summarize(evs, win.get("delay_ms", "none"))
        r["leg"] = label
        r["win"] = win
        r["ff"] = frontier_facts(evs)
        r["tail"] = period_tail(evs)
        r["stall_by"] = stall_attribution(evs)
        r["outstanding"], r["gated"], r["free"] = discriminate(evs)
        legs.append(r)

    print("=== BACKLOG #80 step 4: the credit frontier, live, on the "
          "#81 netem harness ===")
    print("warm-up dropped: first %.1f s of each leg; a 'stall' is "
          "withheld > %.0f ms\n" % (WARMUP_S, STALL_MS))

    print("--- P5 MECHANISM CHECK (before any rate) ---")
    bad = False
    for r in legs:
        ff = r["ff"]
        cs = ff["C_seen"]
        rtt = r["win"].get("rtt_measured_ms", "n/a")
        note = ""
        if cs == [1]:
            note = "C=1 on every ack record"
        elif cs == [0]:
            note = ("C field absent (0) -- this is the OLD build's "
                    "legacy ackregion record, as expected")
        else:
            note = "C values seen: %s  <-- UNEXPECTED" % cs
            bad = True
        print("  %-12s rtt measured %-8s  %s" % (r["leg"], rtt, note))
        print("  %-12s ackslot records %d, ackregion records %d"
              % ("", ff["n_slot"], ff["n_region"]))
    print("  P5 -> %s" % ("RED" if bad else "OK"))

    print("\n--- P1 THE DEFECT: is the slot credit still withheld? ---")
    print("  `withheld` = from the instant the children have absorbed "
          "frame k-2's input")
    print("  (the in-tree safety condition) to the instant a credit "
          "permitting capture k is emitted.")
    print("  %-14s %6s %8s %8s %8s %8s %6s" %
          ("leg", "n", "p50 ms", "p90 ms", "mean ms", "stall%", "neg"))
    for r in legs:
        print("  %-14s %6d %8.2f %8.1f %8.2f %7.1f%% %6d"
              % (r["leg"], r["withheld_n"], r["withheld_p50"],
                 r["withheld_p90"], r["withheld_mean"],
                 100 * r["stall_frac"], r["withheld_neg"]))
    print("  (neg = derived intervals that came out negative. A negative"
          " duration is a broken")
    print("   pairing, not a measurement -- quality gate 2c. Any nonzero"
          " value voids the row.)")
    print()
    print("  of the cycles that DID wait more than %.0f ms, which term "
          "was holding the credit:" % STALL_MS)
    for r in legs:
        sb = r["stall_by"]
        if sb is None:
            print("    %-14s (old build -- no frontier record, no "
                  "attribution possible)" % r["leg"])
            continue
        tot = max(1, sb["n"])
        print("    %-14s n=%-4d %s" % (r["leg"], sb["n"], "  ".join(
            "%s %d (%.0f%%)" % (k, sb[k], 100.0 * sb[k] / tot)
            for k in ("consumed", "server+1", "client+C", "tied"))))

    print("\n--- P2 THE LONG TAIL: period distribution ---")
    print("  %-12s %6s %7s %7s %7s %7s %7s %8s" %
          ("leg", "n", "mean", "p50", "p90", "p99", "max", "p90/p50"))
    for r in legs:
        t = r["tail"]
        if t is None:
            continue
        print("  %-12s %6d %7.1f %7.1f %7.1f %7.1f %7.1f %8.2f"
              % (r["leg"], t["n"], t["mean"], t["p50"], t["p90"],
                 t["p99"], t["max"], t["p90"] / t["p50"]))

    print("\n--- P3 DROP, NOT HOLD: what is queued behind egress ---")
    print("  transport bytes waiting on trans::wait_s, sampled at every "
          "egress (KiB)")
    print("  %-12s %8s %8s %8s %8s" %
          ("leg", "n", "mean", "p90", "max"))
    for r in legs:
        ff = r["ff"]
        if not ff["is_frontier"]:
            # trans::wait_bytes landed with #80. On an older build this
            # field is a hard zero because nothing writes it -- printing
            # it as 0.0 KiB would read as "the queue was empty", which is
            # a claim this capture cannot make.
            print("  %-12s %8s   NOT MEASURED -- trans::wait_bytes does "
                  "not exist in this build" % (r["leg"], "-"))
            continue
        print("  %-12s %8d %8.1f %8.1f %8.1f"
              % (r["leg"], ff["queued_n"], ff["queued_kib_mean"],
                 ff["queued_kib_p90"], ff["queued_kib_max"]))

    print("\n--- P4 THE WIRE BOUND: id_server - id_client at send ---")
    for r in legs:
        cs = [c for c in r["ff"]["C_seen"] if c > 0]
        c = cs[0] if cs else None
        hist = r["outstanding"]
        worst = max(hist) if hist else 0
        if c is None:
            verdict = "(old build, no C -- bound not applicable)"
        else:
            bound = c + WIRE_SLOTS
            verdict = ("worst %d, bound C+%d = %d -> %s"
                       % (worst, WIRE_SLOTS, bound,
                          "OK" if worst <= bound else "VIOLATED"))
        print("  %-12s %-44s %s"
              % (r["leg"],
                 " ".join("%d:%d" % kv for kv in sorted(hist.items())),
                 verdict))

    print("\n--- which term of the frontier was binding ---")
    print("  (counted per emitted ack, only where one term is the "
          "strict minimum)")
    for r in legs:
        b = r["ff"]["binder"]
        tot = sum(b.values())
        if not r["ff"]["is_frontier"]:
            print("  %-12s (old build -- the ack record carries no C, so "
                  "there is nothing to attribute)" % r["leg"])
            continue
        print("  %-12s %s" % (r["leg"], "  ".join(
            "%s %d (%.0f%%)" % (k, v, 100.0 * v / tot)
            for k, v in b.items())))

    print("\n--- RTT as the SERVER saw it: egress -> client ack ---")
    print("  %-12s %8s %8s %8s" % ("leg", "n", "p50 ms", "p90 ms"))
    for r in legs:
        print("  %-12s %8d %8.1f %8.1f"
              % (r["leg"], r["lag_n"], r["lag_p50"], r["lag_p90"]))


if __name__ == "__main__":
    main()
