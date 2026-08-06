#!/usr/bin/env python3
"""i61e_period_attribute.py -- attribute the encoder's send-to-send period.

BACKLOG #61e. Reads a perf_trace file (common/perf_trace.h, PRD
FR-TRACE-1) and decomposes the worker's cycle into named stages and the
gaps between them.

WHY IT IS SHAPED THIS WAY, versus i70b_stage_split.py which it replaces
for this purpose:

  * i70b sums bracketed STAGES and calls the remainder "unattributed".
    On x004 that remainder was 6.88 ms of a 35.95 ms period -- a number
    with no code behind it. Here the cycle is partitioned by EVERY
    consecutive pair of worker events, so the decomposition closes by
    construction and the open question becomes a specific, named list of
    end -> beg gaps. "Unknown" means "between these two brackets", which
    is a line of code, not a residue.

  * Closure is computed PER CYCLE and reported as a distribution, never
    as sum-of-means minus mean-of-sums. A single 2665 ms session-startup
    wait divided across 408 cycles produced a -6.77 ms "unattributed" on
    2026-08-01; quality gate 2c caught it. A per-cycle residual cannot be
    contaminated that way -- an outlier shows up as one bad cycle.

  * The steady-state window is stated, not chosen after looking. Cycle 0
    contains the wait for the session's first frame (seconds), so it is
    reported separately and excluded from the steady-state table by an
    explicit rule, printed with the result.

THE TWO CLAIMS IT EXISTS TO TEST (PRD `capture || encode`):

  C1  capture latency is HIDDEN: the frame is already on the fifo when
      the worker asks for it. Measured as the `wait` bracket -- the time
      the worker holds nothing to encode -- and as fifo residency
      (enq(id) -> take(id)).

  C2  ffmpeg could NOT have seen frame N sooner: the interval from
      enq(N) to subm_beg(N) contains no worker idle. If the worker spent
      none of it waiting, the delay is serial work that must finish
      first, not slack that better scheduling would recover.

Usage: i61e_period_attribute.py <trace-file> [--steady N]
"""
import sys
import statistics as st


def load(path):
    """Read a perf_trace file.

    The record is <ns> <tid> <tag> <a> <b> <c> <d> <e> <f> -- NINE
    fields since BACKLOG #61h widened the payload from two ints to six
    so a GFX send fits in one event. This reader accepted only the old
    five-field form and silently `continue`d past every record of the
    new one: against the x013 trace it loaded 0 events and reported
    "0 drops, 0 records / not enough cycles", which reads like an empty
    run rather than like a parser that cannot read the file. Both widths
    are accepted, and a file that yields nothing is now an error rather
    than an empty table.
    """
    evs = []
    seen = 0
    for line in open(path, errors="replace"):
        if line.startswith("#"):
            continue          # the `# perfbase` clock-base line
        f = line.split()
        if len(f) not in (5, 9):
            continue
        seen += 1
        try:
            evs.append((int(f[0]), f[1], f[2], int(f[3]), int(f[4])))
        except ValueError:
            pass
    if seen == 0:
        sys.exit("%s: no records this reader could parse. A perf_trace "
                 "record is 5 fields (pre-#61h) or 9 (current); this file "
                 "has neither, so it is a format mismatch and NOT an "
                 "empty run." % path)
    return evs


def pct(v, q):
    if not v:
        return float("nan")
    s = sorted(v)
    return s[min(len(s) - 1, int(q * len(s)))]


def main():
    path = sys.argv[1]
    skip = 1
    if "--steady" in sys.argv:
        skip = int(sys.argv[sys.argv.index("--steady") + 1])
    evs = load(path)

    drops = [e for e in evs if e[2] in ("perfdrop", "perfnoring")]
    if drops:
        print("!! TRACE IS TRUNCATED -- %d drop record(s); every number "
              "below is suspect" % len(drops))
        for d in drops:
            print("   ", d)
    else:
        print("trace integrity: 0 drops, %d records" % len(evs))

    # The worker is the thread that brackets the drain. The assembler is
    # whichever thread emits, when that is not the worker (emit_thread).
    wtid = set(t for _, t, tag, _, _ in evs if tag == "drain_beg")
    etid = set(t for _, t, tag, _, _ in evs if tag == "emit_beg") - wtid
    mtid = set(t for _, t, tag, _, _ in evs if tag == "enq") - wtid
    print("threads: worker=%s assembler=%s main=%s"
          % (sorted(wtid), sorted(etid) or "(inline)", sorted(mtid)))

    w = [e for e in evs if e[1] in wtid]

    # --- cycles: wait_beg to wait_beg, the top of the worker loop ------
    starts = [i for i, e in enumerate(w) if e[2] == "wait_beg"]
    cycles = []
    for a, b in zip(starts, starts[1:]):
        cycles.append(w[a:b + 1])
    print("cycles: %d (excluding %d as pre-steady-state)" % (len(cycles), skip))
    if len(cycles) <= skip:
        print("not enough cycles")
        return
    steady = cycles[skip:]

    # --- per-cycle partition -----------------------------------------
    # Every consecutive pair inside a cycle. The partition is exact, so
    # the interesting quantity is not "what is missing" but "how much
    # lands in a gap between two brackets rather than inside one".
    segs = {}
    lens = []
    resid = []
    for c in steady:
        total = (c[-1][0] - c[0][0]) / 1e6
        lens.append(total)
        acc = 0.0
        for x, y in zip(c, c[1:]):
            d = (y[0] - x[0]) / 1e6
            segs.setdefault((x[2], y[2]), []).append(d)
            acc += d
        resid.append(total - acc)

    n = len(steady)
    period = sum(lens) / n
    print()
    print("=== the worker cycle, wait_beg -> wait_beg, n=%d ===" % n)
    print("period: mean %.3f ms  p50 %.3f  p90 %.3f  p99 %.3f"
          % (period, st.median(lens), pct(lens, .9), pct(lens, .99)))
    print("per-cycle closure residual: max |%.6f| ms   (exact partition;"
          " a nonzero value here is a bug in this script)"
          % max(abs(r) for r in resid))

    print()
    print("%-24s %6s %9s %9s %9s %11s %7s"
          % ("segment", "n", "mean", "p50", "p90", "per-cycle", "share"))
    rows = []
    for k, v in segs.items():
        per = sum(v) / n
        rows.append((per, k, len(v), sum(v) / len(v), st.median(v),
                     pct(v, .9)))
    named = 0.0
    gaps = 0.0
    for per, k, cnt, mean, p50, p90 in sorted(rows, reverse=True):
        a, b = k
        # a bracketed stage is <x>_beg -> <x>_end; everything else is a
        # gap BETWEEN brackets, and that is what "unknown" means here
        stage = a.endswith("_beg") and b == a[:-4] + "_end"
        if stage:
            named += per
        else:
            gaps += per
        print("%-24s %6d %8.3f %8.3f %8.3f %10.3f %6.1f%% %s"
              % ("%s -> %s" % k, cnt, mean, p50, p90, per,
                 100.0 * per / period, "" if stage else "   <- gap"))
    print("-" * 82)
    print("%-24s %41.3f ms  %6.1f%%" % ("NAMED STAGES", named,
                                        100.0 * named / period))
    print("%-24s %41.3f ms  %6.1f%%" % ("GAPS (the unknown)", gaps,
                                        100.0 * gaps / period))
    print("%-24s %41.3f ms" % ("period", period))

    # --- C1: does the worker ever wait for a frame? -------------------
    waits = segs.get(("wait_beg", "wait_end"), [])
    over = [x for x in waits if x > 1.0]
    print()
    print("=== C1 -- the `wait` bracket: time the worker holds nothing "
          "to encode ===")
    print("n=%d  mean %.4f ms  p50 %.4f  p90 %.4f  p99 %.4f  max %.3f"
          % (len(waits), sum(waits) / len(waits), st.median(waits),
             pct(waits, .9), pct(waits, .99), max(waits)))
    print("waits > 1 ms: %d of %d (%.2f%%), summing %.1f ms of %.1f ms"
          % (len(over), len(waits), 100.0 * len(over) / len(waits),
             sum(over), sum(waits)))

    # --- fifo residency: enq(id) -> take(id) --------------------------
    enq = {}
    for ns, t, tag, a, b in evs:
        if tag == "enq" and a > 0 and a not in enq:
            enq[a] = ns
    res = []
    early = 0
    for c in steady:
        tk = [e for e in c if e[2] == "take"]
        wb = c[0][0]
        for ns, t, tag, a, b in tk:
            if a in enq:
                res.append((ns - enq[a]) / 1e6)
                if enq[a] <= wb:
                    early += 1
    if res:
        print()
        print("=== C1b -- fifo residency, enq(id) -> take(id) ===")
        print("n=%d  mean %.3f ms  p50 %.3f  p90 %.3f  min %.3f"
              % (len(res), sum(res) / len(res), st.median(res),
                 pct(res, .9), min(res)))
        print("frames already enqueued BEFORE the worker's wait_beg: "
              "%d of %d (%.1f%%)"
              % (early, len(res), 100.0 * early / len(res)))

    # --- C2: could ffmpeg have seen the frame sooner? -----------------
    # From enq(N) to subm_beg(N): how much of that interval did the
    # worker spend idle in `wait`? Idle time is recoverable; busy time
    # is serial work that has to finish first.
    idle_iv = []
    for c in steady:
        for x, y in zip(c, c[1:]):
            if x[2] == "wait_beg" and y[2] == "wait_end":
                idle_iv.append((x[0], y[0]))
    idle_iv.sort()
    gapms = []
    idlems = []
    for c in steady:
        tk = [e for e in c if e[2] == "take"]
        sb = [e for e in c if e[2] == "subm_beg"]
        if not tk or not sb:
            continue
        fid = tk[0][3]
        if fid not in enq:
            continue
        t0, t1 = enq[fid], sb[0][0]
        if t1 <= t0:
            continue
        gapms.append((t1 - t0) / 1e6)
        ov = 0
        for a, b in idle_iv:
            if b <= t0:
                continue
            if a >= t1:
                break
            ov += min(b, t1) - max(a, t0)
        idlems.append(ov / 1e6)
    if gapms:
        print()
        print("=== C2 -- enq(N) -> subm_beg(N): could ffmpeg have had it "
              "sooner? ===")
        print("delay from enqueue to submit: n=%d  mean %.3f ms  p50 %.3f"
              "  p90 %.3f" % (len(gapms), sum(gapms) / len(gapms),
                              st.median(gapms), pct(gapms, .9)))
        print("of which the worker was IDLE (in `wait`): mean %.4f ms  "
              "p50 %.4f  max %.3f"
              % (sum(idlems) / len(idlems), st.median(idlems),
                 max(idlems)))
        print("recoverable share: %.2f%% -- the rest is serial work "
              "already in progress"
              % (100.0 * sum(idlems) / sum(gapms)))

    # --- emit overlap, when the assembler is a separate thread --------
    if etid:
        ev = [e for e in evs if e[1] in etid]
        spans = []
        for x, y in zip(ev, ev[1:]):
            if x[2] == "emit_beg" and y[2] == "emit_end":
                spans.append((x[0], y[0], x[3]))
        # worker busy = everything in a cycle that is NOT the wait
        busy = []
        for c in steady:
            for x, y in zip(c, c[1:]):
                if not (x[2] == "wait_beg" and y[2] == "wait_end"):
                    busy.append((x[0], y[0]))
        busy.sort()
        tot = 0.0
        hid = 0.0
        for a, b, fid in spans:
            if b <= a:
                continue
            tot += (b - a) / 1e6
            ov = 0
            for p, q in busy:
                if q <= a:
                    continue
                if p >= b:
                    break
                ov += min(q, b) - max(p, a)
            hid += ov / 1e6
        if tot > 0:
            print()
            print("=== the emit split -- is assembly hidden behind the "
                  "worker? ===")
            print("emit spans: n=%d  total %.1f ms  mean %.3f ms/frame"
                  % (len(spans), tot, tot / len(spans)))
            print("overlapped with worker BUSY time: %.1f ms (%.1f%%)"
                  % (hid, 100.0 * hid / tot))
            print("exposed (worker idle or emit alone): %.1f ms "
                  "= %.3f ms/frame"
                  % (tot - hid, (tot - hid) / len(spans)))


main()
