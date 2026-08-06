# #87 — the emit split was measuring its own logger

2026-08-06.

Records in this directory are kept verbatim. Supersede with a dated
note; do not tidy. The one exception, which this file exercises, is
CLAUDE.md's instrument-on-the-measured-path rule: a number produced by
an experiment that was measuring itself is **deleted**, not superseded,
and every item that closed on it is reopened.

---

## The answer first

**PRD FR-ACK-2 forbade shipping the eager slot-release ack until the
"emit split" shipped, and the whole of that prohibition rested on one
number: the assembly stage costing 6.39 ms per frame at 2560×1440,
27 % of a 24.02 ms chain of serial work. Re-derived from the raw
performance rings on the current build, at that same 2560×1440 the
stage costs 0.105 ms — 61× less. At 3840×2400, where each frame
carries 2.5× the bytes, it costs 0.333 ms, which is 1.3 %–1.9 % of the
frame period on every unshaped run in the tree.**

A stage worth a quarter of the encoder's cycle is a shipping
prerequisite. A stage worth a third of a millisecond is a tuning
question. The prohibition is struck; the two changes are independent
from here on.

**The old number is deleted rather than corrected, because the
experiment that produced it had a logger inside the interval it was
timing.** Two `LOG()` calls — each one a timestamp format, a global
mutex, and an *unbuffered* `write()` syscall — ran between the stage's
start and end stamps on every frame of the run that produced 6.39 ms.
Both are verified below by commit and line number.

---

## 1. What was claimed, and where it sat

PRD FR-ACK-2 (text as it stood before today's retirement block) said:

> **The eager slot-release ack (BACKLOG #70) MUST NOT be shipped
> without the assembly (`emit`) split of BACKLOG #70B.**

and justified it with a decomposition of one 32.56 ms encoder-worker
cycle at 2560×1440:

```
pump           10.78 ms   waiting for the two ffmpeg children
pump_end -> coll_beg 2.60
coll                 3.22   pop the encoded packets, rewrite LTR refs
coll_end -> emit_beg 2.25
emit                 5.96   <-- assembly: pure CPU, touches no child
emit_end -> drain    4.04
drain + subm         3.70
               -------
               21.77 ms   children have NOTHING, and a frame is queued
```

from which came "**the children are idle 67 % of wall time**", the
acceptance projection "**period 22.6–26.6 ms from 32.56 ms — 1.22×–
1.44×**", and the sizing argument "`emit` (5.96 ms) fits entirely
inside `submit(N+1) + pump(N+1)` (14.5 ms), so the join is not
expected to block the worker". The 6.39 ms / 27 % / 24.02 ms form of
the same number is the one the acceptance clause quotes.

The run behind it is **arm-w**, committed as `8bd8647c`
(*test(#74): emit is 5.96 ms, and the worker cycle closes to 0.00 ms*,
2026-08-01 01:24): one monitor at 2560×1440, `codeflood` payload, 45 s,
1290 frames, eager slot ack on, assembly running **inline on the
encoder worker** (the split itself had not been written yet).

---

## 2. What the `emit` stage actually is

Not "assembly" as an abstraction — this is the code that runs between
the two stamps. The bracket is three lines of
`xrdp/xrdp_encoder.c` (`gfx_emit_run_set`, line 3643; the bracket at
lines 3658–3660, at HEAD):

```c
PERF_TRACE("emit_beg", pf_id, set_mon[index]);
self->process_enc(self, set[index]);
PERF_TRACE("emit_end", pf_id, set_mon[index]);
```

`process_enc` is `process_enc_egfx` (`xrdp/xrdp_encoder.c:3437`). For
one AVC444 frame it does, in order:

1. **Walk the EGFX command batch** that xorgxrdp handed over — for a
   normal frame exactly three commands: STARTFRAME, WIRETOSURFACE_1,
   ENDFRAME.
2. **Build the two picture PDUs** (`gfx_wiretosurface1_avc444`,
   `xrdp/xrdp_encoder.c:1984`). It parses the damage rectangles, takes
   the *already encoded* H.264 pair out of the worker's snapshot — it
   opens no ffmpeg child, reads no capture memory — and then, once for
   the luma view and once for the chroma view, writes an
   `RFX_AVC444_BITMAP_STREAM` header followed by that view's H.264
   bytes into a scratch buffer
   (`out_RFX_AVC444_BITMAP_STREAM_view`), and wraps the result in an
   EGFX `WIRE_TO_SURFACE_1` PDU (`xrdp_egfx_wire_to_surface1`).
3. **Hand each finished PDU to the xrdp main thread** — one push onto
   `fifo_processed` under the encoder mutex plus an 8-byte wake, per
   PDU (`gfx_send_done`).
4. **Emit the terminal acknowledgement** for the frame
   (`gfx_close_egfx_msg`).

There is no compression on this path (`xrdp_egfx_bulk` is a struct
holding one int), no syscall on the data path, no encoder, no network
write. In one sentence: **the stage copies this frame's already-encoded
bytes into wire-shaped buffers and queues them.** That is why it is
separable from the ffmpeg children, and it is also why it was always
implausible that it should cost 6 ms.

**Why it was nevertheless believed to be expensive:** because it had
been *measured* at 5.96 ms, and the commit that measured it reasoned
backwards from the number to a mechanism — `8bd8647c`'s own message
proposes "~2 MB of allocate-and-copy at ~300 MB/s ... pointing at the
per-frame ~900 KB `g_new`/`free` faulting in fresh mmap'd pages", and
labels that a hypothesis rather than a measurement. That hypothesis is
now refuted from the other end: the allocation is unchanged
(`d_rects = g_new0(...)` and `s->data = g_new(char, s->size)` are the
same two calls at `8bd8647c` and at HEAD), and
`out_RFX_AVC444_BITMAP_STREAM_view` and `gfx_send_done` are
**byte-identical** between the two commits — yet the same code on the
same class of payload now measures 0.105 ms.

---

## 3. How the numbers below were re-derived

The raw material is the performance ring
(`common/perf_trace.h`, PRD FR-TRACE-1). Each armed xrdp process writes
one file, `/var/log/xrdp-perf/enc.<pid>`, whose first line is a clock
base (`# perfbase mono_ns <M> real_ns <R>`) and whose every subsequent
line is one event:

```
<monotonic_ns> <thread_id> <tag> <a> <b> <c> <d> <e> <f>
```

For the two tags of interest, `<a>` is the frame's own id as peeked out
of the batch and `<b>` is the monitor index.

**Pairing.** `emit_beg` is paired with the next `emit_end` **on the
same thread**, and the pair is discarded unless `<a>` and `<b>` match
on both events — i.e. paired by echoed identity, never by a time
window (CLAUDE.md quality gate 2c). Across every file read for this
record: **0 identity mismatches, 0 unclosed brackets, 0 dropped
records** (`perfdrop`/`perfnoring` absent everywhere).

**Which ring belongs to which run — this is a trap, and it was
walked into once before writing the table.** `e_gate_run.sh` collects
the four most recently modified ring files from the pod
(`ls -t … | head -4`), and a ring covers the whole life of its xrdp
process, so a capture directory contains rings from *earlier runs on
the same pod as well*. Several of those files are byte-identical
across capture directories — for example
`i78_x014_fif2_clocks_20260802/perf/enc.340` has the same MD5 as
`i75_x014_rewrite_20260801/perf/enc.340`, and
`i79_x017_ackdelay_20260802_s20/leg_d0/perf/enc.1449` reappears in
`leg_d10`, `leg_d20` and `leg_d40`. Pooling every file under
`captures/**/perf/` therefore counts the same 3075 frames up to four
times: it yields 49 534 pairs where the honest count is 21 922.

The rule applied instead, fixed before any number was looked at:

* one ring per run directory — the `enc.<pid>` whose `# perfbase` line
  carries the **latest** `real_ns`, i.e. the xrdp process that started
  most recently, which is the session process forked for that run;
* its records then windowed to the run itself, using the first and last
  wall-clock stamp in that capture's own `gfx_trace.txt` (±5 s), mapped
  onto the ring's monotonic clock through the `perfbase` pair.

This reproduces, independently, the two figures the parent agent
reported: `i75_x014_rewrite_20260801` n = 3075, median 0.342 ms,
p99 0.558, max 2.284; `i78_x017_pumpsplit_20260802` n = 2515,
median 0.333.

**Frame period**, quoted alongside so the stage's share is computable,
is the encoder worker's own cycle: the interval from one `wait_beg`
(the worker asking the queue for a frame) to the next, on the thread
that brackets `drain`, with the first cycle of each run dropped because
it contains the wait for the session's first frame.

---

## 4. Every ring capture in the tree

Thirteen capture directories hold rings, spread over 25 run
directories (some captures are multi-leg). All of them.

`n` counts `emit_beg`→`emit_end` pairs; there is one pair per monitor
per frame and every run here is single-monitor, so n is also the frame
count. Durations are milliseconds of wall clock for one execution of
the stage. `period` is the worker-cycle median in ms. `share` is the
stage median as a percentage of the period median.

### 3840×2400 (9.22 Mpx), one monitor, `textflood` payload, oracle client

| run directory | arm and what it is | n | p50 | mean | p90 | p99 | max | period | share |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `i61e_x013_eager_m1_4k_20260801` | x013: eager ack + emit split, first ring build, loopback | 2227 | 0.360 | 0.393 | 0.475 | 1.599 | 2.158 | 25.065 | 1.44 % |
| `i61e_x013_verify_20260801` | x013 again, repeat of the above | 2228 | 0.357 | 0.366 | 0.448 | 0.619 | 2.935 | 25.118 | 1.42 % |
| `i75_x014_rewrite_20260801` | x014: x013's config on the #75 deb (LTR rewrite copies the payload) | 3075 | 0.342 | 0.341 | 0.428 | 0.558 | 2.284 | 17.962 | 1.90 % |
| `i76_x015_fif1_20260802` | x015: x014 with one frame in flight instead of two | 2015 | 0.358 | 0.362 | 0.448 | 0.562 | 1.784 | 28.054 | 1.28 % |
| `i78_x014_fif2_clocks_20260802` | x014 again, with cross-host clock stamps added | 3143 | 0.342 | 0.344 | 0.438 | 0.558 | 2.779 | 17.800 | 1.92 % |
| `i78_x017_pumpsplit_20260802` | x017: x014 plus the pump split | 2515 | 0.333 | 0.333 | 0.425 | 0.572 | 2.327 | 17.954 | 1.86 % |
| `i79_…_s20/leg_direct` | x017, acks passed straight through (control) | 790 | 0.298 | 0.326 | 0.412 | 1.240 | 2.544 | 16.943 | 1.76 % |
| `i79_…_s20/leg_d0` | x017 through the ack-delay proxy, 0 ms added | 751 | 0.268 | 0.304 | 0.384 | 1.281 | 2.440 | 17.038 | 1.57 % |
| `i79_…_s20/leg_d10` | x017, proxy adding 10 ms to each ack | 600 | 0.265 | 0.304 | 0.373 | 1.398 | 1.808 | 17.653 | 1.50 % |
| `i79_…_s20/leg_d20` | x017, proxy adding 20 ms | 507 | 0.261 | 0.289 | 0.349 | 1.244 | 1.851 | 18.155 | 1.44 % |
| `i79_…_s20/leg_d40` | x017, proxy adding 40 ms | 416 | 0.283 | 0.316 | 0.430 | 1.249 | 1.478 | 18.400 | 1.54 % |
| `i79_…_s5/leg_direct` | 5 s version of the control leg | 88 | 0.344 | 0.383 | 0.476 | 1.750 | 1.750 | 16.770 | 2.05 % |
| `i79_…_s5/leg_d0` | 5 s version of the 0 ms proxy leg | 88 | 0.268 | 0.336 | 0.414 | 1.537 | 1.537 | 16.997 | 1.58 % |
| `i80_wanpair_…/leg_lan` | x018: the #80 credit frontier build, no network shaping | 913 | 0.286 | 0.309 | 0.397 | 0.611 | 1.602 | 17.241 | 1.66 % |
| `i80_wan40_fixedlimit_…/leg_wan` | x019: same build behind 40 ms of simulated round trip | 191 | 0.253 | 0.322 | 0.433 | 1.313 | 1.570 | 85.618 | 0.30 % |
| `i98_tier0_ab_…/leg_cubic` | x019, 40 ms, Cubic congestion control | 191 | 0.282 | 0.460 | 1.137 | 1.408 | 1.818 | 86.176 | 0.33 % |
| `i98_tier0_ab_…/leg_bbr` | x019, 40 ms, BBR congestion control | 608 | 0.254 | 0.289 | 0.374 | 1.240 | 1.659 | 25.341 | 1.00 % |
| `i98_bwlimit_…/leg_bbr400` | x019, 40 ms + 400 Mbit/s declared ceiling | 213 | 0.268 | 0.359 | 0.592 | 1.333 | 1.469 | 76.507 | 0.35 % |
| `i98_bwlimit_…/leg_bbr200` | x019, 40 ms + 200 Mbit/s | 108 | 0.296 | 0.372 | 0.545 | 1.759 | 2.054 | 154.738 | 0.19 % |
| `i98_bwlimit_…/leg_bbr100` | x019, 40 ms + 100 Mbit/s | 55 | 0.323 | 0.361 | 0.455 | 1.518 | 1.518 | 309.425 | 0.10 % |
| `i98_bwlimit_…/leg_cubic200` | x019, 40 ms + 200 Mbit/s, Cubic | 104 | 0.242 | 0.286 | 0.362 | 1.185 | 1.203 | 159.577 | 0.15 % |

### 2560×1440 (3.69 Mpx) and 1920×1080 (2.07 Mpx), one monitor, `textflood`

Both from `i98_bwlimit_res_20260806_004607_s20` — arm x019, 40 ms
simulated round trip plus a declared bandwidth ceiling, 20 s per leg.

| run directory | ceiling | n | p50 | mean | p90 | p99 | max | period | share |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|
| `leg_r1440_200` (2560×1440) | 200 Mbit/s | 260 | 0.107 | 0.111 | 0.134 | 0.219 | 0.566 | 62.576 | 0.17 % |
| `leg_r1440_100` (2560×1440) | 100 Mbit/s | 137 | 0.104 | 0.112 | 0.128 | 0.322 | 0.780 | 120.255 | 0.09 % |
| `leg_r1080_200` (1920×1080) | 200 Mbit/s | 471 | 0.063 | 0.066 | 0.080 | 0.120 | 0.331 | 34.243 | 0.19 % |
| `leg_r1080_100` (1920×1080) | 100 Mbit/s | 228 | 0.062 | 0.064 | 0.078 | 0.100 | 0.482 | 67.795 | 0.09 % |

### Totals, grouped by geometry

Pooling across network conditions is legitimate for this stage and
illegitimate for the period: the stage is CPU work that never touches
the network, whereas on every shaped leg the period is set by the link
rather than by the pipeline. **So read the stage columns across the
whole group; do not read the `share` column of a shaped leg as a
statement about the pipeline.**

| geometry | runs | n | p50 | mean | p90 | p99 | max |
|---|---:|---:|---:|---:|---:|---:|---:|
| 3840×2400 | 21 | 20 826 | 0.333 | 0.344 | 0.437 | 0.922 | 2.935 |
| 2560×1440 | 2 | 397 | 0.105 | 0.112 | 0.133 | 0.322 | 0.780 |
| 1920×1080 | 2 | 699 | 0.063 | 0.065 | 0.080 | 0.105 | 0.482 |

**Share of the frame period, from the runs where the period is the
pipeline's own** — 3840×2400 with no network shaping of any kind (the
first six rows of the table plus `i80_wanpair/leg_lan`): **1.28 % –
1.92 %**, over 7 runs and 16 116 frames. The single largest excursion
anywhere in the 21 922 pairs is 2.935 ms, which is one frame in a run
of 2228.

---

## 5. The stage is proportional to the frame's own bytes

Dividing each group's stage median by that group's median wire bytes
per frame (summed from the `GFX_TRACE send` records of the same
capture, grouped by the server's frame counter):

| geometry | wire bytes per frame (median) | stage p50 | stage per MB of payload |
|---|---:|---:|---:|
| 3840×2400 | 3 473 012 B (3.31 MB) | 0.333 ms | 0.101 ms/MB |
| 2560×1440 | 1 410 632 B (1.35 MB) | 0.105 ms | 0.078 ms/MB |
| 1920×1080 | 801 062 B (0.76 MB) | 0.063 ms | 0.083 ms/MB |

0.078–0.101 ms per megabyte across a 4.3× range of payload. A stage
whose cost is that close to linear in the bytes it copies is a stage
that is doing exactly what section 2 says it does, and nothing else.

**This falsifies a claim the PRD used to bound the split's value.** The
retirement note's predecessor argued that "`pump` and `coll` scale with
pixel count and `emit` does not, so at 3840×2400 `emit` is 17 % of a
35.04 ms chain and the most the split could buy is smaller". The stage
*does* scale — with the payload, which itself scales with pixels. What
is wrong with the old sentence is not the direction, it is the
premise: it was reasoning about the scaling of a number that was
mostly logger.

It also gives an independent check on the retired figure. arm-w's
codeflood frames at 2560×1440 measured ~984 KB (BACKLOG note in
`bc897464`). At 0.078–0.101 ms/MB the stage should have cost
**0.075–0.097 ms**. It was reported at **5.96 ms** — a factor of
roughly 70.

---

## 6. Where the 6.39 ms actually came from

Two mechanisms were checked. The first is verified; the second is
named because it cannot be excluded, and the record should not pretend
a choice was made between them.

### VERIFIED: two unbuffered log writes were inside the timed interval

Read directly out of commit `8bd8647c` — the commit that reported
5.96 ms — not from memory or from the conversation:

* `xrdp/xrdp_encoder.c:1993`, inside `gfx_wiretosurface1_avc444` (which
  begins at line 1898): `gfx_trace_rects("avc dmg", …)`, whose body at
  line 869 is `LOG(LOG_LEVEL_INFO, "GFX_TRACE %s surface=%d …")`.
* `xrdp/xrdp_encoder.c:2126`, same function:
  `LOG(LOG_LEVEL_INFO, "GFX_TRACE enc submitted_seq=%llu …")`.
* Both are gated by `gfx_enc_trace_on()` (line 812), which reads the
  environment variable `XRDP_GFX_TRACE`. arm-w set it:
  `PR-demo/mac_bisect_matrix/k8s/arm-w.yaml`, lines 51–52 at that
  commit, `XRDP_GFX_TRACE: "1"`.
* `gfx_wiretosurface1_avc444` is reached from `process_enc_egfx`, which
  is the entirety of what runs between `PERF_TRACE("emit_beg")` and
  `PERF_TRACE("emit_end")` (lines 3473 and 3475 at that commit).

So on every frame of arm-w, the interval reported as the assembly stage
contained two calls into `common/log.c`, each of which formats a
timestamp, takes a **process-global mutex**, and performs an
**unbuffered `write()`** — while other threads were contending for the
same mutex with more of them. (#61h counted the per-frame trace at
~12 log.c lines per frame, nine of them on the xrdp main thread; that
count is from the #61h build rather than from arm-w, so treat it as
the order of magnitude of the contention, not as arm-w's exact
number.)

**Magnitude.** The cost of one *contended* log.c line on this pipeline
was measured during #61e at **~2.5 ms** — the `book` bracket, which
contains three integer increments and one log write, and nothing else
(quoted in CLAUDE.md's strict-honesty section; the report it came from
was itself deleted by #61h, and the figure survives only as that
quotation). Two such writes account for ~5.0 ms of the 5.96 ms
reported, against a payload-derived expectation of ~0.09 ms for the
real work. **I did not re-measure the 2.5 ms myself**; the presence of
the two writes is verified, their magnitude is quoted.

The same commit's own reasoning is worth preserving as the lesson.
`arm-w.yaml`'s header argues that the trace sink "cannot plausibly
perturb the stage it measures, which is the whole reason it is not
`LOG()` (~350–450 ns/line, and a `write()` syscall under a global
mutex)" — a correct argument about the *new* sink, written directly
above an arm that armed two `LOG()` lines inside the bracket.

### NOT the flockfile defect, in this particular run

It is tempting to reach for #61h's headline mechanism — the sink was
`fprintf` onto one shared `FILE*`, `fprintf` takes `flockfile`, so
events serialise across threads and the wait lands inside whichever
bracket is open. `common/perf_trace.c` at `8bd8647c` is indeed that
`fprintf` sink. But at that commit `PERF_TRACE` appears in exactly one
file (`xrdp/xrdp_encoder.c`, 10 call sites), all on the encoder worker,
so there was only ever **one** thread on the stream, and the cross-
thread contention was not yet present. Commit `211db78f` says the same
thing from the other side: "while exactly one thread wrote to it, this
was invisible", and its one-thread arm (x003) reports `emit` at
**5.89 ms** — reproducing arm-w's 5.96 ms without any second writer.
The endpoints were on a defective instrument; what inflated *this*
number was the pair of log writes between them.

### NOT EXCLUDED: wall-clock preemption on a saturated box

`emit_beg`→`emit_end` is wall clock, not CPU time. arm-w ran the
`codeflood` payload, which BACKLOG #61c later found to be
producer-bound with the session's Xorg at **96.4 % of one core**. A
worker preempted mid-stage inflates a wall-clock bracket with no
instrument involved at all. Nothing in the surviving evidence
distinguishes how much of the 5.96 ms was log writes and how much was
scheduling. **Both mechanisms are sufficient to void the number, and
this record does not claim to have chosen between them.**

---

## 7. The caveat that bounds what may be claimed

**Every capture in this tree with a ring runs with the split ON.**
Checked across all 25 run directories of section 4 — each one archives
the `gfx.toml` the arm was actually running — `emit_thread = true`
appears in every one, with no exceptions. So what section 4 measures is
**the cost of the stage
with the split already applied** — the time the dedicated assembler
thread spends on it — and not the time the worker would spend doing the
same work inline.

**Nothing on a ring build measures it inline.** The one measurement of
the inline configuration that exists anywhere is arm-w's, which is the
number being deleted.

What follows, and what does not:

* The ceiling on what the split can **remove** from the worker's serial
  chain is the stage cost: **~0.33 ms at 3840×2400, ~0.11 ms at
  2560×1440.** Moving work to another thread cannot save more than the
  work costs.
* That ceiling holds only if running the stage inline costs about the
  same as running it on the assembler. The code makes this likely by
  construction rather than by measurement: `gfx_emit_run_set`
  (`xrdp/xrdp_encoder.c:3643`) is deliberately the single body called
  both ways — "called on the assembler thread when the split is on, and
  inline on the worker when it is off, so the two configurations
  execute exactly the same code" — and with batching on, the AVC444
  path takes the same worker-snapshot branch either way. **This is
  reasoning from the source, not a measurement.**
* **~0.33 ms is a ceiling and never a measured gain.** The side-by-side
  that would produce a gain — one arm with `emit_thread = false`, one
  with it true, same build, same payload, ring-traced — has never been
  run, and is what BACKLOG #87 exists to do.

---

## 8. What this voids

Deleted, not superseded, under the instrument-on-the-measured-path
rule. All of it comes from arm-w and its siblings, all of which carried
the two in-bracket log writes:

1. **The per-cycle stage table** quoted in section 1 in its entirety —
   not merely its `emit` row. `pump`, `coll`, `subm`, `drain` and every
   inter-stage gap in that table came from the same brackets on the
   same build, and this inflation is known to hit the CPU-side stages
   hardest — it is the same effect that made a bracket containing three
   integer increments and one log write, and nothing else, measure
   2.5 ms.
2. **"The children are idle 67 % of wall time"**, which is arithmetic
   over that table.
3. **`emit` 6.39 ms = 27 % of a 24.02 ms serial chain**, and the
   acceptance projection derived from it: **projected period
   22.6–26.6 ms from 32.56 ms, 1.22×–1.44×**. (The PRD had already
   withdrawn the projection on a different ground — that the gain is
   resolution-dependent. It is now withdrawn for a stronger one: the
   quantity it scaled was not the stage.)
4. **The sizing argument for the join point** — "`emit` (5.96 ms) fits
   entirely inside `submit(N+1) + pump(N+1)` (14.5 ms), so the join is
   not expected to block the worker". The conclusion is now more
   comfortably true than before, but its arithmetic is void and must
   not be re-quoted.
5. **The cost argument against a thread-per-frame assembler** — "clone
   + stack + first-touch is tens of µs against a 5.96 ms body". The
   direction survives (tens of µs against ~330 µs is still a bad
   trade), the number does not.
6. **The claim that `emit` does not scale with pixel count**, which is
   additionally falsified by measurement in section 5, not merely
   voided.

### What is NOT voided

* **The correctness contract of the split**, in full: the join must
  happen before `collect(N+1)` and not before `submit(N+1)`; the
  use-after-free that a later join would create (`collect_pair` calls
  `grow()`, which **reallocs**, so a later join can free the buffer the
  assembler is reading); exactly one permanent assembler thread, for
  PDU order and for the resident-frame bound. These are arguments from
  the source, and no timing enters them.
* **The frame-budget argument** that the split costs no extra frame of
  latency — also structural.
* **Everything in the deleted table's *shape* that is an ordering or a
  count** rather than a duration, consistent with #61h's own rule.

---

## 9. What reopens

1. **Whether the emit split is worth shipping at all.** Its ceiling is
   1.3 %–1.9 % of the frame period at 4K. It must now justify itself
   against that ceiling, on its own evidence, and not as a prerequisite
   of anything. BACKLOG #87.
2. **The eager slot-release ack's own ratio.** #70's 1.11× and #70B's
   0.96×/1.12× were all measured on log.c-instrumented builds under a
   payload (`codeflood`) later shown to be producer-bound. All three
   need re-running on the ring build under `textflood`, with the
   producer margin printed beside every number. BACKLOG #87.
3. **The "children are idle 67 %" question itself**, which is the real
   content of the deleted table: how much of the encoder worker's cycle
   are the ffmpeg children idle with work available? Whatever the
   answer is on the ring build, `emit` is not where it went — 0.333 ms
   of a 17.9 ms cycle cannot be it.
4. **Where the remaining serial time actually is.** The retired table
   attributed 5.96 ms of a 32.56 ms cycle to a stage that costs a third
   of a millisecond. That time was somewhere. On the current ring-traced
   build the period at 3840×2400 is 17.8–18.0 ms (arms x014 and x017 in
   section 4) and is dominated by waiting for the two ffmpeg children,
   with the LTR reference rewrite a distant second since #75 cut it from
   8.802 to 1.362 ms per frame — see
   `61e-the-period-is-encode-and-rewrite.md` and
   `75-the-rewrite-was-re-serialising-the-picture.md`. No re-attribution
   of the 2560×1440 codeflood cycle is claimed here; this record only
   removes a wrong one.
5. **The inline measurement that does not exist.** Until an
   `emit_thread = false` arm is run on a ring build, "the split removes
   X ms from the worker" has no measured value at any resolution — only
   the ceiling in section 7.

---

## 10. Method notes worth keeping

* **Pooling `captures/**/perf/enc.*` double- and quadruple-counts
  runs.** The gate copies four rings per capture and a ring outlives
  its run; several files are byte-identical across directories. The
  naive pool gives 49 534 pairs for 21 922 real ones. Select one ring
  per run (latest `perfbase real_ns`) and window it to that run's own
  `gfx_trace.txt` span.
* **Never read the `share` column of a bandwidth-shaped leg as a
  pipeline statement.** On the 100 Mbit/s 4K leg the stage is 0.10 % of
  the period — because the period is 309 ms of waiting for the network,
  not because the stage got faster.
* **A stage whose cost is linear in the bytes it copies is a stage you
  understand.** The ms/MB column in section 5 is what turned "0.333 ms
  is a suspiciously small number" into "0.333 ms is the number this
  code should produce", and it is what let arm-w's figure be checked
  independently of any archaeology.
