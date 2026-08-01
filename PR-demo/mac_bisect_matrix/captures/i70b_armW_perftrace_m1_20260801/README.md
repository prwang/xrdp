# BACKLOG #70B — the worker cycle, decomposed

arm-w = arm-v's encoder configuration on an xrdp deb carrying
`common/perf_trace.{c,h}` and the worker-stage brackets
(`e6e1f6f5641e`, xorgxrdp `10fa3aa23033` — the SAME xorgxrdp as
arm-u/arm-v). m=1, **2560x1440 = 3.69 Mpx**, `SESSION_KIND=codeflood`,
oracle client, 45 s, AMD VAAPI (`h264_vaapi`, CQP 20).

Analyzers: `../../i70b_stage_split.py <dir>` (the sink) and
`../../i70_ack_overlap.py <dir>` (the ACK_TRACE cross-check).

## The instrument did not move the thing it measures

arm-w reproduces arm-v on every metric arm-v was read on, which is the
mechanism check (gate 2) for the sink itself:

| | arm-v (#70) | arm-w (#70B) |
|---|---|---|
| period | 33.0 ms | 32.5 ms |
| encode(N+1) ‖ tail(N) | 8.5 ms | 8.6 ms |
| worker absorb→1st send | 7.0 ms | 6.9 ms |
| main 1st send→egress | 12.7 ms | 12.6 ms |
| worker gap absorb(N)→submit(N+1) | 12.5 ms | 12.3 ms |
| egress(N−1) binds the ack | 1 % | 2 % |

At ~100 ns/event and 10 events/frame the sink costs ~1 µs of a 32.5 ms
frame (0.003 %). Correctness held: wire audit `--assert` 7/7 PASS,
1226 pictures decoded, **zero black frames**.

## Result — the cycle adds up to itself

Per frame, mean over 1290 frames:

| stage | mean | p50 | p90 | max |
|---|---|---|---|---|
| `drain` fifo pop | 0.00 ms | 0.00 | 0.00 | 0.01 |
| `subm` parse + hand to children | 3.70 ms | 3.53 | 3.74 | 15.68 |
| `pump` wait for both children | **10.78 ms** | 10.82 | 12.76 | 124.72 |
| `coll` NUT pop + LTR rewrite ×2 | 3.22 ms | 3.24 | 5.10 | 8.02 |
| **`emit` build the EGFX PDUs** | **5.96 ms** | **5.49** | 7.33 | 21.51 |
| `pump_end`→`coll_beg` bookkeeping | 2.60 ms | 2.40 | | |
| `coll_end`→`emit_beg` (release_slots) | 2.25 ms | 1.90 | | |
| `emit_end`→`drain_beg` loop gap | 4.04 ms | 0.00 | 15.83 | 39.66 |
| **total** | **32.56 ms** | | | |

against a measured cycle of **32.56 ms** — closure to 0.00 ms (gate 1).
The `gap` has p50 0.00: half the cycles never wait for input at all, and
its mean is a tail.

## The question that prompted this: emit is ~6 ms, not <1 ms

**`emit` = 5.96 ms mean, 5.49 ms median.** It was inferred at "≤ 8.5 ms"
by differencing ACK_TRACE stamps and could have been anywhere in that
range; it is 6.

Cross-checked by a second, independent instrument on the same run: the
wall-clock tail split puts `absorb → first main-thread send` at 6.9 ms
mean / 6.0 p50, and `absorb` falls inside the 2.25 ms `release_slots`
window immediately preceding `emit_beg`. Two instruments, one clock each,
agreeing to within their resolutions.

**`emit` performs no I/O.** `xrdp_egfx_bulk` is `{ int id; }` — there is
no compression on this path, no socket, no syscall on the data path. It
is `g_new` + two passes of copying (~900 KB into the AVC444 metablock
stream, then again into the EGFX PDU stream) plus a mutex'd fifo push and
an 8-byte wake of the main thread. ~2 MB of allocate-and-copy taking
6 ms is ~300 MB/s, an order of magnitude under memcpy, which points at
the per-frame `g_new`/`free` of ~900 KB: a fresh mmap'd allocation faults
in every page it touches. **Not measured — the brackets time `emit` as a
whole, and the internal split needs finer brackets.**

## What this says about the next lever

The worker's serial chain is **28.5 ms of a 32.5 ms cycle (88 %
occupancy)**; the main thread's transport write is 12.6 ms on its own
thread. `pump` — waiting for the two ffmpeg children — is the largest
single stage at 10.78 ms, and `emit` at 5.96 ms is the largest piece of
the chain that is neither waiting for the children nor sending.

Moving `emit` off the encoder worker would cut the chain to ~22.5 ms
(a projected 1.22x-1.44x period, 22.6-26.6 ms; the range is the 4.04 ms
inter-cycle gap, which this change does not determine) **without adding
a frame of latency**: during
`pump(N+1)` the previous frame is already alive on the main thread being
egressed, so an assembler thread does not raise the number of resident
frames. See BACKLOG #70B for the ordering hazard that governs how.

The main thread's 12.6 ms is NOT a term in the table above — the two
threads run concurrently and the table already closes to 0.00 ms without
it. Nor is it orthogonal: it is the NEXT ceiling. At <=39 % occupancy it
is not binding today and does not bind after the emit split either
(~47-56 %); it binds near a 12.6 ms period (~79 fps), once `pump` is
also attacked. Whether those 12.6 ms are the server's send cost or the
oracle client failing to drain 288 Mbit/s decides the FIX at that point,
not when it arrives — and it does NOT gate the emit split.
