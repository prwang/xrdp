# i91_window4_m2_20260807_201346_s20 — scaling the window by monitor count does NOT zero the two-monitor stall

**The prediction is falsified in the way that matters.** Doubling the
window at two monitors — `wire_window = 4`, which is exactly the
"scale C by monitor count" fix at M = 2 — improves the tail but leaves
the bulk of the wait untouched. The one-monitor regime is nowhere in
sight. The session-wide window is therefore at most a minor part of the
two-monitor residual, and the fix that was on the table would not have
bought what it was expected to buy.

`PREDICTION.md` in this directory was written before the run and names
the stall as the discriminator, with the bound explicitly excluded as
"structural, proves nothing on its own".

## The two arms

Identical image, identical strip-render payload, two monitors
(2560×1440 + 3840×2400), **one config line apart**:

| | window 2 | window 4 |
|---|---|---|
| arm / port | x022 / 40038 | x028 / 40044 |
| `wire_window` | 2 (today's shipped default) | 4 (= C·M at M = 2) |

Four legs of 20 s interleaved W2 W4 W2 W4. Certified at deploy, 7 checks
clean, 0 black frames.

## Mechanism check, before any rate

The window each arm ran is echoed on every credit record: **2** and
**4**. The bound rose from a measured maximum of 6 to 7–8, matching
`C + 2·M` = 8. That is the structural consequence and, as the prediction
said in advance, it confirms nothing about the diagnosis — it would look
the same whichever term binds.

## The result

**Withheld** is the wait between the encoder children absorbing a
frame's pixels and the producer being told it may capture again. Pooled
across each arm's two legs, same sitting:

| | n | p50 | p90 | stalled > 10 ms | < 0.1 ms | 1–10 ms |
|---|---|---|---|---|---|---|
| window 2 | 316 | 4.886 ms | 10.96 ms | **14.9 %** | 25 % | 57 % |
| window 4 | 580 | 4.546 ms | 9.70 ms | **8.4 %** | 12 % | 77 % |
| *one monitor, window 2* | *951* | *0.015 ms* | *0.040 ms* | *0.8 %* | *98 %* | *—* |

Read the columns, not the headline:

* **The tail improves.** Cycles waiting over 10 ms fall from 14.9 % to
  8.4 %, and the p90 from 11.0 to 9.7 ms.
* **The bulk does not.** The median wait is 4.9 ms before and 4.5 ms
  after. At one monitor the median is 0.015 ms — three orders of
  magnitude smaller. Doubling the window moved cycles *out of* the
  fast bucket (25 % → 12 % under 0.1 ms) and *into* the 1–10 ms band
  (57 % → 77 %); it did not move them to zero.
* **Frame period is flat**: p50 10.87/11.01 → 11.13/11.05 ms. The p90
  improves slightly, 28.1 → 26.9/27.6 ms.

So at two monitors nearly every cycle waits some milliseconds for
permission whatever the window is, and the window only controls how
often that wait becomes long. At one monitor essentially no cycle waits
at all. **Those are different mechanisms, not different amounts of the
same one.**

## What this means for the fix that was proposed

Scaling `wire_window` by the monitor count would:

* change a shipped default's meaning at M ≥ 2;
* raise the wire bound from 6 to 8 frames at two monitors, and each
  frame is ~3.3 MB at 4K, so roughly 6.6 MB more may be outstanding;
* silently re-mean any value an administrator has already set;

and buy, on this evidence, a fall in the long-wait fraction from ~15 %
to ~8 % with no change in the typical wait and no change in frame rate.
**That is not the trade it was expected to be**, and this capture is the
reason to say so before shipping it rather than after.

## Where the residual probably is — named, not measured

The candidate this run points at is not flow control at all. At two
monitors ONE encoder worker serialises both screens: the per-monitor
period is ~28 ms while frames leave for the client every ~13 ms, and the
worker's own encode wait is 15–16 ms per frame. A monitor's next credit
cannot be granted until the worker has finished with the *other*
monitor's frame, so a multi-millisecond wait on nearly every cycle is
what serial encoding of two screens looks like from inside this metric.

That also means **the withheld metric may not measure the same thing at
M ≥ 2 that it measures at M = 1.** At one monitor it is "the flow
control made the producer wait"; at two it may largely be "the single
worker was busy elsewhere". Distinguishing them needs the wait broken
down by which monitor the worker was serving, which this trace does not
record. Until that exists, no two-monitor number here should be
attributed to the window.

The serial cost itself is already on file as the second half of
BACKLOG #91, projected but never decomposed.

## Limits

* Saturation margin 1.20–1.27×, below the 2.0× floor, so overlap and
  concurrency claims from this run are void; the worker-idle figures are
  not quoted above for that reason.
* The encoder wait rose slightly on the window-4 arm, 15.2/15.3 →
  16.3/16.5 ms. Small, unexplained, and in the direction that would
  *understate* any improvement — noted rather than adjusted for.
* The window-2 stall here (14.9 % pooled) is lower than the 21.5/22.0 %
  measured on the same arm and config earlier the same day. Run-to-run
  variation on this host is why the comparison is made within one
  sitting and why the earlier figure is not mixed in.
* The test client acknowledges each frame before decoding it.
* Two monitors only.
