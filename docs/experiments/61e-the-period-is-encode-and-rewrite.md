# #61e — the frame period at 4K is encode plus LTR rewrite, and nothing else

2026-08-01. This replaces the period attribution that #61h voided and
deleted (`61e-period-attribution-and-the-tracer.md`, git history only).
Every number below was taken with the per-frame trace on
`common/perf_trace`'s ring, whose producer cost is ~7 µs/frame —
0.02 % of the period measured here (`61h-what-the-ring-costs.md`).

**One arm, no control.** The deliverable is a within-run accounting that
closes against its own period. It is not a ratio, and nothing in it may
be compared with a pre-#61h number.

## The answer, in one paragraph

At 3840×2400, one monitor, textflood, with the eager slot ack and the
emit split both on, the frame period is **25.5 ms and all of it is the
encoder worker running serially**: 16.7 ms waiting for the two ffmpeg
children to encode the frame (65 %) and 8.8 ms popping the encoded NALs
and rewriting both views' long-term-reference lists (35 %). Capture,
the fifo handoff, submit, bookkeeping, slot release, EGFX assembly and
the main thread's rect handling together account for **0.1 %**. There is
no idle in the loop and no queueing delay to recover — the only way this
period gets shorter is if encode or rewrite gets cheaper, or if they
stop being serial with each other.

## Conditions

xrdp `82babb9fe4ba` (the #61h ring build), xorgxrdp `10fa3aa23033`,
arm x013, `eager_slot_ack = true`, `emit_thread = true`, VAAPI 444,
`intra_refresh_frames = 240`, one monitor at 9.22 Mpx, 2227 sends over
56.7 s. Trace integrity: 75 715 records, **0 drops**.
Capture: `PR-demo/mac_bisect_matrix/captures/i61e_x013_eager_m1_4k_20260801`.

## The decomposition

Worker cycle `wait_beg → wait_beg`, n = 2226. The cycle is partitioned by
every consecutive pair of events, so it closes by construction:
**per-cycle residual max |0.000000| ms.**

| what the machine is doing | ms | share |
|---|---|---|
| wait for the two ffmpeg children to finish encoding this frame | 16.654 | 65.4 % |
| pop the encoded NALs out of both children and rewrite their LTR references | 8.802 | 34.6 % |
| all remaining stages and every gap between them | 0.018 | 0.1 % |
| **period** | **25.474** | |

Cross-check: the server's own send-to-send interval is 25.5 ms mean
(2227 sends / 56.7 s = 25.46 ms). Two instruments, one number.

Shape checked before quoting the centres — both stages are unimodal.
`pump` p10 15.674 / p50 16.610 / p90 17.640 (p90÷p10 = 1.13); `collect`
p10 7.174 / p50 8.276 / p90 11.386 (1.59).

## PRD `capture ‖ encode` at m = 1: CONFIRMED — for the conditional reason the row already gives

The row was left UNSETTLED by #61h after the "falsified" verdict was
withdrawn. It is now settled, and it holds:

* the worker's `wait` bracket — time spent holding nothing to encode —
  is **0.0019 ms mean, with 0 of 2226 cycles above 1 ms**;
* **100 %** of frames were already on the fifo when the worker asked;
* `fifo_to_proc_depth` was **0 at all 2227 takes** — the queue never
  held more than one frame.

**Gate 2b was run before this was believed.** The worker skips the wait
entirely when it carries items, so an empty `wait` could have been empty
by construction rather than by fact. It was not: `wait_beg` fired in all
2227 cycles, holding `n_items = 0` in every one. The worker entered a
real blocking wait each cycle and it returned in about two microseconds.

**But read the row's condition, because it is the whole content of the
result.** Capture is hidden *because encode is slow*. The producer keeps
up with a 25.5 ms consumer; it is not evidence that the producer is
fast. Anything that makes the encoder materially quicker moves this row
back to being a live question, and the number that decides it is the
9.22 Mpx capture cost, which this run does not measure.

## What this says to #61f, which is the item that needed it

#61f exists to unblock ffmpeg from seeing newly captured data. That
delay is now measured: **enqueue → submit is 15.316 ms**, 60 % of the
period. Of it, the worker was idle **0.0019 ms** — a recoverable share
of **0.01 %**.

The frame sits on the fifo for 15 ms because the worker is still
encoding the previous one. That is serial work in progress, not
scheduling slack. **No re-ordering, no earlier wake-up and no cheaper
handoff recovers it**, which retires the whole class of fix #61f's first
attempt reached for — and confirms, from the other direction, that the
egress cork was not merely a slow lever but an unrelated one.

What is left is the arithmetic: `pump` and `collect` are serial with
each other within a frame and serial across frames. Overlapping them is
BACKLOG #74's Lever 2, and this measurement is the first thing that puts
a size on the prize — up to 8.8 ms of the 25.5 ms period, if collect for
frame N could run while the children encode N+1.

## The other two threads are not in the period

* EGFX assembler `emit`: 0.393 ms mean **on its own thread**, 1.5 % of a
  frame. The emit split is off the critical path, which is what it was
  built for.
* xrdp main `msgin → enq`: 0.0008 ms mean. Handling an inbound xup rect
  costs about a microsecond and is not a term in this period.
* Wire: 3.49 MB/frame, 7.76 GB over the run, 136.9 MB/s.

## Two numbers in the raw output that are NOT results

1. **`E5 GATE: baseline 51.1 ms → 2.01x PASS` is void as a comparison.**
   51.1 ms is the harness default measured under `SESSION_KIND=code`, a
   10 Hz `sleep 0.1` metronome, at a different geometry; this run is
   textflood at 9.22 Mpx. `e_gate_run.sh`'s own header forbids the
   comparison and it printed only because `E5_BASE_MS` was left at its
   default. This is the identical failure quality gate 1 caught on
   2026-07-31. **No baseline exists for this configuration**; 25.5 ms
   stands as an absolute measurement and as nothing else.
2. **Nothing here measures what the eager ack bought.** One arm, no
   `eager_slot_ack = false` twin. The run says where the period goes,
   not what any switch is worth.

## Open, and left open

`collect` is 8.8 ms/frame of xrdp's own CPU for a single monitor. The
comment beside that bracket in `xrdp_encoder.c` cites an offline
rewrite bench of **1.75 ms/pair** — 5× less. The offline figure records
no resolution, and this run is 9.22 Mpx at 3.5 MB/frame, so the gap may
be entirely size. It is **not verified**, and it is the largest single
piece of xrdp-side CPU in the period, so it is worth its own cheap
check (`tools/avc444_ltr_rewrite_bench.c` at this frame size) before
anyone designs against either number.

## Correctness, which timing cannot affect but which ran anyway

E2 wire audit `--assert`: 7/7 (A1–A7). Black frames: 0 of 4448 decoded
pictures. Server log: 0 rewrite failures, 0 unsupported, 0 pair aborts,
0 budget assertions. E4: `kids_armed = 2` on all 2227 cycles.
