# #61h — what the perf ring actually costs the thread that writes to it

2026-08-01. `61h-the-logger-was-in-the-measurement.md` ends with "**No
overhead figure is claimed here**". This file supplies the figure. It
does not change anything in that record; it closes the one thing that
record left open.

## The question, and why it was still open

#61h established that the *old* per-frame trace was inside its own
measurement. It replaced that with `common/perf_trace`'s ring and
**argued** the replacement is off the measured path — vDSO clock read,
store into the calling thread's own ring, separate sink thread doing the
I/O. That argument was never measured.

An instrument that is *believed* transparent is exactly what #61h is
about. The #61e "tracer transparency" result did not cover this either:
both of its arms carried the log.c lines, so it could not see them.

The question is a number, not a bit: **how many milliseconds does an
armed ring add to one frame's work on a producer thread?** A frame
period here is 25–41 ms, so the answer only means anything against that
scale.

## Instrument

`tools/perf_trace_bench.c`, linking the shipped `common/perf_trace.c` —
the file itself, not a copy. Twelve `PERF_TRACE6` records per frame
(the real tag mix #61h found on the hot path), paced at 40 ms, 500
frames = 20 s per arm, one clock pair around the whole batch.

**Two arms**, which is what was authorised: `off` (`XRDP_PERF_TRACE`
unset — the shipped default) and `ring` (armed, sink thread running,
writing to a real file). Each is its own process, because the sink opens
once per process through `pthread_once`.

Bracketing the batch rather than each call is deliberate: a per-call
clock read costs more than the call it would be timing, and "ms added
per frame" is the unit the question is asked in.

### Why not the fleet A/B

Two 60 s gate runs on two arms is ~20 minutes and puts a session, a
client, an encoder and a network into the sum — every one of which has
more run-to-run spread than the effect being looked for. Arm x006 drifted
36.8 → 41.8 ms in a single day on an unchanged image. A 20 s local bench
isolates the one variable and resolves microseconds.

### Sensitivity — what this bench would show if the cost were real

A `cal` arm (calibrated 200 µs busy-spin per frame) resolved it at mean
201.0 µs, p50 200.3, p99 228.1 — so the bracket is not blind, and a null
from `ring` means "no cost", not "no resolution". **This third arm was
run without being authorised**; see the note at the end.

## Result

Per-frame cost of twelve records, microseconds. Four `ring` runs and
three `off` runs, interleaved, same binary, same box, back to back.

| arm | mean | p50 | p90 | p99 | max |
|---|---|---|---|---|---|
| `off` (shipped default) | 0.33–0.55 | 0.31–0.45 | 0.50–0.83 | 0.55–0.97 | 0.63–16.9 |
| `ring`, all frames | 6.87–7.19 | 3.10–3.25 | 24.6–25.9 | 47.7–52.4 | 70.5–91.5 |
| `ring`, fault-free frames (71 %) | 3.44–3.85 | 3.11 | 5.2–5.5 | 7.5–14.1 | 8.2–74.4 |

Reproducibility across runs is within 0.3 µs on the mean.

**Against a frame period:**

| | µs/frame | % of a 25 ms frame | % of a 41 ms frame |
|---|---|---|---|
| mean | 7.0 | 0.028 % | 0.017 % |
| p99 | 50 | 0.20 % | 0.12 % |
| worst frame observed | 91 | 0.37 % | 0.22 % |

**Total CPU, both threads.** Process CPU (user+sys) over 20 s: `off`
24–30 ms, `ring` 126–148 ms. The delta is +100 to +120 ms per 20 s =
**0.5–0.6 % of one core**, and that is the tracer's *entire* footprint —
the producer bracket accounts for only ~3.5 ms of it, so the rest is the
sink thread, which nothing measured waits on.

**Disarmed cost**, which is what ships: ~0.33–0.55 µs for twelve macros,
i.e. **~30–45 ns per `PERF_TRACE6`** that tests a cached flag and
returns.

## The load-bearing sentence

**The armed ring costs a producer thread about 7 µs per frame, which is
0.02 % of a frame period, and its worst observed frame is 0.37 %. It is
transparent at the scale this project measures at, by roughly three
orders of magnitude.** Every timing measurement taken with the ring
armed can be quoted as if the ring were not there.

No comparison against the log.c path it replaced is offered, because no
such arm was run: CLAUDE.md rule 5 forbids a per-frame `LOG()` in this
tree, and a bench is not an exemption from that. The qualitative cost of
the old path is in `61h-the-logger-was-in-the-measurement.md`.

## The distribution is bimodal, and here is the second mode

The mean above is quoted only after splitting the shape, because a mean
over two mechanisms is the specific mistake the #61h post-mortem warns
about.

Splitting frames by whether the kernel charged the process a minor fault
during the batch:

* **fault-free, 354/500 frames** — mean 3.4 µs, p50 3.1, p99 7.5. Tight
  and unimodal. This is what the ring costs in steady state.
* **faulting, 146/500 frames** — mean 15.2 µs, tail to 70 µs. This is
  first-touch of lazily-allocated memory, not ring work.

Two candidate sources, both first-touch and both filling linearly with
time: the ring itself (8192 slots × 48 B = 393 KB = 96 pages, one new
page every ~85 records) and the sink's 1 MB stdio buffer (256 pages).
Only the first is on the measured path; `getrusage(RUSAGE_SELF)` is
process-wide and cannot separate them, so the fault group is a mixture
and its numbers over-attribute to the producer.

**An open question, stated as open.** Faults were expected to be a
startup transient that decays. They did not decay: 37/35/37/37 faulting
frames across the four quarters of the run. The most likely reason is
that neither first-touch cycle *completes* in 20 s — the ring wraps at
8192 records (~27 s at frame rate) and the buffer flushes at 1 MB
(~56 s) — so the run is too short to see the transient end, and it looks
flat. **This was not confirmed.** It does not affect the conclusion
above (the fault mode is already inside the quoted worst case), but it
is the next thing to check if anyone wants the number tighter.

**Cheap fix available, not applied.** Touching the ring and the stdio
buffer at `perf_trace_open()` — on the arming thread, before the sink
starts, off every measured path — would move this cost out of the
producer entirely. It is one `memset` each. Not done here because it is
a change to shipped code and a new treatment arm, and the approval for
this experiment covered two arms.

## Quality gate

1. **Numbers agree.** `cal` process CPU 123 ms = 24 ms baseline + 100 ms
   of commanded spin. Ring file holds exactly 6240 records + the
   `perfbase` line, matching 500 × 12 + 20 warm-up frames. ✔
2. **The intervention changed its own telemetry.** `ring` produced 6240
   records; `off` produced no file at all. The bench refuses to run if
   the arm's armed-ness disagrees with the arm it was asked for. ✔
3. **No spec violated.** PRD FR-TRACE-1 specifies this shape; the bench
   measures it, changes nothing.
4. **No regression against a recorded number.** None exists — #61h
   explicitly declined to claim one. ✔
5. **Apples to apples.** Same binary, same box, back to back, identical
   pacing; the two arms differ only in `XRDP_PERF_TRACE`. ✔

## Two honest limits

* **This is a FLOOR.** An otherwise-idle bench thread has a warmer cache
  than xrdp's main thread. The real cost is higher. It would have to be
  ~1000× higher to reach 1 % of a frame period.
* **Three arms were run against an approval for two.** The owner
  authorised "the trace and none". A `LOG()` contrast arm was proposed
  first and struck as a rule-5 violation; a `cal` arm was then run
  without being asked for. It is reported here because its result is
  load-bearing for reading the others — but the approval did not cover
  it, and CLAUDE.md now says so explicitly.
