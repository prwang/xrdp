# #61h — the logger was inside every measurement, and what that voids

2026-08-01. This file exists because a whole campaign's timing numbers
were retired at once, and a reader who finds those numbers quoted
somewhere needs to know why they are gone.

## What was wrong

`GFX_TRACE` and `ACK_TRACE` records were `LOG(LOG_LEVEL_INFO, …)` calls.
log.c formats a timestamp, takes a **global mutex** and does an
**unbuffered `write()` per line**. There were ~12 per frame, nine of them
on the xrdp main thread — the same thread whose service latency #61f
exists to cut, and the thread that also writes every frame to the client.

So on every arm that ran with `XRDP_GFX_TRACE=1` / `XRDP_ACK_TRACE=1`:

* the frame period being measured included the cost of measuring it;
* the "17 ms egress send window", the interval that the #61f work was
  designed against, is the interval **between two of those log lines**;
* the #61e conclusion that the tracer's cost is "bounded below the ~1 ms
  same-build run noise" was about the **perf ring** only. Its controls
  (armed vs disarmed ring, traced vs old build) all had the log.c lines
  in both arms, so the comparison could not see them. The statement was
  true about the thing it compared and irrelevant to the thing that
  mattered.

## What was voided

Twenty-five capture directories — every one whose run carried a
`gfx_trace.txt`, i.e. every fleet run with the trace armed — were deleted
from the working tree on the owner's instruction, together with the
records built on them:

* `61e-period-attribution-and-the-tracer.md` — the period attribution,
  the 0.007 ms closure, the 2.249 ms/cycle worker idle, the tracer
  transparency argument.
* `70B-perf-trace-sink.md` — the emit split's 1.12x under textflood and
  0.96x under codeflood.
* `61f-the-cork-was-the-wrong-lever.md` — the cork A/B numbers (51.2,
  127.3 vs a 41–42 control) and the send-window decomposition.

They are in git history and nowhere else. Nothing in them should be
quoted forward without being measured again.

## What is NOT voided

* **Code.** The emit split, the eager slot ack, FR-TRACE-1's ring, the
  use-after-free its prerequisite refactor fixed, and the #61f cork's
  revert are all in git and unaffected by how they were measured.
* **Correctness results.** The wire audits, the assert gate (A1–A7), the
  black-frame checks and the CI assertions do not depend on timing. They
  were run on the void arms too, but a bitstream property is not made
  wrong by a slow logger.
* **The one architectural finding of #61f**, which came from the ORDER of
  events and not from their durations: capture is ack-clocked, not
  damage-clocked — the xorgxrdp frontier reads `ack = N−2, shown = N−3`
  at essentially every capture. Ordering survives a slow instrument;
  every interval quoted alongside it does not.

## The fix

Per-frame records now go to `common/perf_trace`'s ring (commit
`66a60311`): the source path is a vDSO clock read and a store into the
calling thread's own ring, with a separate sink thread doing the I/O.
The payload widened from two ints to six so a record like a GFX send
(`bytes, last, frame_id, id_server, id_client, fif`) fits in one event
rather than being split and re-joined by a reader.
`PR-demo/mac_bisect_matrix/perf_trace_lines.py` renders the ring back
into the line shapes the existing analyses read.

Arming a trace knob without `XRDP_PERF_TRACE` now warns once and
disarms, so a run that records nothing cannot be mistaken for a session
with no damage.

CLAUDE.md coding rule 5 makes the ring mandatory for anything at frame
rate, and forbids building a parallel tracer or sampler beside it.

## The one observation this file does rest on

A 60 s run of the fixed build (arm x011, since garbage-collected)
produced **zero** `GFX_TRACE` lines in the pod's `xrdp.log`, with the
records present in the ring instead. That is the acceptance criterion —
the per-frame path no longer touches log.c.

**No overhead figure is claimed here.** That run also read 25.4 ms/frame
against ~41 ms on the previous arm, but the two are different builds
measured on different days, on a box whose own control drifted 36.8 →
41.8 ms within one day. Quoting a difference from that pair would repeat
the mistake this file is about. The rebaseline, when someone needs
numbers again, starts from a control and a treatment in one pass.

**Superseded on this point, 2026-08-01 (same day).** The figure the
paragraph above declines to claim now exists, measured directly instead
of inferred from a before/after: the armed ring costs a producer thread
~7 µs per frame, 0.02 % of a frame period, worst observed frame 0.37 %.
See `61h-what-the-ring-costs.md`. Nothing else in this file changes —
the reasoning for refusing the 25.4-vs-41 comparison was correct, and
the right answer was to measure the ring on its own rather than to
subtract two whole-system runs.
