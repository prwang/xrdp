# BACKLOG #70 — the eager slot-release ack, measured on the local fleet

Rung 2 of the #70 ladder (the T4 is decommissioned, so this is the last
rung). **arm-u and arm-v run the SAME xrdp deb (`348a16dde3f3`) and the
SAME xorgxrdp deb (`10fa3aa23033`)** and differ in exactly one gfx.toml
line, `eager_slot_ack`. A build difference cannot confound this A/B.

- `../i70_local_armU_control_20260731/` — arm-u run 1
- `../i70_local_armU_control_run4_20260731/` — arm-u run 4
- `../i70_local_armV_eager_20260731/` — arm-v run 1
- `../i70_local_armV_eager_run4_20260731/` — arm-v run 4

Analyzer: `../../i70_ack_overlap.py <dir>`. Oracle dumps deleted after
the wire audit and black-frame check (both PASS on all four); traces
gzipped in place.

## Conditions — read this before comparing to anything

m=1, **2560x1440 = 3.69 Mpx**, `SESSION_KIND=codeflood`, oracle client,
60 s, AMD VAAPI (`h264_vaapi`, CQP 20), 32-core dev box. **These numbers
are NOT comparable to the T4 series** (8.29 Mpx, Tesla T4/NVENC, 4 or 8
vCPU Xeon): different payload size, different encoder, different box.
The 4K attempt was refused by `setup_monitors.sh`'s active-geometry
check — DUMMY0 reports `3840x2400R` as its current mode while its CRTC
stays at 2560x1440, and the check caught it (the guard added after the
four voided T4 runs doing its job). Not chased further: the geometry is
not what this A/B is about, and both arms ran identically.

## Result

| | arm-u control | arm-v eager | |
|---|---|---|---|
| period (send→send) | 36.6 / 37.1 ms | 33.0 / 33.6 ms | **1.11×** |
| sends/s | 27.4 / 27.1 | 30.4 / 29.8 | |
| capture (begin→sent) | 1.4 ms | 1.4 ms | |
| encode (submit→absorb) | 19.9 / 20.4 ms | 20.5 / 20.9 ms | |
| tail (absorb→egress) | 18.0 / 18.2 ms | 19.7 / 20.2 ms | |
| **encode(N+1) ‖ tail(N)** | **4.8 / 5.0 ms** (p50 **0.0**) | **8.5 / 8.8 ms** (p50 **10.3**) | **+77 %** |
| encode share of tail | 27 / 28 % | 43 / 44 % | |
| capture(N+1) ‖ tail(N) | 0.7 ms | 0.4 ms | |
| slot acks emitted | 0 | 1689–1728 (one per frame) | |

Two 60 s runs per arm, values quoted `run4 / run1`; every leg reproduces
within 0.5 ms and the period ratio is 1.11× in both pairings. Two more
runs (run2, run3) measured the same periods — 36.2/32.9 and 36.8/33.1 ms
— but their session-Xorg window came out empty (a cold run truncates
`.xorgxrdp.<display>.log`, so the line mark taken before the run was
past the end of the file); those directories were dropped and
`e_gate_run.sh` now detects the truncation. The period numbers from them
are recorded here and nowhere else.

**The eager ack does what it claims: it unblocks the encode.** The
control's MEDIAN concurrent-encode time is 0.0 ms — half its frames
overlap nothing at all — against 10.3 ms for the eager arm. Mechanism
check (quality gate 2) passes on its own telemetry: 1689 `kind=slot`
acks in arm-v, zero in arm-u.

Arithmetic closes (gate 1): eager 1.4 + 20.5 + 19.7 − (8.5 + 0.4) = 32.7
against a 33.0 ms period, 0.3 ms unattributed. The control leaves 2.8 ms
— which is the idle gap its later ack creates, and is itself part of the
result.

Correctness held on the live path: wire audit `--assert` PASS (7/7),
zero black frames, zero rewrite failures, zero budget assertions, and
**zero region-map overflows** ("no free sent-region entry") — the
`XUP_CAP_SENT_SLOTS = slots + 1` sizing CI forced is sufficient in
practice, not just in the model.

## Why 1.11× and not more

The single encoder worker thread does the submit/collect wait AND the
LTR rewrite serially, so the ack can only overlap the part of the tail
that is NOT the worker. 8.5 ms of a ~19.7 ms tail is roughly the
non-worker part. **The remaining serializer is the worker thread, not
the ack** — BACKLOG #40/#41 (FR-PROC-7 submit/collect construction with
bounded worker admission), which #70 already names as its co-requisite.

## Step 0 — ANSWERED, and the answer is NO

The July probe (`e41bcb59`) concluded from raw-offset uprobes that "the
ack value xorgxrdp receives trails its rect_id by one frame beyond true
in-flight — one slot pinned forever by a ghost". The identity-carrying
log line now says otherwise. Outstanding frames at capture admission,
`(id-1) − ack`, over 1559 + 1730 admissions:

```
   +0 :     1-2   (a genuinely free slot, at session start)
   +1 :  1558 / 1728
```

At the admission of frame N the previously sent frame N−1 is still in
the pipeline and **cannot legitimately be acked**, so `ack = N−2` is
exactly what a correct cumulative ack looks like — not a dead slot. The
producer is running at its designed depth of two (one outstanding plus
the one being captured). There is no off-by-one to fix, and #70 does not
depend on one. The July claim is retired; its raw-offset instrument
could not tell "correctly one behind" from "wrongly one behind" because
it never carried the frame's identity.
