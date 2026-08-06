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
                  ADMISSION, and -- the part the count alone cannot
                  answer -- whether the frame the producer is waiting
                  on had ACTUALLY finished. "outstanding = +1" is what
                  a healthy depth-2 producer looks like AND what a
                  one-frame-stale ack looks like; the two are told
                  apart only by asking whether egress(N-1) had already
                  happened when capture N was admitted.

  tail split      tail(N) cut at submit(N+1). The encoder worker is
                  ONE thread that runs the emit pass of N and the
                  submit of N+1 in the same serial loop, so
                  absorb(N)->submit(N+1) is worker-serial time that no
                  ack can overlap, and submit(N+1)->egress(N) is the
                  only part that is available to overlap.

Usage: i70_ack_overlap.py <capture-dir> [<capture-dir> ...]
"""
import gzip
import re
import sys
import os

CAP_RE = re.compile(
    r"ACK_TRACE cap id=(\d+) mon=(\d+) begin_us=(\d+) packed_us=(\d+) "
    r"sent_us=(\d+) ack=(-?\d+) shown=(-?\d+)")
XRDP_RE = re.compile(
    r"ACK_TRACE (msgin|submit|absorb|egress|ack) id=(-?\d+).* us=(\d+)")
# for tail_split(): the LOG's own wall-clock stamp, one clock for all
TS_RE = re.compile(r"^\[\d{4}-\d\d-\d\dT(\d\d):(\d\d):(\d\d)\.(\d{3})")
ABS_RE = re.compile(r"ACK_TRACE absorb id=(\d+)")
EGR_RE = re.compile(r"ACK_TRACE egress id=(\d+)")
SND_RE = re.compile(r"GFX_TRACE send .*id_server=(\d+)")


def open_log(d, name):
    """archived captures are gzipped in place; live ones are not"""
    p = os.path.join(d, name)
    if os.path.exists(p + ".gz"):
        return gzip.open(p + ".gz", "rt", errors="replace")
    return open(p, errors="replace")


def load(d):
    caps = {}
    for line in open_log(d, "session-xorg.log"):
        m = CAP_RE.search(line)
        if m:
            i = int(m.group(1))
            caps[i] = dict(mon=int(m.group(2)), begin=int(m.group(3)),
                           packed=int(m.group(4)), sent=int(m.group(5)),
                           ack=int(m.group(6)), shown=int(m.group(7)))
    ev = {}
    for line in open_log(d, "xrdp.log"):
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

    # --- step 0, the part the histogram CANNOT answer ---
    # "+1 outstanding" is what a healthy depth-2 producer looks like AND
    # what a one-frame-stale ack looks like. The two differ only in
    # whether frame N-1 had actually finished when capture N was
    # admitted, which needs N-1's egress stamp -- i.e. identity, which
    # is exactly what the July raw-offset uprobe did not carry.
    still = 0
    done = 0
    lead = []
    for i in ids:
        if caps[i]["ack"] != i - 2:
            continue
        pe = ev.get(i - 1)
        if not pe or "egress" not in pe:
            continue
        if pe["egress"] > caps[i]["begin"]:
            still += 1
            lead.append((pe["egress"] - caps[i]["begin"]) / 1000.0)
        else:
            done += 1
    if still + done > 0:
        print("     of the %d admissions at ack == N-2:" % (still + done))
        print("       N-1 STILL IN FLIGHT (ack is CORRECT) : %5d (%.1f%%)"
              % (still, 100.0 * still / (still + done)))
        print("       N-1 already egressed (ack is STALE)  : %5d (%.1f%%)"
              % (done, 100.0 * done / (still + done)))
        if lead:
            print("       median ms of N-1's tail still to run: %.1f"
                  % pct(lead, .5))

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

    # --- is the worker WORKING or STARVED between two frames? ---
    # absorb(N) -> submit(N+1) is the worker's serial gap. msgin(N+1) is
    # when xrdp received N+1, so the gap splits into "N+1 did not exist
    # yet" (producer-bound) and "N+1 sat in the fifo" (worker-bound).
    win = []
    arrive = []
    queued = []
    for a, b in zip(ids, ids[1:]):
        if b != a + 1:
            continue
        ea = ev[a]
        eb = ev[b]
        if "absorb" not in ea or "submit" not in eb or "msgin" not in eb:
            continue
        win.append((eb["submit"] - ea["absorb"]) / 1000.0)
        arrive.append((eb["msgin"] - ea["absorb"]) / 1000.0)
        queued.append((eb["submit"] - eb["msgin"]) / 1000.0)
    if win:
        late = sum(1 for x in arrive if x > 0.0)
        print("  WORKER GAP absorb(N) -> submit(N+1):")
        line("  whole gap", win)
        line("  absorb(N)->msgin(N+1)", arrive)
        line("  msgin(N+1)->submit(N+1)", queued)
        print("  %-26s %d of %d (%.0f%%) -- the rest were already queued"
              % ("  N+1 arrived AFTER absorb", late, len(arrive),
                 100.0 * late / len(arrive)))

    # --- does the slot ack's egress condition (b) actually bind? ---
    # ack(N) = max(absorb(N), egress(N-1)). Whichever term is later is
    # the one setting the ack, and therefore the one gating capture
    # N+2. If egress(N-1) always precedes absorb(N), condition (b) is
    # present for correctness but is NOT the constraint.
    binds = 0
    slack = []
    for i in ids:
        pe = ev.get(i - 1)
        e = ev[i]
        if not pe or "egress" not in pe or "absorb" not in e:
            continue
        slack.append((e["absorb"] - pe["egress"]) / 1000.0)
        if pe["egress"] > e["absorb"]:
            binds += 1
    if slack:
        print("  ACK CONDITION -- which term of max(absorb N, egress N-1) "
              "is later:")
        print("  %-26s %d of %d (%.0f%%)"
              % ("  egress(N-1) binds", binds, len(slack),
                 100.0 * binds / len(slack)))
        line("  absorb(N) - egress(N-1)", slack)


def tail_split(d):
    """Split tail(N) at the instant the MAIN thread starts writing N.

    absorb and egress are ACK_TRACE lines; the first GFX_TRACE send with
    id_server = N-1 (id_server is 0-based, the ACK_TRACE id is 1-based)
    is the handoff. All three are lines in ONE log, so this reads the
    LOG's wall-clock stamp for all of them -- no cross-clock mapping.
    Resolution is 1 ms. The ordering rate printed below is the check
    that the id pairing is right: it must be ~100%.
    """
    ab = {}
    eg = {}
    snd = {}
    for line_s in open_log(d, "xrdp.log"):
        t = TS_RE.match(line_s)
        if not t:
            continue
        w = ((int(t.group(1)) * 3600 + int(t.group(2)) * 60 +
              int(t.group(3))) * 1000 + int(t.group(4)))
        m = ABS_RE.search(line_s)
        if m:
            ab.setdefault(int(m.group(1)), w)
            continue
        m = EGR_RE.search(line_s)
        if m:
            eg.setdefault(int(m.group(1)), w)
            continue
        m = SND_RE.search(line_s)
        if m:
            snd.setdefault(int(m.group(1)), w)
    worker = []
    main = []
    tail = []
    cand = 0
    for n in sorted(ab):
        if n not in eg or (n - 1) not in snd:
            continue
        cand += 1
        a, s, g = ab[n], snd[n - 1], eg[n]
        if not (a <= s <= g):
            continue
        worker.append(s - a)
        main.append(g - s)
        tail.append(g - a)
    if not cand:
        return
    print("  TAIL SPLIT (wall clock, 1 ms resolution): %d of %d ordered "
          "correctly (%.1f%%)"
          % (len(tail), cand, 100.0 * len(tail) / cand))
    if not tail:
        print("    ORDERING FAILED -- the id pairing is wrong, ignore below")
        return
    for name, v in (("tail absorb->egress", tail),
                    ("worker absorb->1st send", worker),
                    ("main 1st send->egress", main)):
        print("  %-26s mean %6.1f ms   p50 %6.1f"
              % ("  " + name, sum(v) / len(v), pct(v, .5)))


if __name__ == "__main__":
    if len(sys.argv) < 2:
        sys.exit(__doc__)
    for d in sys.argv[1:]:
        report(d)
        tail_split(d)
