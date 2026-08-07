# Pre-registered prediction — multi-monitor, written BEFORE the run

**Why this run.** Every credit-frontier measurement so far is ONE
monitor. The wire bound carries a per-monitor term -- the producer's
capture budget is 2 slots PER MONITOR, so the bound is C + 2*M, not
C + 2 -- and BACKLOG #80 carries an explicit owner caution not to carry
any single-monitor number across by multiplication. This run is the
first measurement at M = 2.

**Arms** (both already deployed, no new arm, no image build):
* x020 (:40036) legacy ack gate, window 2 from XRDP_GFX_FRAMES_IN_FLIGHT
* x021 (:40037) credit frontier at wire_window 2 -- the NEW DEFAULT

Two monitors: 2560x1440 and 3840x2400 side by side, textflood payload,
oracle client on the host. Four legs of 20 s, interleaved L T L T.

**Predictions.**

1. **The bound becomes C + 2*M = 2 + 4 = 6 on the frontier arm.** The
   measured maximum distance between a frame and the client's last
   acknowledgement should exceed the single-monitor maximum of 4, and
   must never exceed 6. A value above 6 is a RED result and stops the
   run being read as anything but a bug.
2. **The legacy arm's bound is 2*M + fif - 1 = 4 + 1 = 5** by the same
   arithmetic, since it grants up to client + fif - 1 and rides 2 slots
   per monitor above it. So the frontier again costs ONE more frame
   than the legacy path, as it does at M = 1.
3. **The stall difference should persist.** The wait between the encoder
   children absorbing a frame's pixels and the producer being told it
   may capture again: large on the legacy arm (order 10 ms at p90, of
   the order of 15 % of cycles), near zero on the frontier arm. If it
   does NOT persist at two monitors, the single-monitor result does not
   generalise and the default flip needs revisiting.
4. **Throughput will be far below the single-monitor figure on BOTH
   arms**, because one encoder worker serialises both monitors: the
   pair cost is roughly 2x the per-monitor encode. This is expected and
   is NOT a regression of either mechanism; it is the known m>=2 serial
   cost that BACKLOG #91 owns. Do not read a slow period here as a
   frontier problem.

**What this run cannot settle.** Whether the per-monitor capture budget
is correctly independent (BACKLOG #91's first item) -- that needs the
per-monitor slot accounting, not the aggregate. And nothing about a real
decoding client: the oracle client acknowledges before it decodes.

---

# Outcome (appended after the run; the text above is unchanged)

Predictions 1, 2 and 4 CONFIRMED: the frontier's bound reached exactly
6 and never exceeded it, the legacy path's exactly 5, and the
per-monitor period fell to ~28-29 ms against ~17.5 ms at one monitor.

Prediction 3 PARTLY CONFIRMED, and the shortfall is the finding: the
stall difference persists but the frontier does NOT remove the stall at
two monitors as it does at one. Withheld p90 21.9/22.3 ms legacy against
12.3/12.3 ms frontier; cycles stalled 43.1/48.1 % against 19.5/20.5 %.
A halving, not an elimination.

THIS RUN'S RATES ARE PRODUCER-LIMITED and must not be quoted: the
benchmark payload had to redraw both screens and came out slower than
the pipeline, a saturation margin of 0.51-0.55x. The bound numbers stand
because a bound is a property of frame identities rather than a rate.
The comparison was repeated with the strip-render payload in
`i80_multimon_strip_20260807_152223_s20`, which is the readable run and
carries the analysis.
