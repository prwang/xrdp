# Pre-registered prediction — written BEFORE the run

**The question.** At two monitors the credit frontier only HALVES the
producer stall (21.5/22.0 % of cycles waiting over 10 ms) where at one
monitor it removes it (0.8 %). A static analysis says why it COULD: a
frame id is one monitor's frame, not a screen refresh, so a
session-wide window of C ids gives each of M screens only about C/M.
On that account C = 2 at M = 2 is the same headroom as C = 1 at M = 1,
which was measured at 16.3 % — the same regime as the 22 % seen here.

**But two models disagree about which term actually binds**, and the
trace cannot arbitrate because all three terms of the credit are equal
at the instant one is emitted. Model A: the client window (client + C)
binds, so scaling C by the monitor count moves the limit off the
network. Model B: the pipeline inventory term (server + 1) binds first
at M >= 2, so C buys inventory and never depth, and raising it changes
nothing.

**The intervention.** Identical arms, identical image, identical
payload, two monitors, ONE config line apart:
  x022 (:40038) wire_window = 2   -- today's shipped default
  x028 (:40044) wire_window = 4   -- C*M at M = 2
Four legs of 20 s, interleaved. The strip-render payload, so the run is
not producer-limited the way the standard payload was at two monitors.

**PREDICTIONS.**

1. **The discriminator is the STALL, not the bound.** If model A is
   right, cycles stalled over 10 ms fall from ~22 % toward the
   one-monitor figure (under a few per cent), and the wait for
   permission to capture collapses from ~12 ms at p90 toward zero. If
   model B is right, both stay where they are.
2. **The bound rises to 8 either way, and proves nothing on its own.**
   C + 2*M = 4 + 4 = 8. It is structural: it follows from the
   arithmetic whichever term binds, so it must NOT be read as
   confirmation. It is a mechanism check — a maximum above 8 would mean
   the bound is not what we think and the run is void.
3. **Frame period should improve if and only if the stall does.**

**Falsified if:** the stall does not move materially while the mechanism
check passes. That would mean the session-wide window is NOT what limits
a monitor at M = 2, the "scale by monitor count" fix would not work, and
the residual belongs to the inventory term instead.

**What this cannot settle.** Three or more monitors. A real decoding
client (this one acknowledges before it decodes, so its acknowledgement
latency is a floor). And whether a per-monitor window would beat a
scaled global one -- that is a different design, needing a wire change.

---

# Outcome (appended after the run; the text above is unchanged)

**Prediction 1 FALSIFIED in its decisive half.** The stall fraction did
move -- 14.9 % to 8.4 % pooled over each arm's two legs -- but the wait
itself did not: median 4.886 ms at window 2 against 4.546 ms at window
4, where the one-monitor reference is 0.015 ms. Doubling the window
moved cycles out of the sub-0.1 ms bucket (25 % -> 12 %) and into the
1-10 ms band (57 % -> 77 %). It made long waits rarer without making
waits go away.

**Prediction 2 held and was correctly discounted in advance.** The bound
rose to 7-8, matching C + 2*M = 8. Mechanism check passed; it confirms
nothing about the diagnosis, as stated before the run.

**Prediction 3 held.** Frame period p50 is flat (10.87/11.01 ->
11.13/11.05 ms), which is consistent with the stall not having been
removed.

**Verdict: the session-wide window is at most a minor part of the
two-monitor residual.** Model A (the client window binds, so scaling by
monitor count moves the limit off the network) is not supported: scaling
it left the typical wait where it was. Model B is not confirmed either
-- this run does not attribute the residual, it only rules out the
window as its main cause.

The candidate the numbers point at, named in the capture README and NOT
measured here: at two monitors one encoder worker serialises both
screens, so a monitor's credit waits on the other monitor's encode. If
that is right, the withheld metric measures something different at M = 2
than at M = 1, and no two-monitor number here should be attributed to
flow control until the wait can be split by which monitor the worker was
serving.
