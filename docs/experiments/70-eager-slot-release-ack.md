<!--
Experiment record moved out of BACKLOG.md on 2026-08-01.

BACKLOG.md is the OPEN work list: hypotheses, justification, and a
pointer. This file is the closed record it points at -- the conditions,
the numbers, the anomalies and the retractions, kept verbatim as they
were written at the time. Nothing here is a live task.
-->

## #70 — Eager slot-release ack: ack(N) fires at max(absorb N, egress N−1) (DONE 2026-07-31 — shipped behind a default-off knob, CI 174/174, local A/B **1.11× and encode‖tail 4.8 → 8.5 ms**; step 0 answered NO; the remaining serializer is the worker thread → **#70B**, which PRD FR-ACK-2 now makes a REQUIREMENT of shipping this ack, not a follow-up)

The m=1 pipeline has four stages — capture, ffmpeg, LTR rewrite, net
egress — and the shipped ack releases the next capture only after the
LAST of them (`mod_frame_ack` rides the frame's last=1 enc_done, ~3 ms
after the final transport write). Every stage serializes behind every
other. Measured basis (T4 redo 2026-07-31, commit `0db74f6e`, captures
`i55_t4_cond{A,B}*`; legs sum = period, 0.0 ms unattributed):

| leg | ms (pinned / 8 vCPU) |
|---|---|
| capture + pack (Xorg) | 8.7 / 8.4 |
| handoff + NVENC encode | 24.1 / 26.5 |
| worker LTR rewrite (~3.5 MB, zero pump polls: working, not waiting) | 35.8 / 32.8 |
| EGFX assembly | 9.1 / 9.2 |
| main-thread egress drain (ack emitted only after the last write) | 30.4 / 27.4 |
| ack transit + deferred timer | 5.4 / 5.3 |
| **period** | **113.6 / 109.5** |

**The change.** Emit the module ack for capture N when BOTH hold:

* **absorb(N)** — the encoder children have fully drained N's
  vmsplice'd input. The pages are BORROWED capture shmem (FR-PROC-6,
  no copy, no GIFT), so the slot is lossy until the pipe drains;
  collect return is the already-proven marker (the collect path
  hard-fails if input is not fully spliced), a mid-encode FIONREAD==0
  check is the earliest admissible one.
* **egress(N−1)** — the previous frame's last EGFX write has been
  handed to the transport.

The xup ack stays the ONLY flow-control token: no new queues, no new
windows, frames past capture ≤ 2 forever — the existing FR-CAPTURE-8
ring finally used at its designed depth. Condition (b) is what makes
it BACKPRESSURE: absorb-only acking would let the tail queue grow at
damage rate against a ~45 ms/frame worker (bufferbloat, the exact
shape the PRD forbids). fif stays the outer network gate; a slow
client pushes the last write later, which pushes the ack later.

**Pre-registered prediction** (named before any run): period →
(capture+absorb + tail)/2 ≈ **55–57 ms (~18 fps)** at m=1 4K textflood
on the T4, vs 113.6 today; resource floor ≈ 45 ms (worker
rewrite+assembly), which then becomes the next lever (FR-PROC-7 /
#40-#41 territory). Latency: off saturation nothing changes (no queue
forms); at saturation per-frame in-pipe time may grow by up to one
rewrite leg while event-to-glass IMPROVES (sampling delay collapses
113→~40 ms) — the +2-frames regime the PRD forbade is the K≥2 shape,
which the depth-2 token bound excludes by construction.

**Step 0 — off-by-one audit (prerequisite).** The July histogram (340
captures @ outstanding=1, 1004 refusals @ 2, 0 @ 0 — raw-offset
uprobe, UN-AUDITED) implies the ack VALUE chain trails the newest
capture by one, which wastes a slot under ANY emission policy. Add the
identity-carrying log line, cross-check the offsets, and fix or
explain the off-by-one first — otherwise #70's depth-2 cannot
materialize and the prediction is void.

**Correctness machinery (revived from `wip/fr_ack_1_checkpoint`).**
The early ack decouples slot-release from content disposition, so
every post-consume failure — pair timeout/child recreate, rewrite
failure, oversize skip, teardown, the checkpoint's known
alloc-failure corner — MUST emit NOT_DISPLAYED + region-return or a
stale rectangle survives on screen. Echoed identity, ack totality
(single exhaustive exit), the displayed flag in the msg106 flags word,
and the split Xorg structures (`xup_cap_budget` ring vs
`cap_sent_region`) come from the checkpoint as-is. What changes vs
FR-ACK-1-as-filed: the rationale (concurrency, not a ghost) and the
emission point (max(absorb N, egress N−1), not last-EGFX-byte).

**Hard safety invariant — CI-pinned, never probed-for after the
fact:** the consumed-ack is emitted strictly after the vmsplice pipe
drains. Violated, Xorg overwrites borrowed pages mid-read: silently
corrupted encodes, the worst failure class in the pipeline.

**Blast radius.** Server-internal + private xup protocol only; EGFX /
client wire and bitstream untouched. xup contract version bump — an
old xorgxrdp receiving an early ack frees the slot with no region
safety behind it, so mixed deployments are contract-gated. One new
"consumed" message on the existing worker→main enc_done queue seam
(ordering natural: it precedes the frame's data on the same queue).
Single-worker nuance: absorb(N+1) waits for the worker to finish
rewrite(N); the overlap is staggered per resource (capture ‖ worker
tail, encode ‖ egress) and still ≈halves the period.

**Escalation ladder.** (1) CI: the checkpoint's five joint-model
tests plus an eager-emission model with the (a)&&(b) condition and
failure injection; (2) local fleet arm, 5 s: negative arm gaps at
m=1, outstanding=0 callback entries appear; (3) T4 last, 30 s gate
run against the 55–57 ms prediction, decomposed with the #55 i55
instrument (`i55_h1h2_uprobe.sh` + `i55_analyze.py`).

### Rung 1 (CI) RESULT — implemented, 174/174 green, and it moved the item

Shipped behind `gfx.toml [avc444_ffmpeg] eager_slot_ack` (default 0,
so the shipped pacing is unchanged byte for byte). xup contract
20260731. The T4 is decommissioned, so rung 3 is now the local fleet.

**The ack was SPLIT, not moved.** An early ack that also disposed of
the frame's region would lose pixels: a tail that fails after the slot
is released still owes the producer its capture region back. So there
are two acks per frame — `XUP_ACK_FLAGS_SLOT_ONLY` at
`max(absorb N, egress N−1)`, which frees the slot and says nothing
about the frame, and the ordinary region-disposing ack at egress,
unchanged and at its shipped emission point. The producer keeps two
frontiers (`rect_id_ack` / `rect_id_ack_shown`) advanced by one shared
rule, `xup_ack_frontier_apply()` in `common/xup_client_info.h`, which
is the copy xorgxrdp calls and the copy CI tests.

**CI FINDING 1 — the pre-registered 55–57 ms prediction is CONDITIONAL,
and step 0 is now the load-bearing question.** A four-resource model
(capture ‖ worker[absorb+rewrite] ‖ transport, the T4's shape: worker
heaviest) says that with a HEALTHY two-slot budget the SHIPPED ack
already reaches the worker floor — 67 vs 68 captures per 400 ticks,
i.e. eager acking buys nothing at m=1. It pays only when the effective
capture depth is ONE, and there it restores the floor exactly:
40 → 67 captures, **1.68×**, against a chain/worker ratio of 10/6.
The deployed T4 measured 113.6 ms against a 69 ms worker (24.1 absorb
+ 44.9 rewrite/assembly) = **1.65×** — the depth-1 arm, not the
depth-2 arm. So the T4's regime is explained by an effective depth of
one, and #70's value is exactly the value of getting off it. Whether
that is the ack VALUE off-by-one (step 0's dead slot) or something
else is now the question that decides whether #70 is the fix or a
second fix for the same 1.65×. Both assertions are pinned
(`test_eager_ack_buys_nothing_when_the_budget_already_admits_two`,
`test_eager_ack_restores_the_floor_at_effective_depth_one`) so the
claim cannot be quietly upgraded.

**CI FINDING 2 — the design as filed LOSES REGIONS; caught before
deployment.** The producer's held-region map had one entry per capture
slot, which was exact while slot-release and disposal were the same
event. The eager ack separates them, so a monitor holds one more
undisposed frame than it holds slots, and a capture overwrote a region
entry whose frame could still fail: 45 overflows and 21 lost regions
in a 400-tick run with a failing tail every third frame — a stale
rectangle on screen with no event anywhere. Fixed by sizing the map at
`XUP_CAP_SENT_SLOTS = XUP_CAP_AVC444_SLOT_COUNT + 1` and allocating
entries by identity (`xup_cap_sent_take`) rather than by capture slot.
The +1 is exactly what condition (b) bounds the lag at — the
adversarial absorb-only arm overflows even three entries (15 times),
which is the same fact seen from the other side.

**Also pinned:** the absorb-only arm (condition (b) dropped)
bufferbloats visibly — its egress backlog is a function of run length
(200 ticks → 400 ticks grows it) while the (a)&&(b) arm stays at 2 for
any run length. That is the PRD-forbidden shape, and it is why (b) is
not optional.

### Rung 2 (local fleet) RESULT — it unblocks the encode; 1.11× overall

arm-u / arm-v, `captures/i70_local_eager_ack_ab_20260731/README.md`.
SAME xrdp deb (`348a16dde3f3`) and SAME xorgxrdp deb (`10fa3aa23033`)
on both arms, differing in one gfx.toml line. m=1, 2560x1440 =
3.69 Mpx, codeflood, AMD VAAPI, 60 s, two runs per arm.
**Not comparable to the T4 series** (different Mpx, encoder and box).

| | control | eager | |
|---|---|---|---|
| period | 36.6 / 37.1 ms | 33.0 / 33.6 ms | **1.11×** |
| encode (submit→absorb) | 19.9 ms | 20.5 ms | |
| tail (absorb→egress) | 18.0 ms | 19.7 ms | |
| **encode(N+1) ‖ tail(N)** | **4.8 ms**, p50 **0.0** | **8.5 ms**, p50 **10.3** | **+77 %** |
| encode share of tail | 27 % | 43 % | |
| slot acks | 0 | one per frame | |

**The mechanism moved, in milliseconds.** The control's MEDIAN
concurrent-encode time is zero — half its frames overlap nothing —
against 10.3 ms eager. Legs close to 0.3 ms unattributed on the eager
arm (the control's 2.8 ms is the idle gap its later ack creates, and is
part of the result). Live correctness held: wire audit 7/7, zero black
frames, zero rewrite failures, zero budget assertions, **zero
region-map overflows** — the +1 sizing CI forced is sufficient in
practice too.

**Why only 1.11×:** the single encoder worker does the submit/collect
wait AND the LTR rewrite serially, so the ack can only overlap the part
of the tail that is not the worker — and 8.5 ms of a 19.7 ms tail is
about that part. **The remaining serializer is the worker thread, not
the ack.** That is #40/#41 (FR-PROC-7 submit/collect with bounded
worker admission), already named here as #70's co-requisite; it is now
the measured next lever rather than a predicted one.

### Step 0 — ANSWERED, and the answer is NO. The July claim is retired.

`e41bcb59` concluded from raw-offset uprobes that the ack "trails its
rect_id by one frame beyond true in-flight — one slot pinned forever by
a ghost". The identity-carrying log line refutes it. Over 3289 capture
admissions across both arms, `(id-1) − ack` is +1 on all but 3: at the
admission of frame N the previously sent frame N−1 is still in the
pipeline and **cannot legitimately be acked**, so `ack = N−2` is
exactly what a correct cumulative ack looks like. The producer runs at
its designed depth of two. There is no dead slot and #70 never depended
on one. The raw-offset instrument could not tell "correctly one behind"
from "wrongly one behind" because it never carried the frame's
identity — the same lesson as #64.

Instrument for rungs 2–3: `XRDP_ACK_TRACE=1` (separate from
`XRDP_GFX_TRACE`, so a timing run does not pay for per-rect tracing)
emits `ACK_TRACE {msgin,submit,absorb,egress,ack} id=… us=…` from xrdp
and `ACK_TRACE cap id=… begin_us=… packed_us=… sent_us=… ack=… shown=…`
from xorgxrdp, all stamped in CLOCK_MONOTONIC µs — one system-wide
clock, so a capture leg and the previous frame's tail can be
INTERSECTED and the concurrency stated in milliseconds. The `ack=`
field on the capture line is also the step-0 instrument: `id − ack` at
capture admission says directly whether a slot is dead.
