#!/usr/bin/env python3
"""E5 reassessment on REPAIRED timestamps.

common/log.c getFormattedDateTime prints tv_usec's first three digits as
the 'millisecond' field (upstream bug db962399: `tv.tv_usec + 500 / 1000`).
Whenever the true sub-second part is < 100 ms the printed fraction is a
bogus larger value in [.100, .999] -- corruption only ever moves a stamp
FORWARD, within its own second. File order is write order (causal), so
the right-running minimum is exact for every correct line and errs by at
most the distance to the next correct line (~ms) for corrupted ones."""
import re
import sys

TS = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)\.(\d+)")
DMG = re.compile(r"GFX_TRACE avc dmg surface=(\d+)")
BATCH = re.compile(r"GFX_TRACE batch cycle=(\d+) set_n=(\d+) "
                   r"monitors_armed=(\d+) kids_armed=(\d+)")
ENC = re.compile(r"GFX_TRACE enc submitted_seq=(\d+)")
SEND = re.compile(r"GFX_TRACE send bytes=(\d+) last=(\d+) frame_id=(\d+)")
ACK = re.compile(r"GFX_TRACE ack frame_id=(\d+)")

raw = []
for line in open(sys.argv[1], errors="replace"):
    m = TS.match(line)
    if not m:
        continue
    t = (int(m.group(4)) * 3600 + int(m.group(5)) * 60 + int(m.group(6))
         + float("0." + m.group(7)))
    raw.append((t, line.rstrip()))

# repair: right-running minimum
rep = [0.0] * len(raw)
cur = float("inf")
for i in range(len(raw) - 1, -1, -1):
    cur = min(cur, raw[i][0])
    rep[i] = cur
nfix = sum(1 for i in range(len(raw)) if raw[i][0] - rep[i] > 0.0005)
adj = sorted(raw[i][0] - rep[i] for i in range(len(raw))
             if raw[i][0] - rep[i] > 0.0005)
print("repaired %d/%d lines (%.1f%%); adjustment ms: p50 %.0f max %.0f"
      % (nfix, len(raw), 100.0 * nfix / len(raw),
        adj[len(adj) // 2] * 1000, adj[-1] * 1000))

events = []
for i, (t, line) in enumerate(raw):
    t = rep[i]
    m = DMG.search(line)
    if m:
        events.append((t, "dmg", (int(m.group(1)),)))
        continue
    m = BATCH.search(line)
    if m:
        events.append((t, "batch", tuple(int(g) for g in m.groups())))
        continue
    if ENC.search(line):
        events.append((t, "enc", ()))
        continue
    m = SEND.search(line)
    if m:
        events.append((t, "send", tuple(int(g) for g in m.groups())))
        continue
    m = ACK.search(line)
    if m:
        events.append((t, "ack", (int(m.group(1)),)))


def pct(v, p):
    v = sorted(v)
    return v[min(len(v) - 1, int(p / 100.0 * len(v)))]


def ms(x):
    return x * 1000.0


def dist(name, v):
    if not v:
        print("%-34s (empty)" % name)
        return
    print("%-34s n=%-5d mean %6.1f  p50 %6.1f  p90 %6.1f  p99 %6.1f  "
          "max %7.1f ms"
          % (name, len(v), ms(sum(v) / len(v)), ms(pct(v, 50)),
             ms(pct(v, 90)), ms(pct(v, 99)), ms(max(v))))


dmgs = [(t, p[0]) for (t, k, p) in events if k == "dmg"]
t0, t1 = dmgs[0][0], dmgs[-1][0]
window = t1 - t0
merged = [b - a for (a, _), (b, _) in zip(dmgs, dmgs[1:])]
print("\n=== repaired gap distributions ===")
dist("merged (all pair sends)", merged)
same, diff = [], []
for (ta, sa), (tb, sb) in zip(dmgs, dmgs[1:]):
    (same if sa == sb else diff).append(tb - ta)
dist("  next send OTHER monitor", diff)
dist("  next send SAME monitor", same)
per_surf = {}
for t, s in dmgs:
    per_surf.setdefault(s, []).append(t)
for s in sorted(per_surf):
    v = per_surf[s]
    dist("  surface %d own period" % s, [b - a for a, b in zip(v, v[1:])])

print("\nhistogram of merged gaps -> contribution to the mean:")
edges = [0, 10, 20, 35, 50, 70, 100, 150, 250, 500, 1e9]
tot = sum(merged)
for lo, hi in zip(edges, edges[1:]):
    sel = [g for g in merged if lo <= ms(g) < hi]
    if not sel:
        continue
    label = ("%4d-%4d ms" % (lo, hi)) if hi < 1e9 else ("  >=%4d ms" % lo)
    print("  %s  n=%-5d (%5.1f%%)  time %6.1f s (%5.1f%%)  adds %5.1f ms"
          % (label, len(sel), 100.0 * len(sel) / len(merged),
             sum(sel), 100.0 * sum(sel) / tot, ms(sum(sel) / len(merged))))
print("mean %.1f ms over %.1f s, %d sends" % (ms(tot / len(merged)),
                                              window, len(dmgs)))
big = [(a, b - a) for (a, _), (b, _) in zip(dmgs, dmgs[1:])
       if b - a > 0.150]
print("gaps > 150 ms: %d, total %.1f s (%.1f%% of elapsed)"
      % (len(big), sum(g for _, g in big),
         100.0 * sum(g for _, g in big) / window))
notail = [g for g in merged if g <= 0.150]
print("tail-free mean: %.1f ms" % ms(sum(notail) / len(notail)))

# long gaps vs the 8 refresh cuts: cut ordinals every 240 per view; find
# the dmg index nearest each cut by counting per-surface ordinals
ords = {}
cutt = []
for t, s in dmgs:
    o = ords.get(s, 0)
    ords[s] = o + 1
    if o % 240 == 0:
        cutt.append(t)
near = sum(1 for a, g in big if any(abs(a - c) < 0.5 for c in cutt))
print("long gaps within 0.5 s of a scheduled cut: %d of %d"
      % (near, len(big)))

# === pipeline stages on repaired time ===
print("\n=== repaired per-cycle pipeline ===")
cycles = []
cur = None
for t, k, p in events:
    if k == "batch":
        if cur:
            cycles.append(cur)
        cur = {"t": t, "set_n": p[1], "dmg": [], "last": [], "enc": []}
    elif cur is not None:
        if k == "dmg":
            cur["dmg"].append((t, p[0]))
        elif k == "enc":
            cur["enc"].append(t)
        elif k == "send" and p[1] == 1:
            cur["last"].append((t, p[2]))
if cur:
    cycles.append(cur)
svc = [(c["last"][-1][0] - c["t"]) for c in cycles if c["last"]]
dist("cycle: batch arm -> last=1 sent", svc)
enc_only = [(c["enc"][-1] - c["t"]) for c in cycles if c["enc"]]
dist("cycle: batch arm -> enc collected", enc_only)
emit = [(c["last"][-1][0] - c["enc"][-1]) for c in cycles
        if c["enc"] and c["last"] and c["last"][-1][0] >= c["enc"][-1]]
dist("cycle: enc collected -> last=1", emit)
idle = []
for a, b in zip(cycles, cycles[1:]):
    if a["last"]:
        idle.append(max(0.0, b["t"] - a["last"][-1][0]))
dist("worker idle: last=1 -> next batch", idle)
busy = sum(svc)
print("worker busy %.1f s / %.1f s = %.0f%%" % (busy, window,
                                                100.0 * busy / window))

# ack latency repaired
last_sends = {}
ack_lat = []
for t, k, p in events:
    if k == "send" and p[1] == 1:
        last_sends[p[2]] = t
    elif k == "ack" and p[0] in last_sends:
        ack_lat.append(t - last_sends.pop(p[0]))
dist("ack round trip (last=1 -> ack)", ack_lat)

# per-monitor wait decomposition
print("\n=== a monitor's ~105 ms period, repaired decomposition ===")
by_dmg = {}
for c in cycles:
    for (t, s) in c["dmg"]:
        by_dmg[(t, s)] = c
for s in sorted(per_surf):
    v = per_surf[s]
    waits, svcs = [], []
    for a, b in zip(v, v[1:]):
        c = by_dmg.get((a, s))
        if not c or not c["last"]:
            continue
        end = c["last"][-1][0]
        if end <= b:
            svcs.append(end - a)
            waits.append(b - end)
    dist("surface %d: dmg -> frame done" % s, svcs)
    dist("surface %d: done -> next dmg" % s, waits)
