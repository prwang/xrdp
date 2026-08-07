# i80_widen_legacy_20260807_143617_s20 — widening the old window pays the cost and buys none of the benefit

**The reviewer's objection is answered: no.** Raising the 2017-era
`frames_in_flight` knob from 2 to 3 widens the number of frames the
client may have outstanding to 4 — the same cost as the credit frontier
at wire_window 2 — while leaving the producer stalling exactly as
before. The prediction in `PREDICTION.md`, written before the run, holds
on all three points.

Owner-directed 2026-08-07 after the question "why not just raise the old
knob?" was identified as the objection the change has to survive.

## The two arms

| | widened legacy | credit frontier |
|---|---|---|
| arm / port | x025 / 40041 | x021 / 40037 |
| ack mechanism | the shipped gate, `eager_slot_ack = false` | the credit frontier, `eager_slot_ack = true` |
| window | 3, from `XRDP_GFX_FRAMES_IN_FLIGHT` | 2, from `wire_window` |
| bound this implies | client + 4 | client + 4 |

Same image (`1d5bc0960db8.xx10fa3aa-tf`), same producer, same payload,
one monitor at 3840×2400, oracle client on the host. The widened arm's
`gfx.toml` body is **byte-identical** to the window-2 legacy arm already
on record, so the environment variable is the only difference between
them. Certified at deploy: 7 checks clean, 0 black frames.

Four legs of 20 s, interleaved W T W T so each condition appears in both
halves of the sitting.

## Mechanism check, before any rate

The window each arm actually ran is echoed on every network write: **3**
on the widened legacy arm, **2** on the frontier arm. The knob applied.

## The result

Terms in plain words. **Withheld** is the wait from the encoder's
children absorbing a frame's pixels to the producer being told it may
capture again. **Idle** is the encoder worker blocked with nothing to
encode — the direct measure of whether capture overlapped encode.
**Encoder wait** is the control and must not move between conditions.

| leg | condition | encoder wait | period p50 | period p90 | withheld p90 | stalled > 10 ms | idle p90 |
|---|---|---|---|---|---|---|---|
| W1 | legacy, window 3 | 16.43 ms | 17.85 ms | 26.46 ms | **10.570 ms** | **17.6 %** | **8.985 ms** |
| T1 | frontier, window 2 | 16.24 ms | 17.42 ms | 19.55 ms | 0.048 ms | 3.9 % | 0.002 ms |
| W2 | legacy, window 3 | 16.53 ms | 17.91 ms | 26.51 ms | **10.388 ms** | **15.6 %** | **8.872 ms** |
| T2 | frontier, window 2 | 16.19 ms | 17.38 ms | 19.23 ms | 0.040 ms | 2.6 % | 0.002 ms |

The encoder wait is 16.19–16.53 ms across all four legs, so the
conditions are comparable and the host did not step underneath them.

**Prediction 1 — the widened legacy path still stalls: CONFIRMED.**
Against the same path at window 2 (10.502 ms p90, 16.4 % of cycles,
idle 8.671 ms, measured earlier the same day), widening to 3 gives
10.570/10.388 ms, 17.6/15.6 %, idle 8.985/8.872 ms. Nothing moved.

**Prediction 2 — the bound widens to 4: CONFIRMED.** The distance
between a frame and the client's last acknowledgement reached 4 on the
widened arm, and its distribution shifted outward (19 frames at distance
3 in W1, against 1 at window 2). So the widened knob pays the full
queueing cost.

**Prediction 3 — the frontier reproduces its near-zero wait: CONFIRMED.**
0.048 and 0.040 ms at p90, stalls 3.9 % and 2.6 %, worker idle
0.002 ms.

Frame period follows: the frontier's p90 is **19.55/19.23 ms against
26.46/26.51 ms**, a 7 ms improvement in the tail, with the p50 slightly
better as well (17.42/17.38 against 17.85/17.91).

## What this establishes, and it is the upstream argument

The two configurations permit the **same** number of frames outstanding.
They differ only in what the producer is told, and only one of them
removes the stall. So the frontier's benefit does not come from allowing
more frames in flight — that part is available to the old code and does
nothing on its own — it comes from the value the grant is able to carry.

The code reason, verified before the run: the legacy path grants
`frame_id_server` (`xrdp/xrdp_mm.c:1772`), which advances when a frame
reaches the transport (`:4423`), so the producer is always bounded by a
frame that has already been sent. The frontier grants
`min(frame_id_consumed, frame_id_server + 1, frame_id_client + C)`
(`xrdp/xrdp_encoder.h:114-130`), and `frame_id_consumed` advances when
the encoder children are done with the pixels (`:4366`) — before egress.
Widening the legacy window changes how often its gate opens, never what
value passes through it.

Together with the window-1 result in
`i80_c1_nonregression_20260807_141752_s20`, the change reads as an
extension rather than a replacement:

* at wire_window 1 the frontier is **today's behaviour**, measured
  identical on period, tail, stall rate and bound;
* at wire_window 2 it expresses a pipeline-state term the old gate has
  no variable for, and that is where the concurrency comes from;
* the old knob cannot reach the same place — measured here — so the new
  mechanism is load-bearing rather than a rewrite of a working one.

## Limits

* One monitor. The bound carries a per-monitor term and the whole
  argument is untested at m ≥ 2.
* The test client acknowledges a frame before decoding it, so every
  acknowledgement latency here is a floor. A real client would make the
  window term bind harder, which strengthens the finding that the
  window alone is not the mechanism, and weakens any claim that
  wire_window 2 is sufficient in the field.
* The frontier's stall rate here (3.9 % / 2.6 %) is higher than the
  1.0 % / 0.0 % measured on 2026-08-06 at the same setting. Same
  direction, same order of magnitude; the difference is not explained
  and is small against the 16 % it is being compared with.
* An older record has a wider window measuring *worse* (98.1 ms period
  at window 4 against 87.0 at window 2, 2026-07-31). That run predates
  the tracing fix and is suspect; this capture does not reproduce a
  period collapse at window 3, but it did not test window 4.
