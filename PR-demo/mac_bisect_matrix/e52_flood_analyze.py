#!/usr/bin/env python3
"""e52_flood_analyze.py — where the frame interval goes, per arm.

BACKLOG #52 (E5-2). Reads one or more `gfx_trace.txt` files (the
GFX_TRACE lines e_gate_run.sh extracts from the pod's xrdp.log) and
prints, per arm, the send interval and the decomposition that says WHO
IS WAITING ON WHOM:

  * per-monitor own period and the merged send interval;
  * per-pair service, split into damage -> encode collected -> last=1;
  * the WAIT between one pair's last=1 and the next damage on the SAME
    monitor. This is the discriminator E5-2 exists for: a wait near zero
    means the server is the limit (the pipeline is full and the interval
    is the server's own), a large wait means the producer or the capture
    path is, whatever the encoder does;
  * worker busy fraction, and for step-7 builds the kids_armed histogram;
  * ack round trip, un-acked frames against the fif cap, and the
    client-reported queue depth (was flow control ever binding?).

Timestamps are taken at face value: every build measured here carries
the #52 step-0 log clock fix. The script says so if it finds stamps
going backwards in file order, which is the signature of a build that
does not.

Usage: e52_flood_analyze.py LABEL=path/to/gfx_trace.txt [LABEL=... ...]
"""
import re
import sys

TS = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)\.(\d+)")
DMG = re.compile(r"GFX_TRACE avc dmg surface=(\d+) num_rects=(\d+)")
BATCH = re.compile(r"GFX_TRACE batch cycle=(\d+) set_n=(\d+) "
                   r"monitors_armed=(\d+) kids_armed=(\d+)")
ENC = re.compile(r"GFX_TRACE enc submitted_seq=(\d+)")
SEND = re.compile(r"GFX_TRACE send bytes=(\d+) last=(\d+) frame_id=(\d+) "
                  r"id_server=(\d+) id_client=(\d+) fif=(\d+)")
ACK = re.compile(r"GFX_TRACE ack frame_id=(\d+) queue_depth=(\d+)")


def ms(x):
    return x * 1000.0


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p / 100.0 * len(v)))]


def dist(name, v, unit="ms"):
    if not v:
        print("  %-38s (none)" % name)
        return
    print("  %-38s n=%-5d mean %7.1f  p50 %7.1f  p90 %7.1f  max %8.1f %s"
          % (name, len(v), ms(sum(v) / len(v)), ms(pct(v, 50)),
             ms(pct(v, 90)), ms(max(v)), unit))


def parse(path):
    ev = []
    for line in open(path, errors="replace"):
        m = TS.match(line)
        if not m:
            continue
        t = (int(m.group(4)) * 3600 + int(m.group(5)) * 60
             + int(m.group(6)) + float("0." + m.group(7)))
        for rx, kind in ((DMG, "dmg"), (BATCH, "batch"), (ENC, "enc"),
                         (SEND, "send"), (ACK, "ack")):
            mm = rx.search(line)
            if mm:
                ev.append((t, kind, mm.groups()))
                break
    return ev


def analyze(label, path):
    ev = parse(path)
    print("=== %s ===" % label)
    # Two very different causes look alike here, so classify by magnitude.
    # The pre-#52 log clock printed microseconds as the millisecond field,
    # which throws a stamp up to ~0.9 s forward; step 5's worker thread and
    # the main thread writing the same log interleave by a few ms. Measured
    # 2026-07-30: the fixed build shows jumps of p50 6 ms / max 22 ms and a
    # FLAT histogram of fractional parts, the buggy one an empty .0xx bin.
    back = sorted(a[0] - b[0] for a, b in zip(ev, ev[1:]) if b[0] < a[0])
    if back:
        worst = ms(back[-1])
        print("  out-of-order stamps: %d of %d (%.1f%%), worst %.0f ms — %s"
              % (len(back), len(ev), 100.0 * len(back) / len(ev), worst,
                 "THREAD INTERLEAVE (expected on a pump_set build)"
                 if worst < 100 else
                 "TOO LARGE: this build predates the #52 step-0 log clock "
                 "fix, every percentile below needs offline repair"))

    enc = [t for t, k, g in ev if k == "enc"]
    dmg = [(t, int(g[0])) for t, k, g in ev if k == "dmg"]
    lastd = [(t, int(g[2])) for t, k, g in ev if k == "send" and g[1] == "1"]
    span = enc[-1] - enc[0]
    gaps = [b - a for a, b in zip(enc, enc[1:])]
    print("  sends %d over %.1f s  ->  %.2f sends/s" % (len(enc), span,
                                                        len(enc) / span))
    dist("merged send interval", gaps)
    per = {}
    for t, s in dmg:
        per.setdefault(s, []).append(t)
    for s in sorted(per):
        dist("surface %d own period" % s,
             [b - a for a, b in zip(per[s], per[s][1:])])

    # --- per-pair service, and the wait that follows it ------------------
    # walk in file order: a dmg opens a pair, the next last=1 closes it
    pairs = []          # (dmg_t, surface, enc_t or None, last_t)
    open_dmg = None
    open_enc = None
    for t, k, g in ev:
        if k == "dmg":
            if open_dmg is None:
                open_dmg = (t, int(g[0]))
                open_enc = None
        elif k == "enc" and open_dmg is not None and open_enc is None:
            open_enc = t
        elif k == "send" and g[1] == "1" and open_dmg is not None:
            pairs.append((open_dmg[0], open_dmg[1], open_enc, t))
            open_dmg = None
            open_enc = None
    # NOTE on pre-step-5 builds: they log `enc` for frame N+1 AFTER frame
    # N's last=1, so the encode/emit split below collapses (n=1) and only
    # the dmg -> last=1 total is meaningful there. On a pump_set build the
    # `enc` line lands inside its own frame and the split is real.
    dist("pair service: dmg -> last=1", [d - a for a, s, e, d in pairs])
    dist("  dmg -> encode collected",
         [e - a for a, s, e, d in pairs if e is not None])
    dist("  encode collected -> last=1",
         [d - e for a, s, e, d in pairs if e is not None and d >= e])

    # THE DISCRIMINATOR: after a monitor's frame is out, how long until
    # that monitor has new damage to encode?
    for s in sorted(per):
        mine = [(a, d) for a, sf, e, d in pairs if sf == s]
        waits = [b[0] - a[1] for a, b in zip(mine, mine[1:]) if b[0] >= a[1]]
        dist("surface %d: last=1 -> next own dmg" % s, waits)

    busy = sum(d - a for a, s, e, d in pairs)
    print("  worker busy %.1f s of %.1f s = %.0f%%"
          % (busy, span, 100.0 * busy / span))

    kids = {}
    setn = {}
    for t, k, g in ev:
        if k == "batch":
            kids[int(g[3])] = kids.get(int(g[3]), 0) + 1
            setn[int(g[1])] = setn.get(int(g[1]), 0) + 1
    if kids:
        n = sum(kids.values())
        print("  batch cycles %d   kids_armed %s   set_n %s"
              % (n,
                 " ".join("%d:%.0f%%" % (k, 100.0 * c / n)
                          for k, c in sorted(kids.items())),
                 " ".join("%d:%.0f%%" % (k, 100.0 * c / n)
                          for k, c in sorted(setn.items()))))
    else:
        print("  batch cycles 0 (pre-step-5 build: one child per pump)")

    # --- payload size and the ack path ----------------------------------
    sizes = [int(g[0]) for t, k, g in ev if k == "send" and int(g[0]) > 4096]
    if sizes:
        print("  picture payloads > 4 KiB: n=%d  mean %.0f B  p50 %.0f B  "
              "max %.0f B  total %.0f MiB"
              % (len(sizes), sum(sizes) / float(len(sizes)),
                 pct(sizes, 50), max(sizes), sum(sizes) / 1048576.0))
        print("  wire rate: %.1f Mbit/s" % (8.0 * sum(sizes) / span / 1e6))
    sent = {}
    lat = []
    for t, k, g in ev:
        if k == "send" and g[1] == "1":
            sent[int(g[2])] = t
        elif k == "ack" and int(g[0]) in sent:
            lat.append(t - sent.pop(int(g[0])))
    dist("ack round trip (last=1 -> ack)", lat)
    unacked = [int(g[3]) - int(g[4]) for t, k, g in ev if k == "send"]
    fifs = sorted(set(int(g[5]) for t, k, g in ev if k == "send"))
    qd = [int(g[1]) for t, k, g in ev if k == "ack"]
    if unacked:
        print("  un-acked frames at send time: p50 %d  p90 %d  max %d "
              "(fif cap %s)" % (pct(unacked, 50), pct(unacked, 90),
                                max(unacked), fifs))
    if qd:
        print("  client queue_depth: p50 %d  max %d" % (pct(qd, 50), max(qd)))
    print()
    return {"label": label, "mean": sum(gaps) / len(gaps) * 1000.0,
            "sends": len(enc), "span": span}


def main():
    out = []
    for arg in sys.argv[1:]:
        label, _, path = arg.partition("=")
        out.append(analyze(label, path))
    if len(out) == 2:
        base, meas = out
        print("=== E5-2 ratio (flood against flood) ===")
        print("  baseline %-10s %.1f ms mean per send" % (base["label"],
                                                          base["mean"]))
        print("  measured %-10s %.1f ms mean per send" % (meas["label"],
                                                          meas["mean"]))
        r = base["mean"] / meas["mean"]
        verdict = ("GREEN (>= 2.0x)" if r >= 2.0 else
                   "AMBER (>= 1.5x)" if r >= 1.5 else "RED (< 1.5x)")
        print("  ratio %.2fx  ->  %s" % (r, verdict))


if __name__ == "__main__":
    main()
