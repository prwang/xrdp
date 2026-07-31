#!/usr/bin/env python3
"""i70_ack_overlap.py -- BACKLOG #70: how many MILLISECONDS of work
actually run concurrently, and does the ack value trail its frame.

Reads the two halves of the XRDP_ACK_TRACE stream:

  session Xorg  ACK_TRACE cap id=N mon=M begin_us= packed_us= sent_us=
                              ack=A shown=S
  xrdp          ACK_TRACE msgin|submit|absorb|egress|ack id=N ... us=

Both are stamped with CLOCK_MONOTONIC, which is system-wide, so the two
processes share one timeline and legs from either can be intersected.
The frames are joined by the ECHOED id -- never by a time window, which
is what produced negative segments in the 07-31 analysis before it was
fixed (BACKLOG #64 / quality gate 2c).

What it reports, in the unit the concurrency claim is made in:

  capture||tail   ms of frame N+1's capture that ran while frame N was
                  between "absorbed by the encoder children" and "last
                  PDU handed to the transport"
  encode||tail    ms of frame N+1's encode (submit -> absorb) that ran
                  inside the same window of frame N
  step 0          the distribution of (id - ack) READ AT CAPTURE
                  ADMISSION. The producer's budget is two slots, so a
                  capture is admitted at 0 or 1 outstanding; if the
                  value is NEVER 0 the ack chain trails its frame by
                  one and one slot is permanently dead, whatever the
                  emission point is.

Usage: i70_ack_overlap.py <capture-dir> [<capture-dir> ...]
"""
import re
import sys
import os

CAP_RE = re.compile(
    r"ACK_TRACE cap id=(\d+) mon=(\d+) begin_us=(\d+) packed_us=(\d+) "
    r"sent_us=(\d+) ack=(-?\d+) shown=(-?\d+)")
XRDP_RE = re.compile(
    r"ACK_TRACE (msgin|submit|absorb|egress|ack) id=(-?\d+).* us=(\d+)")


def load(d):
    caps = {}
    for line in open(os.path.join(d, "session-xorg.log"),
                     errors="replace"):
        m = CAP_RE.search(line)
        if m:
            i = int(m.group(1))
            caps[i] = dict(mon=int(m.group(2)), begin=int(m.group(3)),
                           packed=int(m.group(4)), sent=int(m.group(5)),
                           ack=int(m.group(6)), shown=int(m.group(7)))
    ev = {}
    for line in open(os.path.join(d, "xrdp.log"), errors="replace"):
        m = XRDP_RE.search(line)
        if m:
            kind, i, us = m.group(1), int(m.group(2)), int(m.group(3))
            if i < 0:
                continue
            # first stamp wins: an id is submitted/absorbed once, and the
            # ack line repeats a value the producer has already been told
            ev.setdefault(i, {}).setdefault(kind, us)
    return caps, ev


def overlap(a0, a1, b0, b1):
    """ms of [a0,a1] that lies inside [b0,b1]"""
    lo = max(a0, b0)
    hi = min(a1, b1)
    return (hi - lo) / 1000.0 if hi > lo else 0.0


def pct(vals, p):
    if not vals:
        return float("nan")
    s = sorted(vals)
    return s[min(len(s) - 1, int(p * len(s)))]


def report(d):
    caps, ev = load(d)
    ids = sorted(i for i in caps if i in ev)
    print("=" * 70)
    print(os.path.basename(d))
    print("  capture lines %d   xrdp frames %d   joined %d"
          % (len(caps), len(ev), len(ids)))
    if not ids:
        print("  NO JOINED FRAMES -- is XRDP_ACK_TRACE armed on both sides?")
        return

    # --- step 0: what the producer knew when it admitted a capture ---
    hist = {}
    for i in ids:
        # id is assigned by the send that ENDS this capture, so the
        # outstanding count the admission saw is (id - 1) - ack
        out = (i - 1) - caps[i]["ack"]
        hist[out] = hist.get(out, 0) + 1
    print("  step 0 -- outstanding frames at capture admission "
          "((id-1) - ack):")
    for k in sorted(hist):
        print("     %+d : %5d  %s" % (k, hist[k],
                                      "<-- a free slot" if k == 0 else ""))
    if 0 not in hist:
        print("     NEVER 0: the ack chain trails its frame; one capture "
              "slot is dead for the whole session")

    # --- legs, per frame, joined by identity ---
    cap_tail = []
    enc_tail = []
    tail_ms = []
    cap_ms = []
    enc_ms = []
    period = []
    prev = None
    for i in ids:
        e = ev[i]
        c = caps[i]
        if "absorb" in e and "egress" in e and e["egress"] > e["absorb"]:
            tail_ms.append((e["egress"] - e["absorb"]) / 1000.0)
        cap_ms.append((c["sent"] - c["begin"]) / 1000.0)
        if "submit" in e and "absorb" in e:
            enc_ms.append((e["absorb"] - e["submit"]) / 1000.0)
        if prev is not None:
            period.append((c["sent"] - caps[prev]["sent"]) / 1000.0)
            pe = ev[prev]
            if "absorb" in pe and "egress" in pe:
                t0, t1 = pe["absorb"], pe["egress"]
                cap_tail.append(overlap(c["begin"], c["sent"], t0, t1))
                if "submit" in e and "absorb" in e:
                    enc_tail.append(
                        overlap(e["submit"], e["absorb"], t0, t1))
        prev = i

    def line(name, v, unit="ms"):
        if not v:
            print("  %-26s (none)" % name)
            return
        print("  %-26s mean %6.1f %s   p50 %6.1f   p90 %6.1f   max %6.1f"
              % (name, sum(v) / len(v), unit, pct(v, .5), pct(v, .9),
                 max(v)))

    print("  legs:")
    line("capture (begin->sent)", cap_ms)
    line("encode (submit->absorb)", enc_ms)
    line("tail (absorb->egress)", tail_ms)
    line("period (sent->sent)", period)
    print("  CONCURRENCY, in ms of work that ran at the same time:")
    line("capture(N+1) || tail(N)", cap_tail)
    line("encode(N+1)  || tail(N)", enc_tail)
    if cap_tail:
        n = sum(1 for x in cap_tail if x > 0.0)
        print("  %-26s %d of %d frames (%.0f%%)"
              % ("frames with ANY overlap", n, len(cap_tail),
                 100.0 * n / len(cap_tail)))
    if enc_tail and tail_ms:
        share = sum(enc_tail) / sum(tail_ms) * 100.0
        print("  %-26s %.0f%% of all tail time had an encode running "
              "inside it" % ("encode share of tail", share))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for d in sys.argv[1:]:
        report(d)
