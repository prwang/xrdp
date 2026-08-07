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
