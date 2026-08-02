#!/usr/bin/env python3
"""#79: print the exact instants where each candidate H would gate, with
the exclusive resource's occupancy at every event.

Written because "the client is one frame behind" is a claim about a
number, and the question it has to answer is a claim about a machine:
WHICH event frees WHICH exclusive resource, and who was waiting on it.

The exclusive resource is a **capture slot**. xorgxrdp holds exactly two
for AVC444 (`XUP_CAP_AVC444_SLOT_COUNT = 2`). A capture writes pixels
into one; the slot cannot be written again until xrdp sends a **frame
ack** naming a frame id at or past the one occupying it. That ack is the
only token that admits a new capture -- so this script calls it CREDIT
and tracks slot occupancy as it moves.

The GATE is `frame_id_client + H > frame_id_server`, i.e. "emit credit
only while the client is fewer than H frames behind what we have sent".
H is `frames_in_flight` today. H = 1 therefore means "only while the
client has acked EVERYTHING" -- and this script exists to show what that
costs on a link where nothing is wrong.

Reconstructed state at each event:
    server  = last frame id handed to the transport   (egress)
    client  = last frame id the client acked          (cliack)
    slots   = which frame ids are occupying the two capture slots
              (slot index = frame_id & 1; occupied at msgin, released
              when a credit >= that frame id is emitted)

Usage: i79_wedge_timeline.py <capture-dir> <leg> [H ...]
"""

import os
import sys
import glob

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from i79_ack_delay_analyze import load, WARMUP_S

KINDS = ("msgin", "absorb", "egress", "cliack", "ackslot", "ackregion")

WHAT = {
    "msgin": "capture arrives at xrdp    (slot %d now busy with frame %d)",
    "absorb": "children have read frame %d (its slot is SAFE to recycle)",
    "egress": "frame %d handed to transport (server := %d)",
    "cliack": "client acked frame %d       (client := %d)",
    "ackslot": "CREDIT -> xorgxrdp: slots up to %d may be reused",
    "ackregion": "credit (region ack) -> xorgxrdp: up to %d",
}


def timeline(evs, hs):
    """walk events in time order, tracking server/client/slots/gates"""
    first = None
    rows = []
    server = client = -1
    slots = {}          # slot index -> frame id occupying it
    for ts, tid, name, a in evs:
        if name not in KINDS:
            continue
        if first is None:
            first = ts
        fid = a[0]
        if name == "msgin":
            slots[fid & 1] = fid
        elif name == "egress":
            server = max(server, fid)
        elif name == "cliack":
            client = max(client, fid)
        elif name in ("ackslot", "ackregion"):
            for s in list(slots):
                if slots[s] <= fid:
                    del slots[s]
        gates = {h: (client + h > server) for h in hs}
        rows.append((ts, name, fid, server, client, dict(slots), gates))
    return rows


def main():
    root, leg = sys.argv[1], sys.argv[2]
    hs = [int(x) for x in sys.argv[3:]] or [1, 2, 3]
    d = os.path.join(root, "leg_" + leg)
    evs, win = load(d)
    if not evs:
        print("no events in %s" % d)
        return
    cut = min(ts for ts, _, n, _ in evs if n == "msgin") + int(WARMUP_S * 1e9)
    rows = timeline([e for e in evs if e[0] >= cut], hs)

    # An "absorb" is the instant the in-tree safety condition is met:
    # the slot is provably free and xorgxrdp could be told right now.
    # Whether it IS told is the gate. So absorbs are the decision points.
    dec = [r for r in rows if r[1] == "absorb"]
    print("leg %s (D=%s)  decision points (absorb events) after warm-up: %d\n"
          % (leg, win.get("delay_ms", "none"), len(dec)))
    print("how often the gate is CLOSED at the instant a slot becomes free:")
    for h in hs:
        n = sum(1 for r in dec if not r[6][h])
        print("  H = %d :  %5d / %5d  = %5.1f %%   (\"client may be at most "
              "%d frame(s) behind\")" % (h, n, len(dec),
                                         100.0 * n / len(dec), h - 1))

    # the instants that separate the candidate Hs: closed under the
    # smaller H, open under the larger
    print()
    for lo, hi in zip(hs, hs[1:]):
        sep = [r for r in dec if not r[6][lo] and r[6][hi]]
        print("H = %d closes but H = %d does not: %d of %d absorbs (%.1f %%)"
              % (lo, hi, len(sep), len(dec), 100.0 * len(sep) / len(dec)))

    # print the neighbourhood of the first instant where H = 2 gates
    tgt = next((r for r in dec if not r[6][2]), None)
    if tgt is None:
        print("\nno instant in this leg where H = 2 would gate.")
        return
    t0 = tgt[0]
    print("\n" + "=" * 78)
    print("FIRST INSTANT WHERE H = 2 WOULD GATE  (leg %s, t = +%.3f s into "
          "the leg)" % (leg, (t0 - cut) / 1e9 + WARMUP_S))
    print("=" * 78)
    print("%9s  %-9s %4s  %6s %6s  %-14s %s"
          % ("t (ms)", "event", "id", "server", "client", "slots busy",
             "gate open?"))
    for ts, name, fid, server, client, slots, gates in rows:
        if abs(ts - t0) > 60e6:
            continue
        occ = ",".join("s%d=f%d" % (s, slots[s]) for s in sorted(slots)) \
            or "-"
        g = " ".join("H%d:%s" % (h, "open" if gates[h] else "SHUT")
                     for h in hs)
        mark = "  <<<" if ts == t0 else ""
        print("%+9.3f  %-9s %4d  %6d %6d  %-14s %s%s"
              % ((ts - t0) / 1e6, name, fid, server, client, occ, g, mark))



def race(evs):
    """the two intervals whose ORDER decides the gate at fif = 1.

    At the instant slot k becomes free (absorb k), the gate asks whether
    the client has acked frame k-1 -- the frame sent just before. So the
    credit is emitted iff

        cliack(k-1) - egress(k-1)   <   absorb(k) - egress(k-1)

    i.e. iff the ack round trip is SHORTER than the interval from the
    previous send to the next slot becoming free. Both are printed here.
    Neither is under the client's control in any meaningful sense.
    """
    eg, ab, cl = {}, {}, {}
    for ts, tid, name, a in evs:
        if name == "egress":
            eg.setdefault(a[0], ts)
        elif name == "absorb":
            ab.setdefault(a[0], ts)
        elif name == "cliack":
            cl.setdefault(a[0], ts)
    deadline, roundtrip = [], []
    for k in sorted(ab):
        if (k - 1) in eg and ab[k] > eg[k - 1]:
            deadline.append((ab[k] - eg[k - 1]) / 1e6)
    for k in sorted(cl):
        if k in eg:
            roundtrip.append((cl[k] - eg[k]) / 1e6)
    return sorted(deadline), sorted(roundtrip)


def behind(evs):
    """distribution of (server - client) at every absorb instant --
    exactly the quantity the gate compares against H."""
    rows = timeline(evs, [1])
    h = {}
    for ts, name, fid, server, client, slots, gates in rows:
        if name == "absorb" and server >= 0 and client >= 0:
            h[server - client] = h.get(server - client, 0) + 1
    return h


if __name__ == "__main__":
    main()
