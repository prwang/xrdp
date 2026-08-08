# i91_attribution_m2_20260807_211825_s20 — two monitors, with the pump attributable by screen

Two 20 s legs taken to answer one question left open by BACKLOG #91:
inside a pump that carries both screens, do the two screens' encodes run
at the same time, or does something below us run them one after the
other?

**Answer: they overlap. Nothing serialises them.** What staggers them is
the handover of their raw pictures to the ffmpeg children, which takes
11.3 ms of a 25.9 ms pump — and what limits *that* rate is not
established here.

Full record and reasoning:
`docs/experiments/91-the-multimon-window-and-the-shared-pump.md`.
Every number below is re-derivable with
`PR-demo/mac_bisect_matrix/i91_encode_overlap.py leg_B/perf/enc.599`
(and `leg_A/perf/enc.252`).

## Setup

Arm x029, host port 40045, built from `1fed64c16a89` so it carries the
three attribution fields. Shipped defaults — credit frontier at
`wire_window = 2`, no emit thread, `h264_vaapi` on
`/dev/dri/renderD128`. Two monitors: monitor 0 at 2560x1440 (3.69 Mpx,
5.5 MB of NV12 per view), monitor 1 at 3840x2400 (9.22 Mpx, 13.8 MB),
confirmed from the client's own `xrandr --listmonitors`. Strip-render
payload. Two legs, same config, 20 s each. Certified at deploy: 7 checks
clean, 0 black frames.

**The two legs landed in different host states, which is why two were
run:** leg A's pumps average 36.05 ms and leg B's 26.14 ms for the same
work. Every conclusion holds in both; no number is pooled across them.

**The GPU's clocks were not pinned** in either leg. That is the
uncontrolled cause behind leg A being 22-48 % slower per megapixel, and
it is why leg A is read below as evidence for a *direction* and not as a
calibrated arm.

## Finding 1 — a screen that misses a pump is not held back by our flow control

| leg | pumps | both screens | one screen | absent AND permitted | absent AND not permitted |
|---|---|---|---|---|---|
| A | 420 | 418 | 2 | 1 | 1 |
| B | 614 | 456 | 158 | **157 (99.4 %)** | **1 (0.6 %)** |

A screen that missed a pump had, almost always, a free capture slot as
far as our credit was concerned. The reason it did not send is on the
producer side — no damage, or its capture slot still busy — and this
trace cannot separate those two, because xorgxrdp has no perf ring
anywhere.

Read with the field's bias: the credit is sampled when the worker begins
waiting, which is *after* the producer already decided, so a screen that
was genuinely credit-blocked can read as permitted. The bias runs the
same way as the finding, which makes it suggestive on its own. Finding 2
is what makes it safe to act on, because it uses none of this field.

## Finding 2 — the encodes overlap

A child cannot begin encoding until its whole raw picture has arrived:
ffmpeg's raw-video reader hands nothing to the encoder until it holds a
complete frame. So `feedend` (last byte into the pipe) to `outfirst`
(first byte of the result) is the encode window, and two overlapping
windows are two encodes running at once.

| | leg A | leg B |
|---|---|---|
| the two **screens'** encode windows overlap | **412 / 418** | 94 / 456 |
| the two **views of one screen** overlap | **836 / 836** | **912 / 912** |

Two encodes always run at once — the two views of a screen, in every
pump of both legs. Across screens, in leg A, nearly always.

Biases in that window, stated: `feedend` fires when the last byte enters
the pipe and the child may still hold up to one pipe buffer (1 MiB,
about 0.9 ms at the rates below) unread; and on this VAAPI arm the
window contains the upload to the GPU as well as the encode.

**Why leg B's screens mostly miss each other, when nothing forbids them
meeting.** All four children are fed at once, each taking bytes at about
1.2-1.3 GB/s, so their inputs complete in proportion to frame size. The
big picture is 2.5x the small one, so its input lands 6.9 ms later. The
small screen's encode takes 5.9 ms — it finishes about a millisecond
before the big screen's encode is allowed to begin. A near-miss from
frame-size asymmetry, not a lock.

**Leg B timeline, milliseconds after `pump_beg`.** Each line is the
median of that instant across the 456 both-screen pumps, so the whole is
a composite rather than one pump that happened (the script also prints a
single representative pump, which looks the same):

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

**The counterfactual, since the near-miss is what the picture rests on.**
Scaling every encode by k, the two windows meet at
k > stagger / small-screen encode: k = 1.18 at the median, 436/456
(96 %) at k = 1.5, 455/456 (100 %) at k = 2.0. And leg A is the natural
experiment — 22-48 % slower per megapixel, stagger unchanged (5.9 vs
6.9 ms), overlap 99 % instead of 21 %. A slower encoder produced more
overlap, measured.

## Finding 3 — the screens are not competing for the encoder

Cost per megapixel for the same screen, alone in a pump versus sharing
one, within leg B:

| screen | alone in the pump | sharing with the other | change |
|---|---|---|---|
| 2560x1440 | 1.609 ms/Mpx (n=158) | 1.644 ms/Mpx (n=912) | +2.2 % |
| 3840x2400 | 1.414 ms/Mpx (n=158) | 1.431 ms/Mpx (n=912) | +1.2 % |

A single serial engine would roughly double the per-job cost. It adds
one to two per cent. *Limit:* because leg B's screens mostly do not
overlap, this compares 2 concurrent children against 4 armed-but-
staggered ones. Leg A has only n=2 single-screen pumps, so the clean
four-concurrent contention number is not here.

Nor is there a throughput ceiling. By pump composition:

| leg | both screens (12.90 Mpx) | monitor 1 alone (9.22 Mpx) | monitor 0 alone (3.69 Mpx) |
|---|---|---|---|
| A | 2.79 ms/Mpx | 3.61 | 66.42 (n=1, ignore) |
| B | **2.03 ms/Mpx** | 2.43 | 3.56 |

A both-screen pump is the *most* pixel-efficient composition; the fixed
per-pump overhead is being amortised rather than capacity exhausted.

## The handover, and what is NOT concluded from it

11.3 ms of a 25.9 ms pump elapse before the last raw byte reaches a
child — 38.7 MB of NV12 at about 3.4 GB/s aggregate, 1.2-1.3 GB/s per
child. It is the largest measured block of the pump.

**What limits that rate is not established.** xrdp's own side is nearly
free — `feed_vmsplice` moves page references, not bytes — so the copy
happens in the child's `read()`, which means the rate is how fast the
ffmpeg children take their input and the pump is waiting on them. That
is consistent with ffmpeg simply copying at that speed, with the child
not reading promptly (i.e. **the encoder backpressuring us**, in which
case the time is encode time under another name), and with the pipe
geometry forcing extra round trips. This capture does not distinguish
them, and no optimisation is proposed on the strength of it.

## Limits

* Two monitors only; nothing here speaks to three.
* The GPU's clocks were not pinned (above).
* The producer's own reason for skipping a screen is unrecordable from
  this side.
* The credit-permitted field is biased toward "permitted".
* Saturation margins 1.43x and 1.90x, below the 2.0x floor, so overlap
  claims about *pipeline stages* from this run are void — finding 2 is
  not such a claim: it is about two encoders within one stage, measured
  by their own per-child records.
* The test client acknowledges each frame before decoding it.

## One retraction

The first analysis of this capture concluded the opposite of finding 2 —
*"in 874 of 874 both-screen pumps the encodes do not overlap"* — and was
reported and committed before being withdrawn a day later from these
same records, with no new run. The metric was the ORDER of the four
first-output instants, which is completion order; it was read as
concurrency. Intervals can be ordered by their right edges and overlap
throughout, which is what these do. The counts were right; the
conclusion was not.
