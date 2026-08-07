# Breaking change — the credit frontier becomes the default acknowledgement mechanism

Commit `2963e3b4`, dev branch. Owner directive 2026-08-07.

Audience: an upstream maintainer reviewing the pull request, and an
operator upgrading a deployment. It assumes no knowledge of this
codebase and no involvement in the experiments.

---

## 1. What changes if you upgrade and edit nothing

One default flips. In `gfx.toml`, under `[avc444_ffmpeg]`, the key
`eager_slot_ack` changes from `false` to `true`. The companion key
`wire_window` keeps its existing default of `2`.

The behaviour that follows, where the feature is active, is this: **the
X server is allowed to start capturing the next frame earlier than it
used to — as soon as the encoder has finished reading the previous
frame's pixels, instead of waiting until that frame's last byte has been
pushed onto the network.** The price is that **one more frame may be
unacknowledged by the client at any instant: at most four instead of at
most three, per monitor.**

Two things must be true before any of that happens in your install, and
in a stock install neither is:

| condition | stock value | where |
|---|---|---|
| `h264_encoder = "ffmpeg"` (the external-ffmpeg AVC444/AVC420 backend) | `"x264"` — this whole code path is not entered | `xrdp/gfx.toml`, `xrdp/xrdp_mm.c:1349` |
| `[avc444_ffmpeg] aux_ltr_chain = true` (experimental) | `false` — `eager_slot_ack` is ANDed with it and forced off, with a warning in the log | `xrdp/xrdp_tconfig.c:422`, `xrdp/xrdp_mm.c:1454-1468` |

So a stock upgrade changes nothing observable. The flip is a decision
about what the *right* default is for the moment the experimental
feature above ships; it is not a change that reaches existing
deployments today. Section 5 says this again, less kindly, because it is
the most important limit on the whole change.

It is filed as a breaking change for two reasons even so. Where the
feature *is* active it alters the shipped flow-control contract — one
more frame may be on the wire, which is a property an operator may have
sized a link around — and getting the previous behaviour back requires
an explicit line in `gfx.toml` (section 6), not merely leaving the file
alone.

The honest one-line summary: **this is an extension of the old
mechanism, with the equivalent setting proven equivalent by
measurement. It is not a free speed-up.**

---

## 2. The two rules, in terms of what they can tell the X server

### The one thing the X server is waiting for

xrdp does not pull frames from the X server. The X server (xorgxrdp)
captures a frame into a shared buffer, and it captures the *next* one
only when xrdp has told it a frame id whose buffer it may overwrite.
That single message is the admission token for the entire pipeline —
capture, encode, and transmit are all paced by it. Per monitor the X
server holds two such buffers ("capture slots"), so it may run at most
two frame ids ahead of whatever id xrdp last named.

The whole of this change is about **which number xrdp puts in that
message.**

### The old rule

The number is *the id of the last frame that has been handed to the
transport*, and the message is sent only while the client is not more
than `frames_in_flight` frames behind that id.

The crux is the first half, not the second. That number can only ever
name a frame that has already left. There is no state of the machine in
which the old rule can say "the pixels of the frame currently inside the
encoder are free" — **it has no variable that holds that fact.** The
consequence is that the X server is always pinned behind a frame that
has already been sent, no matter how the window is sized.

### The new rule

The number is the smallest of three, each of which is a fact about
exactly one layer:

| term | plain meaning | why it is a bound |
|---|---|---|
| frames whose pixels the encoder children have finished reading | the borrowed capture buffer is genuinely free | naming anything above this would hand back pages a child is still reading |
| the last frame handed to the transport, **plus one** | one frame of pipeline inventory, no more | the stage downstream of capture may be one frame behind; beyond that the X server waits |
| the last frame the client acknowledged, **plus `wire_window`** | how far the far end may fall behind | the end-to-end limit on how much video is in flight |

The middle term is the new capability. It advances when the encoder is
*done with the input pixels*, which on the measured rig happens roughly
8–10 ms before that frame's bytes reach the network — the remainder of
the encode, plus about 1.3 ms rewriting the H.264 reference marking and
about 0.3 ms assembling the EGFX PDUs. That 8–10 ms is not an estimate:
under the old rule it is exactly the wait that was measured (p50 8.8 ms,
p90 10.5 ms — section 3, claim A). It is dead time, during which a free
capture buffer exists and nobody is allowed to use it.

Code: the arithmetic is `xrdp_gfx_credit_frontier()`,
`xrdp/xrdp_encoder.h:114-130`. The old rule is
`xrdp_mm_emit_legacy_frame_ack()`, `xrdp/xrdp_mm.c:1764-1796`, whose
value `frame_id_server` is advanced at `xrdp/xrdp_mm.c:4423` — the point
at which a frame's last byte goes to the transport. Both are still
present; `xrdp/xrdp_mm.c:1815-1822` dispatches between them on the flag,
and neither is removed.

### A worked example: one frame that can be captured under the new rule and cannot under the old

One monitor. Steady state on the measured rig. The state of the world at
one instant:

* frame 7 is inside the encoder; the two ffmpeg children have **just
  finished reading its input pixels**, so frame 7's capture buffer is
  free right now;
* frame 7's encoded bytes will not reach the network for another
  ~8–10 ms;
* frame 6 is the last frame that reached the network;
* frame 5 is the last frame the client has acknowledged. (The client
  running one behind the last frame sent is the state actually observed
  on this rig, and it is why the setting comparison below comes out the
  way it does.)

| rule | number xrdp sends | highest frame the X server may now capture (that number + 2 slots) |
|---|---|---|
| old, `frames_in_flight = 2` | 6 — the last frame *sent*; frame 7 does not count, it has not left | 8 |
| new, `wire_window = 2` | min(7, 6+1, 5+2) = **7** | **9** |
| new, `wire_window = 1` | min(7, 6+1, 5+1) = 6 | 8 |

**Frame 9 is the frame in question.** Under the new default its capture
starts immediately, overlapping the tail of frame 7's encode. Under the
old rule it cannot start until frame 7 has been handed to the transport
8–10 ms later, because only then does the number become 7.

The third row is the equivalence claim: at `wire_window = 1` the new
rule's client term caps at "last acknowledged + 1", which is exactly the
old rule's ceiling, and the new rule then produces the old rule's number.
That is asserted in code review *and* measured — section 3, claim A.

### Why widening the old knob cannot reach the same place

`XRDP_GFX_FRAMES_IN_FLIGHT` (environment variable, range 1–16, default
2) controls only *how far the last-sent id may run ahead of the client's
acknowledgement before the message stops being sent at all*. It changes
how often the old rule speaks. It does not change what the old rule is
able to say, because the value put on the wire is still the last frame
that left. Section 3, claim C, is the measurement of that prediction,
pre-registered before the run.

### The resulting limit on frames in flight

The bound both mechanisms obey, at the instant a frame is handed to the
transport, with `M` monitors:

| configuration | ceiling on the number xrdp sends | frames unacknowledged at send | measured maximum, 1 monitor |
|---|---|---|---|
| old, `frames_in_flight = 2` (previous default) | client + 1 | client + 3 | 3 |
| new, `wire_window = 1` | client + 1 | client + 3 | 3 |
| **new, `wire_window = 2` (new default)** | client + 2 | **client + 4** | **4** |
| old, `frames_in_flight = 3` | client + 2 | client + 4 | 4 |

In general: old rule behaves as a window of `frames_in_flight − 1`; new
rule's bound is `wire_window + 2·M`. The two capture slots per monitor
are the X server's own budget, not xrdp's. So **the legacy-equivalent
setting of the new knob is `wire_window = 1`, not 2** — the shipped
default deliberately permits one more frame than the old default did.
That is the cost, and it has its own section.

---

## 3. The evidence

All measurements: one monitor at 3840×2400 (9.22 Mpx), the `textflood`
payload, the same server build
(`xrdp-dev 0.10.80+git20260803024109.1d5bc0960db8`), the same X-server
build (`xorgxrdp-dev 1:0.10.80+git20260731212221.10fa3aa23033`), the
same container image, and the same test client on the same host
(loopback). Each configuration is a separate container on its own port;
legs are 20 s and interleaved so each condition appears in both halves
of a sitting, because this host has been observed changing its encoder
speed between adjacent legs.

Terms used in every table below, in plain words:

* **wait for permission to capture** — the interval from the encoder's
  ffmpeg children finishing with a frame's input pixels to xrdp actually
  telling the X server it may capture again. This is the quantity the
  change targets. Paired per frame identity, never by time window.
* **cycles waiting > 10 ms** — the share of frames whose wait above
  exceeded 10 ms, as a percentage of that leg's frames; raw counts are
  given where the capture record reports them.
* **frame period** — the interval between one frame's last byte reaching
  the transport and the next frame's.
* **encoder wait** — the time the encoder worker spends blocked on the
  two ffmpeg children. This is the *control*: if it moves between two
  legs, those legs are not comparable and nothing else in the row means
  anything.

**A caveat that applies to every table in this section.** Each run
prints the ratio of the pipeline's mean frame interval to the test
payload's own mean frame interval. On these legs it is 1.05×–1.18×
(one leg 1.70×), all below the 2.0× floor this project requires before
a run may claim that *two pipeline stages overlapped*. Under that rule,
throughput and regression comparisons between arms sharing this payload,
geometry and client remain valid — that is what the tables below are —
but the "encoder worker idle" figures, quoted here only for
completeness, are inside the void: with the payload this close to the
pipeline, a worker with nothing to encode cannot be distinguished from a
worker whose next frame simply had not been produced yet.

### Claim A — configured to match the old behaviour, it costs nothing

Capture: `PR-demo/mac_bisect_matrix/captures/i80_c1_nonregression_20260807_141752_s20`
(legs `leg_L1`, `leg_F1`; per-leg gate output in each `VERDICT.txt`, full
analysis in the capture's `README.md`).

Two containers, one differing configuration line: the old mechanism at
`frames_in_flight = 2` against the new mechanism at `wire_window = 1`.

| condition | encoder wait (control) | frame period p50 | frame period p90 | wait for permission, p90 | cycles waiting > 10 ms | worker idle p90 (inside the void) |
|---|---|---|---|---|---|---|
| old rule, window 2 | 16.26 ms | 17.64 ms | 26.05 ms | 10.502 ms | 16.4 % | 8.671 ms |
| new rule, `wire_window = 1` | 16.33 ms | 17.65 ms | 26.40 ms | 10.697 ms | 16.3 % | 8.943 ms |

The control moved by 0.07 ms, so the legs are comparable. Every other
column matches within this host's leg-to-leg spread. **The new mechanism
at the equivalent window is not merely non-regressive; it is
behaviourally the same code path's outcome, defect included** — the wait
is still ~10.6 ms at p90 and still occurs on about one cycle in six.

The mechanism check that licenses reading the table at all: the new arm
emitted 579 capture-credit records reporting a window of 1, the old arm
emitted 0 of them (it has no such message). The knob applied.

The independent check on the bound: the distance between a frame and the
client's last acknowledgement, counted once per frame handed to the
transport. Old arm: 724 at distance 1, 175 at 2, 1 at 3 (900 frames).
New arm at window 1: 579 at distance 1, 320 at 2, 1 at 3 (900 frames).
**Maximum 3 on both**, as section 2's table predicts.

### Claim B — at the shipped window it buys something real

Captures: `PR-demo/mac_bisect_matrix/captures/i80_widen_legacy_20260807_143617_s20`
(legs `leg_T1`, `leg_T2`) and
`PR-demo/mac_bisect_matrix/captures/i87_eager_ab_20260806_180910_s20`
(legs `leg_b1`, `leg_b2`, against controls `leg_a1`, `leg_a2`).

New mechanism at the shipped `wire_window = 2`, against the old rule —
at its old default window of 2 in the 2026-08-06 capture, and at the
widened window of 3 in the 2026-08-07 one (that arm is claim C's, and is
the like-for-like cost comparison). Same payload, geometry and client
throughout. Read the 2026-08-06 rows only against each other: the host's
encoder stepped speed between that capture's two pairs of legs, so only
within-pair comparisons are valid there.

| capture, leg | condition | encoder wait (control) | frame period p50 | frame period p90 | wait for permission, p90 | cycles waiting > 10 ms |
|---|---|---|---|---|---|---|
| 2026-08-06, `leg_a1` | old rule, window 2 | 16.26 ms | 17.717 ms | 26.585 ms | 10.582 ms | 146 of 886 = 16.5 % |
| 2026-08-06, `leg_b1` | **new, window 2** | 16.46 ms | 17.445 ms | **19.280 ms** | **0.040 ms** | **8 of 951 = 0.8 %** |
| 2026-08-07, `leg_W1` | old rule, window 3 | 16.43 ms | 17.85 ms | 26.46 ms | 10.570 ms | 17.6 % |
| 2026-08-07, `leg_T1` | **new, window 2** | 16.24 ms | 17.42 ms | **19.55 ms** | **0.048 ms** | **3.9 %** |
| 2026-08-07, `leg_W2` | old rule, window 3 | 16.53 ms | 17.91 ms | 26.51 ms | 10.388 ms | 15.6 % |
| 2026-08-07, `leg_T2` | **new, window 2** | 16.19 ms | 17.38 ms | **19.23 ms** | **0.040 ms** | **2.6 %** |

The encoder wait is 16.19–16.53 ms on all six legs, so none of the
differences is the host changing underneath the measurement.

What moved: the wait for permission to capture falls from ~10.5 ms at
p90 to ~0.04 ms, and the share of cycles waiting more than 10 ms falls
from 15.6–17.6 % to 0.8–3.9 %. What follows from it: the frame period's
p90 — the slow frames, which are what a user perceives as
stutter — falls from about 26.5 ms to 19.2–19.6 ms, roughly 7 ms. The
p50 barely moves (17.7–17.9 → 17.4 ms), which is expected: the median
frame was never the one that stalled.

Two honesty notes on this claim, both from the capture records
themselves. First, the stall share on the new arm is 0.8 %/0.0 % on
2026-08-06 and 3.9 %/2.6 % on 2026-08-07 at the same setting — same
direction and order of magnitude, difference unexplained, small against
the 16 % it replaces. Second, in a second pair of legs on 2026-08-06 the
host's encoder slowed to 26.2 ms per frame and the two arms became
indistinguishable (27.42 ms against 27.39 ms mean period). That is a
check on the result, not a disappointment: when the encoder itself needs
26 ms, permission to capture is never the late thing, so the
acknowledgement rule cannot matter.

### Claim C — widening the old knob does not get there

Capture: `PR-demo/mac_bisect_matrix/captures/i80_widen_legacy_20260807_143617_s20`.
Its `PREDICTION.md` was written and committed **before** the run, and
states what result would falsify the change's justification.

The obvious reviewer objection is that the old mechanism already has a
window knob, so raising it to 3 should buy the same thing. It does not.
The old rule at `frames_in_flight = 3` permits the same number of frames
outstanding as the new rule at `wire_window = 2` — client + 4, verified
on the wire — and is compared against it in the same interleaved
sitting. The `leg_W*` and `leg_T*` rows of the Claim B table are that
comparison; restated as the objection's answer:

| | old rule, window 3 | new rule, window 2 |
|---|---|---|
| frames outstanding permitted | client + 4 | client + 4 |
| maximum outstanding observed | 4 | 4 |
| wait for permission, p90 | 10.570 / 10.388 ms | 0.048 / 0.040 ms |
| cycles waiting > 10 ms | 17.6 % / 15.6 % | 3.9 % / 2.6 % |
| frame period p90 | 26.46 / 26.51 ms | 19.55 / 19.23 ms |

**Same queueing cost, none of the benefit.** Widening the old window
also shifted its outstanding-frame distribution outward (19 frames at
distance 3 in `leg_W1`, against 1 at window 2) — it pays in full and
returns nothing. This is the measurement that makes the new mechanism
load-bearing rather than a rewrite of something that already worked.

---

## 4. The cost, stated plainly

**At the shipped default, one more frame may be unacknowledged by the
client than under the old default: four instead of three, per monitor,
at the instant a frame is handed to the transport.**

The legacy-equivalent setting is `wire_window = 1`, not 2. Shipping 2
is a deliberate choice to spend one frame of queue on the concurrency
measured in claim B.

What one extra outstanding frame means:

* **On a link where acknowledgements are fast relative to the encoder**
  — a LAN, loopback, anything where the round trip is well under a frame
  period — the window is never the binding constraint, and the extra
  frame is theoretical. Across the four legs of the 2026-08-06 capture,
  3057 of 3073 frames (99.5 %) were only 1 or 2 frames ahead of the
  client's last acknowledgement, well inside a permitted 3 (old rule)
  or 4 (new rule).
* **On a slow link the window is the binding constraint**, and it works
  in both directions. Throughput ceiling: the frame rate cannot exceed
  (frames outstanding) per round trip, so the change raises the ceiling
  from 3 to 4 frames per round trip — about 37.5 → 50 frames/s at 80 ms
  RTT, 15 → 20 frames/s at 200 ms. Latency: one more frame of video may
  be in flight or queued in the server, so on a link narrow enough that
  a frame takes real time to push, the delay between an action and
  seeing its result can grow by roughly the time it takes to transmit
  one frame. **This is derived from the bound, not measured** — every
  measurement in this document was taken over loopback.
* Note what does *not* happen when the window is reached: the server
  does not queue frames it could not send. It stops capturing, and the
  damage those frames would have carried is merged into the next frame
  it does capture. A slow link gets fewer frames, each of them current,
  rather than a backlog of stale ones.

**And the alternative charges the same price.** Raising
`XRDP_GFX_FRAMES_IN_FLIGHT` from 2 to 3 permits exactly the same four
outstanding frames, with the same latency implication, and measurably
buys nothing (claim C). If you were ever going to accept one more frame
of queue, this is the change that gives you something for it.

---

## 5. Limits — what has not been shown

These are limits of the evidence, not caveats about the code. A
skeptical reviewer should treat the change as unproven on each of these
points.

1. **At two monitors the benefit is halved, not delivered — measured
   2026-08-07, after the rest of this document was written.** The bound
   behaves exactly as the per-monitor formula (`wire_window + 2·M`)
   predicts: measured maxima of 6 for the new default and 5 for the old
   at two monitors, never exceeded in four legs each, so the new
   mechanism costs one extra frame there too rather than one per
   monitor. But the producer stall, which the new mechanism takes to
   under 1 % of cycles at one monitor, only falls from about 52 % to
   about 22 % at two. The likely cause is filed but not measured: the
   window is a single session-wide number while the capture budget is
   per monitor, so a window of 2 across two screens is about one frame
   each. **Read every stall and latency claim in this document as a
   one-monitor claim.** Captures
   `PR-demo/mac_bisect_matrix/captures/i80_multimon_strip_20260807_152223_s20`
   and `.../i80_multimon_20260807_151836_s20`. Nothing has been measured
   at three or more monitors, where the formula gives 2 + 2×3 = 8
   unacknowledged frames for the new default against 1 + 2×3 = 7 for
   the old.
2. **The test client acknowledges a frame before decoding it.** It saves
   the bytes and acks; it does not render. Every acknowledgement latency
   in this document is therefore a floor. A real client acks later, so
   the client term binds *harder* in the field than it does here. That
   cuts both ways honestly: it strengthens claim A's finding that
   `wire_window = 1` stalls, and it weakens any claim that
   `wire_window = 2` is sufficient on a real link.
3. **The flip is currently inert in a stock install** (section 1). It is
   ANDed with the experimental `aux_ltr_chain`, which is default-off,
   and the whole path requires `h264_encoder = "ffmpeg"`. Nothing in a
   default deployment changes today. The measurements above were taken
   with both of those enabled, which is not a configuration anyone runs
   by default.
4. **Loopback only.** No measurement over a link with meaningful round
   trip or bandwidth limit. The section 4 latency discussion is
   arithmetic from the bound.
5. **The benefit at the shipped window rests on a small number of
   legs** — two sittings, four treatment legs of 20 s each. The
   direction reproduces in all four; the magnitude does not settle down
   (share of cycles waiting more than 10 ms: 0.8 % and 0.0 % on
   2026-08-06, 3.9 % and 2.6 % on 2026-08-07, same setting).
6. **One older record disagrees and is not fully explained.** A
   2026-07-31 run measured a *wider* window performing worse (98.1 ms
   frame period at window 4 against 87.0 ms at window 2). That run
   predates a fix to the tracing itself and is treated as suspect; the
   2026-08-07 capture does not reproduce a collapse at window 3, but it
   did not test window 4.
7. **Requires the matching X-server build.** The early acknowledgement
   uses a wire flag (`XUP_ACK_FLAGS_SLOT_ONLY`) that an older xorgxrdp
   would ignore, retiring a screen region it must still hold. That is
   not silently tolerated: the two daemons compare a contract version
   for exact equality and a mismatched pair is refused at connect.

---

## 6. Operator: how to choose, and how to change it back

Edit `[avc444_ffmpeg]` in `gfx.toml` (normally `/etc/xrdp/gfx.toml`).
**Changes to that table take effect at the next fresh login
(logoff → login), not on disconnect/reconnect to an existing session.**

**To restore the pre-upgrade behaviour exactly — the old code path,
bit for bit:**

```toml
[avc444_ffmpeg]
eager_slot_ack = false
```

The old mechanism is still in the binary and is still selected by this
flag; nothing else needs changing. `wire_window` then has no effect —
only the new mechanism's emission consults it.

**To keep the new code path but match the old behaviour's flow control
— same number of frames in flight, same bound:**

```toml
[avc444_ffmpeg]
eager_slot_ack = true
wire_window = 1
```

**Which one do you want?**

* If your goal is "do not change anything until I have tested it", use
  `eager_slot_ack = false`. It selects the code that shipped before, so
  a problem cannot be in the new mechanism at all. This is the right
  setting for a rollback and for bisecting a regression.
* If your goal is "I accept the new mechanism but not the extra frame in
  flight" — a high-latency or bandwidth-constrained link where one more
  frame of queue matters more than the tail improvement — use
  `wire_window = 1`. Claim A is the evidence that this is genuinely
  equivalent, so it is a supported configuration rather than a
  workaround, but note it also reproduces the old behaviour's stall:
  ~10.7 ms at p90 waiting for permission to capture, on ~16 % of cycles.
  You are giving up the entire benefit.
* Otherwise leave the default. On a link that acknowledges a frame
  faster than the server can produce one, the window is never the limit
  and the extra frame never materialises.

`wire_window` accepts 1–64. A value outside that range is refused with a
warning in the log and the default is kept — it is never silently
clamped. Raise it above 2 only as far as the round trip demands: every
extra frame is more latency and more memory queued in the server.

The environment variable `XRDP_GFX_FRAMES_IN_FLIGHT` continues to apply
to the old mechanism only. It is ignored when `eager_slot_ack` is on.

---

## 7. For the reviewer

### What to look at in the diff

* `xrdp/xrdp_tconfig.c:430-443` — the one-line default change, with its
  rationale and the capture paths in the comment.
* `xrdp/xrdp_encoder.h:114-130` — `xrdp_gfx_credit_frontier()`, the
  three-term arithmetic. The load-bearing question for review is whether
  each term consults only its own layer, and whether the derived wire
  bound `wire_window + 2·M` follows.
* `xrdp/xrdp_encoder.h:36-44` — `xrdp_gfx_ack_window_open()`, the old
  rule's gate, and `xrdp/xrdp_mm.c:1764-1796`, the old rule's emission,
  both unchanged and still reachable.
* `xrdp/xrdp_mm.c:1815-1822` — the dispatch between the two, which is
  the only place the flag decides anything at runtime.
* `xrdp/xrdp_mm.c:1454-1468` — the AND with `aux_ltr_chain`, the refusal
  to enable without it, and the warning that says so.
* `tests/xrdp/test_tconfig.c::test_tconfig_gfx_avc444_defaults` — the
  new assertions. They pin `eager_slot_ack = 1` **and**
  `wire_window = 2` together, because the mechanism alone does not
  describe what ships (at window 1 it is the old behaviour), plus
  `emit_thread = 0` beside them so the shipped set is stated in one
  place. The expected values are the owner's directive, not a reading of
  the loader.
* `tests/xrdp/test_avc444_credit_frontier.c` — 10 test cases, 64
  `ck_assert` checks, on the frontier itself: each term binding in isolation, monotonicity, the
  region acknowledgement obeying the same window, a joint state-machine
  enumeration, and two recorded wedge replays at `wire_window` 1 and 2.
* `docs/man/gfx.toml.5.in` — the `eager_slot_ack` and `wire_window`
  entries. Check that the stated defaults match `xrdp_tconfig.c` and
  that the legacy-equivalent setting is documented as
  `wire_window = 1`.

### What is deliberately NOT part of this change

* **No new mechanism.** Both acknowledgement paths already existed in
  the tree; this commit changes which one a config-less session selects.
* **`wire_window`'s default is unchanged at 2.** It is quoted throughout
  because it is half of what ships, not because it moved.
* **The emit thread stays off.** A separate thread for assembling the
  EGFX PDUs exists behind `emit_thread` and is *not* part of the shipped
  default; measured, the assembly stage is cheaper inline than on its
  own thread (0.230 ms against 0.313 ms mean, capture
  `i80_c1_nonregression_20260807_141752_s20`, legs `leg_F1`/`leg_E1`).
  The new test asserts it off so a future change cannot enable it
  silently.
* **No change to the wire protocol.** The slot-only acknowledgement flag
  and its contract-version bump landed earlier; this commit does not
  touch `common/xup_client_info.h`.
* **No change to the encoder, the bitstream, or the LTR rewrite.** This
  is purely about when the X server is told it may capture.
* **No multi-monitor claim.** See limit 1.

### The question this change has to survive, and where its answer is

*"Why not just raise `XRDP_GFX_FRAMES_IN_FLIGHT`?"* — section 3 claim C,
capture `i80_widen_legacy_20260807_143617_s20`, whose `PREDICTION.md`
was committed before the run and names the result that would have sunk
the change.
