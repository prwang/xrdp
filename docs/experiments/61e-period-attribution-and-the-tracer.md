# #61e — closing the period to 0.007 ms, and the tracer that was the bug

Record for BACKLOG #61e, closed 2026-08-01. Kept verbatim, wrong claims
included; superseded statements are marked, not deleted.

Primary evidence:
[`PR-demo/mac_bisect_matrix/captures/i61e_x006_heis_20260801/README.md`](../../PR-demo/mac_bisect_matrix/captures/i61e_x006_heis_20260801/README.md)
— arms, preconditions, full tables, reproduce steps.

## The question

PRD's `capture || encode = YES for m = 1, shipped` asserts the frame
period *equals* the encode duration and the capture is *fully hidden*.
It was measured at 1600x912 in a different era. At 3840x2400, x004's
stage sum left **6.88 ms of a 35.95 ms period** attributed to no stage,
and PRD's row is exactly the claim that none of it is the encoder
waiting for a frame.

The item was falsifiable in advance: *"If `wait` is ~0 on both arms,
PRD's row survives at 3840x2400. If `wait` is several ms, the row is
wrong at this geometry and the capture pipeline — not the encoder — is
the next lever."*

## What it decided

**The row is wrong once the emit split is on, and the capture pipeline
is the next lever.** Under textflood at 3840x2400, 60 s, m=1:

| | x005 (split OFF) | x006 (split ON) |
|---|---|---|
| period | 41.124 ms | 36.764 ms |
| **`wait` per cycle** | **0.085 ms (0.2 %)** | **2.249 ms (6.1 %)** |
| cycles stalling > 1 ms | 4 of 1381 (0.3 %) | **544 of 1543 (35.3 %)** |
| unknown (gaps between brackets) | **0.005 ms** | **0.007 ms** |

The period closes to **0.007 ms** against the 0.5 ms target — from 6.88
ms — because the cycle is partitioned by every consecutive pair of
worker events rather than by summing bracketed stages, so the open
quantity is a named list of `end -> beg` gaps instead of a residue.

The split's arithmetic closes exactly: it removes **6.005 ms** of serial
assembly from the worker and hands back **2.164 ms** of new idle, plus
small stage shifts, for **−4.360 ms** — the whole of the 1.12x, and the
first mechanical account of why it is 1.12x and not 1.17x.

The stall is named, not inferred: on every stalled cycle the wait ends
within **0.01–0.03 ms** of the main thread's `enq`, and
`fifo_to_proc_depth` is **1 at every one of the 3088 enqueues across both
arms**. The capture path is demand-clocked by the pipeline and exactly
one frame deep. `capture || encode` therefore holds only while the
encoder is *slower* than the capture path — true until the emit split,
false for 35 % of cycles after it.

> **CORRECTION 2026-08-01, same day, prompted by the owner asking who
> waits for whom.** Two claims in the paragraph above are wrong, and the
> sentence that mattered was never written.
>
> **(a) "demand-clocked" is wrong, and it rests on a metric paired by
> time window.** The supporting metric — "slot release → the next
> enqueue in time" — reported ~30 ms and looked like gating. It is
> broken: in the 65 % of cycles where the frame arrives *early* it picks
> up the frame AFTER next. That is precisely the 2c gate's failure mode,
> committed while writing up an item whose own record cites that gate
> twice.
>
> Paired by **frame identity** instead, the median frame is enqueued
> **1.874 ms BEFORE** the worker releases the slot (x005: **10.115 ms**
> before). Capture runs slightly *ahead* of the release and is not gated
> on it. "Exactly one frame deep" survives — `fifo_to_proc_depth` is
> read after its increment, so 1 means the queue was empty — but that
> depth is a consequence of the two rates having converged, not of the
> encoder clocking the capture.
>
> **(b) 2.249 ms is two mechanisms, and the mean pointed at the smaller
> one.**
>
> | population | cycles | each | share of the 2.249 |
> |---|---|---|---|
> | no wait | 1000 (65 %) | ~2 µs | 0 % |
> | arrived slightly early | 466 (30 %) | ~1.87 ms (min 1.70, p90 1.95) | **25 %** |
> | capture genuinely fell behind | 78 (5 %) | ~33 ms | **75 %** |
>
> The middle group is a phase offset between two nearly-equal-rate
> loops. **The 78-cycle tail is three quarters of the idle time and is
> NOT root-caused** — not the intra-refresh cut (not periodic mod 240),
> not the payload (textflood held 59.6 fps against a 27 fps consumer).
> #61f is written against the phase margin; if the tail is a different
> mechanism, adding capture depth will not remove 75 % of the wait.
>
> **(c) the plain sentence was missing.** "The encoder worker waits for
> the xrdp main thread" — `g_obj_wait` on `event_to_proc`,
> `xrdp_encoder.c:3746`. The evidence was in the report; the conclusion
> was not. Codified under the strict honesty rule in `CLAUDE.md`
> (2026-08-01): an unreadable result is not a reported result.
>
> Unaffected: the 0.007 ms closure, the −4.360 ms split arithmetic, the
> tracer-transparency result, and the direction of the finding — the
> margin between capture and encode fell from **15.8 ms** to **1.9 ms**
> when the split took 6 ms off the worker, and PRD's row is still
> falsified.

What *is* proven, and was the other half of the ask: once a frame is
enqueued, **≥ 99.78 %** of the time before ffmpeg receives it is serial
work already in progress, not slack (x005 99.99 %, x006 99.78 %). No
scheduling change recovers it; only removing work from the chain does.

## Retraction 1 — the instrument was the bug

**The first version of this item's instrumentation produced a 3.3x
regression and a plausible-looking table to go with it.**

`common/perf_trace.c` v1 wrote every event with `fprintf` onto one
shared `FILE*`. `fprintf` takes `flockfile`. While a single thread
recorded, this was invisible. Adding **one event per frame on a second
thread** — ~140 events/second in total — moved the measured period from
**40.4 ms to 135.3 ms**, with the wait landing inside whichever stage
bracket happened to be open.

| build | threads recording | period |
|---|---|---|
| x003, pre-instrumentation | 1 | 40.4 ms |
| x005, v1 shared `FILE*` | 2 | **135.3 ms** |
| x005, ring tracer | 2 | **41.0 ms** |

Two wrong explanations were offered to the owner before the right one,
and both were killed by measurement only because the owner refused them:

- *"log volume"* — false: 14.11 lines/frame vs x003's 14.08, i.e.
  identical per frame and 3.3x **fewer** per second.
- *"a fresh pod has cold memory"* — false: `tools/avc444_pack_bench.c`
  inside each pod measured 3.32 vs 3.23 ms/frame. Also ruled out cgroup
  throttling (`cpu.max = max`, `nr_throttled 0`).

The question that found it was the owner's: *"are you sure your tracer
never uses a mutex and serializes things? You just told me a write is
100 ns, but now you say 34 µs — a 340x discrepancy."* The 34 µs figure
was itself bad: it came from a **shell `echo >> file` loop** that
reopens the file per iteration, and it is withdrawn.

The fix is PRD **FR-TRACE-1**: source and sink on different threads,
joined by a per-thread SPSC ring; the source does a vDSO clock read and
one store into a ring it owns; overflow drops and counts. spdlog was
considered and rejected on record (libstdc++ into a server that links no
C++ runtime, for a diagnostic disarmed in production; its
`mpmc_blocking_q` is a `std::mutex` around a ring anyway).

**The control that should have existed from the start now does.** Arm
**x007** is the untraced twin of x006 — same image, byte-identical
`gfx.toml` body, one env var removed:

| arm | trace | period |
|---|---|---|
| x006 | ARMED | **36.8 ms** |
| x007 | DISARMED | **37.1 ms** |

The traced arm is 0.3 ms *faster* — the wrong sign for an overhead, and
inside noise. x003 was never a control for this: it has the sink armed
too and merely happened to have one writing thread.

## Retraction 2 — "the encoder essentially never waits"

Reported to the owner on 2026-08-01 from x005's 20 s run: *"in steady
state the encoder waits 1.4 µs for the next frame — it is already on the
fifo when the worker asks."*

**True of x005 (split OFF). False of x006 (split ON)**, where the worker
stalls on 35.2 % of cycles for 2.249 ms/cycle. The statement was made
from the arm that happened to be deployed and generalised to a pipeline
configuration that had not been run. Quality gate 5: a number holds
within one payload, one client, one resolution set — and one
*configuration*.

## Retraction 3 — a mean that was one startup wait

The same 20 s run first reported `wait` mean **6.770 ms** and a closure
of **−6.772 ms**. Both were artefacts of one **2665.3 ms** session-start
wait divided across 408 cycles; 4 of 410 waits carried 2775.3 of
2775.9 ms. Quality gate 2c (a derived quantity with the wrong sign is a
broken pairing, not a measurement) is what caught it.

`i61e_period_attribute.py` computes closure **per cycle** and reports it
as a distribution, so no single outlier can contaminate it; the
steady-state exclusion is stated and printed with the result rather than
chosen after looking. Per-cycle residual on both arms above:
**max 0.000000 ms**.

## Process failure recorded alongside

The same episode spent roughly 30 minutes of full `e_gate_run.sh` runs —
and built a fleet arm (x007, in its original form) — to answer the
single bit *"is a fresh pod slower than a warm one?"*, which
`avc444_pack_bench` answered inside the existing pods in seconds. Two of
the three runs were pure waste. The owner directed the rule into
`CLAUDE.md` ("Never spend a long run on a binary check", 2026-08-01);
x007 was rebuilt to hold a *configuration* instead, which is what an arm
is for.

## What stays open

The stall is upstream of the encoder worker, in the capture-and-enqueue
path, and it is one frame deep by construction. Until that path can run
at least one frame ahead of a split-emit worker, ~2.2 ms/cycle (6.1 %)
of the period is the encoder waiting on it, and any further reduction of
worker-side serial time converts into more of exactly this wait rather
than into rate. Carried forward as BACKLOG #61f.

## ADDENDUM 2026-08-01, same day — the 78-cycle tail root-caused; two owner challenges answered

The owner raised two anomalies against the results above, and both
were justified. This section supersedes "the 78-cycle tail is NOT
root-caused" and sharpens the tracer-transparency claim. Full
per-run evidence and tables:
[`captures/i61e_x006_heis_20260801/README.md`](../../PR-demo/mac_bisect_matrix/captures/i61e_x006_heis_20260801/README.md),
reproducible with `PR-demo/mac_bisect_matrix/i61f_delivery_chain.py`.

### Challenge 1 — "the producer is always faster, and a capture on ack
### always finds damage; how can 33 ms stalls exist at all?"

Because **capture is not damage-clocked. It is ack-clocked, two frames
back, through the one xrdp thread that also writes frames to the
client.** Measured chain (x006, medians): eager slot ack for N−2 sent
at t=0 → xorgxrdp begins capturing frame N at **+8.0 ms** (IQR 2.8,
and it stays locked at 9.8 during stalls — the perturbation test no
other candidate event survives) → capture 3.8 ms → the rect then waits
**16.2 ms** (p50; p90 at stalls **56.3 ms**) for the xrdp main thread
to read it, because that thread is spreading each frame's ~3.7 MB
socket write over 17 ms/frame at the client's drain pace → submit
+3.9 ms. The xorgxrdp frontier at every capture reads `ack = N−2,
shown = N−3` (1524/1533): FR-CAPTURE-8's two-slot budget is
permanently at cap and is re-opened exactly once per encoded frame.
Damage wakes the deferred pass every ~15 ms; the pass finds the budget
full and returns. The producer's speed never enters.

The tail is this loop hit by an external forcing: the client stops
draining the socket for 50–150 ms (its GFX-ack stream has matching
100–160 ms holes at 33/34 stalls), egress goes late, the slot ack goes
late, and capture(N+2) inherits the delay — the stall **echoes at
two-frame spacing** (modal consecutive-stall spacing: exactly 2,
63/156) until slack rebuilds. Exonerated with evidence: the fif=2
window (open — `id_server − id_client` = 0 at 73/91 resumes), the
producer and session Xorg (16 273 frames, zero gaps > 30 ms, XSync
max 10.4 ms), the tracer and the new build (hole census 91/93/82 on
traced x006 / untraced x007 / old-build x004). The two wait
populations are one loop at two amplitudes: 1.87 ms is its
steady-state phase, ~33 ms is a client hiccup echoing through it.
x005's 10.1 ms margin absorbed the same hiccups (19 missed beats);
x006's 1.9 ms margin exposed them (118).

### Challenge 2 — "x003/x004 measured 40.1/35.9; the new runs are ~1 ms
### slower, yet you claimed the instrumentation has zero impact"

"Zero impact" was too strong, and the +1 ms deserved a same-day
control rather than a gate-4 footnote. The control exists: the OLD
untraced build (image `4bbf1181`), split OFF, was run twice on
2026-08-01 — **40.4 ms** (x003 re-run) and **41.3 ms** (the original
x007 fresh-pod control, same config) — bracketing the NEW traced
build's **41.0 / 41.1 ms** (x005 ring runs, same day). The ~1 ms shift
against the previous evening's 40.1/35.9 appears in the old build
itself: it is day/rig drift, common-mode across builds, not the
instrumentation. The defensible statement is: any tracer cost is below
the ~1 ms same-build run-to-run spread; the armed-vs-disarmed twin
(x006 36.8 vs x007 37.1, same image, one env var) differs by 0.3 ms
with the armed arm *faster*; and the stall census is the same on both
(91 vs 93). The split ratio survives unchanged: 41.05/36.95 ≈ 1.11x
same-day vs 40.1/35.9 = 1.12x the evening before.
