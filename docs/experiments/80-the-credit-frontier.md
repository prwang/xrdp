# #80 — the credit frontier: what landed, and what it decided

**Status:** implementation and CI landed 2026-08-03, behind
`gfx.toml [avc444_ffmpeg] eager_slot_ack` (default false). Steps 4 and 5
of the backlog item — the validation gate against #81's RTT harness, and
the fleet A/B — have NOT run. Nothing here is a live measurement of the
new code.

Records in this directory are kept verbatim. Supersede with a dated
note; do not tidy.

---

## The question this item answers

Not "how do we make it faster". The question was **who is allowed to
stop whom**, and the answer the code gave before this change was wrong
in a way that had a name.

xorgxrdp captures a frame only into a buffer that xrdp has said it may
overwrite. That permission arrives as one message — the module ack — and
it is the **only** admission token anywhere in the pipeline. Until this
change, the entire emission of that token was wrapped in one comparison:

```c
if (xrdp_gfx_ack_window_open(frame_id_client, frame_id_server, fif))
```

which asks "has the CLIENT acknowledged enough of what we sent?". So a
client on a slow link stopped the X server from capturing — not because
any pipeline stage was busy, but because of a fact about the network,
several layers away, that had nothing to do with whether the capture
buffer was free. PRD FR-FLOW-1 clause 1 forbids exactly this: a lossless
stall may consult only the immediately adjacent stage.

## Step 1 (blocking pre-step): what xorgxrdp does with a SLOT_ONLY ack

Read in `/workUpdateXorgXrdp` at `10fa3aa23033`. The pre-step asked
whether releasing a capture slot consumes region-retirement state — if it
did, clamping or delaying either ack could lose pixels.

It does not, and the separation is explicit:

* `rdpClientConProcessMsgClientRegionEx` (`module/rdpClientCon.c:1967`)
  applies every ack through `xup_ack_frontier_apply`
  (`common/xup_client_info.h:611`), which advances `f->shown` **only
  when the ack is not SLOT_ONLY**.
* Retirement is then driven from the *region* frontier:
  `rdpClientConRetireSentRegions(clientCon, clientCon->rect_id_ack_shown)`.
* Admission is driven from the *slot* frontier: `rect_id_ack`, read by
  `xup_cap_budget_has_capacity` via `rdpClientConMonitorHasCapacity`
  (`module/rdpClientCon.c:152`).
* `rdpClientConReturnFrameRegion` — the path that gives pixels back — is
  reached only when the ack is neither SLOT_ONLY nor displayed.

Two independent frontiers, two independent consumers. Confirmed.

The same read established the **drop path needs no new code**, which is
what made the whole design possible: `rdpDeferredUpdateCallback` returns
early when no monitor has capacity (`module/rdpClientCon.c:3984`), the
damage stays in `dirtyRegion`, and the next admitted capture takes the
union. Refusing admission *is* drop-by-coalesce (PRD FR-CAPTURE-8
clause 4).

## What was built

The gate is not removed. Removing it would leave nothing at all between
the encoder and the wire — `enc_done` hands every frame to
`trans_write_copy_s()`, which cannot refuse and mallocs the remainder
onto an unbounded list. Instead the client's window becomes a **third
term of the credit**, applied at capture admission, where refusing a
frame is free:

```
credit = min(frame_id_consumed,     /* slot fact: children done with the pages */
             frame_id_server + 1,   /* nearest neighbour: one frame of inventory */
             frame_id_client + C)   /* end-to-end wire window */
```

emitted **unconditionally** whenever it advances. Each term consults
exactly its own layer. When the third term binds, the producer is not
stalled holding a finished frame; it simply does not capture, and a slow
client gets fewer frames, each of them current, instead of a backlog of
stale ones.

`xrdp_gfx_plan_acks()` (`xrdp/xrdp_encoder.h`) is the whole decision,
pure and driven directly from CI. `xrdp_mm_emit_credit_frontier()`
(`xrdp/xrdp_mm.c`) does nothing but fill in the state, call it, and send
what it says.

### Finding, not in the design as approved: the region ack is a second admission token

The design as written in BACKLOG #80 described one enforcement point.
There are two, and the second was found while implementing.

The ordinary region-disposing ack is **not** flagged SLOT_ONLY, so on
the producer `xup_ack_frontier_apply` moves the slot frontier as well.
It therefore admits captures exactly as the slot ack does, and a window
enforced on one and bypassed on the other is not a window. It is now
clamped by the same term:

```
region = min(frame_id_server, frame_id_client + C)
```

Clamping it is safe for the producer's region bookkeeping, and the
margin is exactly one: xorgxrdp sizes `cap_sent` at
`XUP_CAP_SENT_SLOTS = XUP_CAP_AVC444_SLOT_COUNT + 1` per monitor and
**refuses rather than overwrites** when full (`xup_cap_sent_take`
returns −1). The clamped region target never lags the credit by more
than one id, which is asserted over the whole lattice in
`test_region_ack_never_lags_the_credit_by_more_than_one`. Delaying the
region ack costs held pixels in the producer; it never loses them.

### Finding: one ack is deliberately NOT clamped

The `NOT_DISPLAYED` region-return for a frame that produced no output
(the `!displayed` branch of the `enc_done` handler) stays unclamped.
That frame never reached the transport, so it occupies no wire, and its
pixels are owed straight back under FR-ACK-1 Invariant III; delaying it
would hold a region in `cap_sent` waiting for a client to acknowledge an
id it was never sent.

The honest consequence, stated because "≤ C + 2" gets quoted: releasing
that slot lifts the producer's admission ceiling by one frame beyond the
credit. **The bound is a statement about frames that reached the
transport**, and a run of discarded frames relaxes it transiently. In a
healthy pipeline discarded frames do not occur; if they do, that is
itself the thing to investigate.

### The bound is per monitor

xorgxrdp's budget is `XUP_CAP_AVC444_SLOT_COUNT` **per monitor**, never a
global pool, so the wire bound is `C + 2·M` frame ids at M monitors, not
`C + 2`. The PRD's single-meaning requirement is met by stating it that
way in `gfx.toml(5)` and in the code comment; the CI enumeration models
one monitor.

## Step 3: CI, and the RED-on-HEAD verification

`tests/xrdp/test_avc444_credit_frontier.c`, 9 cases.

**The exhaustive enumeration** (`test_joint_machine_enumeration`) is the
load-bearing one. It builds the joint xrdp + xorgxrdp state machine —
capture admission by xorgxrdp's own budget rule, absorb, egress, client
ack — and walks the entire reachable state space by DFS, running the real
planner after every event, for C ∈ {1,2,3}. In every reachable state:

* **INV-SENT** — no ack ever grants past `client + C`.
* **INV-WIRE** — `server − client ≤ C + slots`.
* **INV-LIVE** — the producer already holds every credit the three terms
  permit. *This is the property the shipped gate does not have.*
* **INV-HELD** — held regions ≤ `slots + 1`, xorgxrdp's ring size.
* **deadlock freedom** — the only state with nothing enabled is the
  drained end state.

Plus three non-vacuity checks, because an enumeration that wedges early
passes all of the above in silence: the pipeline runs to the end, the
wire bound is **attained** (so "≤ C + slots" is tight, not comfortable),
and the window term is the strict minimum somewhere (so the end-to-end
guard is exercised rather than dominated by the slot fact throughout).
The first draft asserted `visited > 100` instead; that threshold was a
guess with no specification behind it, it failed at 91 states, and it was
replaced rather than adjusted to fit.

**RED-on-HEAD, verified rather than asserted.** The shipped policy was
temporarily reinstated inside `xrdp_gfx_plan_acks` (an early return under
`xrdp_gfx_ack_window_open`), the suite was run, and **4 cases went red**:

```
FAIL test_planner_never_withholds_an_earned_credit
     Assertion 'held >= want' failed: held == 0, want == 1
FAIL test_joint_machine_enumeration
     Assertion 'xrdp_gfx_credit_frontier(...) <= cur.ack' failed: 6 <= 5
FAIL test_wedge_replay_d40_c2
     Assertion 'plan.region == steps[i].want_region' failed: -1 == 407
FAIL test_wedge_replay_d40_c1
     Assertion 'plan.region == steps[i].want_region' failed: -1 == 406
```

The mutation was reverted and the suite re-run green. The scratch
mutation is not committed — CLAUDE.md's rule that a forbidden pattern
does not get a "measurement only" exemption applies to a re-added
cross-layer gate as much as to a per-frame `LOG()`.

## The wedge, replayed

The golden replay is not synthetic. Its **event order** is the
perf_trace of arm x017 at 40 ms injected ack delay
(`captures/i79_x017_ackdelay_20260802_s20/leg_d40`, frames 406–409,
`XRDP_GFX_FRAMES_IN_FLIGHT = 1`). Its **expected ack values** are
computed by hand from FR-FLOW-1 clause 3 and written out per row, so the
test states the specification rather than the implementation.

What the shipped build did on that exact sequence, read off the trace:

```
 -53.853  ackregion 405           client catches up; the gate opens
 -27.975  absorb 406
 -27.915  ackslot   406           the last thing it emits for 140 ms
 -17.554  egress 406              server=406, client=405 -> gate SHUT
  +0.000  absorb 407              slot free, encoder idle, NOTHING emitted
 +10.227  egress 407
 +17.222  absorb 408              NOTHING emitted
 +27.036  egress 408              last production; the producer now idles
 +34.157  cliack 406              gate still shut (406+1 > 408 is false)
 +66.294  cliack 407              gate still shut
 +87.391  cliack 408 -> ackregion 408
+112.270  absorb 409
+112.329  ackslot   409           the credit that was due at +0.000
```

**85.2 ms with a free capture slot, an idle encoder, and no permission to
use either**, and the slot credit arrived 112.3 ms late. The frontier at
C = 2 emits it at +0.000 — and then, correctly, withholds at absorb 408,
because 406 and 407 are genuinely unacknowledged on the wire and that is
what C means. The design does not open the wire; it stops the *slot*
signal from being suppressed by a *network* fact.

## What is NOT known

* **No live measurement of this code exists.** Every number above is
  from the pre-change captures or from CI.
* **The shipped default C = 2 is a placeholder**, matching the legacy
  `frames_in_flight` so short-RTT behaviour is preserved. PRD FR-FLOW-1
  clause 4 requires the default to be chosen with #81's RTT data, and
  #81 has not run. Do not quote 2 as a recommendation.
* **The predictions for step 4 must be re-derived for DROP semantics
  before the gate runs**, not after: period roughly flat at all injected
  delays (admission drops instead of stalling), withheld ≈ 0 at every
  delay, `id_server − id_client` at send never above C + 2, the frozen
  client stopping production within C + 2 frames with `wait_bytes`
  plateauing, and the drop visible as damage area per admitted frame
  growing with RTT.
* Little's law is unchanged by any of this: on a long link nothing
  raises the frame rate above (C + 2)/RTT. The item buys latency and
  freshness, not throughput.

---

# Step 4 (2026-08-03) — the first live run: two legs on the #81 netem harness

Owner-approved 2026-08-03: "stand up two more arms comparing RTT0 and
RTT40ms, perf trace on for the new mechanism single monitor
in-flight=1, and analyse against old data." Exactly two legs were run.

Capture: `PR-demo/mac_bisect_matrix/captures/i80_wanpair_20260803_125816_s20`
(reproduce with `i80_wan_pair.sh`, which carries the predictions below in
its header so they are versioned before the run rather than after it).
Arms x018 (loopback baseline) and x019 (40 ms true RTT) carry the same
image, the same xorgxrdp as x014/x015/x017, the same gfx.toml body
(diff-verified) and `wire_window = 1`.

## What this settles, first

**The section above says "no live measurement of this code exists" and
"the code has never encoded a frame on a real link". That is no longer
true.** Both arms were certified on real bytes at deploy — 584 pictures,
7/7 wire asserts, 0 black frames each — and both legs ran a verified
single-monitor 3840×2400 session.

## P1 — the defect: PARTIALLY MET, and the part that failed matters

Head to head, LAN against LAN. x017 `direct` is the same payload,
geometry, monitor count, client rig and xorgxrdp on the OLD build at
`fif = 1`, with nothing in the network path — the only apples-to-apples
comparison available.

| | x017 direct (old) | x018 (frontier, C=1) | |
|---|---|---|---|
| withheld p50 | 0.03 ms | 0.03 ms | — |
| withheld p90 | 35.3 ms | **10.6 ms** | −70 % |
| withheld mean | 8.45 ms | **3.46 ms** | −59 % |
| stalls (> 10 ms) | 29.7 % | **18.2 %** | −39 % |

The prediction was "p50 ≤ 1 ms **and** stall fraction ≤ 5 %". The p50
half was already true of the old build, so it was never a discriminator
and should not have been written as one. The stall half is **not met**:
18.2 % against a predicted 5 %.

What did move is the tail, which is where the cross-layer gate lived.

**The residual 18.2 % is NOT attributed, and the instrument cannot
attribute it.** The ack record carries the frontier's three inputs at the
instant of emission, and at that instant all three are equal — 157 of 157
stalled cycles are ties. This is a "2b" situation: the metric cannot show
the thing, so its silence is not evidence. What the measured means make
plausible: at C = 1 capture k needs the client to have acknowledged k−3;
with L = 32.3 ms capture→egress and a 7.3 ms ack, that permission arrives
about 12.6 ms *earlier* than the natural 17.4 ms cadence — comfortable at
p50, and inside the noise once a frame runs long (period p90 25.9 ms).
That is the end-to-end guard firing correctly, not the old defect. It is
a derivation from means, not a measurement, and it is stated as such.

**What would settle it: one more leg at C = 2 on the LAN arm** (a
configmap edit, a roll and a 20 s run, ~2 min). If the residual stalls
are the `client + C` term they disappear; if they persist, something
else is holding the credit. Not run — it is not in the approved
description, and an extra leg is a finding to report, not a licence.

## P2 — the long tail: MET

| | x017 direct | x018 |
|---|---|---|
| period mean | 21.5 ms | **18.5 ms** |
| period p90 | 42.7 ms | **25.9 ms** |
| period p99 | 52.2 ms | **30.4 ms** |
| period max | 64.1 ms | **50.1 ms** |
| **p90 / p50** | **2.51** | **1.49** |
| throughput | 46.5 /s | **54.1 /s** |

Predicted below 2.0; measured 1.49. The p99 fell 42 % and throughput
rose 16 % on an unshaped link, with the client window nominally *tighter*
(C = 1 permits one unacknowledged frame, the same as fif = 1).

## P3 — drop, not hold: the PREDICTION was wrong, the INDUCTION holds

Predicted: transport bytes queued stay at ~0 KiB. Falsifier: "queued
bytes grow with RTT ⇒ frames are being held at egress and the induction
in #80 is wrong."

Measured, from `trans::wait_bytes` sampled at every egress:

| leg | mean | p90 | max | max in FRAMES |
|---|---|---|---|---|
| x018 lan | 5 KiB | 0 KiB | 3 620 KiB | 1.07 |
| x019 wan40 | 4 906 KiB | 5 613 KiB | 6 822 KiB | **1.95** |

The bytes plainly do grow with RTT — **~4.9 MB sitting behind egress at
40 ms**. So the prediction as written is false.

**The falsifier does not fire.** A 4K AVC444 frame measures **3 386 KiB
on the wire** (measured here; #80's filing predicted "~3.4 MB"), so
6 822 KiB is 1.95 frames against a bound of C + 2 = 3 frames, and
**0.0 %** of egress samples exceed 3 frames' worth on either leg. P4
confirms the same bound independently from the send records. Nothing is
held beyond the window; the window is simply denominated in frames, and a
frame at 4K is 3.3 MB.

**The real finding, which the prediction's threshold hid: each unit of C
costs about 3.3 MB of potential transport queue at 4K.** That is the
"queue in front of the display" FR-ACK-3 objects to, now measured rather
than argued — and it is the number the shipped default has to be chosen
against.

The queue's cost is visible end to end and the arithmetic closes:
4.9 MB draining at 278 × 3.49 MB / 16.6 s = 58 MB/s is **84 ms**, and the
server measured egress→client-ack at **122.9 ms** against a 40.4 ms link.
40 + 84 = 124.

## P4 — the wire bound: MET, live

`id_server − id_client` at send, per record:

| leg | histogram | worst | bound |
|---|---|---|---|
| x018 lan | 0:2396 1:1252 2:4 | 2 | 3 |
| x019 wan40 | 0:4 1:4 2:1104 | 2 | 3 |
| x017 d40 (old, proxy) | 0:556 1:552 2:552 | 2 | (no window enforced) |

CI asserts this over the whole reachable state space; it now also holds
on real hardware at 40 ms. Note the shape difference: the old build at
D = 40 split evenly across 0/1/2 because the window was not being
enforced at all (BACKLOG #80: "two thirds of sends exceeded the bound"),
while the frontier at 40 ms sits at 2 for 99 % of sends — the window is
saturated and holding.

## P5 — mechanism check: MET

Every `ackslot` and `ackregion` record on both new legs reads C = 1.
RTT verified by measurement through each arm's own RDP port before the
leg (40.490 ms) and again after it (40.428 ms), so a qdisc that fell off
mid-run could not hide.

Ack traffic, which is the change itself: x018 emits 601 slot acks and
911 region acks in 20 s where x017 emitted 559 and 558.

## The rate at 40 ms, and what it does NOT say

x019 ran at **16.8 frames/s, period 58.6 ms**, against x018's 54.1 /s.
The period model closes: capture k needs the client to have acked k−3, so
3P = L + ack latency; 3 × 58.6 = 175.8 against 43.7 + 122.9 = 166.6,
within 6 %.

**There is no old-build leg under netem, so this is not an A/B at 40 ms.**
x017's D = 40 leg used the ack-delay proxy, which delayed one direction
above TLS; netem delays both directions below TCP. Quality gate 5 forbids
reading those against each other, and the analysis output labels that row
`x017-d40prox` for exactly this reason. A true old-vs-new comparison at
40 ms needs one more leg (x017 behind netem 40, ~2 min).

## Still not known after this run

* **The shipped default C is still unchosen.** Two RTT points do not make
  the RTT → C table FR-FLOW-1 clause 4 asks for, and the run measured C = 1
  only. What it did produce is the missing unit of account: 3.3 MB of
  transport queue per unit of C at 4K, against (C+2)/RTT of frame rate.
* **The freeze leg has not run.** #80 step 4 also calls for a client that
  stops acking; that tests whether production stops within C + 2 frames
  and `wait_bytes` plateaus. Not part of the approved two.
* **The per-monitor bound is untested.** Everything here is one monitor.
  At M monitors the wire bound is C + 2·M and the queue term scales with
  it — see BACKLOG #80's note.
