# #91 — the multi-monitor window, and the shared pump

**Closed 2026-08-08.** Question as filed: at two or more monitors the
per-screen frame rate falls and the stall rate roughly doubles — is that
the credit window, and is something serialising the two screens' encode
work?

Three answers, and they are independent of each other:

1. **The window divides by monitor count**, because a frame id is one
   monitor's frame and the window is a single session-wide number. Static
   analysis, code chain below.
2. **Widening the window does not fix the stall.** Measured. The default
   is therefore *not* scaled by monitor count; the per-screen consequence
   is documented in `gfx.toml(5)` instead.
3. **Our code does not serialise the two screens.** The encodes overlap.
   What staggers them is the handover of the raw pictures to the ffmpeg
   children, which takes 11.3 ms of a 25.9 ms two-monitor pump. **What
   sets that rate is not established** — see the open question at the
   end.

Records: captures `i91_window4_m2_20260807_201346_s20`,
`i80_multimon_strip_20260807_152223_s20`,
`i91_attribution_m2_20260807_211825_s20`. Every number in the last
section is re-derivable with
`PR-demo/mac_bisect_matrix/i91_encode_overlap.py`.

---

# Part 1 — why the window divides by monitor count

Static analysis. Every claim here is a code reference or an
already-committed measurement.

## The one-sentence answer

A "frame id" is **one monitor's frame, not one screen refresh**. The
window is denominated in frame ids and is a single session-wide number,
so at M monitors a refresh consumes M ids and each monitor gets between
⌊C/M⌋ and ⌈C/M⌉ ids of headroom. The capture slots underneath it are
per monitor and do **not** divide. That is the whole asymmetry.

## The chain, verified

**The id is a scalar, and it advances once per monitor.**
`int rect_id;` is a plain field on the client connection
(`/workUpdateXorgXrdp/module/rdpClientCon.h:153`), incremented once per
*send* at `rdpClientCon.c:3501`, `:3548` and `:3621` — and a send is one
monitor's capture, issued from the per-monitor loop in
`rdpDeferredUpdateCallback`. The driver says so in its own words at
`rdpClientCon.h:120-121`:

> rect_id advances by the monitor count between one monitor's
> consecutive sends, which pinned each monitor to a single slot

That comment exists because deriving the capture slot from `rect_id`
parity failed at even monitor counts — the same fact, discovered
earlier, for a different reason.

**That id is echoed unchanged all the way to the client and back.** xrdp
reads it out of the producer's blob, re-emits the same value in the EGFX
frame markers, assigns it verbatim to `frame_id_server`, and the
client's frame acknowledgement lands on the same axis in
`frame_id_client`. So all three terms of the credit frontier —
`frame_id_consumed`, `frame_id_server`, `frame_id_client` — are counted
in monitor-frames.

**The credit that goes back carries no monitor index.** The ack is
message 106 and its entire body is two words
(`/work/xup/xup.c:1255-1257`):

```
out_uint16_le(s, 106);
out_uint32_le(s, flags);
out_uint32_le(s, frame_id);
```

The `flags` word holds two bits, neither of them a monitor. The paint
direction, by contrast, **does** carry a monitor index — in the top
nibble of its own flags word (`rdpClientCon.c:3688`,
`mon = (id->flags >> 28) & 0xF`). So the producer tells the consumer
which screen a frame belongs to, and the consumer's answer cannot say
which screen it is about.

**The producer retires per-monitor rings against that one global
number.** `xup_cap_budget_has_capacity(budget, mon, rect_id_ack, cap)`
(`/work/common/xup_client_info.h:482-491`) indexes a per-monitor ring
`ids[mon][...]` but takes the single session-global `rect_id_ack` as the
frontier every monitor is retired against.

## The arithmetic, and how it matches what was measured

Between the client's acknowledged position and the credit ceiling there
are **C ids**, dealt round-robin across M monitors.

| | ids of headroom above the client's ack | belonging to one monitor | in whole refreshes |
|---|---|---|---|
| M = 1, C = 2 | 2 | 2 | 2.0 |
| M = 2, C = 2 | 2 | 1 | 1.0 |
| M = 3, C = 2 | 2 | 0 or 1 | 0.67 |

So **C = 2 at two monitors gives each screen the same headroom as
C = 1 at one monitor.** That is a configuration we have measured
directly, and the stall rates line up:

| configuration | headroom per monitor | cycles stalled > 10 ms |
|---|---|---|
| C = 1, M = 1 | 1 | 16.3 % |
| C = 2, M = 2 | 1 | 21.5 / 22.0 % |
| C = 2, M = 1 | 2 | 0.8 % |

The two one-headroom configurations sit in the same regime and the
two-headroom one does not. This is consistency between an arithmetic
prediction and two independently measured runs; on its own it was **not**
proof that the window is the binding term — Part 2 tested that directly.

## Why the two knobs disagree — one was decided, the other inherited

**The capture budget is per monitor because a global pool was measured
failing.** Decision D13,
`docs/experiments/45-intra-refresh-and-pump-set.md:344`:

> Capture budget is ≤ 2 outstanding PER MONITOR — never a global pool of
> 2m. A pool lets one damaged monitor take all of it: 4-deep on 2 slots
> at m = 2, i.e. bufferbloat, +2 frames of latency, and slot aliasing.

The measurement behind it (2026-07-29, two monitors, 1100 sends): the
same capture slot was reused on **1079 of 1079** consecutive full-pass
sends, one monitor's second slot was **never written** for the whole
run, and 543 of 1100 sends went out with the global budget already at
its cap. The prohibition is now restated in the header both daemons
compile, and the slot count is fixed at 2 in the versioned contract —
explicitly *not* a tuning knob.

**The window is session-wide because it always was.** PRD's own
provenance trace dates it to a 2017 commit whose parameter was the value
the *client* advertises in the Frame Acknowledge capability — a single
per-connection integer with no monitor dimension, from a protocol
capability that has none. The GFX path then **cut that tether**: the
client's advertised value is no longer read, and the number became a
local constant. Nothing re-derived what it should bound when that
happened, and monitor count was never part of the discussion.
`wire_window` inherited that scope unexamined: a scalar in every struct
it appears in, one TOML integer, one value per session.

**So one mechanism is per monitor on measured grounds and the other is
global by inheritance from a number this project has documented as
having no protocol meaning.** That is the inconsistency, stated exactly.
It was known before `wire_window` existed — there is a CI test pinning
the present behaviour with the fixed behaviour written beside it,
`test_overlap_m2_global_window_pins_each_monitor_to_one`, which asserts
depth 1 per monitor today and carries `drive_pipeline(2, 2, 2*2, 2, 32)
>= 2` as the assertion a window scaled by M would satisfy.

---

# Part 2 — widening the window does not fix it

Two models built during the analysis disagreed about which term binds at
M ≥ 2: one had `frame_id_client + C` binding on 99 % of ticks with
C = 2·M moving it off the network; the other had `frame_id_server + 1`
binding first, so that raising C "buys inventory, never depth". Neither
was a measurement, and the fleet capture cannot attribute the term,
because at the instant a credit is emitted all three terms are equal.

**One leg settled it: `wire_window = 4` at two monitors**, which is
numerically identical to "scale the default by monitor count" at M = 2,
so it priced the proposed fix and tested the diagnosis at once.

| condition | cycles arming all four children | cycles stalled |
|---|---|---|
| legacy path | 32.5 % | — |
| frontier, `wire_window` 2 | 39.6 / 39.8 / 41.1 % | 21.5 / 22.0 % |
| frontier, `wire_window` 4 | 52.0 / 53.1 % | still ~halved only |

Batching is monotone in the credit — more credit does get both screens
into the same pump more often — and the typical wait did **not** move to
the one-monitor figure. So the window is not the binding term at two
monitors, and scaling it by M is not the fix.

**Decision (owner, 2026-08-07): default stays C = 2, with no ×M
multiplier.** The per-screen consequence is documented rather than
tuned. What went into `gfx.toml(5)`:

* the bound is `wire_window + 2 × monitors`, and a "frame" there is one
  monitor's frame — the page previously said "at most `wire_window` + 2
  frames per monitor", which parses two ways and over-provisions by a
  third at two monitors on the more likely reading;
* the frame-rate guidance is stated with the monitor count in it —
  it silently assumed one monitor ("at 80 ms RTT the default gives about
  50 frames per second" is `C + 2` over the round trip, with no M);
* an explicit warning that adding a monitor divides the per-screen
  headroom with no config change and nothing in the log to see;
* an explicit statement that the knob does not cure multi-monitor
  stuttering, since that is what an admin will reach for it to do.

The set-membership cost that makes this matter: a pump covering both
monitors takes **~19.9 ms** against **~12.2 ms** for one, so a second
monitor joining an existing pump costs ~7.7 ms at the margin while
missing it costs that monitor a further ~12.2 ms cycle of its own.
Per-monitor service interval, from `absorb` records paired by monitor:
p50 ~22 ms, p90 36–46 ms.

**One more knob a reviewer will notice:** `XRDP_GFX_FRAMES_IN_FLIGHT` is
still live on the legacy path — an environment variable, range 1–16,
undocumented in any man page. We have the measurement showing that
raising it pays the full queue cost and returns nothing, so the question
"why do two window controls exist" can be answered today without new
evidence.

---

# Part 3 — the encodes overlap; the input transfer staggers them

Part 2 left a set-membership question — how often a screen misses the
pump, and why — and one unknown the trace could not touch: whether the
two screens' encodes genuinely overlap inside a pump, or serialise
somewhere below us in ffmpeg, the VAAPI driver or the GPU.

## The three fields that made it answerable

All three extend existing records in the existing ring; no new tracer,
sampler or log line. Landed in `1fed64c1`.

* **Which screens are in the pump.** `pump_beg`/`pump_end` carry a
  present mask, accumulated with one bitwise-or inside the loop that
  already fills the array handed to the pump, so the mask cannot
  disagree with what was armed.
* **Which screen each per-child record belongs to.** `outfirst` and
  `feedend` carry the monitor index. It went into the encoder *config*
  rather than onto the handle, because the auxiliary child is built by
  copying its parent's config and so inherits the monitor by
  construction rather than by an assignment a future respawn could
  forget.
* **Whether the credit would have permitted an absent screen** — the
  weakest of the three. xorgxrdp has no perf ring anywhere, so the
  producer's own reason (no damage, or capture slot still busy) cannot
  be recorded; what xrdp can say is whether *its* credit left that
  screen a free slot. Four limits, recorded before the run rather than
  discovered after it: the credit is sampled when the worker begins
  waiting, which is *after* the producer already decided, so the bias
  runs toward reporting "permitted"; the per-monitor id history is fed
  only from the batching path; a monitor xrdp has never received a frame
  for is unknown rather than permitted; and the credit is read across
  threads without the mutex (one int, at most one ack stale — taking the
  encoder mutex to fill a diagnostic field would put an instrument on
  the measured path).

## Setup

Arm x029 (host port 40045), built from `1fed64c16a89`, shipped defaults
— credit frontier at `wire_window = 2`, no emit thread, `h264_vaapi` on
`/dev/dri/renderD128`. Two monitors: monitor 0 at 2560×1440 (3.69 Mpx,
5.5 MB of NV12 per view), monitor 1 at 3840×2400 (9.22 Mpx, 13.8 MB),
confirmed from the client's own `xrandr --listmonitors`. Strip-render
payload. Two legs, same config, 20 s each, back to back. Certified at
deploy: 7 checks clean, 0 black frames.

**The two legs landed in different host states and that is why two were
run** — leg A's pumps average 36.05 ms and leg B's 26.14 ms for the same
work. Every conclusion below holds in both, and no number is pooled
across them.

## What the pump is

Each screen is encoded by **two ffmpeg child processes** — AVC444 sends
a main view and an auxiliary view, one process each — so two screens
means **four children**. A **pump** is one pass of the encoder worker
over all of them (`xrdp_ffmpeg_avc444_pump_pairs`, `pump_set`):

1. Submit puts each picture into its child's pending-input list; nothing
   is written to a pipe yet.
2. The pump builds **one `pollfd` array covering every child** — input,
   output and stderr for all four — and **one shared deadline** for the
   whole set. One deadline rather than one per child, because a
   per-child budget would cost four timeouts when a single child stalls.
3. It polls once, then services **every** child that came back ready:
   `vmsplice` more raw pixels into whichever input pipes have room, read
   whatever output is available.
4. Loop until every child has produced a picture, or the deadline fires.

There is no per-child blocking wait anywhere in that loop.

## Finding 1 — absences are not credit-limited

| leg | pumps | both screens | one screen | absent AND permitted | absent AND not permitted |
|---|---|---|---|---|---|
| A | 420 | 418 | 2 | 1 | 1 |
| B | 614 | 456 | 158 | **157 (99.4 %)** | **1 (0.6 %)** |

A screen that missed a pump had, almost always, a free capture slot as
far as our credit was concerned. The reason it did not send is on the
producer side, and this trace cannot separate "no damage" from "slot
still busy".

Read with the field's known bias (above): it pushes in the same
direction as this finding, so on its own it is suggestive rather than
conclusive. Finding 2 is what makes it safe to act on, because it does
not use this field at all.

## Finding 2 — the encodes overlap

A child cannot begin encoding until its whole raw picture has arrived:
ffmpeg's raw-video reader hands nothing to the encoder until it holds a
complete frame. So `feedend` (last byte of the picture into the pipe) to
`outfirst` (first byte of the result back) is the **encode window**, and
two overlapping windows are two encodes running at once.

| | leg A | leg B |
|---|---|---|
| the two **screens'** encode windows overlap | **412 / 418** | 94 / 456 |
| the two **views of one screen** overlap | **836 / 836** | **912 / 912** |

Two encodes always run at once — the two views of a screen, in every
single pump of both legs. Across screens, in leg A, nearly always.

Two biases in that window, stated rather than buried: `feedend` fires
when the last byte enters the pipe and the child may still hold up to
one pipe buffer (1 MiB, `F_SETPIPE_SZ`, about 0.9 ms at the transfer
rates below) unread, so the true encode start is a fraction of a
millisecond later than the window's left edge; and on a VAAPI arm the
window contains the upload of the frame from system memory to the GPU as
well as the encode itself.

## Why leg B's screens mostly miss each other, when nothing forbids them meeting

All four children are fed concurrently, each taking bytes at about
1.2–1.3 GB/s, so their inputs complete in proportion to frame size. The
big picture is 2.5× the small one, so its input lands **6.9 ms later**
(the *stagger*). The small screen's encode takes **5.9 ms**. It finishes
about a millisecond before the big screen's encode is even allowed to
begin. A near-miss produced by frame-size asymmetry — not a lock, not a
queue.

**Leg B timeline, milliseconds after `pump_beg`.** Each line is the
median of that instant across the 456 both-screen pumps, so the whole is
a composite rather than one pump that happened
(`i91_encode_overlap.py` also prints a single representative pump, which
looks the same):

```
 0.00  four children armed in one poll set; 38.7 MB of raw NV12 queued
 3.42  small screen, first view's input complete   -> its encode starts
 4.60  small screen, second view's input complete
 9.64  small screen, first output   (encode 5.9 ms = 1.61 ms/Mpx)
10.39  small screen, second output
11.31  big screen's input complete  -> its encode starts   <- 6.9 ms later
23.51  big screen, first output     (encode 13.2 ms = 1.43 ms/Mpx)
25.03  big screen, second output
25.88  pump_end
```

The timeline closes: 0.85 ms from the last output byte to `pump_end`.

## Finding 3 — the screens are not competing for the encoder

Cost per megapixel for the same screen, alone in a pump versus sharing
one, within leg B:

| screen | alone in the pump | sharing with the other | change |
|---|---|---|---|
| 2560×1440 | 1.609 ms/Mpx (n=158) | 1.644 ms/Mpx (n=912) | +2.2 % |
| 3840×2400 | 1.414 ms/Mpx (n=158) | 1.431 ms/Mpx (n=912) | +1.2 % |

A single serial engine would roughly double the per-job cost. It adds
one to two per cent. *Limit:* because leg B's screens mostly do not
overlap, this compares 2 concurrent children against 4 armed-but-
staggered ones, not against 4 truly concurrent ones. Leg A has only n=2
single-screen pumps, so the clean four-concurrent contention number is
not in this capture.

Nor is there a throughput ceiling. Cost per megapixel by which screens
were in the pump:

| leg | both screens (12.90 Mpx) | monitor 1 alone (9.22 Mpx) | monitor 0 alone (3.69 Mpx) |
|---|---|---|---|
| A | 2.79 ms/Mpx | 3.61 | 66.42 (n=1, ignore) |
| B | **2.03 ms/Mpx** | 2.43 | 3.56 |

A both-screen pump is the **most** pixel-efficient composition. If the
encoder were saturated, adding pixels would cost at least
proportionally; instead the fixed per-pump overhead is amortised.

## The counterfactual: what if encode were slower

Overlap begins when the small screen's encode outlasts the stagger.
Scaling every encode by k, the windows meet at
**k > stagger ÷ small-screen encode**:

| encode slowdown k | leg B pumps whose two screens would overlap |
|---|---|
| 1.18 | half — that is the median threshold |
| 1.5 | 436 / 456 (96 %) |
| **2.0** | **455 / 456 (100 %)** |

**And leg A is the natural experiment.** Its host state made encode
22–48 % slower per megapixel (2.007 and 2.113 ms/Mpx against leg B's
1.609 and 1.414) with the stagger essentially unchanged (5.9 vs 6.9 ms),
and its overlap fraction is 99 % instead of 21 %. A slower encoder
produced more overlap, measured, in this capture.

## The handover, and the question it leaves open

**11.3 ms of a 25.9 ms pump elapse before the last raw byte reaches a
child** — 38.7 MB of NV12 at about 3.4 GB/s aggregate, roughly
1.2–1.3 GB/s per child, four children concurrently. That is when any
encode may start, and it is the largest measured block of the pump.

**What limits that rate is NOT established, and the difference matters.**
xrdp's own side is nearly free: `feed_vmsplice` moves *page references*
into the pipe (`vmsplice`, `SPLICE_F_NONBLOCK`, no `SPLICE_F_GIFT` —
the pages belong to the capture shared memory), so xrdp does not copy
the frame. The copy happens in the child, when ffmpeg `read()`s it out
of the pipe. So the 1.2–1.3 GB/s is the rate at which **the children
take their input**, and the pump is waiting on them. Three candidates,
none tested:

1. ffmpeg's reader genuinely copies at about that rate and nothing is
   wrong;
2. the child is not reading promptly — busy elsewhere, or its input
   thread is descheduled — in which case this is **the encoder
   backpressuring us**, and the time is encode time wearing a different
   name;
3. the pipe geometry (1 MiB, `F_SETPIPE_SZ`) forces more round trips
   than necessary.

One weak piece of evidence in hand, offered as no more than that: the
per-child rate is roughly the same in both legs (1.08–1.32 GB/s) even
though leg A's encode was 22–48 % slower per megapixel, which argues
mildly against candidate 2. It is not a test of it.

**No optimisation item is opened on this.** Naming a cost is not the
same as knowing who owns it, and "the transfer is ours to make faster"
does not follow from any measurement here — under candidate 2 it would
be flatly wrong. The next step, if this is picked up, is to identify the
rate limiter, not to shrink the bytes.

---

# What is claimed, and what is not

**Claimed: our code does not serialise the two screens.** The pump arms
every child in one `pollfd` set with one shared deadline and has no
per-child blocking wait; the encode windows overlap; and what staggers
the screens is the size-proportional handover of their pictures, not any
ordering our code imposes.

**Not claimed: that the hardware is deterministic.** It schedules its
own work in an order we neither observe nor control, and **the GPU's
clocks were not pinned in either leg** (owner, 2026-08-08). That is the
uncontrolled cause behind leg A being 22–48 % slower per megapixel,
which makes leg A evidence for the *direction* of the counterfactual
rather than a calibrated 1.25× arm; and it means the alone-versus-
sharing figures (+2.2 % / +1.2 %) should be read as "nothing resembling
a 2× serialisation penalty" rather than as two-per-cent measurements.
The overlap counts themselves compare four instants inside a single pump
spanning tens of milliseconds and are immune to clock drift.

## One retraction, recorded

The first analysis of this capture concluded the opposite — *"in 874 of
874 both-screen pumps the encodes do not overlap; the encode stack runs
them one screen at a time"* — and that was reported to the owner and
committed. It is wrong.

The metric was the ORDER of the four `outfirst` instants: did one
screen's two children both emit output before the other screen's did.
That is completion order. The claim made from it was about concurrency.
A set of intervals can be ordered by their right edges and overlap
throughout, which is exactly what these do. The counts were correct; the
conclusion drawn from them was not. It is the failure mode `CLAUDE.md`
already names — *a metric that cannot express the unit the claim is made
in does not support the claim, however clean its number looks* — and it
survived a review that had explicitly flagged the metric as
non-load-bearing. Withdrawn from the same capture, no new run, one day
later.

## Limits

* Two monitors only; nothing here speaks to three.
* The GPU's clocks were not pinned (above).
* The producer's own reason for skipping a screen is unrecordable from
  this side.
* The credit-permitted field is biased toward "permitted".
* Saturation margins 1.43× and 1.90×, below the 2.0× floor, so overlap
  claims about *pipeline stages* from this run are void — the
  encode-window finding is not such a claim: it is about two encoders
  within one stage, measured by their own per-child records.
* The test client acknowledges each frame before decoding it.
* **Still unmeasured:** the per-monitor slot budget's independence,
  which was this item's original first half, was never decomposed.
* **Left open, not answered:** what limits the rate at which the ffmpeg
  children take their input — including whether it is the encoder
  backpressuring us. Stated above; not opened as work.
