# BACKLOG

**What this file is:** the open work list. Nothing else.

**What it is not:** a lab notebook. Persistent decisions, contracts,
invariants, measured performance baselines and acceptance evidence live in
`PRD.md`; operational procedure lives in `DEPLOY_RUNBOOK.md`; working rules
live in `CLAUDE.md`. Incident narratives and campaign logs live in **git
history** — that is what it is for. If an entry here is still true after the
task closes, it belonged in the PRD; move it and delete it from here.

Rewritten 2026-07-28 (3268 lines), **2026-08-01 (1923 lines)** and again
**2026-08-06 (1901 lines)** — it drifts back into a lab notebook every
time, so the rule is restated plainly: **an entry here is an OPEN
question, its justification, and a pointer.** Conditions, tables,
anomalies and retractions from finished work belong in
`docs/experiments/`; durable contracts and baselines in `PRD.md`;
procedure in `DEPLOY_RUNBOOK.md`; narrative in git history. Capture
evidence stays with its capture, under
`PR-demo/mac_bisect_matrix/captures/<run>/README.md`.

If you are about to paste a results table into this file, it goes in
`docs/experiments/` and you leave a line here saying what it decided.

---

## Deployed state (2026-08-06)

* **Branches:** `/work` and `/workUpdateXorgXrdp` are both on
  `dev/avc444_metablock_checkpoint` (WIP ended 2026-08-06; merge
  `de0d92c4`, tree = `wip/eager-ack` HEAD; xorgxrdp at `10fa3aa`).
  Nothing pushed — publishing is the owner's command to run.
* **Host install:** the fixed known-good reference for bisect sessions;
  never mutated during a campaign (CLAUDE.md deployment rules).
* **Fleet:** arms are containers on `127.0.0.1:400xx`, images pinned
  per-arm in `PR-demo/mac_bisect_matrix/k8s/*.yaml`, certificates in
  `certs/`. x018/x019 carry the #80 credit-frontier build
  (`1d5bc0960db8.xx10fa3aa-tf`, `eager_slot_ack = true`,
  `wire_window = 1`); x013–x017 are pre-frontier reference arms.
* **T4:** decommissioned 2026-07-31. Its recorded numbers stay
  attributable through `docs/experiments/` and `PRD.md`; re-provisioning
  from bare AMI is #93's first step.
* The `-g 30000` interim risk and the intra-refresh/re-key contract that
  retires it live in `PRD.md` (FR-H264-6, FR-H264-8) — no longer
  duplicated here.

---

# Open work

## Execution order

The 2026-08-03 owner directive made the backlog LINEAR with #80 first.
**PRIORITY REVIEW PENDING (owner, 2026-08-06): the owner intends to
weigh everything below against #92 (4:2:0 in motion) — the order past
#80 is therefore provisional until that review.** Historical numbers
stay as "(was #NN)" in each header.

1. **#80** — credit frontier: remaining steps (rescoped 2026-08-06)
2. **#82** (was #76) — the unreproduced 26.7 ms x015 pump: reproduce or retire
3. **#83** (was #77) — a faster producer
4. **#87** (was #61d; absorbs #89/was #70B) — re-measure the emit-split and eager-ack ratios under textflood
5. **#88** (was #61g) — oracle client 50–150 ms pauses
6. **#90** (was #74) — lever-2 architecture decision
7. **#91** (was #71) — multimon per-monitor ack window + m≥2 serial cost
8. **#92** (was #72) — 4:2:0 in motion / 4:4:4 at rest (consolidated 2026-08-06)
9. **#93** (was #73; absorbs #97/was #60) — T4 re-establishment and re-runs
10. **#94** (was #53) — arm a monitor only when its pixels changed
11. **#95** (was #54) — capture-side handoff: the remaining 2×
12. **#96** (was #59) — move the pack off the X server thread
13. **#98** — flow-control survey: owner decisions + owed legs

## #80 — the credit frontier: what remains (steps 1–3 landed 2026-08-03; step 4's mechanism legs run; RESCOPED 2026-08-06)

**Done and recorded** — design, implementation behind `eager_slot_ack`
with C as `gfx.toml [avc444_ffmpeg] wire_window` (default 2 =
legacy-equivalent), CI enumeration RED-on-HEAD verified
(`tests/xrdp/test_avc444_credit_frontier.c`), LAN head-to-head vs the
old build (withheld p90 35.3 → 10.6 ms, stalls 29.7 → 18.2 %,
throughput +16 %), the 40 ms legs including the voided-and-rerun
harness incident, and the wire bound `id_server − id_client ≤ C + 2`
held on every send. Record:
`docs/experiments/80-the-credit-frontier.md`; harness record:
`docs/experiments/81-the-netem-rtt-harness.md`.

**RESCOPED 2026-08-06 (owner): the RTT → C table and "default chosen
from #81's data" are OUT of this item.** The owner's ruling: a netem
simulation cannot — and should not — choose shipped defaults or claim
validity for wild WAN environments. Real WANs add loss, cross-traffic,
variable RTT, middleboxes and unknown TCP stacks that netem does not
model, and #98 measured that at 4K the WAN constraint is TCP/byte
behaviour, not the credit count. Simulation's job here is verifying the
MECHANISM and its BOUND, nothing more. Consequences:

* The shipped default stays **C = 2**, justified as legacy
  `frames_in_flight` equivalence (coding rule 2), not as a
  simulation-derived optimum. C is documented as an operator knob; the
  closed form ⌈RTT/period⌉ + 1 is offered as guidance to validate per
  deployment, never as a certified table.
* **PRD FR-FLOW-1 clause 4 ("default chosen with #81's data") needs a
  matching amendment — owner sign-off required, not yet applied.**
* Dropped with the rescope: "enough RTT points to choose the default",
  and the old-build-under-netem A/B (it would measure TCP as much as
  the frontier — quality gate 5).

**Still open:**

1. **The freeze leg** (mechanism, from the original approved design): a
   client that stops acking mid-session → production must stop within
   C + 2 frames and the transport's queued bytes (`trans::wait_bytes`)
   must plateau. Seconds of trace; binary check.
2. **A C = 2 LAN leg** to attribute the residual 18.2 % LAN stalls — at
   C = 1 all three frontier terms tie at emission (157/157), so the
   existing records cannot say which term held (gate 2b).
3. **Step 5 — fleet A/B** on the unmodified harness, acceptance on the
   mechanism's own telemetry (the withheld distribution; fif = 2
   equivalent non-regression), before `eager_slot_ack` can be
   considered for default-on. Arm count needs owner approval.
4. **Per-monitor caution (owner, 2026-08-03):** the `C + 2·M` bound at
   M monitors is UNTESTED — everything measured is single monitor. On
   the return to 2 monitors, re-derive the bound from measurement, not
   multiplication, and only after the single-monitor stall work closes.
   (Findings (a) two-token clamp and (b) unclamped `NOT_DISPLAYED` are
   accepted as-is until a later test rejects them.)

## #82 (was #76) — the unreproduced 26.7 ms x015 pump: reproduce or retire

x015 (fif = 1) measured `pump` = 26.7 ms once; #78's controlled re-run
measured 16.40 ms — equal to fif = 2 — same host, same config, and the
reproducing part of the fif = 1 cost (the withheld slot credit) has
since been fixed by #80's frontier. What is left is ONE unreproduced
observation. Candidates: transient host power/thermal state during the
02:07 run; concurrent fleet activity (session state was not recorded
then; it is now). A rerun of the uninstrumented x015 pod needs owner
approval. If it does not reproduce, retire the observation with a dated
note. Records:
`docs/experiments/76-fif1-costs-throughput-in-a-bracket-it-cannot-reach.md`,
`docs/experiments/78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md`.

## #83 (was #77) — a faster producer

**Why.** The textflood producer runs at 16.91 ms/frame against an
18.48 ms pipeline (x014, `i75_x014_rewrite_20260801`): FR-BENCH-1
margin **1.09×, MARGINAL**. At that margin an arm measures the payload
as much as the server — 66 of 3074 cycles stall on the producer and
carry 99.7 % of all wait time, and p99 regressed 31 → 46.5 ms while the
mean improved. Every further worker-side ratio is gated on this.

**Scope.** PRD design B: memmove scroll + strip render instead of a
full redraw. Offline: 7.1 ms/frame against today's ~16 — margin ~2.6×
at the current pipeline rate, still >2× if the pipeline reaches 14 ms.

**Acceptance.** Producer p90 strictly below pipeline p10 at 3840×2400,
stated in the arm's own capture; FR-BENCH-1 margin ≥ 2.0× reported
beside every ratio thereafter.

## #87 (was #61d; absorbs #89/was #70B) — re-measure the emit-split and eager-ack ratios under textflood on the ring build

The codeflood-era ratios (#70 eager ack 1.11×, #70B emit split 0.96×)
were measured against a saturated producer, and #70B's later 1.12× was
measured with the per-frame trace on log.c (#61h) — all void. The
emit-split code ships default-off; its prerequisite refactor fixed a
real use-after-free and stands. **Work:** re-run the eager-ack A/B and
the emit-split A/B under textflood on ring-traced builds, including the
emit split at 2560×1440 — its prize is resolution-dependent (`emit` was
27 % of the serial chain at 1440p vs 17 % at 4K), so the smaller
geometry should show the LARGER ratio: a falsifiable prediction. PRD
FR-ACK-2 still names the emit split as #70's completion.

## #88 (was #61g) — the oracle client's 50–150 ms pauses: unattributed

Host CPU contention is ruled out (run-delay 0.5 % of client CPU, no
higher inside ack holes than outside), the dump is ruled out (tmpfs).
What the client does during a pause is UNKNOWN and stays that way until
someone has a cheap way to ask. Next instruments, cheapest first: the
client's own timing logs on the DVC receive path; 5 s of `perf record`
on the client pid; a one-line `/proc/<pid>/stat` delta. **Do not build
another sampler** — the last one cost 36 % of a core and moved the rate
it measured. Out of scope without owner sign-off: enlarging loopback
socket buffers (it would change what E5 means — the fallback shape the
honesty rule catches). Not a substitute for the server-side items: a
perfect client removes the forcing, not the vulnerability.

## #90 (was #74) — lever-2 architecture: DECISION OPEN (owner discussion)

Overlap `pump` (wait for the ffmpeg children) with `collect` (NUT
demux + LTR rewrite) — sized by #61e at up to 8.8 of 25.5 ms. **Do not
start implementation until the shape is decided.** Candidates:

- **A. Depth reorder, one thread** (`subm(N+1) → coll(N) → pump(N+1)`):
  ~21 ms at m=1, but only while `tail ≤ encode` (3 ms margin today),
  and a hardwired order idles the children when encode finishes early.
- **B. Submit/collect stage threads** (owner proposal): submit side
  owns stdin fds + cadence, collect side owns stdout fds + demux/LTR
  rewrite, bounded one-frame SPSC queue between; period
  `max(subm, tail, encode)`, generalises to m≥2 (~27 vs A's ~36 ms
  projected). Requires a dated amendment to PRD's "exactly one worker
  thread" paragraph.
- **C. Resumable-coll coroutines, one thread** — rejected: strictly
  heavier than a thread for the same 8.5 ms.

Constraints that survive whichever wins: the bounded queue is the
contract (the bufferbloat prohibition applies); the m≥2 arithmetic is
projected, not measured (#91 owns those numbers). The old "do #61f
step 1 first" constraint is retired — #61f's measurement showed the
delivery-loop delay IS the encode in progress, not scheduling slack.

## #91 (was #71, earlier #65) — multimon: per-monitor ack window + the m≥2 serial cost

At m≥2, two issues on top of the m=1 serializer:

1. **The ack window is global while the budget is per-monitor.** At
   m=2 the global fif=2 window admits ~1 outstanding per monitor and
   halves the intended depth. The PRD forbids widening the global pool
   (bufferbloat, measured); the spec shape is ≤2 PER MONITOR. Pinned in
   CI: `test_overlap_m2_global_window_pins_each_monitor_to_one` flips
   on the fix. Interacts with #80's per-monitor caution: the wire
   bound at M monitors must be re-derived from measurement.
2. **The m≥2 serial cost is real and unexplained by overlap alone**
   (173.7 ms period, 132.1 ms inside our pipeline at 1.33 of 4 cores,
   T4-era numbers). After the fix, re-decompose: ack pacing vs
   whole-set drain vs genuinely serial assembly.

Acceptance: per-monitor window (never a pool), CI updated deliberately,
fleet-arm decomposition showing per-monitor depth 2 at m=2, negative
arm gaps on both monitors.

## #92 (was #72, earlier #66/#63) — 4:2:0 while the screen is in motion, 4:4:4 when it settles (CONSOLIDATED 2026-08-06)

*One entry now; the motivation was previously scattered across the
T4-era decomposition (#62), the Xorg profile (#96) and the WAN budget
work (#98).*

**The idea.** While pixels are changing fast, send only the 4:2:0 main
view and drop the aux (chroma-detail) view; send the full 4:4:4 pair
when the screen settles. Motion is when chroma detail is least
perceptible and when the period is longest; a still screen is when
subpixel-AA text fringes matter and when there is time to spare.

**Why it is worth doing — three independent measurements, all
byte-based (the T4-era timing decomposition is NOT load-bearing here;
it predates the ring and is void under #61h):**

1. **The aux view is 44.8 % of the bytes** (measured from wire dumps:
   main P 2.09 MB, aux P 1.69 MB per picture) and is a second
   full-frame pack (`a8r8g8b8_to_avc444_box`, the top symbol in the
   Xorg profile) plus a second encode — it is paid in capture, encode
   AND assembly, which is why one change moves all three.
2. **On a bandwidth-limited WAN, frame BYTES are the whole game**
   (#98, measured 2026-08-06): fps = link_rate/frame_bytes within 3 %,
   and delay sits at the window bound (C+2)·S/B + RTT. At 40 ms RTT /
   200 Mbit today's build is interactive at 1080p (91 ms), marginal at
   1440p (167 ms), not at 4K (436 ms). Cutting S ~45 % in motion
   scales fps and the delay bound by the same laws — the prediction to
   verify is that 1440p moves inside the 150 ms budget.
3. **Bytes scale with pixels at ~0.39 B/px** (CQP 20 textflood,
   2.07–9.22 Mpx), so the effect of dropping the aux view is
   predictable from geometry before any run.

**Relationship to #98 Tier 2:** both reduce frame bytes. Tier 2 is a
continuous rate control (per-frame byte budget = delivered rate ×
period); this item is a structural mode switch (drop the aux view
under motion). They compose — Tier 2 sets the budget, this item is the
largest single lever for meeting it without dropping resolution — but
they are separate decisions, and the owner is weighing this item's
priority against the tier ladder now.

**Open questions to settle BEFORE implementing:**

1. **What is "in motion"?** A cheap, deterministic signal — damaged
   area per cycle, or consecutive damaged cycles above a threshold. It
   must not flap: 420/444 oscillation is visible chroma breathing on
   static text.
2. **How does the settle transition avoid a visible pop?** The aux
   chain is an LTR chain (#44/#45): resuming after a gap needs its own
   intra, or the chain must survive the motion window unreferenced.
   Interacts with `intra_refresh_frames` and wire-audit ratchets A1–A7.
3. **Does the client tolerate an alternating stream?** The EGFX
   capability is negotiated once. Verify on Mac and Windows clients
   first — this is the class of change that produced the wrong-colour
   bisect.
4. **Is the win real?** Predicted: ~45 % off the encode segment,
   roughly half the pack. Measure with textflood and the period
   decomposition, same pair, same box — no rate number without the
   decomposition.
5. **Does the old blocker still bind?** The entry used to say "blocked
   by #71 — FR-PROC-7's preemption signal needs the fifo non-empty at
   pop time, which needs #70/#71 concurrency first". That was written
   before #80's frontier changed the admission mechanics; whether it
   still holds is a question to answer by reading, not a fact to carry.

**Acceptance criteria:** motion detector is pure logic with unit tests
under `tests/`; default OFF (absent/invalid config reproduces today's
behaviour exactly); smoke gate PASS before any measurement; wire audit
A1–A7 PASS in both regimes; the E5-2 pair re-run and DECOMPOSED, not
just rated; a still-screen visual check that subpixel-AA text is 4:4:4
sharp.

## #93 (was #73; absorbs #97/was #60) — T4 re-establishment and re-runs

The numbers the PR sells, re-measured on the representative box. The T4
is gone; re-provision from bare AMI (DEPLOY_RUNBOOK + persistent
harness — must reach measurable with no ad-hoc steps). Then:

* E5-2 m=1 4K and the m=2 A/B, decomposed, FR-BENCH-1 saturation
  checks green, on ring-traced builds.
* **The E5-2 bimodality root cause (absorbed #97):** sixteen repeats
  split into two tight clusters (47–49 vs 67–74 ms) differing in
  BYTES per picture (580 KB @ 92 ms vs 875 KB @ 140 ms) — two
  self-consistent equilibria, selector unknown. Ruled out: codec
  fallback, corpus position, the profiler, compositing, leftover
  probes, GPU clocks. Until root-caused, T4 ratios need ≥180 s runs,
  both arms in one sitting, and matching mean bytes/picture — else the
  pairing measures content. Worth trying: pin the payload's write
  rate; log per-picture size from session start.
* Owner onscreen walk (UWP + macOS) on the fixed build.

## #94 (was #53) — arm a monitor only when its pixels changed

**Why.** With one active monitor beside an idle one the batch is ~9 %
SLOWER than serialized (68.5 vs 62.6 ms/send): the idle monitor reports
full-monitor damage every cycle and the shared deadline ties the active
monitor to the idle one's full capture + upload + encode. An ordinary
desktop shape, so a real regression, not a bench artifact.

**Scope.** Per-cycle set membership on evidence of changed pixels, not
damage-rect coverage. Cheapest first: (a) drop a monitor whose previous
pair coded all-skip below a byte threshold with unchanged damage rects;
(b) capture-side changed-region test in xorgxrdp; (c) per-monitor
deadline so an idle monitor cannot hold a healthy one. Must NOT drop a
real update.

**Gate.** The first flood pair (ink on one monitor) currently reads
0.91×; acceptance ≥1.0× with the 2.13× two-monitor result unchanged
within noise — both arms re-measured. E2 clean, no monitor left
un-updated over 180 s.

## #95 (was #54) — capture-side handoff: the remaining 2×

**Why.** The encode side is no longer the constraint (worker 32 % busy,
14.7 ms service against a 59.9 ms per-monitor period); after a frame's
`last=1` the same monitor's next damage arrives 41–105 ms later. Flow
control never binds; not bandwidth. The wait is the capture handoff.

**Scope** — #45's two deferred capture questions: (1) what sets a
monitor's floor period (deferred-update pacing vs ack-budget retirement
in xorgxrdp), measured per stage; (2) whether the producer can hand
BOTH monitors over in one cycle.

**Gate.** Same instrument as E5-2: ≥1.5× on top of 2.13×, worker busy
>60 %, `last=1 → next own dmg` below per-pair service time. Under 1.5×
the remainder is attributed, not re-tuned.

## #96 (was #59) — move the pack off the X server thread

The T4 profile (record:
`captures/e52_t4_batched_20260730/CPU_BOTTLENECK.md`) put xorgxrdp's
capture at 13.8 % of the saturated session-Xorg thread — payload
rendering owns 44.9 % — and the AVX2 kernels agree with the offline
bench to 5 %, so the kernels are not the lever. **The one lever that is
ours:** the ~12 ms pack sits on the single thread the whole session
queues behind. Hand xrdp a raw XRGB snapshot and pack in the
encoder-side worker — same arithmetic, off the critical path. (The
`present_fake` config lever was WITHDRAWN 2026-07-31 — we neither own
nor ship that code; the rule it left: when a profile blames code we do
not own, stop generating the work, do not retune the other component.
Detail in git history and `docs/experiments/59-61-corrected-attribution.md`.)

## #98 — flow-control survey: owner decisions + owed legs (filed 2026-08-04)

The survey (`docs/research/flow-control-survey-2026-08.md`) confirmed
our WAN equations are textbook (BBR operating region, Little, RFC 7661;
bound to chase: send-to-ack ≥ RTT + S/B) and set the tier ladder.
Everything measured so far is in
`docs/experiments/98-tier0-bbr-ab.md` + capture READMEs; one line each:

* **Tier 0 A/B (2026-08-05):** BBR + ssai=0 + wmem on the pod at 40 ms
  → send-to-ack 204.2 → **50.1 ms** (theory floor ~50–55), fps 11.5 →
  36.6, transport queue 6.65 MB → 25 KiB. Confirmed at the floor.
* **Bandwidth-limited (2026-08-06):** declared tbf bottleneck; fps
  tracks B/S within 3 %, delay flat at 0.72–0.90 of the window bound
  (C+2)·S/B + RTT — graceful by the pre-registered definition; cubic ≈
  bbr when the link binds. Interactivity at low rates needs smaller
  frames (Tier 2 / #92).
* **Resolution sweep (2026-08-06):** trend holds at 1440p/1080p;
  ~0.39 B/px invariant; budget map at 40 ms/200 Mbit: 1080p 91 ms,
  1440p 167 ms, 4K 436 ms.
* **Logistics:** all per-netns sysctls and per-socket options are
  settable from here; BBR needed the metal-host `modprobe tcp_bbr`
  (owner ran it 2026-08-05, persisted via modules-load.d).

**Decisions this item needs from the owner, in order:**

1. **Adopt the field's figures of merit in the PRD** (p95/p99 per-frame
   delivery delay at a quality floor + stall rate + quality×delay
   Pareto) beside or instead of fps-at-RTT — changes what every future
   WAN gate asserts. Industry budgets: <100 ms gaming-class, <150 ms
   RTC-class.
2. **Tier 1 experiment** (`TCP_NOTSENT_LOWAT` + userspace frame pacing
   in xrdp — the standing queue moves into xrdp where damage-coalescing
   can eat it). Includes the shipping-shape question: per-socket
   `TCP_CONGESTION` in xrdp vs documented host guidance. Arm count +
   wall time stated before running.
3. **Whether Tier 2 (encoder rate adaptation) enters the roadmap** as
   its own item — a new control loop, not a tweak. Weigh against #92,
   which is the largest single-lever byte reduction (they compose).

**Still owed measurements:** LAN regression check of Tier 0 (BBR at
0.07 ms RTT); C > 1 under Tier 0 at 40 ms (model predicts ~56 fps at
C = 2).

**Interaction with #80:** none of this reopens #80's steps 1–3. The
2026-08-06 rescope removed the C-table entirely; the closed form
⌈RTT/period⌉ + 1 survives as operator guidance to validate, not as a
default-selection procedure.

---

# Closed — records in `docs/experiments/`

Each line is what the work decided. The conditions, tables and
retractions are in the linked file; the code and full backlog bodies
are in git history.

| item | outcome | record |
|---|---|---|
| **#45** intra refresh, one-thread `pump_set`, per-monitor capture budget | DONE. E5 resolved by #52 at 2.13×. | [`45-intra-refresh-and-pump-set.md`](docs/experiments/45-intra-refresh-and-pump-set.md) |
| **#52** E5-2 saturated-payload frame interval | DONE. 2.13× GREEN; retired the 51.1 ms cadence baseline as a reading of a 10 Hz metronome. | [`52-e5-2-saturated-payload.md`](docs/experiments/52-e5-2-saturated-payload.md) |
| **#55** E5-2 on the T4 | DONE. 1.5×–2.3× AMBER, attributed to a saturated Xorg. T4 now decommissioned. | [`55-e5-2-on-the-t4.md`](docs/experiments/55-e5-2-on-the-t4.md) |
| **#59/#60** T4 attribution corrections | Recorded. Capture is 13.8 % of the bottleneck thread; the bimodality moved to #93. | [`59-61-corrected-attribution.md`](docs/experiments/59-61-corrected-attribution.md) |
| **#61** GLAMOR on NVIDIA | CLOSED-WONTFIX — renders black. | [`61-glamor-on-nvidia.md`](docs/experiments/61-glamor-on-nvidia.md) |
| **#61b** textflood in the fleet | DONE. Plumbing shipped; ring-build numbers re-established: producer 16.91 ms vs pipeline 18.48 ms — margin 1.09×, → #83. Textflood is the standard throughput payload; codeflood ratios measure the X server. | capture `i75_x014_rewrite_20260801` |
| **#61c** is the producer the ceiling | DONE. Yes, under codeflood (Xorg saturated with no client connected); resolved by the textflood switch. The question is now carried live by FR-BENCH-1 margin reporting in every capture. | captures deleted by #61h (git history) |
| **#61e** period attribution at m=1 | DONE (redone on the ring). Period 25.474 ms = encode 16.654 + LTR rewrite 8.802; `capture ‖ encode` HOLDS at m=1 (worker wait 0.0019 ms) — because encode is slow; a faster encoder reopens it. | [`61e-the-period-is-encode-and-rewrite.md`](docs/experiments/61e-the-period-is-encode-and-rewrite.md) |
| **#61f** delivery-loop latency | CLOSED 2026-08-06. The enqueue→submit delay (15.3 ms) is the worker still encoding — recoverable share 0.01 %, so no scheduling/handoff fix exists; the prize moved to #90 (overlap). Egress batching was the wrong lever and is reverted (`b588a954`). | [`61e-the-period-is-encode-and-rewrite.md`](docs/experiments/61e-the-period-is-encode-and-rewrite.md) |
| **#61h** per-frame trace off log.c | DONE. The logger was in the measurement; ring costs ~7 µs/frame armed, ~40 ns disarmed — quotable as transparent. Voided 25 captures + 3 records; reopened items now #82–#89. | [`61h-the-logger-was-in-the-measurement.md`](docs/experiments/61h-the-logger-was-in-the-measurement.md), [`61h-what-the-ring-costs.md`](docs/experiments/61h-what-the-ring-costs.md) |
| **#62** textflood payload | DONE. 1.41× RED, then annotated producer-confounded. | [`62-textflood-payload.md`](docs/experiments/62-textflood-payload.md) |
| **#64** rect_id ack "ghost" | CLOSED. Root cause REFUTED — the ack is an echo and never drifted. | [`64-rect-id-ack-ghost.md`](docs/experiments/64-rect-id-ack-ghost.md) |
| **#70** eager slot-release ack | DONE, shipped default-off. 1.11× (codeflood-era, re-measure = #87). Incomplete without the emit split per FR-ACK-2. | [`70-eager-slot-release-ack.md`](docs/experiments/70-eager-slot-release-ack.md) |
| **#75** LTR rewrite re-serialised the picture | DONE. 11.0 → 1.03 ms/pair offline, period 25.474 → 18.476 ms on-arm, output byte-identical (goldens untouched). Left FR-BENCH-1 margin at 1.09× → #83. | [`75-the-rewrite-was-re-serialising-the-picture.md`](docs/experiments/75-the-rewrite-was-re-serialising-the-picture.md) |
| **#78** split the `pump` bracket | DONE. FEED 2.61 / ENCODE 13.38 / DRAIN 0.41 ms; falsified its own baseline (fif=1 pump = fif=2's) → #82; the reproducing fif=1 cost was the ack-gated slot release → fixed by #80. | [`78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md`](docs/experiments/78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md) |
| **#79** ungate the eager slot ack → horizon form | MERGED INTO #80. Plain ungate rejected (no bound exists past the window — `trans_write_copy_s` cannot fail); horizon form superseded by the credit frontier. Layer-1 sweep confirmed the withheld-credit mechanism causally. | [`79-layer1-the-ack-delay-sweep-confirms-the-withheld-slot-credit.md`](docs/experiments/79-layer1-the-ack-delay-sweep-confirms-the-withheld-slot-credit.md) |
| **#81** netem RTT harness | DONE. Both-direction delay on the pod veth pair, selftest incl. throughput + zero-drops (after the limit-1000 incident voided the first WAN leg); ack-delay proxy retired. | [`81-the-netem-rtt-harness.md`](docs/experiments/81-the-netem-rtt-harness.md) |
