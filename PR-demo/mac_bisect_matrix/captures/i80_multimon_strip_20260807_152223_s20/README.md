# i80_multimon_strip_20260807_152223_s20 — the credit frontier at two monitors

**The single-monitor result does not fully generalise, and that is the
finding.** At two monitors the credit frontier roughly HALVES the stall
instead of removing it: 21.5 / 22.0 % of cycles still wait more than
10 ms against the legacy path's 54.4 / 50.4 %, where at one monitor the
same change took 16.4 % to under 1 %. The wire bound behaves exactly as
predicted, and the residual points at a defect this project already has
on file.

Two runs are described here. This capture is the readable one; its
sibling `i80_multimon_20260807_151836_s20` ran the same comparison with
the slower payload and is summarised at the end.

## Why there are two runs

The first multi-monitor attempt used the standard `textflood` payload.
At two monitors that payload must redraw both screens, and it came out
**slower than the pipeline**: a saturation margin of 0.51–0.55×, well
under 1, so the payload was the clock and neither arm was being
measured. That run's bound numbers are still good — a bound is a
structural property of frame identities, not a rate — but its rates are
not.

This run repeats it with the strip-render payload (BACKLOG #83), which
draws a frame in about 10 ms instead of about 27. Margin **1.27–1.47×**:
still under the 2.0× floor, so by the 2026-08-06 ruling any claim from
this run about two pipeline stages OVERLAPPING is void, while throughput
and regression comparisons between the two arms — which share the
payload, the geometry and the client — stand. The worker-idle column
below is an overlap measure and is reported for completeness only.

## The two arms

| | legacy | frontier |
|---|---|---|
| arm / port | x026 / 40042 | x022 / 40038 |
| ack mechanism | shipped gate, `eager_slot_ack = false` | credit frontier, `eager_slot_ack = true`, `wire_window = 2` |
| window source | `XRDP_GFX_FRAMES_IN_FLIGHT = 2` | `wire_window = 2` |
| payload | `textflood_strip` | `textflood_strip` |
| image | `1d5bc0960db8.xx10fa3aa-tf.pc0097388` | same |

The two `gfx.toml` bodies differ by exactly one line, verified. Two
monitors: 2560×1440 and 3840×2400 side by side. Four legs of 20 s,
interleaved. Both arms certified at deploy: 7 checks clean, 0 black
frames.

## The bound — both predictions confirmed exactly

The producer's capture budget is two slots **per monitor**, so the bound
is `C + 2·M`, not `C + 2`. At M = 2 that is 6 for the frontier at
`wire_window = 2`, and `2·M + fif − 1` = 5 for the legacy path at
`frames_in_flight = 2`. Written down in `PREDICTION.md` of the sibling
capture before either run.

Measured maximum distance between a frame and the client's last
acknowledgement:

| payload | legacy | frontier |
|---|---|---|
| strip (this run) | **5**, 5 | **6**, 6 |
| textflood (sibling run) | **5**, 5 | **6**, 6 |

Never exceeded, in four legs of each. So:

* the `C + 2·M` formula holds at M = 2 rather than needing to be
  re-derived — the owner's caution against carrying it across by
  multiplication was right to demand the check, and the check passed;
* **the frontier costs exactly ONE more frame than the legacy path at
  two monitors, not two.** The excess does not scale with monitor count.

## The rates, and the part that does not generalise

**Withheld** is the wait between the encoder children absorbing a
frame's pixels and the producer being told it may capture again — the
quantity the frontier exists to remove. **Period** is the interval
between frames leaving for the client, counting both monitors.

| leg | arm | encoder wait | period p50 | period p90 | withheld p90 | cycles stalled > 10 ms | worker idle p90 (overlap, void) |
|---|---|---|---|---|---|---|---|
| L1 | legacy | 14.84 ms | 12.34 ms | 28.77 ms | 26.090 ms | **54.4 %** | 10.727 ms |
| T1 | frontier | 15.34 ms | 10.98 ms | 28.55 ms | 11.621 ms | **21.5 %** | 9.577 ms |
| L2 | legacy | 14.62 ms | 12.19 ms | 29.10 ms | 23.267 ms | **50.4 %** | 10.958 ms |
| T2 | frontier | 14.91 ms | 10.75 ms | 28.91 ms | 12.072 ms | **22.0 %** | 8.803 ms |

The frontier is better on every row — the wait roughly halves, the stall
rate roughly halves, the period p50 improves about 11 % — but at one
monitor the same change took the wait to 0.04 ms and the stall to under
1 %. **Two monitors leave a large residual.**

## What the residual most likely is, and it is already filed

The frontier's third term is `frame_id_client + C`, and `C` is a single
number for the whole session, not per monitor. At two monitors a window
of 2 therefore allows two frames outstanding **across both screens** —
about one each — which is precisely the defect BACKLOG #91 records for
the legacy window: *"the ack window is global while the budget is per
monitor; at m = 2 the global window admits ~1 outstanding per monitor
and halves the intended depth."*

The credit frontier inherits it. That is consistent with everything
measured here: the bound is per monitor (it scales, 5 → 6 with M), while
the window that gates the credit is global (it does not).

This is a hypothesis about the residual, not a measurement of it. What
would settle it is a leg at `wire_window = 4` — two per monitor — which
should take the two-monitor stall down toward the one-monitor figure if
the global window is the cause, and leave it where it is if something
else is. That leg was not run: it changes the shipped configuration
under test and belongs to a decision about whether `C` should become
per-monitor, which is #91's scope.

## Consequence for the default that just shipped

The default flip to the credit frontier at `wire_window = 2` is still
an improvement at two monitors — every measured row moves the right way
and the bound stays one frame above the legacy path. But **the claim
that the frontier removes the producer stall is a one-monitor claim**,
and anything written for upstream must say so. At two monitors it halves
it.

## Limits

* The saturation margin is 1.27–1.47×, so overlap and concurrency claims
  from this run are void; the worker-idle column is shown but must not
  be cited as evidence about stage overlap.
* The test client acknowledges each frame before decoding it, so the
  acknowledgement latencies here are floors.
* The per-monitor capture budget's independence — #91's first item — is
  not measured here. This run reads the aggregate; that question needs
  per-monitor slot accounting.
* Two monitors only. Nothing here says anything about three or more.

## The sibling run, for the record

`i80_multimon_20260807_151836_s20` — same arms in mechanism (x020
legacy, x021 frontier) with the `textflood` payload, four legs. Bound
maxima 5 and 6, matching. Withheld p90 21.9 / 22.3 ms legacy against
12.3 / 12.3 ms frontier; stalls 43.1 / 48.1 % against 19.5 / 20.5 %. The
same halving, from a run whose rates are producer-limited at 0.51–0.55×
and are therefore reported only as corroboration of direction.
