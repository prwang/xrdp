# i98_tier0_ab — Tier 0 A/B at 40 ms RTT, against the theory bounds

BACKLOG #98 decision 2, owner-directed 2026-08-05 ("done, now do a/b
testing and compare results against predicted theory bounds") after the
metal host loaded tcp_bbr (verified: `setsockopt(TCP_CONGESTION,"bbr")`
succeeds in the pod netns).

## The experiment, exactly two legs, one pod (x019), sequential

Both legs: netem 40 ms (fixed harness, limit 25000, zero-drop
verified), single monitor 3840x2400 textflood, oracle client, 20 s,
image 1d5bc0960db8 (credit frontier, C = 1). The ONLY difference
between legs is the pod-netns TCP configuration, applied by sysctl and
reverted afterward (fresh RDP connection per leg, so the netns default
CC binds at accept time):

  leg A (control):  cubic, tcp_slow_start_after_idle=1,
                    tcp_wmem max 4 MB       -- the shipped environment
  leg B (tier0):    bbr,   tcp_slow_start_after_idle=0,
                    tcp_wmem max 16 MB

Mechanism check for the knob (quality gate 2): `ss -tin` sampled every
0.5 s inside the pod netns during BOTH legs; leg B's RDP socket must
show `bbr` and leg A's `cubic`, or the leg says nothing.

## Predictions and bounds, written BEFORE the run

From the survey (docs/research/flow-control-survey-2026-08.md) and the
measured baseline (leg A expected ~= the corrected 2026-08-03 leg:
period 86.7 ms, 11.5 fps, send-to-ack 203.7 ms, queue 6.7 MB):

  THEORY FLOOR   send-to-ack >= RTT + frame/bandwidth ~= 40 + a few ms.
                 Nothing measured can beat ~45 ms on this link.
  P-B1  leg B send-to-ack falls into the survey's predicted 60-100 ms
        band (stretch: toward ~45-60 if BBR's startup opens the window
        fully). FALSIFIER: stays ~200 ms -> the RFC 7661 pin was not
        the binding mechanism, or BBR cannot ramp on this burst shape
        (the SQP caveat) -- either way Tier 1 pacing becomes the lever.
  P-B2  period = (L + send_to_ack)/3 with L ~= 44 ms capture-to-send:
        at 60-100 ms that is 35-48 ms -> 21-29 fps
        (band quoted in BACKLOG: 25-45 fps allows L to shrink too).
  P-B3  the standing transport queue (trans::wait_bytes at egress)
        collapses: with wmem max 16 MB the socket can hold everything
        the C+2 window commits (~6.8 MB), so wait_bytes -> ~0 and the
        latency moves from "queue wait" into "in flight".
  P-B4  the wire bound id_server - id_client <= C+2 = 3 still holds on
        every send (CI invariant; environment-independent).
  P-B5  zero netem qdisc drops on both legs (harness contract).

Leg A is a same-day control: it must reproduce the 2026-08-03 corrected
leg within noise, or the pod/environment drifted and neither leg reads.

## Results (analysis in ANALYSIS.txt; every prediction below quoted against its measurement)

Control gate: leg A reproduces the 2026-08-03 corrected leg — period
86.8 vs 86.7 ms, send-to-ack 204.2 vs 203.7, queue 6.65 vs 6.7 MB. The
environment did not drift; the A/B reads.

| | A cubic | B bbr+tier0 | prediction |
|---|---|---|---|
| send-to-ack p50 | 204.2 ms | **50.1 ms** | P-B1: 60–100, stretch 45–60 — **at the stretch floor** |
| frames/s | 11.5 | **36.6** | P-B2: 21–29 — **exceeded** (L fell too, see below) |
| period p50 / p99 | 87.2 / 108.7 | **25.5 / 44.3** | |
| queue at egress, mean | 6 654 KiB | **24.7 KiB** | P-B3: ~0 — **confirmed** (270× collapse) |
| worst id_server−id_client | 2 | 2 | P-B4: ≤ 3 — **held** |
| qdisc drops | 0 | 0 | P-B5 — **held** |
| cc on live socket (ss) | cubic ×38 | bbr ×76 | mechanism check — **applied** |
| cwnd p50 | 4 467 pkts | 22 906 pkts (~32 MB) | the RFC 7661 pin is gone |

Theory floor: RTT 40.38 + serialization + ~10 ms client ≈ 50–55 ms.
Measured 50.1 ms p50 — **the transport is at its theoretical floor at
this frame size**. The per-frame regression that explained the cubic
leg (ack = 54.4 + queue×0.0222, R² 0.926) degenerates on the BBR leg
(slope ≈ 0, R² 0.001): latency no longer depends on queue because
there is no queue.

Why fps beat prediction P-B2: the band assumed capture-to-send L fixed
at ~44 ms; L itself fell 49.9 → 21.3 ms once the transport stopped
exerting backpressure into the server. Period model closes:
(21.3 + 50.1)/3 = 23.8 vs 25.5 measured (7 %).

Known BBR cost, visible: period max 250.8 ms (p99 is 44.3). The ss
samples include cwnd = 4 — BBR's ProbeRTT phase (drains to 4 packets
for ~200 ms every ~10 s). One ~250 ms hiccup per ~10 s is the price of
BBR's RTT re-anchoring; it is in the max, not the p99.

NOT measured here: the LAN effect of tier0 (BBR at 0.07 ms RTT), and
C > 1 under tier0 — with the queue gone, (L+A)/(C+2) predicts C = 2
would reach ~18 ms period (~56 fps) at unchanged frame size; that is
the natural next leg but is not in this A/B's approved scope.

Harness note: the "declared ceiling" line printed the HOST wmem (4 MB)
during leg B while the pod ran 16 MB — display bug, fixed in
netem_rtt.sh after this capture (reads the pod netns now).
