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

1. **#80** — credit frontier: freeze leg and fleet A/B DONE 2026-08-06;
   what remains is the per-monitor bound and the default-on decision
2. ~~**#82** — the unreproduced 26.7 ms x015 pump~~ **RETIRED 2026-08-06**
3. ~~**#83** — a faster producer~~ **LANDED 2026-08-06**, acceptance met
4. ~~**#87** — emit-split and eager-ack ratios~~ **CLOSED 2026-08-06**
5. **#88** (was #61g) — oracle client 50–150 ms pauses
6. **#90** (was #74) — lever-2 architecture decision
7. ~~**#91** — multimon window + m≥2 serial cost~~ **CLOSED 2026-08-08** — our code does not serialise the two screens
8. **#92** (was #72) — 4:2:0 in motion / 4:4:4 at rest (consolidated 2026-08-06)
9. **#93** (was #73; absorbs #97/was #60) — T4 re-establishment and re-runs
10. **#94** (was #53) — arm a monitor only when its pixels changed
11. **#95** (was #54) — capture-side handoff: the remaining 2×
12. **#96** (was #59) — move the pack off the X server thread
13. **#98** — flow-control survey: owner decisions + owed legs
14. **#99** — the gate cannot tell "wrong target" from "no records" (filed 2026-08-06)
15. **#102** — a client displaces one screen by 33 px until minimise+restore (filed 2026-08-08; DOCUMENT ONLY by owner ruling, below #92)
16. ~~**#100** — remove the emit thread~~ **DONE 2026-08-07** (measured: it bought nothing at one monitor)

## #80 — the credit frontier: what remains (steps 1–3 landed 2026-08-03; step 4's mechanism legs run; RESCOPED 2026-08-06)

**Done and recorded** — design, implementation behind `eager_slot_ack`
with C as `gfx.toml [avc444_ffmpeg] wire_window` (default 2 — but see
the correction below: it is NOT legacy-equivalent), CI enumeration
RED-on-HEAD verified
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

* **The shipped default is UNRESOLVED, and the reason changed on
  2026-08-07.** Two findings, in order:
  * **C = 2 is not legacy-equivalent.** Legacy grants credit up to
    `client + fif - 1` = `client + 1` (`xrdp/xrdp_encoder.h:40-44`); the
    frontier clamps at `client + C` (`:114-130`). With the two capture
    slots that is `client + 3` today against `client + 4` at C = 2.
    Measured: maximum outstanding 3 on legacy, 3 at C = 1, 4 at C = 2.
    **The legacy-equivalent window is C = 1.**
  * **But C = 1 reproduces the DEFECT as well as the behaviour.** At
    matched encoder speed, legacy and the frontier at C = 1 stall
    identically (withheld p90 10.50 vs 10.70 ms; 16.4 % vs 16.3 % of
    cycles; worker idle p90 8.67 vs 8.94 ms), while C = 2 removes it
    (0.037 ms, 1.0 %, 0.002 ms). One config line, stall on and off:
    **the term that holds the credit is the end-to-end window, not the
    emission point.** This also closes #80's open attribution question,
    which the emission-time record could not answer because all three
    terms tie there.
  * **SETTLED 2026-08-07: the change is an EXTENSION, and the old knob
    cannot substitute.** Widening the legacy window to 3 gives the same
    bound the frontier has at C = 2 (client + 4, measured, distance 4
    reached) and leaves the stall exactly where it was — withheld p90
    10.57/10.39 ms and 17.6/15.6 % of cycles, against 10.50 ms and
    16.4 % at window 2. The frontier in the same sitting: 0.048/0.040 ms
    and 3.9/2.6 %, with period p90 19.6/19.2 against 26.5/26.5 ms. Same
    cost, none of the benefit. The reason is in the code and was written
    down before the run: legacy grants `frame_id_server`, which advances
    at EGRESS (`xrdp_mm.c:1772`, `:4423`), so no window value lets the
    producer start a capture that depends on a frame still inside the
    encoder; the frontier's `frame_id_consumed` / `server + 1` terms are
    what express that (`xrdp_encoder.h:114-130`, `xrdp_mm.c:4366`).
    Captures `i80_c1_nonregression_20260807_141752_s20` (window 1 is
    today's behaviour, measured identical) and
    `i80_widen_legacy_20260807_143617_s20` (the widened knob, with its
    prediction pre-registered).
  * **The upstream sentence, therefore:** window 1 is today's behaviour
    bit for bit; the frontier adds a pipeline-state term the old gate
    has no variable for; window 2 is what makes it reachable. One design,
    one reason, and the cost stated — one more frame outstanding, which
    the old knob also costs without buying anything.
  * Open, and it is the upstream-facing question: present ONE design
    with one firm reason (owner, 2026-08-07 — maintainers should not be
    handed two ambiguous options for a breaking change to 2017-2018 era
    code). The closed form ceil(RTT/period)+1 stays operator guidance to
    validate per deployment, never a certified table.
* **PRD FR-FLOW-1 clause 4: AMENDED 2026-08-07 (owner).** The
  simulation-chooses-the-default requirement is withdrawn, and the
  clause's two contradictory formulas are resolved to the measured one,
  `C + 2·M`. The default stays C = 2, unscaled by monitor count, with
  the per-screen consequence documented in `gfx.toml(5)` instead —
  option 3 of the four laid out in
  `docs/experiments/91-the-multimon-window-and-the-shared-pump.md`,
  chosen after the window-4 leg showed a multiplier would not fix the
  two-monitor stall.
* Dropped with the rescope: "enough RTT points to choose the default",
  and the old-build-under-netem A/B (it would measure TCP as much as
  the frontier — quality gate 5).

**RUN 2026-08-06 (owner-approved) — the freeze leg and the fleet A/B
both landed, and items 1–3 below are CLOSED.**

* **Freeze leg GREEN, and the bound is ATTAINED rather than merely
  held.** Arm x018 at C = 1, the test client's process group stopped
  mid-leg. The last client acknowledgement was frame 362; exactly one
  further frame (365) reached the network and production then stopped —
  365 − 362 = 3 = C + 2 exactly. Zero credit records were emitted in the
  10 s that followed, while the trace kept recording throughout, so
  production stopped and the instrument did not. Transport queued bytes
  went 1109 → 4703 KiB and could not grow further, no frame being
  produced. Capture `i80_freeze_20260806_180613_s20`. Caveat in the
  record: stopping the process freezes reading as well as acknowledging,
  which is a HARSHER condition for the queue than an ack-only freeze.
* **The A/B ran as one experiment covering items 2 and 3** (arms x020
  legacy / x021 frontier at C = 2, four interleaved legs; runner
  `i87_eager_ab.sh`, capture `i87_eager_ab_20260806_180910_s20`). The
  wait between the encoder absorbing a frame's pixels and the producer
  being told it may capture again falls from p90 10.6 ms with 16.4 % of
  cycles waiting over 10 ms, to p90 0.04 ms with 1.0 % — and it holds in
  both host states the run encountered. Frame period, in the pair where
  the encoder ran at its normal speed: mean 19.11 → 17.68 ms, p90
  26.58 → 19.28, p99 30.16 → 24.16. Nothing regressed.
* **First live run of the shipped default.** Every `wire_window` on
  record before today was 1; x021 is the first arm to encode a frame at
  C = 2.
* **Read the A/B within pairs only.** Between the first and second pair
  the wait for the ffmpeg children moved 16.3 → 26.2 ms on BOTH arms
  with every other stage unchanged — a host-level encoder slowdown. GPU
  DVFS is plausible but was NOT measured during the legs.
* **The wire bound held and WAS exercised — correcting a first reading
  of this run.** An initial pass called it a vacuous pass on the grounds
  that "nothing is ever outstanding at send time"; that statistic came
  from two fields of the network-write record which are zero by
  construction (the trace only fires for chunks carrying bytes, and the
  message bearing a frame's terminal marker carries none). Read from the
  egress record, which carries the frame's own id and the client's last
  acknowledged id, the treatment arm reached its bound of C + 2 = 4
  once and came within one of it 14 times. On loopback it is rarely
  approached — 99.5 % of frames sit 1 or 2 ids ahead of the client —
  but "rarely approached" is not "never exercised".

**Still open:**

1. **Per-monitor: MEASURED 2026-08-07, and one half does not
   generalise.** The `C + 2·M` bound holds at M = 2 exactly — maximum
   frames outstanding 6 on the frontier at C = 2 and 5 on the legacy
   path, in four legs each, never exceeded — so the frontier costs ONE
   more frame than legacy at two monitors, not two, and the excess does
   not scale with monitor count. But the stall is only HALVED, not
   removed: 21.5/22.0 % of cycles still wait over 10 ms against
   50.4/54.4 % on legacy, where at one monitor the same change reached
   under 1 %. **Likely cause, filed rather than measured:** the
   frontier's `client + C` term is a single session-wide number while
   the capture budget is per monitor, so at M = 2 a window of 2 allows
   about one outstanding frame per screen — exactly the defect #91
   records for the legacy window, inherited. A leg at `wire_window = 4`
   would settle it; not run, because making C per-monitor is #91's
   decision. Captures `i80_multimon_strip_20260807_152223_s20` (readable)
   and `i80_multimon_20260807_151836_s20` (same result, rates
   producer-limited at 0.51x). **Consequence: "the frontier removes the
   producer stall" is a ONE-MONITOR claim and must be written that way
   upstream.**
2. **Superseded caution (owner, 2026-08-03), kept for the record:** the `C + 2·M` bound at
   M monitors is UNTESTED — everything measured is single monitor. On
   the return to 2 monitors, re-derive the bound from measurement, not
   multiplication, and only after the single-monitor stall work closes.
   (Findings (a) two-token clamp and (b) unclamped `NOT_DISPLAYED` are
   accepted as-is until a later test rejects them.)
2. **Whether `eager_slot_ack` may default ON.** Still open, but the
   objection that blocked it is now answered. The flag is ANDed with
   `aux_ltr_chain`, which is EXPERIMENTAL and default-off, so flipping
   this alone is a no-op in a default install and a live change only for
   operators already on the experimental path. Recommendation stands:
   fold the flip into `aux_ltr_chain`'s acceptance gate rather than
   deciding it separately. Owner sign-off required either way (PRD: no
   default change without it).
   - **The "never seen a realistic acknowledgement time" objection is
     RESOLVED, 2026-08-08.** An owner onscreen walk on x027 put the
     frontier in front of a real client that acknowledges after decoding
     and presenting: ack latency p50 **68 ms** (one monitor) and **53 ms**
     (two), against ~9 ms for the oracle client. The window filled and
     its bound held exactly — distance reached 4 on **173** frames at
     M = 1 and 6 on **73** frames at M = 2, against 1 frame in 953 for
     the oracle client on loopback. Zero encoder restarts, sequence
     mismatches, parser errors or pair timeouts. Owner's visual verdict:
     no lag. Record:
     `PR-demo/mac_bisect_matrix/captures/i80_onscreen_walk_x027_20260808/`.
   - **The two-monitor half is the more valuable one:** the `C + 2·M`
     bound was previously measured only against the oracle client, where
     the wire barely approaches it. This is the first time the wire was
     actually made to carry it.
   - **What is still owed before "qualified" is written anywhere:** the
     visual half is PARTIAL — the owner reported no lag on the payload
     they ran, not the full six checks across both clients at both
     sizes; and the client PRODUCT is not recorded in the session log, so
     "both clients" is not evidenced. The criterion itself, both halves,
     is now written down in `PR-demo/INTERACTIVE_ARM.md`.

## #102 — a client displaces one screen by 33 px until it is minimised and restored (filed 2026-08-08, owner-reported; DOCUMENT ONLY, owner decision)

**Owner ruling 2026-08-08: option 1 — record it and move on. This is
below #92 (4:2:0 in motion) and nothing here is to be worked before it.**

**The symptom.** Two monitors, 3840x2160 each, side by side on the
owner's client. The left screen's content is drawn about **33 px to the
right** of where it belongs: a 33 px black strip down the far left of
the left screen, and 33 px spilling onto the right screen. It persists
from connect. It is NOT cleared by a full-screen repaint. It IS cleared
by minimising and restoring the client window.

**NOT the monitor misalignment, and the earlier derivation is
WITHDRAWN.** The client had also been declaring its second monitor 6 px
lower, giving a 7680x2166 desktop with two 6-px strips covered by no
surface. That was real and produced its own artefacts, and "33 = 27 px
panel + 6 px offset" looked convincing. **The owner aligned the displays
and reconnected: the 6 px is gone, the 33 px stays.** The layout is now
textbook — screen 7680x2160, monitors at +3840+0 and +0+0, surfaces
mapped to match, no gaps — and the fault is unchanged. The arithmetic
coincidence was a coincidence; the panel is also now 43 px tall rather
than 27, so neither term survives.

**What is ruled out, each by measurement rather than argument:**
* *Our pixels.* The X framebuffer was scanned pixel by pixel with the
  desktop set flat white: 1045 non-white pixels, all of them
  `colorkey_x11`'s own text label. No hole anywhere.
* *Failing to send it.* Damage bounding boxes are traced per surface;
  the right monitor received full-monitor damage `(0,0,3840,2160)` 61
  times in one 4000-event window.
* *Stale pixels.* A full-screen repaint does not clear it.
* *Flow control.* The stall during the minimise was 258.7 s with the
  credit unused — distance 1 before and after.
* *Encoder geometry.* All four children `coded 3840x2160 generation 1`.
* *Our arithmetic.* The normalisation is exactly derivable from the
  client's declared layout, and **no value we send is 33, on any axis.**

**The one remaining server-side anomaly, and the leading hypothesis:**
on CONNECT the EGFX surfaces are created during capability negotiation
— before login, before `lib_mod_connect`, before xrdp has spoken to the
X server at all — so the framebuffer is resized **2.1-2.8 s later**
(measured across three connects: 2.69, 2.13, 2.80 s). On a RESIZE the
order is the other way round and the gap is 0.01 s. If the client fixes
its canvas layout during that window it fixes it against a framebuffer
that does not exist yet, and a minimise/restore is exactly the event
that makes it revisit. **Unproven: when a client computes its canvas
layout is not observable from this side.**

**NOT OURS as far as can be checked:** `git diff devel..HEAD` changes
zero lines touching the resize/monitor plumbing in `xrdp_mm.c`,
`xrdp_egfx.c`, `xrdp_wm.c`, and
`libxrdp_init_display_size_description` is byte-identical to `devel`.

**If this is ever picked up, the experiment that decides it** is the
ground-truth rig (`PR-demo/win2022_ground_truth/`): put the same layout
in front of a stock Windows Server 2022 host and diff its
`RESET_GRAPHICS` and surface mappings against ours. If Microsoft's
server produces the same shape and the client renders it correctly
there, the difference is ours and is the bug.

**Two free improvements found along the way, neither done:**
1. **Suppression is invisible in release builds.** A client can stop all
   display output indefinitely and xrdp logs nothing —
   `xrdp_rdp_process_suppress` (`libxrdp/xrdp_rdp.c:1474`) logs only at
   `LOG_DEVEL`. Measured here: 258.7 s of total silence, unlogged. This
   caused a wrong reading of the fault. One INFO line each way.
2. **No adjacency or gap validation exists.** Monitor sizes and the
   encompassing box are checked; nothing rejects or warns about a layout
   leaving interior strips covered by no monitor. One warning line.

**Coverage gap this exposed, and it is the durable lesson:** every
two-monitor rig in this tree builds perfectly aligned dummy outputs and
connects once. Neither a misaligned arrangement nor a
reconnect-into-a-wider-layout is a shape any automated test here can
produce, which is why a human at a real client found both in minutes.

Evidence: `captures/i102_wedge_live_20260808/`.

## #99 — the gate cannot tell "wrong target" from "no records" (filed 2026-08-06)

A run whose client dialled a DIFFERENT arm from the one the harness
collected logs for produced no readable result, which is correct — but
NOT because the guard designed for this fired. `e_gate_run.sh`'s span
guard only trips when the rendered trace spans TOO LONG, and it is
skipped entirely when the trace has fewer than two records, which is
exactly what a wrong target yields. What actually caught it was
`perf_trace_lines.py`'s "every record fell outside the window" message
plus the E5 parser reporting zero send records — both loud, neither the
check that was supposed to own this.

**Work:** assert positively that the arm the client dialled is the arm
whose logs were collected. The pod's own identity is already available
on both sides (the session log names it, and the gate knows the port to
arm mapping it was given), so this is a comparison, not new machinery.
Add the "trace has too FEW records for the run length" case to the span
guard while there.

**Why it matters:** the failure is silent in the direction that counts.
A swapped pair yields an empty trace, and an empty trace is
indistinguishable from an idle session unless something asserts identity.

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

**IN PROGRESS.** Design and server implementation are done and green in
CI (2026-08-08); the spec is PRD FR-H264-9 and the record of what was
decided and why lives there, not here. What remains is hardware: the
arm, the throughput A/B, and the onscreen judgement.

The five questions above, answered:

1. **What is "in motion"?** Settled by owner ruling as option
   "B + refresh bound": two configured TIMES and no pixel signal at
   all. `chroma_refresh_ms` is a guarantee (chroma restored at least
   this often, whatever the screen is doing) and `chroma_idle_ms` is
   how long the pipeline must be quiet before chroma is sent, which
   also clamps the aux rate. Flapping is bounded by construction
   rather than by a hysteresis heuristic.
2. **How does the settle transition avoid a visible pop?** Structurally
   there is nothing to re-seed: LT1 keeps the last chroma picture
   across any number of luma-only frames, so the next aux P predicts
   from it. Proved in the DPB simulator in both client decode shapes.
   Whether it *looks* like a pop is question 3's territory and is
   still open.
3. **Does the client tolerate an alternating stream?** STILL OPEN, and
   it is the one that cannot be answered offline. A luma-only frame is
   an LC=1 PDU with no LC=2 behind it, which is exactly what the LC
   field is for, but "spec-legal" is not "renders correctly on
   VideoToolbox" — that is the distinction the wrong-colour bisect was
   made of.
4. **Is the win real?** Not measured. Next step.
5. **Does the old blocker still bind?** No: the skip lives in
   submit/pump/collect, which is the batch path #70B already
   restructured, and needs nothing from FR-PROC-7's preemption signal.

**Remaining acceptance criteria** (the met ones and the full table are
in PRD FR-H264-9): smoke gate PASS on the arm before any measurement;
wire audit A1–A7 PASS in both regimes, with A3 exempt under a sparse
cadence by owner ruling; the client tolerates an alternating stream on
the macOS and Windows clients **before any rate is quoted**; the
throughput pair re-run and DECOMPOSED, not just rated; a still-screen
visual check that subpixel-AA text is 4:4:4 sharp.

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
| **#82** the unreproduced 26.7 ms x015 encoder wait | RETIRED 2026-08-06. Third independent null: the wait is EQUAL at frames-in-flight 1 and 2 (16.21/16.31 vs 16.16/16.20 ms, interleaved, same sitting). The period difference is the worker idling on a withheld credit — 0.58/0.79 ms against 3.76/4.98 ms once the one session-startup bracket per leg is excluded, which closes 97.6 % of the period gap — #79's defect, fixed by #80. The ~26 ms condition was then seen live on BOTH A/B arms at once, so it is a host state, not a frames-in-flight property. | capture `i82_x015_rerun_20260806_181825_s20` |
| **#83** a faster benchmark producer | DONE 2026-08-06. `--scroll strip` (default off) takes the payload 16.2 -> 4.5 ms/frame deployed; FR-BENCH-1 margin 1.05x -> 3.94x and acceptance met (producer p90 5.8 ms below pipeline p10 15.75 ms). The control is the more useful result: a 3.6x faster producer did NOT change the pipeline's rate, so the textflood series was pipeline-limited, not producer-clocked. | capture `i83_strip_payload_20260806_182340_s20` |
| **#87** emit-split and eager-ack ratios | CLOSED 2026-08-06. Emit split RETIRED unrun — the stage is 0.333 ms at 4K against the 6.39 ms its requirement rested on, which came from a build with two per-frame log writes inside the timed bracket; PRD FR-ACK-2's table, projection and ship-together clause deleted. The eager-ack half was measured in #80's merged A/B. | [`87-the-emit-split-was-measuring-its-own-logger.md`](docs/experiments/87-the-emit-split-was-measuring-its-own-logger.md) |
| **#91** multimon window + m≥2 serial cost | CLOSED 2026-08-08. Three answers: the window divides by monitor count (a frame id is one monitor's frame, and the window is session-wide); widening it does not fix the two-monitor stall, so the default is NOT scaled by M and the per-screen consequence is documented in `gfx.toml(5)`; and **our code does not serialise the two screens** — the encodes overlap, and what staggers them is our own raw-input transfer. Carries one retraction, and one question left open rather than answered: what sets the rate at which the ffmpeg children take their input. | [`91-the-multimon-window-and-the-shared-pump.md`](docs/experiments/91-the-multimon-window-and-the-shared-pump.md) |
| **#100** remove the emit thread | DONE 2026-08-07. The assembly thread, its two semaphores, its depth-1 hand-off slot, its join, its unarmed-drop counter and the `gfx.toml emit_thread` key are gone; assembly runs inline on the encoder worker. The separation of assembly from the encode path (the use-after-free fix) stays, and an old `gfx.toml` still loads with one warning. **Scoped to one monitor** — the thread is not shown to be worthless at m >= 2. | [`100-the-emit-thread-bought-nothing.md`](docs/experiments/100-the-emit-thread-bought-nothing.md) |
