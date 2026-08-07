# i80_c1_nonregression_20260807_141752_s20 — the credit frontier at the legacy-equivalent window, and what the emit thread buys

**The headline reverses a recommendation made the day before.** At a wire
window of 1 — the value that is genuinely equivalent to the shipped
`frames_in_flight = 2` — the credit frontier reproduces the legacy path
*including its defect*. The stall the frontier was built to remove is
still there. The improvement measured on 2026-08-06 came from the window
being **2**, not from emitting the credit eagerly.

Owner-directed 2026-08-07: "measure it first and get solid evidence of
non-regression", and "also measure [the emit thread]".

## The three conditions

Same server build (`1d5bc0960db8`), same producer, same payload, one
monitor at 3840×2400, oracle client on the host, image
`localhost/xrdp-bisect:1d5bc0960db8.xx10fa3aa-tf`. Six legs of 20 s,
interleaved L F E L F E so each condition appears in both halves of the
sitting — this host has been observed stepping its encoder speed between
adjacent legs.

| leg pair | arm | port | condition | differs from the one above by |
|---|---|---|---|---|
| L1, L2 | x020 | 40036 | **legacy** — `eager_slot_ack = false`, window 2 from `XRDP_GFX_FRAMES_IN_FLIGHT` | — |
| F1, F2 | x023 | 40039 | **frontier at window 1** — `eager_slot_ack = true`, `wire_window = 1` | the ack mechanism |
| E1, E2 | x024 | 40040 | **frontier at window 1, emit thread OFF** | `emit_thread` |

`wire_window` is written in the legacy arm's config too but is inert
there: only `xrdp_mm_emit_credit_frontier()` reads it and the legacy arm
never enters that function (`xrdp/xrdp_mm.c:1800` branches on
`eager_slot_ack`). So the legacy-to-frontier step is one effective line.

Both new arms certified at deploy: 7 checks clean, 0 black frames.

## Mechanism check, before any rate

| leg | credit records emitted | window reported | frames outstanding when one reached the network |
|---|---|---|---|
| L1 | 0 | 0 (legacy call site passes a literal 0) | 1:724 2:175 3:1 |
| F1 | 579 | 1 | 1:579 2:320 3:1 |
| E1 | 565 | 1 | 1:565 2:330 3:2 |
| L2 | 0 | 0 | 1:544 2:311 3:1 |
| F2 | 615 | 1 | 1:615 2:1 3:1 |
| E2 | 715 | 1 | 1:715 2:201 |

The knob applied. And the bound claim is now measured rather than
argued: **the maximum distance between a frame and the client's last
acknowledgement is 3 on the legacy path and 3 on the frontier at window
1** — identical, as the code predicts (legacy grants up to `client + 1`
and rides two capture slots above it; the frontier at window 1 clamps at
`client + 1` and does the same). For contrast, the same measurement at
window 2 on 2026-08-06 reached 4.

## The result — non-regression, and no gain

Definitions in plain words. **Period** is the interval between frames
leaving for the client. **Withheld** is the wait from the encoder's
children absorbing a frame's pixels to the producer being told it may
capture again. **Idle** is the encoder worker blocked with nothing to
encode — the direct measure of whether capture overlapped encode.
**Encoder wait** is the worker waiting for the two ffmpeg children, and
it is the control: it must not move between conditions.

Legs L1/F1/E1, one host state:

| condition | encoder wait | period p50 | period p90 | withheld p90 | frames stalled > 10 ms | idle p90 |
|---|---|---|---|---|---|---|
| legacy | 16.26 ms | 17.64 ms | 26.05 ms | 10.502 ms | 16.4 % | 8.671 ms |
| frontier, window 1 | 16.33 ms | 17.65 ms | 26.40 ms | **10.697 ms** | **16.3 %** | 8.943 ms |
| frontier, window 1, no emit thread | 16.26 ms | 17.77 ms | 25.77 ms | 10.796 ms | 20.1 % | 8.742 ms |

**Non-regression: confirmed.** Period, tail and encoder wait are the same
within the run-to-run spread of this host.

**And that is the whole finding.** The frontier at window 1 is not merely
non-regressive, it is behaviourally *identical* — the stall is still
16.3 % of cycles and the worker still starves for 8.9 ms at p90.

## Why: the window is the mechanism, not the eager emission

Put beside the 2026-08-06 legs at window 2, with the encoder at the same
speed in all four so the comparison is like-for-like:

| condition | encoder wait | period p50 | withheld p90 | stalled > 10 ms | idle p90 |
|---|---|---|---|---|---|
| legacy (2026-08-06) | 16.26 ms | 17.72 ms | 10.569 ms | 16.4 % | 9.129 ms |
| **frontier, window 2** | 16.46 ms | 17.45 ms | **0.040 ms** | **0.8 %** | **0.002 ms** |
| legacy (2026-08-07) | 16.26 ms | 17.64 ms | 10.502 ms | 16.4 % | 8.671 ms |
| **frontier, window 1** | 16.33 ms | 17.65 ms | 10.697 ms | 16.3 % | 8.943 ms |

The window-2 row is `leg_b1` of `i87_eager_ab_20260806_180910_s20`,
quoted per leg rather than pooled with its sibling: `leg_b2` of the
same capture reads 0.047 ms and 0.0 %. Both are quoted here so the
spread is visible rather than averaged into one number.

This is an intervention result, not an attribution from a single leg:
one config line changes, the stall appears and disappears. **The term
that holds the credit is the end-to-end window** (`client + C`), and at
C = 1 it binds on essentially every cycle because the producer needs
permission for frame k while the client has only acknowledged k − 2.

It also closes the question #80 left open. The residual stalls that the
earlier record could not attribute — because all three frontier terms are
equal at the instant a credit is emitted, so the emission record cannot
say which one held — are the window term. The counterfactual settles what
the instantaneous record could not.

**Consequence for the shipped default, stated plainly:** the credit
frontier at window 1 is a *refactor* — same behaviour, same bound, no
gain. The concurrency win requires window 2, which permits one more frame
outstanding than the 2017-era path (`client + 4` against `client + 3`).
That is a real trade to argue on its merits, not a free win and not
something "legacy equivalence" can carry.

## The emit thread

F1 against E1, same host state, differing only in `emit_thread`:

| | emit thread ON | emit thread OFF |
|---|---|---|
| period p50 | 17.65 ms | 17.77 ms |
| period p90 | 26.40 ms | 25.77 ms |
| the assembly stage itself | 0.313 ms mean | **0.230 ms mean** |
| encoder wait (control) | 16.33 ms | 16.26 ms |

The stage is **cheaper inline than on its own thread** — 0.230 against
0.313 ms — which is what a hand-off through a slot and two semaphores
costs. Moving it back onto the worker adds that 0.230 ms to the serial
chain and the period p50 rises by 0.12 ms, about 0.7 %; the p90 goes the
other way by 0.63 ms. Both are inside this host's leg-to-leg spread.

So the thread buys, at most, a tenth of a millisecond of period, and it
is not distinguishable from noise in this sitting. Against that it costs
a permanent thread, two semaphores, a hand-off slot, join logic and a
drop counter.

## Limits of this run, stated rather than left to be discovered

* **One monitor only.** The assembly work grows with monitor count, so
  the emit-thread result does not transfer to m ≥ 2, and neither does the
  wire bound, whose formula carries a per-monitor term.
* **The test client acknowledges before it decodes**, so every
  acknowledgement latency here is a floor. On a real client the window
  term would bind harder, not less — which strengthens the finding that
  window 1 stalls, and weakens any claim that window 2 is sufficient.
* **Leg F2 landed in the slow-encoder state** (encoder wait 26.23 ms
  against 16.3 ms elsewhere), which is the host step observed on
  2026-08-06. Its numbers are reported above but are not comparable to
  its own pair; the L1/F1/E1 round is the readable one, and it is
  complete on its own.
* **The frontier's improvement at window 2 is one leg pair on one day.**
  It reproduces the direction of #80's earlier LAN result but it has not
  been repeated at matched encoder speed more than once.
