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
