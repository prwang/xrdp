# i98_bwlimit — bandwidth-limited links: does fps degrade gracefully without excessive added delay?

BACKLOG #98, owner-directed 2026-08-06 ("we also need bandwidth limited
links the fps needs to downgrade gracefully without excessive added
delay, report that test").

## The experiment: FOUR legs, one pod (x019, credit frontier, C = 1), sequential, 20 s each

All legs: netem 40 ms RTT + a DECLARED bottleneck (tbf on the
server->client direction, drop-tail, 100 ms buffer at the configured
rate, ack direction unshaped), each rate verified by a ~3 s bulk
measurement before the leg. Pod-netns TCP config set per leg and
reverted after.

  leg bbr400   bbr+tier0, 400 Mbit/s (50.0 MB/s)
  leg bbr200   bbr+tier0, 200 Mbit/s (25.0 MB/s)
  leg bbr100   bbr+tier0, 100 Mbit/s (12.5 MB/s)
  leg cubic200 cubic/shipped-sysctls, 200 Mbit/s -- the contrast that
               asks whether tier0 still matters when the LINK, not the
               congestion window, is the constraint. Hypothesis: little
               difference, because the C+2 frame window bounds the
               queue for both CCs.

## Predictions, written BEFORE the run

Frame size S ~= 3.4 MB (measured on prior legs; re-measured per leg).
L (capture-to-send inside the server) ~= 21 ms under tier0.

  P-G1  fps tracks the link: fps ~= B/S within ~15 %
        -> 400 Mbit: ~13-14    200 Mbit: ~6.5-7    100 Mbit: ~3.3-3.5
  P-G2  the delay is BOUNDED BY THE WINDOW, not growing: send-to-ack
        plateaus at ~ (C+2)*S/B + RTT - epsilon:
        -> 400: ~190-250 ms    200: ~390-480 ms    100: ~800-950 ms
        with period model 3P = L + A closing within ~10 %.
  P-G3  no unbounded queue anywhere: server wait_bytes stays bounded
        (NOTE: under tier0 the committed-but-undelivered bytes hide in
        the 16 MB socket buffer, so wait_bytes alone cannot show them
        -- ss Send-Q is sampled every 0.5 s per leg to see the real
        location), and the tbf buffer (100 ms) does not sustain
        overflow drops after startup.
  P-G4  the wire bound id_server - id_client <= C+2 = 3 holds on every
        send, at every rate.
  P-G5  DROP-BY-COALESCE, not stall: frames keep flowing at B/S with
        no multi-second gaps; the payload's damage keeps being
        absorbed into fewer, fresher frames (the #80 design's whole
        point on slow links).

## "Graceful" defined, so the verdict is not adjective-driven

  GRACEFUL =  (i) fps within 15 % of B/S at every rate, AND
             (ii) send-to-ack bounded by the window formula
                  (C+2)*S/B + RTT within +20 %, flat over the leg
                  (no upward trend = no unbounded queue), AND
            (iii) zero sustained tbf overflow after the first second.

  EXCESSIVE DELAY = send-to-ack above the window bound by >20 %, or
  trending upward through the leg -- either means bytes are queueing
  somewhere the window does not control.

The window bound itself is the finding to report either way: at fixed
S, C+2 frames of latency IS the floor's multiplier, and only smaller
frames (Tier 2) or smaller C can lower it.

## Results — every pre-registered prediction met; verdict: GRACEFUL, with the cost quantified

| leg | link (measured) | fps (pred) | ack p50 | window bound (C+2)S/B+RTT | ratio | trend 1st→3rd third | worst unacked |
|---|---|---|---|---|---|---|---|
| bbr400 | 44.1 MB/s | **12.8** (13.0) | 203.8 ms | 284 ms | 0.72 | +1 % | 2 ≤ 3 |
| bbr200 | 22.4 MB/s | **6.5** (6.6) | 435.5 ms | 520 ms | 0.84 | +1 % | 2 ≤ 3 |
| bbr100 | 11.3 MB/s | **3.35** (3.3) | 900.7 ms | 998 ms | 0.90 | +2 % | 2 ≤ 3 |
| cubic200 | 22.2 MB/s | **6.3** (6.6) | 419.2 ms | 525 ms | 0.80 | −2 % | 2 ≤ 3 |

* **P-G1 met**: fps = link rate / frame size within 3 % at every rate.
* **P-G2 met**: send-to-ack sits at 0.72–0.90 of the window bound —
  UNDER it at every rate — and is FLAT across each leg (±2 %): no
  queue grows anywhere. The measured bands hit the predicted ones at
  all three rates.
* **P-G3 met, with a finding**: the committed-but-undelivered ~2
  frames live in DIFFERENT places by CC — under bbr in the socket
  (Send-Q p50 7.6–8.5 MB, xrdp wait_s ≈ 0), under cubic in xrdp's
  wait_s (7.6 MB, Send-Q 1.7 MB). Same total, same latency; the
  window governs it either way. tbf drops per leg: 0 / 4 / 30 / 7
  packets over 168k–667k — no sustained overflow.
* **P-G4 met**: wire bound held on every send of every leg.
* **P-G5 met**: largest send gap 579 ms (~2 periods) at 100 Mbit; the
  55 delivered frames each carry coalesced damage — fewer, fresher
  frames, exactly the #80 drop-at-source design intent.
* **cubic ≈ bbr on a link-limited path** (419 vs 436 ms, 6.3 vs 6.5
  fps): hypothesis confirmed — when the LINK binds, the frame window
  is the governor and CC choice is second-order. (Residual tier0
  benefit: capture-to-send L stays 21 ms under bbr vs 52 ms under
  cubic — backpressure into the server returns with cubic.)

**The quantified conclusion:** at fixed frame size S, a
bandwidth-limited link costs `(C+2)·S/B + RTT` of delay — bounded,
flat, but at 100 Mbit that is ~0.9 s, far beyond any interactivity
budget (<100–150 ms). fps degrades gracefully BY DESIGN (drop-at-
source); delay degrades gracefully TO THE WINDOW BOUND, and the only
levers below that bound are smaller C or smaller frames (Tier 2 —
encoder rate adaptation), as the survey predicted. At 200 Mbit, 4K
interactivity needs frames ≤ (budget−RTT)·B/(C+2) ≈ 0.5 MB — a 7×
compression increase, squarely Tier 2 territory.
