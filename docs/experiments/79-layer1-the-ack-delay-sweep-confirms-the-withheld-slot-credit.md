<!--
Experiment record. BACKLOG.md is the OPEN work list; this file is the
record it points at. Kept verbatim, wrong claims included.
-->

# #79 layer 1 — the ack-delay sweep: the mechanism is confirmed by intervention, and the tail turns out to be a period-3 limit cycle

2026-08-02. Capture:
`PR-demo/mac_bisect_matrix/captures/i79_x017_ackdelay_20260802_s20`
(plus the discarded first attempt, `..._s5`). Harness:
`PR-demo/mac_bisect_matrix/{ack_delay_proxy.c, ack_delay_proxy_selftest.py,
ack_delay_sweep.sh, i79_ack_delay_analyze.py}`.

## Lead with the answer

**#78's attribution is validated.** Delaying only the client's acks, on
the unmodified deployed build, moves the withheld slot credit ~1:1
(Δwithheld/ΔD = 1.10) and the frame period at 0.44 ms per ms of delay —
and the added period lands **entirely** in the cycles whose credit was
withheld. Cycles whose credit was prompt run at 16.3–16.9 ms in every
leg, unmoved by a 40 ms ack delay; `pump` (encode) is 15.2–15.4 ms
everywhere. The rival explanation — that egress is ack-gated and the
whole pipeline is simply later — is excluded by measurement: at D = 40
exactly two thirds of sends go out with 1 or 2 frames unacked.

**And the tail is not a race.** At D ≥ 20 the system locks into a
deterministic period-3 cycle, `SS.SS.SS.` — two captures withheld, one
prompt — with zero exceptions in 480 and 391 cycles. The 31 % tail #76
was filed on is the same motif, intermittent. This changes what layer 2
has to assert and makes the CI target concrete rather than statistical.

**Two of my four predictions were wrong**, both because they were
written as if the loop were open. Stated below before the numbers that
falsified them, because the point of writing them down was to be able
to be wrong in public.

## Design

Legs (the whole experiment; the arm count was not changed): `direct`
(no proxy), then D = 0, 10, 20, 40 ms through the proxy. Arm x017 — the
deployed HEAD build, fif = 1, the arm #78 measured — one monitor
3840×2400, textflood, oracle client, cold session per leg, fleet idle,
host DVFS pinned (GPU `high` / SCLK 2900, CPU `performance`, owner-set
earlier the same day). Nothing differs between legs except when the
client's bytes arrive.

Predictions, written before the run:

* **P0** the delay applies, measured *inside* the server: `egress →
  cliack` rises by ~D. (Not the proxy's own telemetry — quality gate 2.)
* **P1** withheld p50 ≈ D.
* **P2** the stall fraction (withheld > 10 ms) goes 31 % → ~100 % for
  D ≥ 10.
* **P3** the period rises with D.
* **CONTROL** D = 0 through the proxy reproduces `direct`, or nothing
  downstream counts.

Falsifiers, equally explicit: withheld flat while the period rises would
mean #78 named the wrong mechanism; withheld rising while the period
does not would mean the withholding is real but off the critical path,
and the fix pointless.

## The instrument, and the two things it had to prove first

`ack_delay_proxy.c` forwards server→client immediately and releases each
client→server read D ms after arrival. It cannot parse RDP (TLS), so it
delays the whole direction — legitimate here because an oracle session's
client→server traffic is one small ack PDU per frame and nothing else:
**775 chunks of ~70 B for 751 frames** at D = 0. It terminates both TCP
connections, so transport-level flow control between proxy and server is
untouched; only application bytes are held.

Self-test before any session time (`ack_delay_proxy_selftest.py`):
applied delay matches the knob to ±0.2 ms, D = 0 costs 0.037 ms of RTT,
server→client sustains ≥ 3.3 GB/s against the session's 153 MB/s.

*The self-test lied on its first run and had to be fixed first*: it
packed the ping counter as `'<Q'`, so ping 66 serialised its low byte as
0x42 = `'B'`, the bulk-transfer trigger — the client then read 200 MB of
zeros eight bytes at a time and reported 0.011 ms RTT for a 10 ms delay
line that was working perfectly. Recorded because it is the same class
of error as everything else in this file: a measurement that measured
itself.

## The sample-size correction (a deviation from the item's "5 s per leg")

The first sweep ran the specified 5 s per leg and its **control leg
failed**: `direct` and D = 0 came out 18.5 % apart, with `egress →
cliack` 7 ms *lower* through the proxy — which no delay line can cause.
Cause: the gate spends most of a 5 s window on the cold login, so 5 s
yields 1.9 s of frames, 45–51 usable cycles. A distribution whose signal
is a 31 % tail cannot be compared at n = 45.

Rerun at 20 s/leg: 391–746 cycles, control closes to 4.3 %. This is a
sample-size change to the same five legs, not an added arm, condition or
payload — but it is a deviation from what the item described and is
recorded as one. The 5 s figure came from "a mechanism check, not a
rate", which was right in principle and wrong in arithmetic: it counted
wall time instead of frames. The underpowered run is kept at
`captures/i79_x017_ackdelay_20260802_s5`.

## Result

| leg | D | cycles | egress→cliack p50 | withheld p50 / p90 | withheld > 10 ms | period mean / p50 / p90 | wait mean |
|---|---|---|---|---|---|---|---|
| direct | — | 746 | 7.6 | 0.03 / 35.3 | 29.7 % | 21.5 / 17.1 / 42.7 | 4.9 |
| d0 | 0 | 714 | 10.1 | 0.04 / 35.4 | 36.9 % | 22.5 / 17.1 / 43.6 | 5.9 |
| d10 | 10 | 569 | 17.1 | 24.10 / 43.3 | 58.0 % | 27.9 / 18.1 / 52.4 | 11.4 |
| d20 | 20 | 480 | 27.8 | 35.99 / 54.7 | 66.7 % | 33.1 / 18.4 / 63.8 | 16.4 |
| d40 | 40 | 391 | 48.1 | 56.96 / 77.1 | 66.8 % | 40.1 / 18.8 / 85.6 | 23.2 |

`withheld` = credit emission − `absorb(k−2)`: the interval between the
in-tree safety condition (the children have absorbed frame k−2's input,
so its capture slot is reusable) and the credit actually reaching
xorgxrdp. Zero negatives in every leg (gate 2c). Both control legs
reproduce #78 Run A (22.6 ms period, 31.1 % stalls, withheld p90 36.4),
measured 60 s at 03:08 the same day — gate 4, no regression against the
recorded number.

* **CONTROL: OK.** period 21.5 vs 22.5 ms (4.3 %), withheld p90 35.3 vs
  35.4.
* **P0: OK.** `egress → cliack` p50 +7.0 / +17.8 / +38.0 ms for
  D = 10 / 20 / 40, from the server's own ring.
* **P1: FALSIFIED.** withheld p50 24.1 / 36.0 / 57.0 against a predicted
  10 / 20 / 40. The slope is right (1.10 between D = 10 and 40); there is
  a ~13 ms offset. The prediction assumed only the ack moved. In a closed
  loop the rate drops too, so the ack that opens the window is itself
  emitted later, and the p50 additionally jumps discontinuously once the
  median frame changes class from unstalled to stalled. The prediction
  should have been about the stalled subpopulation.
* **P2: FALSIFIED, and this is the finding.** The stall fraction
  saturates at exactly 2/3 — see below.
* **P3: CONFIRMED.** 22.5 → 27.9 → 33.1 → 40.1 ms: 0.44 ms of frame
  period per ms of ack delay.

## Why P2 was wrong: it is a limit cycle, not a race

Per-capture credit classes in order (`S` = withheld > 10 ms,
`.` = prompt):

```
d0    SS....................S............SS.SS..SS...SS.S..SS.S...SS.SS.S..SS..SS
d10   SS..SS.S..SS..SS.SS.S...SS..SS.SS.SS..SS.SS..SS..SS.SS.SS..SS..SS.SS.SS..SS
d20   SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.
d40   SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.SS.
```

Run-length census: D = 20 — 160 runs of `SS`, 160 singleton `.`, nothing
else. D = 40 — 131 and 130, nothing else. D = 10 — 160 `SS`, 10 `S`,
gaps of 1–3. D = 0 — the same motif, intermittent: 115 `SS`, 33 `S`,
gaps up to 22.

So the stall fraction *cannot* reach 100 %: every third capture finds
its credit already emitted, because the credit target
`min(consumed, server + 1)` covers a frame ahead, and one opening of the
window releases the pair. **The "31 % heisenbug" is a deterministic
period-3 cycle whose duty the ack race modulates.** Under injected delay
the modulation disappears and the cycle stands alone.

*Why period 3 exactly* is not proven here — it is the question layer 2's
3-frame interleaving enumeration answers by construction, which is a
lucky coincidence of scope, not a plan.

## The discriminator: the slot credit, not the send path

Delaying acks would also slow the server if egress were gated by the
same window — a rival that predicts the same period curve. Excluded:

| leg | sends by (id_server − id_client) at send time | period, credit withheld | period, credit prompt |
|---|---|---|---|
| direct | 0:2223 1:933 2:4 | 32.7 (n=221) | 16.8 (n=524) |
| d0 | 0:1884 1:1116 2:4 | 31.9 (n=263) | 16.9 (n=450) |
| d10 | 0:1001 1:1395 2:4 | 36.1 (n=329) | 16.6 (n=239) |
| d20 | 0:676 1:975 2:373 | 41.5 (n=320) | 16.3 (n=159) |
| d40 | 0:556 1:552 2:552 | 52.0 (n=260) | 16.4 (n=130) |

1. The send path never waits: at D = 40, one third of sends carry 2
   unacked frames and another third carry 1. (#78's static finding, now
   confirmed under stress.)
2. All of the added period is in the gated cycles. The prompt-credit
   class is **flat at 16.3–16.9 ms under a 40 ms ack delay**. If any
   other stage — encode, assembly, send, client — were on the path, that
   class would have moved.
3. The split closes against the mean: ⅔·52.0 + ⅓·16.4 = 40.1 =
   measured (D = 40); 41.5·320/479 + 16.3·159/479 = 33.1 = measured
   (D = 20). Gate 1.

## One stalled capture, in the domain's words (D = 40, capture 76)

```
  -27.56 ms  absorb  73     the children absorbed frame 73's input
  -27.50 ms  ackslot 73     ...and its slot credit went out at once
  -18.26 ms  egress  73
  -17.46 ms  msgin   74
   -8.30 ms  msgin   75
   +0.00 ms  absorb  74     frame 74's capture slot is SAFE to reuse
   +8.88 ms  egress  74     no credit: the ack window is closed
  +16.14 ms  absorb  75
  +24.78 ms  egress  75     the server sends on, 2 frames unacked
  +31.91 ms  cliack  73     73+1 > 75 false — window stays closed
  +61.98 ms  cliack  74     74+1 > 75 false — still closed
  +79.59 ms  credit for frame 74 emitted        withheld 79.59 ms
  +88.21 ms  msgin   76     the capture itself took 8.62 ms
```

The slot was safe at +0.00 and the producer was told at +79.59. None of
that interval is encode, send or capture: it is a wait for a round trip
the safety condition does not require.

## What is settled, and what is not

Settled: the client's frame ack is on the critical path of capture-slot
release at fif = 1; the effect is deterministic; the send path and the
encoder are not involved; the fix targets the right code.

Not settled: that ungating is safe — the blocking pre-step in #79
(xorgxrdp's SLOT_ONLY handler must not consume region-retirement state)
is untouched by this run and remains blocking. Nor is the fix's effect
measured: the prediction for the fixed build is withheld ≈ 0 and a
period **flat in D**, and this same sweep is what will separate the two
builds.

## What the sweep implies for the fix — and a correction to #79

Reading the emission code to write those predictions turned up an error
in #79's own description, recorded here because it changes what the fix
has to be judged on.

**#79 called the window gate on the slot ack "an implementation artifact
of where the emission code lives". It is not.** `xrdp_mm.c:1692` states
the intent in the code: *"the client's own ack window stays the OUTER
gate in both modes: a client that stops acking still stops the
producer."* The gate has a second job, and only one of its two jobs is
the defect.

So the honest form of "does ungating address the phenomenon" is: **yes
for the phenomenon measured, and it removes something else at the same
time.**

* *Addresses it.* Every millisecond this sweep injected reached the
  period through the withheld credit and through nothing else — the
  prompt-credit class never moved. Emitting the slot credit at the
  absorb frontier removes exactly that interval. Predicted: withheld
  ≈ 0 at every D, no `S` runs, period 16.5–17.5 ms flat in D, and
  therefore fif = 1 at or below fif = 2's 18.1 ms, which is what
  FR-ACK-3 actually asks for.
* *Removes something else.* What bounded client-outstanding at fif = 1
  was the starvation itself: capture stopped, so nothing new could be
  sent. Measured here — HEAD holds outstanding at ≤ 2 even at D = 40.
  Ungated, outstanding becomes rate × client-ack-latency and grows with
  a slow client until the transport stops draining, `frame_id_server`
  stops advancing and the slot target's own cap
  (`min(consumed, server + 1)`, whose comment calls it "what keeps this
  BACKPRESSURE rather than a queue") bites. That is a bound, but a
  socket buffer's worth of frames rather than one — and PRD FR-ACK-3
  says the window is there to bound precisely what the client has
  outstanding.

Hence the validation gate now sitting at #79 step 3, between CI and the
fleet A/B: the same five legs against the fixed deb (the win *and* its
cost measured in one run, since `id_server − id_client` must RISE if the
fix really ungated anything), plus a **frozen-client leg** —
`ack_delay_proxy -F <secs>` stops client→server mid-session while the
video direction keeps flowing — which asks the question the gate's own
comment cares about: with the client no longer acking, how many frames
does the server keep producing? HEAD should stop within 1–2. The fixed
build's number is the new bound it introduces, and it must be recorded
rather than assumed. If it is bad, the targeted change is not "ungate"
but "gate on the right thing": `client + H > server` with H taken from
the two-slot capture budget instead of `fif` — the pathology being that
fif = 1 is a tighter bound than the pipeline it is gating needs.

## Correction, 2026-08-02 (same day): there is no transport backpressure to fall back on

The paragraph above says a slow client widens `id_server − id_client`
"until the transport stops draining, `frame_id_server` stops advancing
and `min(consumed, server + 1)` bites", and calls the result "a socket
buffer's worth of frames". **That is wrong.** It was written from the
`xrdp_mm.c:1648` comment ("what keeps this BACKPRESSURE rather than a
queue") without following `frame_id_server` to the call that advances
it. Kept, per the records rule, and corrected here rather than edited.

`frame_id_server` advances on `enc_done` (`xrdp_mm.c:4320`), i.e. when
the frame has been handed to `trans_write_copy_s()`. That function
**cannot fail for want of a wire**: after one non-blocking attempt,
whatever the socket did not take is `malloc`ed into a new stream,
appended to the singly-linked `self->wait_s` list, and 0 is returned
(`common/trans.c:644-676`). The list has no length or byte limit. So
the cap bounds frames between absorb and *handoff to xrdp's own heap* —
never frames on the wire.

The transport does have one byte-level throttle,
`si->source[my_source] > MAX_SBYTES` with `MAX_SBYTES` defined as **0**
(`trans.c:35, 219, 376`): while a source's bytes sit queued, that
source's input transport is dropped from the `select()` read set. It
does not reach GFX frames. Bytes are charged only when
`si->cur_source != XRDP_SOURCE_NONE` (`trans.c:653`); `cur_source` is
set to a transport's own source only inside `trans_check_wait_objs()`
(`trans.c:396`) and restored on exit; and enc_done is delivered on a
**wait object**, not a transport (`xrdp_mm.c:4061, 4538`). At the
instant a frame is written, `cur_source` is NONE, the bytes are charged
to nobody, and nothing is throttled.

**So the client ack window is today the only rate control anywhere
between the encoder and the link.** That does not change anything the
sweep measured — every leg ran on loopback with ack latency as the sole
variable, and the client always drained — but it changes what step 3's
frozen-client leg is testing. It is not "does another bound take over";
it is "is there one at all". The leg must record buffered bytes and
process RSS, not only frames produced.

It also sharpens the fix's shape. On a link that cannot carry the
encoder's output, ungated production does not settle at
`rate x ack-latency`; the backlog grows in the server's heap until the
client catches up. `fif` is currently doing two jobs — setting the
latency target and bounding client-outstanding — and FR-ACK-3 requires
the first to hold at fif = 1, which drags the second down to 1 with it.
Removing the gate removes the second job outright; the horizon variant
`client + H > server`, H from the pipeline's own depth, separates them.
Only the second of those is a candidate for ever being the default.

## Reassessment: is the stall a bug, or the only honest thing to do on a WAN?

Asked by the owner once the unbounded-`wait_s` finding landed, and it is
the right question: if the ack window is the only rate control in the
path, then starving the producer *is* rate-matching, and the "34 % of
throughput" #76 filed might be the price of not overrunning the link.

**It is not one answer. The crossover is ack latency against frame
period, and this sweep measured both sides of it.**

*Ack latency < period.* The client's ack for frame N arrives before
frame N+1 could possibly be ready, so releasing the slot at absorb(N)
causes nothing to be held: capture N+1 and encode N+1 overlap the round
trip, and when the ack lands the frame is sent immediately.
Client-outstanding stays ≤ 1 — the fif = 1 contract is honoured — and
the period is the pipeline's, not the network's. **In this regime the
stall buys nothing at all.** The measurement says so directly: cycles
whose credit arrived promptly ran at **16.3–16.9 ms in every leg**,
including the 40 ms leg, while `pump` sat at 15.2–15.4 ms. Those cycles
are the counterfactual — the same machine, same payload, same instant,
with the gate not binding — and they are 5–6 ms per frame faster than
the mean. Nothing was traded for that; it is loss.

*Ack latency > period.* Now the pipeline can complete frames faster than
the client retires them, and every mechanism that keeps it busy
accumulates finished frames somewhere. Holding them is a queue in front
of the display — exactly what FR-ACK-3 objects to about fif = 2, just
relocated from the wire into the server. Throttling production is the
correct behaviour, and it is what the current code achieves. **In this
regime the stall is not a bug; it is the design, arrived at by
accident.**

So the honest verdict on #76's 34 %: **a bug, in the regime where it was
measured** — loopback, ack latency 7.6–10 ms against a ~16.4 ms period,
which is the LAN case the fleet and every customer on a local network
runs in. And **not a bug** in a regime this project has never measured.
The defect is not that the server throttles; it is that the throttle's
threshold is `fif`, a latency knob, so the server throttles at *one*
frame regardless of which regime it is in.

A finite horizon H makes the two regimes one mechanism. H never binds
while ack latency < period (LAN: full speed, nothing held, outstanding
≤ 1); H binds at H frames when ack latency > period (WAN: bounded queue,
at most H periods of staleness, producer throttled to the link).
Numerically, H = 2 covers this sweep's LAN legs (ack latency 7.6–10 ms,
period 16.4) and H = 3 covers ~33 ms; at D = 40 the predicted
ack latency of ~48 ms against a ~16.4 ms period gives ~3, so H = 3 is
expected to *bind* in that leg and the period to rise there. That
prediction is written into BACKLOG #79 step 4 before the run.

One more thing this reframing kills: the word **race**. #76 filed this
as a 31 % intermittent tail and #78 called it an ack race. The sweep
shows a period-3 limit cycle with zero exceptions in 871 cycles at
D ≥ 20 — the signature of a control loop at a fixed point, not of a
race. What varies at D = 0 is only whether the loop's own delay lands
inside a frame period. Calling it a race suggested the fix was
synchronisation; it is not, it is the loop's threshold.

## Was the LAN counterfactual sound? Checked, and it holds (2026-08-02)

The reassessment above leans on one number: prompt-credit cycles ran
16.3–16.9 ms in every leg, so removing the gate on a LAN costs nothing.
**That number has a selection problem, and it was quoted before the
problem was faced.** The prompt class is conditioned on the ack having
arrived in time — a statement about a cycle's history, not only its
mechanism. In the period-3 limit cycle a prompt cycle *always* follows
two stalled ones, and a stall gives the producer ~35 ms of idle time in
which capture can run ahead. So 16.4 ms might be the period of a cycle
that started from a pre-loaded pipeline, which the fixed build — where
every cycle is prompt and none follows a stall — would not inherit.

Discriminator, run on the committed captures at no session cost
(`PR-demo/mac_bisect_matrix/i79_lan_counterfactual.py`): condition the
prompt-cycle period on its **position within a run of consecutive prompt
cycles**. Position 1 may be pre-loaded by the stall before it; position
≥ 2 has no stall behind it and is the steady state the fixed build would
run in.

| leg | D | prompt-run lengths | pos 1 | pos ≥ 2 | Δ |
|---|---|---|---|---|---|
| direct | — | 127 runs, max **28**, 31 of length ≥ 6 | 16.6 (n=127) | 16.9 (n=397) | +0.3 |
| d0 | 0 | 149 runs, max **22**, 20 of length ≥ 6 | 17.0 (n=149) | 16.9 (n=301) | −0.1 |
| d10 | 10 | 170 runs, max 3 | 16.9 (n=170) | 15.9 (n=69) | −1.0 |
| d20 | 20 | 159 runs, **all length 1** | 16.3 (n=159) | — | n/a |
| d40 | 40 | 130 runs, **all length 1** | 16.4 (n=130) | — | n/a |

**The counterfactual holds, and it is stronger than an inference.** In
the LAN legs the period is flat across run position (d0: 17.0 / 17.0 /
16.7 / 17.0 for positions 1 / 2 / 3 / 4+), and the unmodified server has
already been observed running **28 consecutive frames with the gate not
binding, at 16.9 ms**. That is the fixed build's LAN steady state,
measured on HEAD. Pre-loading is dead as an explanation: a 28-cycle run
cannot be living off one stall's worth of lookahead, and there is no
drift with position.

Honest limit: this settles the LAN regime for runs up to ~28 cycles
(~0.5 s). A permanently prompt pipeline is what #79 step 4 measures, and
only that can show effects with a longer time constant.

The d20/d40 rows are not a gap in the check — they are the period-3 lock
restated: at D ≥ 20 a prompt cycle is *never* followed by another one,
so those legs contain no steady state to sample. The question they raise
was already answered in the legs that do.

## The `max()` model does not fit, and the reason matters

Owner's proposed shape, in period form (`fps = min(...)` over times is a
`max()` over periods): `period = max(ack_latency / K, compute_serial)`,
with K = fif = 1. Checked against the same captures:

| leg | D | ack latency p50 | compute (prompt p50) | predicted | measured | residual |
|---|---|---|---|---|---|---|
| direct | — | 7.5 | 16.6 | 16.6 | 21.5 | **+4.9** |
| d0 | 0 | 10.1 | 16.5 | 16.5 | 22.5 | **+5.9** |
| d10 | 10 | 17.1 | 16.1 | 17.1 | 27.9 | **+10.8** |
| d20 | 20 | 27.8 | 16.1 | 27.8 | 33.1 | **+5.3** |
| d40 | 40 | 48.1 | 15.9 | 48.1 | 40.1 | **−7.9** |

It misses in **both directions**, which rules out a single wrong
constant. Two separate causes, both already visible in the data:

* **At low D it under-predicts, because the gated cycle is ADDITIVE, not
  a maximum.** A withheld cycle waits for the client's ack *and then*
  still has to capture and encode; the two do not overlap. `max()`
  assumes the system is rate-limited by whichever ceiling is lower,
  which would be true if the gate throttled every cycle uniformly. It
  does not — it throttles a subset.
* **At D = 40 it over-predicts, because credit arrives in PAIRS.**
  `min(consumed, server + 1)` releases two frames' worth when the window
  opens, so the loop delivers more than one frame per round trip:
  3 frames per 120.4 ms against a 48.1 ms ack latency is 1.2 frames per
  round trip, not 1.

So the unmodified loop is not "clocked by the slower of two ceilings" at
all. It **alternates between two states** and the mean is a duty-cycle
average — which is exactly the closure already recorded above,
⅔·52.0 + ⅓·16.4 = 40.1. A `max()` model has no way to express a duty
cycle.

**This is a property of the broken loop, and it yields a model-level
prediction for the fix.** A horizon H converts the alternation into a
uniform rate limit: every cycle is treated the same, so the duty cycle
collapses and the system really does become rate-limited by whichever
ceiling is lower. **Prediction for #79 step 4: the fixed build IS
well-fitted by `period = max(ack_latency / H, compute_serial)`, with the
residual collapsing from +4.9…−7.9 ms to within ~1 ms across all five
legs.** That is a stronger claim than any single-metric row in the gate,
because it constrains the whole curve rather than a point, and it is
falsifiable in the same run at no extra cost. Recorded before the run.

## Terms used above, in the domain's words (added 2026-08-02 on request)

Three words in the tables are the instrument's, not the machine's. What
they mean:

**Cycle.** One frame's worth of pipeline work — the interval between two
consecutive frames reaching the transport, `egress(k−1) → egress(k)`.
That is the frame period. Every row that says "period" is a mean or
median over these.

**Prompt vs withheld (`.` vs `S`).** For each cycle the analyzer asks
one yes/no question about the *producer's* side: when the capture slot
became safe to recycle, did xrdp tell xorgxrdp straight away, or sit on
the news? The slot in question is frame **k−2**'s — with two capture
slots, the slot that frame k will use is the one k−2 just vacated, so
the credit that admits capture k is the credit for k−2. (Paired by that
identity, never by a time window.)

* **prompt** — the credit went out within 10 ms of the slot becoming
  safe. In practice ~0.03 ms: the window was already open, nothing was
  waiting on the client.
* **withheld / gated / `S`** — the credit sat for more than 10 ms
  because `client + fif > server` was false. xrdp knew the slot was
  free and would not say so until the client's ack arrived.

The 10 ms threshold is inherited from #78's ">10 ms" definition so the
two items' numbers are comparable.

**Run length.** How many prompt cycles happened back to back before a
withheld one interrupted. A run of 28 means 28 consecutive frames in
which the gate never bound — the producer was told immediately every
time. That is why run length is the load-bearing statistic and not a
curiosity: **a long prompt run is the unmodified server transiently
behaving exactly as the fixed build would behave permanently**, and its
period is therefore a measurement of the fix rather than a prediction
about it.

## So: is there a LAN fix with no tail AND a bounded wire? Yes, and it is not a compromise

The two goals only look like they conflict because one `if` is doing
both jobs today. Separate them and the LAN case has slack in it:

* **The tail comes from throttling the PRODUCER** (xorgxrdp is not told
  its slot is free).
* **The wire bound comes from throttling the CLIENT** (do not get more
  than N frames ahead of what it has acked).

Different parties. The LAN's short ack latency is what makes them
independent: bounding the client never requires throttling the producer,
because on a LAN the client is never far enough behind for the bound to
be reached.

With `client + H > server` on the slot credit, H = 3:

* **The bound is always present.** Capture k is admitted only while
  fewer than H frames are unacked, so at most H + 1 are unacked when k
  is sent — in both regimes, LAN or WAN, slow client or fast. It does
  not switch off.
* **On a LAN it never binds.** Measured client-outstanding on these
  legs is 0 or 1 in 3157 of 3161 sends (2 in four), and the fixed
  build's predicted value is `1 + ack_latency / period` = 1.6 at p50 and
  2.1 at #78's ack-latency p90. H = 3 sits above both. The gate is
  there, and nothing ever touches it.
* **The 28-frame run is the existence proof.** During it,
  `client + 1 > server` happened to hold on every frame — the client was
  never behind at all — and the machine ran at 16.9 ms against the leg's
  21.5 ms mean. H = 3 turns that from luck into a guarantee, because the
  gate is looser than what the client actually does.

So on a LAN the answer is: **no tail, bound intact, nothing traded.** On
a WAN the bound binds and costs up to H periods of staleness — that is
the real tradeoff, and it is the one worth paying, because the
alternative the plain ungate offered was no bound at all.

**H = 2 is the tempting answer and it is wrong.** It matches the
two-slot capture budget, which makes it look principled, but
client-outstanding at #78's ack-latency p90 (18.9 ms — longer than one
frame period) is already ~2.1. H = 2 would bind on ordinary client
jitter and put back a smaller version of the tail this item exists to
remove. H = 3 binds only past ~2 periods of ack latency (~34 ms), which
is genuinely the WAN regime.

**What this does NOT fix, stated so it is not assumed away.** #79
removes the tail whose mechanism layer 1 confirmed. #76 has a second,
separate open item — the unreproduced 26.7 ms `pump` on arm x015, a
bracket a client ack window cannot reach — and nothing here touches it.
If a tail survives the fixed build's LAN legs, that is where to look
next, not at the horizon.
