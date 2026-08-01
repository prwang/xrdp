<!--
Experiment record moved out of BACKLOG.md on 2026-08-01. BACKLOG.md
keeps the OPEN half of #70B (the emit split, not yet built); this is the
closed half -- how the sink was chosen, and the measurement that
answered "is emit 1 ms or 8 ms".
-->

# #70B — the perf-trace sink, and the worker cycle it decomposed

**Why.** #70's follow-up established, from archived traces alone, that the
encoder worker burns **7.9 ms of CPU per frame** between `absorb(N)` and
`submit(N+1)` (mean; p50 7.6, floor 5.3, and identical on both arms —
8.1 control vs 7.9 eager — so it is real work, not queueing). It could
not say WHICH stage. The offline bench put the LTR rewrite at 1.75 ms per
pair, and the rewrite is on the far side of the `absorb` stamp anyway, so
the 7.9 ms is unexplained.

**Why a new sink rather than LOG().** Measured on the dev box: one
`common/log.c` line costs ~350–450 ns (timestamp format ~68 ns,
vsnprintf ~68 ns, and a mutex'd UNBUFFERED `write()` syscall ~211 ns) and
lands in `/var/log/xrdp` + journald mixed with operator-facing messages.
Owner directive (2026-08-01): performance tracing gets its own sink.

**Why not eBPF/bpftrace.** Investigated and rejected for this box, with
evidence:
- `gfx_wiretosurface1_avc444` and `gfx_batch_run_set` are **inlined** —
  absent from the symbol table — so a uprobe cannot name them. Probing
  by address offset is exactly the instrument that produced the retracted
  step-0 claim (`e41bcb59`); not repeating it.
- USDT markers (`systemtap-sdt-dev`) DO solve the inlining problem: a
  `STAP_PROBE2` compiles to a bare `nopl` plus a `.note.stapsdt` ELF
  note — verified by objdump/readelf. But the consumer cannot run here:
  `bpf()` is blocked by the sandbox's seccomp filter (`Seccomp: 2`,
  "Operation not permitted" loading a trivial program) despite
  `CapEff: 000001ffffffffff`, and `perf` hits `perf_event_paranoid` +
  tracefs permissions from the other side.
- Verdict: markers are free and can be added later if a host permits
  BPF, but the measurement cannot DEPEND on a consumer that will not run
  where the work is done.

**What was built.** `common/perf_trace.{c,h}`: its own file, its own
schema, stdio's buffering rather than a hand-rolled ring buffer —
measured **101.6 ns/event including the clock read**, ~4× cheaper than
LOG() and ~15× more than a raw ring-buffer record (24.8 ns), which is
the price of not writing one. Armed by `XRDP_PERF_TRACE` (a path prefix;
each process appends its pid). Disarmed — the shipped default — is one
branch on a cached flag. Record: `<monotonic_ns> <tid> <tag> <a> <b>`;
CLOCK_MONOTONIC so records from different processes share one timeline,
and the identity goes in `<a>` because frames are joined by echoed id,
never by a time window (#64 / gate 2c).

Brackets added to the worker: `subm_*`, `pump_*`, `coll_*` (the NUT pop
+ LTR rewrite), `emit_*`, `drain_*` — enough to decompose the whole
`absorb → submit` window rather than split it in two.

**Acceptance.** The 7.9 ms is attributed to named stages, and the answer
to "is emit under 1 ms or is it 8 ms" is stated with a distribution, not
an inference from differencing.

**Rung 1 (CI).** `make check` green: common 170 (165 + 5 new schema
tests), xrdp 174, libipm 35, libxrdp 13, memtest 1. The schema
assertions are written from the documented contract in `perf_trace.h`,
NOT read off a run — an analyzer parses these records by position, so
field order and separator are the thing under test.

### Rung 2 (local fleet) RESULT — emit is 5.96 ms, and the cycle closes to 0.00 ms

arm-w (`e6e1f6f5641e` + xorgxrdp `10fa3aa23033`, arm-v's encoder config
byte-identical) — m=1, 2560x1440, codeflood, 45 s, 1290 frames.

**The sink did not perturb what it measures.** arm-w reproduces arm-v on
every metric arm-v was read on: period 32.5 vs 33.0 ms, encode‖tail 8.6
vs 8.5, worker absorb→1st send 6.9 vs 7.0, main 1st send→egress 12.6 vs
12.7, egress-binds 2 % vs 1 %. Wire audit 7/7 PASS, zero black frames.

| stage | mean | p50 |
|---|---|---|
| `drain` | 0.00 ms | 0.00 |
| `subm` | 3.70 ms | 3.53 |
| `pump` (wait for both children) | **10.78 ms** | 10.82 |
| `coll` (NUT pop + LTR rewrite ×2) | 3.22 ms | 3.24 |
| **`emit`** | **5.96 ms** | **5.49** |
| pump_end→coll_beg (bookkeeping) | 2.60 ms | 2.40 |
| coll_end→emit_beg (release_slots) | 2.25 ms | 1.90 |
| emit_end→drain_beg (loop gap) | 4.04 ms | 0.00 |
| **sum** | **32.56 ms** | |

against a measured 32.56 ms cycle — **closure to 0.00 ms** (gate 1).
Cross-checked by the independent wall-clock tail split: absorb→first
main-thread send 6.9 mean / 6.0 p50, with absorb inside the 2.25 ms
window immediately before `emit_beg`.

**ANSWER: emit is ~6 ms, not <1 ms.** It does NO I/O — `xrdp_egfx_bulk`
is `{int id;}`, no compression, no syscall on the data path. It is
`g_new` plus two copy passes over ~900 KB each, a mutex'd fifo push and
an 8-byte wake. ~2 MB of allocate-and-copy at ~300 MB/s is an order
under memcpy, which points at the per-frame ~900 KB `g_new`/`free`
faulting in fresh mmap'd pages — **hypothesis, not measured**; the
internal split needs finer brackets.

**Harness fault found and fixed by its own guard.** The first run was
launched with `MM_MONITORS=1`, but `e_gate_run.sh`'s knob is
`E_MONITORS` — the session came up 2-monitor at 12.9 Mpx. Caught before
any number was computed, by the record counts not lining up (1230 emit
against 654 pump) and `client-monitors.txt` reading "Monitors: 2". That
capture is kept as `captures/VOID_i70b_armW_m2_wrong_geometry_20260801/`
and none of its numbers are quoted: a 12.9 Mpx 2-monitor run is not
comparable to the 3.69 Mpx m=1 series (gate 5).

### Next lever, with the ordering hazard that governs it

The worker's serial chain is **28.5 ms of a 32.5 ms cycle (88 %
occupancy)**. Moving `emit` to its own thread cuts it to ~22.5 ms, a
**1.22x–1.44x** projected period (22.6–26.6 ms from 32.56 ms; the range
is the 4.04 ms inter-cycle gap, which this change does not determine —
quote the range, not its optimistic end), and — the part that matters
for FR-PROC-6 — adds NO
frame of latency: during `pump(N+1)` the previous frame is already alive
on the main thread being egressed, so an assembler thread does not raise
the resident-frame count.

**HAZARD, and it decides the design.** `collect_pair` hands back a pair
whose `main_data`/`aux_data` point into the handle's own
`self->main_buf`/`aux_buf`, which the NEXT collect on that handle
overwrites. So an async emit must be joined BEFORE `collect(N+1)` — not
merely before the next ack — or those two buffers must be double-
buffered. Joining before collect is sufficient AND still wins
everything: `emit(N)` (5.96 ms) hides completely inside
`subm(N+1) + pump(N+1)` (14.5 ms). A bounded depth-1 handoff expresses
the join and needs no ack change at all.

### Why a ready capture does not stop the children starving (FR-ACK-2)

`submit` and `pump` — the only code that feeds the two FFmpeg children —
both run on the encoder worker thread. Input readiness is therefore
necessary but NOT sufficient: any worker-thread time not spent feeding
them is time they idle *with a frame already queued*. #70 made the frame
ready early (`absorb(N) → msgin(N+1)` p50 −1.9 ms; 61 % already in the
fifo) and the wait simply moved in front of the worker
(`msgin → submit` p50 2.4 → 10.3 ms).

Per 32.56 ms cycle: the children have work for the 10.78 ms `pump` and
NOTHING for the other 21.77 ms (2.60 + 3.22 + 2.25 + 5.96 + 4.04 + 3.70)
— **idle 67 % of wall time** behind stages that are not encoding.
`emit` is the largest of those and touches no child, no capture page and
no borrowed shmem (FR-PROC-6), which is what makes it separable. This is
now **PRD FR-ACK-2**: the eager ack may not ship without the split.

### The main thread's 12.6 ms — where it sits in the accounting

**It is NOT a term in the worker accounting.** The stage table above
sums to 32.56 ms against a 32.56 ms cycle with the main thread's 12.6 ms
nowhere in it: the two run on different threads, concurrently. The
worker's cycle is the period; the main thread's send fits inside it.

**Nor is it orthogonal — it is the NEXT ceiling, not the current one.**
Occupancy per frame, against a 32.5 ms period:

| | busy | occupancy |
|---|---|---|
| encoder worker | 28.5 ms | **88 %** |
| main thread (send) | ≤ 12.6 ms | ≤ 39 % |

12.6 ms is an upper bound on main-thread occupancy — it is the span from
the frame's first PDU to its last, so any interleaved event-loop work is
counted against it, not excluded. The main thread therefore has **at
least 61 % slack**, and binds only when the period falls to ~12.6 ms
(~79 fps).

Projected: move `emit` off the worker → chain ~22.5 ms → period
22.6–26.6 ms (37.6–44.3 fps), main occupancy rises to ~47–56 %, still
not binding. Only after
`pump`'s 10.78 ms is also attacked (FR-PROC-7, #40) does the chain
approach 12.6 ms and the transport become the constraint.

**Correction to this item as first written (2026-08-01).** It said the
emit split was a prerequisite on settling the server-vs-client question,
"a 1.4x worker-side win is not realisable if the transport is already
the constraint". That was wrong, and the occupancy arithmetic above is
why: the transport is at ≤39 %, not saturated, so a shorter worker chain
does not merely relocate a queue. **The emit split is not gated on it.**

What the server-vs-client distinction actually changes is the FIX once
12.6 ms does bind — fewer bytes (bitrate/codec) or a faster transport if
it is server cost, versus a client-side problem xrdp cannot fix — and
not WHEN it binds, which is set by the 12.6 ms either way.
