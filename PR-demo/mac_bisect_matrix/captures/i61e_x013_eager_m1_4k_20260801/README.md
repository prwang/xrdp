# i61e_x013_eager_m1_4k_20260801 — the eager-ack period, re-measured on the ring

BACKLOG #61e. **One arm, no control.** The deliverable is the WITHIN-RUN
decomposition of the frame interval — an accounting that closes against
its own period — not a ratio. Nothing here may be compared with a
pre-#61h number or with another arm measured on another day.

## Conditions

| | |
|---|---|
| arm | x013, `127.0.0.1:40030` |
| xrdp | `0.10.80+git20260801213501.82babb9fe4ba` (the #61h ring build) |
| xorgxrdp | `1:0.10.80+git20260731212221.10fa3aa23033` |
| image | `localhost/xrdp-bisect:82babb9fe4ba.xx10fa3aa-tf` |
| config | `eager_slot_ack = true`, `emit_thread = true`, VAAPI 444, `intra_refresh_frames = 240` |
| payload | `SESSION_KIND=textflood` |
| geometry | **one** monitor, 3840×2400 = 9.22 Mpx |
| trace | `XRDP_PERF_TRACE=/var/log/xrdp-perf/enc` — the ring, NOT log.c |
| duration | 64 s wall, 2227 sends over 56.7 s of steady state |

## Trace integrity

75 715 records, **0 `perfdrop`**, 0 `perfnoring`. Three producer rings,
one per thread: encoder worker (44 542), xrdp main (22 265), EGFX
assembler (8 908). Every per-cycle bracket appears exactly 2227 times.

## The period, and where all of it goes

Worker cycle `wait_beg → wait_beg`, n = 2226 steady-state cycles.
Partition is exact by construction; **per-cycle closure residual
max |0.000000| ms**.

| what the machine is doing | ms | share |
|---|---|---|
| **waiting for the two ffmpeg children to encode the frame** (`pump`) | 16.654 | 65.4 % |
| **popping the encoded NALs and rewriting both views' LTR references** (`collect`) | 8.802 | 34.6 % |
| everything else — capture wait, fifo handoff, submit, bookkeeping, slot release | 0.018 | 0.1 % |
| **period** | **25.474** | 100 % |

Two independent instruments agree on the period: the worker cycle says
25.474 ms; the server's own send-to-send interval says 25.5 ms mean
(2227 sends / 56.7 s = 25.46 ms).

Both stages are unimodal, so the means are quotable: `pump`
p10 15.674 / p50 16.610 / p90 17.640 (p90÷p10 = 1.13), `collect`
p10 7.174 / p50 8.276 / p90 11.386 (1.59). One `pump` outlier at
132 ms against a p99 of 19.3 — a single event in 2227.

## C1 — is capture hidden behind encode? YES, and the metric could have said no

`wait` (the worker holding nothing to encode): **mean 0.0019 ms,
0 of 2226 cycles above 1 ms.** 100 % of frames were already on the fifo
before the worker's `wait_beg`.

**Gate 2b, run before believing it.** A bracket that is empty by
construction proves nothing. The worker skips the wait entirely when it
carries items (the starvation rule), which would make an empty `wait`
meaningless. Checked: `wait_beg` fired in every one of the 2227 cycles,
and `n_items` held at `wait_beg` was **0 in every one**. The worker
entered a blocking wait every cycle and it returned in ~2 µs. The metric
was capable of showing a stall; there was none.

`fifo_to_proc_depth` after each take: **0 at all 2227 takes** — the
queue never held more than the one frame. This re-confirms, on a
ring-traced build, the count PRD kept as a #61h survivor.

## C2 — could ffmpeg have seen the frame sooner? NO

Enqueue → submit is **15.316 ms** (p50 15.035, p90 18.199), 60 % of the
period. Of that, the worker was idle for **0.0019 ms**. Recoverable
share **0.01 %**.

The frame sits available for 15 ms because the worker is still encoding
the *previous* one. That is serial work in progress, not scheduling
slack, and no re-ordering of the existing stages recovers it.

## The other two threads are not in the period

* EGFX assembler `emit`: 0.393 ms mean on its own thread = 1.5 % of a
  frame. The emit split is doing what it was built to do.
* xrdp main `msgin → enq`: 0.0008 ms mean. The main thread's handling of
  an inbound xup rect is ~1 µs and is not a term in this period.
* Wire volume: 3.49 MB/frame, 7.76 GB over the run, 136.9 MB/s.

## Correctness (unaffected by timing, but it ran)

E2 wire audit `--assert`: 7/7 PASS (A1–A7). Black-frame check: 4448
pictures, 4448 decoded, **0 black**. Server log: 0 rewrite failures,
0 unsupported, 0 pair aborts, 0 budget assertions. E4: `kids_armed=2` on
all 2227 cycles.

## Two things in the gate output that must NOT be quoted

1. **`E5 GATE: baseline 51.1 ms → 2.01x PASS` is meaningless here.**
   51.1 ms is the harness default, measured under `SESSION_KIND=code` —
   a 10 Hz `sleep 0.1` metronome — at a different geometry. This run is
   textflood at 9.22 Mpx. The script's own header forbids the
   comparison; it prints it because `E5_BASE_MS` was left at its
   default. Quality gate 1 flagged exactly this failure on 2026-07-31.
   **There is no baseline for this configuration.** The 25.5 ms period
   stands on its own as an absolute measurement.
2. Nothing here is a before/after for the eager ack. This arm has no
   `eager_slot_ack = false` twin. What the run establishes is where the
   period goes, not what the eager ack bought.

## Open, and flagged rather than explained away

`collect` is **8.8 ms/frame of xrdp's own CPU** — a third of the frame
period spent popping NALs and rewriting LTR references for one monitor.
The in-source comment beside that bracket cites an offline bench of
**1.75 ms/pair**, i.e. 5× less. The offline figure states no resolution
and this run is 9.22 Mpx at 3.5 MB/frame, so the gap may be entirely
size, but it is **not verified** and is recorded here as an open
discrepancy, not as an explained one.
