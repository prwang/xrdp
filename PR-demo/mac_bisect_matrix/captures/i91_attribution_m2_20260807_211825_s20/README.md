# i91_attribution_m2_20260807_211825_s20 — the two screens are armed together and encode one after the other

**The wall is real and it is not ours.** With the three attribution
fields in place, two 20 s legs at two monitors say:

1. **A monitor that misses a pump is almost never held back by our flow
   control** — 157 of 158 absences had the credit already permitting it.
2. **When both screens ARE in one pump, their encodes do not overlap.**
   In **874 of 874** both-screen pumps, every one of one screen's
   children produced its first output before any of the other screen's
   did. Zero interleaving.

So the poll set arms four children together and the encode stack runs
them one screen after the other anyway. Per the stop rule on BACKLOG
#91, this is written down and the item closes here rather than becoming
an optimisation campaign.

## Setup

Arm x029, port 40045, built from HEAD (`1fed64c16a89`) so it carries the
three fields. Shipped defaults — credit frontier at `wire_window = 2`,
no emit thread. Two monitors: monitor 0 at 2560×1440 (3.69 Mpx),
monitor 1 at 3840×2400 (9.22 Mpx), confirmed from the client's own
`xrandr --listmonitors`. Strip-render payload. Two legs, same config,
20 s each. Certified at deploy: 7 checks clean, 0 black frames.

**The two legs landed in different host states** and that is why two were
run: leg A's pumps average 36.05 ms and leg B's 26.14 ms for the same
work. The conclusions below hold in BOTH, which is the point of having
them; no number is pooled across them.

## Finding 1 — absences are not credit-limited

For every pump, the trace now carries which screens were present and,
for those absent, whether the credit would have permitted them.

| leg | pumps | both screens | one screen | absent AND permitted | absent AND not permitted |
|---|---|---|---|---|---|
| A | 420 | 418 | 2 | 1 | 1 |
| B | 614 | 456 | 158 | **157 (99.4 %)** | **1 (0.6 %)** |

A screen that missed a pump had, almost always, a free capture slot as
far as our credit was concerned. The reason it did not send is on the
producer side — no damage, or its capture slot still busy — and this
trace cannot separate those two, because the X-server half has no perf
ring anywhere.

**Read that with the field's known bias.** The credit is sampled when
the worker begins waiting, which is *after* the producer already decided
to skip the screen; if the credit advanced in between, a screen that was
genuinely credit-blocked reads as permitted. The bias therefore pushes
in the same direction as this finding, so finding 1 on its own is
suggestive rather than conclusive. **Finding 2 is what makes it safe to
act on**, because it identifies a mechanism that does not depend on this
field at all.

## Finding 2 — the screens do not encode concurrently

Every per-child record now names its monitor, so a four-child pump can
be split by screen. For each both-screen pump, take the first-output
instants of one screen's children and of the other's, and ask whether
the two intervals interleave.

| leg | both-screen pumps analysed | intervals interleaved | strictly separated | span across all four children |
|---|---|---|---|---|
| A | 418 | **1 (0.2 %)** | **417 (99.8 %)** | 23.45 ms mean |
| B | 456 | **0 (0.0 %)** | **456 (100 %)** | 15.47 ms mean |

One screen's children finish, then the other's start. In 874 pumps this
happened 873 times. The children are armed in one poll set with one
shared deadline — the mechanism intended to overlap them is present and
working — and they still come out one screen at a time.

**One alternative explanation, stated because this field cannot exclude
it.** `outfirst` fires when the worker reads a child's first output
byte. If the worker drains one child to completion before looking at the
next, the observed separation would be the *reading* order rather than
the encode order. What argues against it: the span across four children
is 15–23 ms, far larger than a read of a few hundred kilobytes, and the
pump is a poll set rather than a sequence of blocking reads. What would
settle it: comparing the feed instants (`feedend`, which also carries
the monitor now) against the output instants — the inputs should be
clustered if the worker feeds all four before draining any. **Not done
here, because the stop rule says to write down the wall rather than
chase it.**

## Finding 3 — no throughput wall in the batching itself

Cost per megapixel, by which screens were in the pump:

| leg | both screens (12.90 Mpx) | monitor 1 alone (9.22 Mpx) | monitor 0 alone (3.69 Mpx) |
|---|---|---|---|
| A | 2.79 ms/Mpx | 3.61 | 66.42 (n=1, ignore) |
| B | **2.03 ms/Mpx** | 2.43 | 3.56 |

A pump carrying both screens is the **most** pixel-efficient of the
three compositions, not the least. If the encoder were saturated,
adding pixels would cost at least proportionally; instead the fixed
per-pump overhead is amortised. So the wall in finding 2 is a
serialisation wall, not a throughput ceiling — the hardware is not out
of capacity, the work is simply not being done at the same time.

## What this means, and where it stops

The two-monitor residual this whole line of work chased is **not** our
flow control, and the mechanism intended to fix it (one poll set over
all children) is already in place and already doing its job at the level
it controls. The serialisation is below us, in ffmpeg, the VAAPI driver
or the GPU's encode engine.

Per the stop rule: documented, and the item closes. What would be needed
to go further — separating "the encode stack serialises" from "our
worker drains serially", then, if it really is the driver, evaluating
separate encoder processes per monitor — is a different piece of work
against a component we do not own, and it should be opened deliberately
rather than drifted into.

## Limits

* Two monitors only; nothing here speaks to three.
* The producer's own reason for skipping a screen is unrecordable from
  this side.
* The credit-permitted field is biased toward "permitted" (above).
* Saturation margins 1.43× and 1.90×, below the 2.0× floor, so overlap
  claims about *pipeline stages* from this run are void — note that
  finding 2 is not such a claim: it is about two encoders within one
  stage, measured by their own per-child records.
* The test client acknowledges each frame before decoding it.

---

## ADDENDUM 2026-08-08 — finding 2's INTERPRETATION is withdrawn; the encodes DO overlap

The original text above is kept verbatim, wrong claim included, per the
records rule. What is withdrawn is the sentence *"the encode stack runs
them one screen at a time"* and everything built on it. The counts it
rests on are still correct; they simply do not mean that.

**What went wrong.** Finding 2 measured the ORDER of the four
`outfirst` instants — did one screen's two children both produce output
before the other's did. That is a statement about completion order. The
claim made from it was about CONCURRENCY. Those are different
quantities, and a set of intervals can be completion-ordered while
overlapping throughout — which is exactly what happened. This is the
same failure mode `CLAUDE.md` already names: a metric that cannot
express the unit the claim is made in does not support the claim,
however clean its number looks.

**The right metric, from the same records, no new run.** A child's
encode cannot begin before its whole raw frame has been handed over
(the ffmpeg rawvideo reader needs a complete picture), and `feedend` is
that instant; `outfirst` is the first byte of the result. So
`[feedend, outfirst]` per child is the encode window, and two windows
overlapping means two encodes running at once.

| | leg A | leg B |
|---|---|---|
| the two SCREENS' encode windows overlap | **412 / 418** | 94 / 456 |
| two children of the SAME screen overlap | **836 / 836** | **912 / 912** |

Encodes overlap. In leg A, nearly always. Nothing is serialising them.

**Why leg B's screens mostly do not overlap, when nothing forbids it.**
The four children are fed concurrently, each at about the same
1.2–1.3 GB/s, so their inputs complete in proportion to frame size. The
big screen's picture is 13.8 MB against the small screen's 5.5 MB —
2.5× — so its input lands 6.9 ms later (leg B p50). The small screen's
encode takes 5.9 ms. It therefore finishes just before the big screen's
encode can start, and the two windows miss each other by about a
millisecond. It is a near-miss produced by frame-size asymmetry, not a
lock.

**Leg B timeline, milliseconds after `pump_beg`.** Each line is the
MEDIAN of that instant across the 456 both-screen pumps, so the whole is
a composite rather than one pump that happened (a single representative
pump is what `i91_encode_overlap.py` prints, and it looks the same):

```
 0.00  four children armed in one poll set; 38.7 MB of raw NV12 queued
 3.42  small screen, first child's input complete   -> its encode starts
 4.60  small screen, second child's input complete
 9.64  small screen, first output   (encode 5.9 ms = 1.61 ms/Mpx)
10.39  small screen, second output
11.31  big screen's input complete  -> its encode starts  (stagger 6.9 ms)
23.51  big screen, first output     (encode 13.2 ms = 1.43 ms/Mpx)
25.03  big screen, second output
25.88  pump_end
```

The timeline closes: 0.85 ms from the last output to `pump_end`.

**The screens are not competing for the encoder.** Cost per megapixel
for the same screen, when it is alone in the pump versus sharing it
(leg B, same run, same payload):

| screen | alone in the pump | sharing with the other | change |
|---|---|---|---|
| 2560×1440 | 1.609 ms/Mpx (n=158) | 1.644 ms/Mpx (n=912) | +2.2 % |
| 3840×2400 | 1.414 ms/Mpx (n=158) | 1.431 ms/Mpx (n=912) | +1.2 % |

If a single serial engine were the constraint, sharing would roughly
double the per-job cost. It adds one to two per cent. *Limit on this
comparison:* in leg B the two screens' encodes mostly do not overlap,
so this measures 2 concurrent children against 4 armed but staggered
ones, not against 4 truly concurrent ones. Leg A has only n=2
single-screen pumps, so the clean four-concurrent contention number is
not in this capture.

**The counterfactual, since the near-miss is what the whole picture
rests on.** Overlap begins when the small screen's encode outlasts the
stagger: scaling every encode by k, the windows meet at
k > stagger / encode_small.

| slowdown k | leg B pumps whose screens would overlap |
|---|---|
| 1.18 (p50 threshold) | half |
| 1.5 | 436 / 456 (96 %) |
| 2.0 | **455 / 456 (100 %)** |

And this is not only arithmetic: **leg A is the natural experiment.** Its
host state made encode 22–48 % slower per megapixel (2.007 and
2.113 ms/Mpx against leg B's 1.609 and 1.414) with the stagger
essentially unchanged (5.9 vs 6.9 ms), and its overlap fraction is 99 %
instead of 21 %. A slower encoder produces MORE overlap, measured, in
this capture.

**What the pump is actually spending its time on.** 11.3 ms of a
25.9 ms pump elapse before the last raw byte reaches a child — 38.7 MB
of NV12 per pump at about 3.4 GB/s aggregate. The raw-input transfer,
not the encoder, is what sets when work can start and what the biggest
single block of the pump is.

**Reproducing all of this:**
`PR-demo/mac_bisect_matrix/i91_encode_overlap.py leg_B/perf/enc.599`
(and `leg_A/perf/enc.252`) prints every number above from the rings in
this capture. No new run is involved and none was made.

**Findings 1 and 3 are unaffected.** Finding 1 never used these records.
Finding 3's conclusion — no throughput ceiling — is reinforced rather
than weakened by the per-megapixel table above.

### Limit added 2026-08-08 — the GPU's clocks were not pinned

**Neither leg pinned the GPU's power/clock state (DVFS).** The AMD
render node was left to scale on its own, so its clocks were free to
move between the two legs and within each of them. Three consequences,
in the order they matter:

1. **The 22-48 % per-megapixel difference between the legs has an
   uncontrolled cause.** "Leg A ran a slower encoder" is what the
   numbers show; *why* it was slower is not established, and unpinned
   clocks are the obvious candidate. Leg A therefore supports the
   DIRECTION of the counterfactual — slower encode, more overlap — and
   is not a calibrated 1.25x arm. The k-threshold table is per-pump
   arithmetic and does not depend on it.
2. **The alone-versus-sharing contention table (+2.2 % / +1.2 %) is
   within one leg**, so drift between legs cannot reach it, but drift
   *inside* leg B can. The effect it reports is one to two per cent,
   which is the size at which clock drift starts to matter. Read it as
   "nothing resembling a 2x serialisation penalty", not as a
   two-per-cent measurement.
3. **What does NOT depend on any of this** is the load-bearing claim:
   whether two encode windows overlap is a comparison of four instants
   inside a single pump, spanning tens of milliseconds. Clock drift
   does not reorder them. The 412/418, 94/456, 912/912 and 836/836
   counts stand.

The hardware retains its own nondeterministic scheduling regardless —
a shared engine whose ordering we neither observe nor control. Nothing
here claims otherwise. The claim is narrower and is what the item
closes on: **our code does not serialise the two screens**, and the
input transfer, which is ours, is what staggers them.
