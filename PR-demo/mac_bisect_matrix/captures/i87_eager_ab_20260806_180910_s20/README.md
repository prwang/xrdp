# i87_eager_ab_20260806_180910_s20 — the eager-ack A/B at the shipped default

The merged eager-ack A/B (owner-approved 2026-08-06 as two arms and four
interleaved 20 s legs), covering BACKLOG #80 steps 2 and 3 and BACKLOG
#87's eager-ack half. Runner: `PR-demo/mac_bisect_matrix/i87_eager_ab.sh`
— read its header for the full rationale.

Every number below was re-derived from the archived rings for this
README. Two of the figures first reported to the owner did not survive
that; they are the first two sections, because they change what the rest
of the table means.

---

## Correction 1 — the wire bound was NOT a vacuous pass, and 0 is not a value it can take

The first reading of this run reported "the wire bound was 0 on all
12292 sends across all four legs", concluded a vacuous pass, and said the
bound was genuinely attained only in the freeze leg
(`i80_freeze_20260806_180613_s20`). Re-derived, none of that holds.

**What the bound is.** `xrdp/xrdp_encoder.h:95-106` states it on the
frame's own identity: at the instant a frame is handed to the transport,
`frame_id − frame_id_client ≤ C + 2·M`, where C is the credit window and
M the monitor count. Here M = 1 and, on the treatment arm, C = 2, so the
bound is 4. The `egress` record carries exactly those two numbers — field
1 is the frame's id, field 4 is the last id the client had acknowledged
(`xrdp/xrdp_mm.c:4379`).

**What it measured.** One row per frame that reached the transport:

| leg (arm) | distance 1 | 2 | 3 | 4 | frames |
|---|---|---|---|---|---|
| a1 (x020, control) | 642 | 245 | 1 | — | 888 |
| b1 (x021, treatment) | 488 | 450 | 14 | **1** | 953 |
| a2 (x020, control) | 616 | — | — | — | 616 |
| b2 (x021, treatment) | 615 | 1 | — | — | 616 |

"distance 1" means the frame just handed over was the only one the client
had not yet acknowledged; "distance 4" means three earlier frames were
also outstanding. The value **0 is unreachable by construction** — a
client cannot acknowledge a frame before it has been sent — so a metric
reading 0 on every sample was never the wire bound.

**The treatment arm attained its bound once** (distance 4, in leg b1) and
came within one of it 14 times. The control arm attained *its* bound once
as well; see correction 3 below for why the two bounds are different
numbers.

**Where a "0 everywhere" reading plausibly comes from.** The `send`
record has two fields that are 0 on all 12292 records in this run: its
frame-id field and its last-PDU flag. That is structural, not a
coincidence — the send trace only fires when a chunk carries bytes
(`xrdp/xrdp_mm.c:4298`), and on this path the message that carries a
frame's terminal marker carries no bytes, so it never reaches that trace.
Any statistic taken from those two fields is constant by construction and
cannot show the bound. The other candidate readable from `send` —
`frame_id_server − frame_id_client`, i.e. completed frames not yet
acknowledged, *not* counting the one currently being written — is 0 on
9409 of the 12292 sends, 1 on 2816, 2 on 63 and 3 on 4. Also not zero
everywhere.

**What survives.** On loopback the client acknowledges in microseconds,
so the bound is rarely approached: the modal distance is 1 (2361 of the
3073 frames across the four legs) and 3057 of 3073, or 99.5 %, sit at 1
or 2. That is the honest version of "the ack is rarely
what limits the pipeline here". It is not the same statement as "the
bound was never exercised".

## Correction 2 — the encoder slowed down between the two pairs, so read within pairs only

This one was in the first report and is confirmed. Between the first pair
of legs and the second, the wait for the two ffmpeg children to finish
encoding a frame — the `pump` bracket, which is one call that submits
both views and blocks until both children return
(`xrdp/xrdp_encoder.c:2693-2696`) — moved from ~16.3 ms to ~26.2 ms **on
both arms**, while every other stage stayed put:

| stage (mean ms per frame) | a1 x020 | b1 x021 | a2 x020 | b2 x021 |
|---|---|---|---|---|
| wait for the ffmpeg children (`pump`) | 16.258 | 16.457 | **26.223** | **26.188** |
| pop the encoded bytes and rewrite the LTR references (`coll`) | 1.309 | 1.264 | 1.350 | 1.351 |
| assemble the EGFX PDUs (`emit`) | 0.304 | 0.332 | 0.356 | 0.309 |
| submit both views to the children (`subm`) | 0.001 | 0.001 | 0.001 | 0.001 |
| drain the work fifo (`drain`) | 0.001 | 0.000 | 0.001 | 0.000 |

The change is a **step, not a drift**. Bucketing each leg's encoder wait
into 2 s windows: b1 reads 16.4 / 15.9 / 16.0 / 16.3 / 16.4 / 16.1 / 16.4
/ 16.4 / 16.2 across its 20 s, and a2 reads 27.0 / 26.0 / 25.8 / 26.1 /
26.0 / 25.9 / 26.2 / 26.1 / 25.8 across its. The transition happened
entirely in the 11 s gap between b1 ending (18:10:09) and a2 starting
(18:10:20). By the time the next capture on this host ran
(`i82_x015_rerun_20260806_181825_s20`, 18:18:34 onwards, different arms
and an older build) the encoder wait was back to 16.16–16.31 ms.

**Only within-pair comparisons (a1 vs b1, a2 vs b2) are valid.** Across
pairs the arms are not comparable, because the host was not the same
machine underneath them.

The cause was not measured. GPU dynamic frequency scaling is plausible —
the pop-in of a step change on a GPU-side stage while the CPU-side stage
(`coll`, the LTR rewrite, 1.26 → 1.35 ms) barely moved points that way —
but **the GPU clock state was sampled only after the runs finished, never
during a leg**, so it is a hypothesis and not a measurement. For the
record, `/sys/class/drm/card0/device/pp_dpm_sclk` read while writing this
README lists levels `0: 600Mhz`, `1: 608Mhz *`, `2: 2900Mhz` with
`power_dpm_force_performance_level = auto`; the middle entry is the
instantaneous clock, not a fixed step, which is why the same file read
shortly after the runs showed 1100 MHz there. Neither reading says
anything about what the clock was doing during the legs.

## Correction 3 — C = 2 is not exactly equivalent to frames_in_flight = 2

The two arms were designed to run "a window of 2 reached by different
mechanisms", so that the A/B isolates the ack mechanism rather than the
window size. Reading the two functions, the windows are **not** the same
size, and the measurement above shows the difference.

* Legacy (control): the ack is emitted only while
  `frame_id_client + fif > frame_id_server`
  (`xrdp/xrdp_encoder.h:40-44`), and the value emitted is
  `frame_id_server`. So the highest id ever acked is `client + fif − 1` =
  client + 1. The producer captures at most 2 ids above the ack, so a
  frame reaches the transport at most **client + 3**.
* Credit frontier (treatment): the credit is clamped at `client + C`
  (`xrdp/xrdp_encoder.h:114-130`) = client + 2, so a frame reaches the
  transport at most **client + 4**.

That is exactly the maximum distance each arm reached in the table in
correction 1: 3 for the control, 4 for the treatment. The
legacy-equivalent credit window is therefore **C = fif − 1 = 1**, not 2,
and shipping C = 2 as "legacy `frames_in_flight` equivalence" permits one
more frame of queue than today's default does. On loopback that costs
nothing measurable; on a link where a frame takes real time it is one
frame of extra latency. Flagged here because the default-flip decision in
BACKLOG #80 rests on the equivalence claim.

---

## The arms, stated before any rate

Same image, same payload, same geometry, same client rig; the two
`gfx.toml` bodies differ by exactly one line.

| | x020 — CONTROL | x021 — TREATMENT |
|---|---|---|
| host port | 40036 | 40037 |
| pod | `xrdp-x020-7dd97875c4-cv749` | `xrdp-x021-d8549f574-m2pbz` |
| image | `localhost/xrdp-bisect:1d5bc0960db8.xx10fa3aa-tf` | same |
| server build | xrdp-dev `0.10.80+git20260803024109.1d5bc0960db8` | same |
| producer build | xorgxrdp-dev `1:0.10.80+git20260731212221.10fa3aa23033` | same |
| the one differing line | `eager_slot_ack = false` | `eager_slot_ack = true` |
| window mechanism | `XRDP_GFX_FRAMES_IN_FLIGHT = 2`, pinned in `k8s/x020.yaml`; this is today's shipped behaviour | `wire_window = 2` in `gfx.toml` — the value the code ships as its default, which had never encoded a frame in this tree (every `wire_window` on record before today is 1) |

Both arms: one monitor at 3840x2400 (9.22 Mpx), `textflood` payload, the
oracle client on this host (it saves the bytes and acknowledges each
frame *before* decoding it, so it measures the server's ceiling).

Legs ran interleaved — a1 (x020), b1 (x021), a2 (x020), b2 (x021), 20 s
each — because an unchanged arm has drifted 36.8 → 41.8 ms across a
single day on this host, and one leg each cannot be read against that.
The interleaving is what made correction 2 visible.

## Mechanism check, before any rate

A knob that was set but changed nothing in its own mechanism's telemetry
has not been tested. Both mechanisms show up directly in the ring:

| | a1 (x020) | b1 (x021) | a2 (x020) | b2 (x021) |
|---|---|---|---|---|
| slot-credit records (`ackslot`) emitted | **0** | **938** | **0** | **616** |
| the credit window C they report | — | 2 | — | 2 |
| region-ack records (`ackregion`) emitted | 886 | 951 | 616 | 616 |
| the C field on those | 0 | 2 | 0 | 2 |
| `frames_in_flight` reported on every network write | 2 | 2 | 2 | 2 |
| network writes | 3552 | 3812 | 2464 | 2464 |

The control emits no slot credit at all — the legacy path has no such ack
— and reports C = 0 because the legacy call site passes a literal 0 in
that field (`xrdp/xrdp_mm.c:1786-1790`). The treatment emits the slot
credit on every cycle and reports C = 2 on both ack types, which is the
proof that `wire_window = 2` reached the encoder. Both arms report
`frames_in_flight = 2` on every one of the 12292 network writes, so that
field means the same thing on both and is not what differs. (On the
treatment arm it is inert: with `eager_slot_ack = true` the dispatch goes
to the credit frontier and never consults `frames_in_flight` —
`xrdp/xrdp_mm.c:1799-1822`.)

## The metric the change targets

**What it is, in plain words:** the wait between the encoder's ffmpeg
children finishing with a frame's pixels — at which point the capture
pages the producer lent are free — and xrdp actually telling the producer
it may capture again. The whole point of the credit frontier is that this
wait should depend only on the encoder, never on the network. The defect
it replaces withheld that permission behind the client's acknowledgement
window (BACKLOG #79).

Measured per frame identity (credit for frame k−2 minus the moment the
children absorbed k−2; paired by id, never by time window), one row per
leg, never pooled:

| leg (arm) | frames | p50 | p90 | mean | cycles waiting > 10 ms |
|---|---|---|---|---|---|
| a1 (x020, control) | 886 | 8.825 ms | 10.582 ms | 8.975 ms | 146 (16.5 %) |
| b1 (x021, treatment) | 951 | **0.015 ms** | **0.040 ms** | 0.163 ms | 8 (0.8 %) |
| a2 (x020, control) | 616 | 8.677 ms | 10.247 ms | 8.814 ms | 82 (13.3 %) |
| b2 (x021, treatment) | 616 | **0.027 ms** | **0.047 ms** | 0.028 ms | 0 (0.0 %) |

This is the one result that is decisive and that survives the encoder
drift: it moves the same way in both pairs, by roughly the same factor,
in both host states.

**Shape warning on one cell.** The treatment's 0.163 ms mean in leg b1 is
not a mean over one thing: 935 of its 951 frames sit under 0.1 ms, 3
between 0.1 and 1 ms, 5 between 1 and 10 ms, and 8 between 10 and
11.7 ms. The p50 and p90 describe the population; the
mean is dragged by those 8. The control's distribution, by contrast, is
tight and unimodal (p50 8.825, p90 10.582, max 16.277), so its mean is
meaningful.

## Frame period

The interval between one frame's last byte reaching the transport and the
next frame's, measured on the ring:

| pair | leg (arm) | mean | p50 | p90 | p99 | frames |
|---|---|---|---|---|---|---|
| 1 (encoder at 16.3 ms) | a1 (x020, control) | 19.110 ms | 17.717 | 26.585 | 30.158 | 888 |
| 1 | b1 (x021, treatment) | **17.683 ms** | 17.445 | **19.280** | **24.161** | 953 |
| 2 (encoder at 26.2 ms) | a2 (x020, control) | 27.423 ms | 27.271 | 29.874 | 32.748 | 616 |
| 2 | b2 (x021, treatment) | 27.391 ms | 27.252 | 30.134 | 33.238 | 616 |

In pair 1 the treatment is faster on the mean and much tighter in the
tail. In pair 2 the two arms are indistinguishable — which is what theory
predicts and is a check on the result rather than a disappointment: when
the encoder itself needs 26 ms per frame, permission to capture is never
the thing that is late, so the acknowledgement mechanism cannot matter.
The corroborating detail is the encoder worker's idle bracket, "time the
worker held nothing to encode": in pair 2 it is 0.002 ms on both arms —
the worker never waits at all — against 1.663 ms (control) and 0.195 ms
(treatment) in pair 1.

## Producer margin, and what it voids

The gate prints, per leg, the ratio of the pipeline's mean frame interval
to the payload's own mean frame interval: **1.14x (a1), 1.06x (b1), 1.70x
(a2), 1.71x (b2)**, all below the 2.0x floor.

Under the owner's 2026-08-06 ruling that means: **any claim from this run
about two pipeline stages overlapping is void** — the payload is close
enough to the pipeline that a missing second frame cannot be told from a
stage that failed to overlap. Throughput and regression comparisons
against an arm sharing this payload, geometry and client remain valid,
and the 1.14x/1.06x must be quoted beside them.

Note that pair 2's higher margin is not the producer getting faster: the
payload's own interval was 16.1/16.0 ms in pair 2 against 16.7/16.7 ms in
pair 1, essentially unchanged. The margin rose because the *pipeline* got
slower (correction 2). A margin that improves because the thing being
measured degraded is not an improvement.

## One cross-capture observation, with its caveat

The control arm here (19.110 ms mean period) is *slower* than arm x014
measured nine minutes later in
`i82_x015_rerun_20260806_181825_s20` (17.909 / 18.153 ms), on the same
payload, geometry and client, with the payload's own interval within
0.1 ms. That is not a like-for-like comparison — x014 runs an older build
(`73e4cb76d483`) whose `eager_slot_ack` is BACKLOG #70's earlier
mechanism, not the legacy gate this control uses — but it is consistent
with the rest of this run: the arms that release the capture slot early
(x014 at 17.9/18.2, x021 at 17.7) sit together, and the one that does not
(x020 at 19.1) sits apart.

## Reproducing the numbers

Per leg, use the ring in `leg_*/perf/` whose `# perfbase real_ns` header
is **latest**; the pods accumulate rings from earlier sessions and
pooling them double-counts. Window each leg to the `t0`/`t1` in its
`window.txt`. No warm-up period is dropped in the tables above, matching
the first reading of the run; dropping the first second after the first
capture changes the encoder-wait means by about 0.13–0.23 ms and removes
one 123–242 ms outlier per leg, which is the session's very first encode
(cold ffmpeg children) and not a recurring event.

Field meanings are at the `PERF_TRACE6` call sites: `egress`
`xrdp/xrdp_mm.c:4379`, `ackslot`/`ackregion` `:1729,1745`, `send` `:4308`,
`absorb` `xrdp/xrdp_encoder.c:2490`, `msgin` `xrdp/xrdp_mm.c:5140`. Stage
brackets are `<name>_beg`/`<name>_end` pairs on the encoder worker
thread.

## Numbers that re-derived exactly as first reported

Frame period 19.110 → 17.683 mean, 26.585 → 19.280 at p90, 30.158 →
24.161 at p99 in pair 1; 27.423 → 27.391 in pair 2. Encoder wait
16.258 / 16.457 → 26.223 / 26.188. Slot-credit counts 938 and 616 at
C = 2, and zero on the control. The wait-for-permission p50/p90 figures
in the table above. The one small difference: the control's share of
cycles waiting over 10 ms re-derives as 16.5 % and 13.3 %, against 16.4 %
and 14.4 % first reported; the second of those is a real 1.1-point gap
and the figure in this README is the one to use.
