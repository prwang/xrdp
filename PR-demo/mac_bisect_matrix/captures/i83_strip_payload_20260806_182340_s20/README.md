# i83_strip_payload_20260806_182340_s20 — the faster benchmark producer, deployed

**BACKLOG #83's acceptance criterion is met, and the control says
something more useful than the headline.** The benchmark payload now
draws a frame in 4.5 ms instead of 16.2 ms, which lifts the
saturation margin from 1.05× to 3.94×. Making it 3.6× faster did **not**
change the pipeline's frame rate — so the pipeline, not the payload, was
already setting the ~17.4 ms period, and the doubt that hung over the
whole textflood series (that it might have been measuring the payload)
is answered for this geometry.

Owner-approved 2026-08-06 as "1 new arm + image, ~30 min".

## The two arms

Both run the same server build
(`xrdp-dev 0.10.80+git20260803024109.1d5bc0960db8`, xorgxrdp
`10fa3aa23033`), the same `gfx.toml` body (verified identical outside
comments), one monitor at 3840×2400, the oracle client on the host.
**They differ in the benchmark payload and nothing else.**

| arm | port | payload | what it does per frame |
|---|---|---|---|
| x021 | 40037 | `SESSION_KIND=textflood` | redraws every text row of the screen |
| x022 | 40038 | `SESSION_KIND=textflood_strip` | copies the frame up by the scroll distance and rasterizes only the newly exposed band |

`textflood_strip` is a separate session kind rather than a flag on
`textflood`, so a capture's `deployed_session_kind.txt` names which
payload produced it — 26 archived captures were measured with the full
redraw, and a rate is only comparable within one payload.

The strip mode pins the **content** advance to 1479.2 corpus lines per
second, which is what the full redraw actually achieved (25 lines per
16.901 ms frame on arm x014). Without that pin a producer drawing 3.6×
faster would also put 3.6× more motion into every encoded frame, and
"the producer got faster" would silently also mean "the encoder's job
got harder" — the confound this item exists to remove.

x022's image is tagged `1d5bc0960db8.xx10fa3aa-tf.pc0097388`, the
suffix being a hash of the payload source. On its first use the deploy
**aborted** because the manifest still pinned the payload-less tag,
rather than silently running the old producer under the new arm's name.

## The legs

Four interleaved 20 s legs — s1(x022), r1(x021), s2(x022), r2(x021) —
because an unchanged arm on this host has drifted 36.8 → 41.8 ms across
a single day, so one leg each cannot be read against that drift.

Terms used below, in plain words:

* **producer frame interval** — how long the benchmark payload takes to
  draw one frame, from its own per-frame timestamps. Lower is faster.
* **pipeline period** — the interval between two consecutive frames
  leaving the server for the client. This is the rate under test.
* **saturation margin** — pipeline period ÷ producer interval. PRD
  FR-BENCH-1 wants ≥ 2.0×, so that a missing frame can be blamed on the
  server rather than on the payload failing to supply one.
* **encoder wait** — the worker's `pump` bracket: time spent waiting for
  the two ffmpeg children to finish encoding a frame.

| leg | arm | payload | producer mean | producer p90 | pipeline p10 | pipeline p50 | pipeline p90 | margin | encoder wait |
|---|---|---|---|---|---|---|---|---|---|
| s1 | x022 | strip | 4.5 ms | 5.8 ms | 15.75 ms | 17.44 ms | 19.41 ms | **3.94×** | 16.283 ms |
| r1 | x021 | full redraw | 16.2 ms | 18.2 ms | 25.30 ms | 27.40 ms | 29.96 ms | 1.70× | 26.355 ms |
| s2 | x022 | strip | 4.6 ms | 6.0 ms | 15.76 ms | 17.51 ms | 19.83 ms | **3.90×** | 16.371 ms |
| r2 | x021 | full redraw | 16.6 ms | 18.3 ms | 15.58 ms | 17.23 ms | 19.32 ms | 1.05× | 16.085 ms |

Producer figures are the gate's own, computed from the payload's
per-frame stamp file archived beside each leg. Pipeline percentiles and
the encoder wait are recomputed from each leg's perf ring.

## Acceptance

#83's criterion is "producer p90 strictly below pipeline p10 at
3840×2400, stated in the arm's own capture".

* **strip payload: PASS** — 5.8 ms against 15.75 ms, and 6.0 against
  15.76.
* **full redraw, same host state (leg r2): FAIL** — 18.3 ms against
  15.58 ms. That is the state the entire existing textflood series was
  measured in, and it is exactly the 1.09× margin the item was filed on.

## The control, and why it matters more than the headline

Compare **s1/s2 against r2 only** — leg r1 is not comparable, see the
next section.

| | strip (s1, s2) | full redraw (r2) |
|---|---|---|
| producer interval | 4.5 / 4.6 ms | 16.6 ms |
| pipeline p50 | 17.44 / 17.51 ms | 17.23 ms |

The producer got 3.6× faster and the pipeline period moved by 0.2–0.3 ms
— within the drift this host shows between adjacent legs. **The pipeline
was already the limit at ~17.4 ms.** A payload sitting at a 1.05× margin
*could* have been clocking the measurement; this run shows it was not,
at this geometry and this pipeline speed. That does not retire the
margin requirement: it says the requirement's worry did not materialise
here, and the margin must still be printed and read (it is now printed
in every VERDICT).

## Leg r1 is not comparable, and why

r1's encoder wait is **26.355 ms** against 16.085 ms in r2 — the same
arm, the same payload, eleven minutes apart. Every other stage is
unchanged. The same host-level encoder slowdown appears in the same
day's A/B capture (`i87_eager_ab_20260806_180910_s20`), where it hit
both arms simultaneously.

Its margin of 1.70× is therefore **not** evidence of a healthier
benchmark: the ratio improved because the pipeline got slower, not
because the producer got faster. A margin is a ratio, and a ratio can be
inflated from below.

The GPU clock state was sampled only *after* the runs (levels
600/1100/2900 MHz, DPM on "auto", idling at 600). DVFS is a plausible
mechanism; it was **not measured during the legs**, so it stays a
hypothesis.

## What the strip path changes about the picture

Rows are clipped to their own band so the scroll distance is an exact
multiple of the line height, which is what makes the copy provably
correct. That changes **18 580 of 9 216 000 pixels — 0.2016 %** —
against the legacy render, all of it antialiasing fringe where ink
crosses a row boundary. Measured, and the reason the two payloads are
separate session kinds rather than one payload with a flag.

The offline check `textflood --verify N` renders N frames down both
paths and compares them byte for byte; it is green at 1080p, 1440p and
4K, and was shown to fail on four deliberate breaks of the strip path.

## Not answered here

* Whether the pipeline's period changes once the encoder is faster —
  the encoder wait is 16.1–16.4 ms of a ~17.4 ms period in every
  comparable leg, so the encoder remains the ceiling and the producer's
  extra headroom buys nothing until that moves.
* Anything about two pipeline stages overlapping: at 3.94× the margin
  now permits such claims, but this run did not instrument for them.
* Any geometry other than 3840×2400.
