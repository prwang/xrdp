# BACKLOG

**What this file is:** the open work list. Nothing else.

**What it is not:** a lab notebook. Persistent decisions, contracts,
invariants, measured performance baselines and acceptance evidence live in
`PRD.md`; operational procedure lives in `DEPLOY_RUNBOOK.md`; working rules
live in `CLAUDE.md`. Incident narratives and campaign logs live in **git
history** — that is what it is for. If an entry here is still true after the
task closes, it belonged in the PRD; move it and delete it from here.

Rewritten 2026-07-28 (3268 lines) and again **2026-08-01 (1923 lines)** —
it drifts back into a lab notebook every time, so the rule is restated
plainly: **an entry here is an OPEN question, its justification, and a
pointer.** Conditions, tables, anomalies and retractions from finished
work belong in `docs/experiments/`; durable contracts and baselines in
`PRD.md`; procedure in `DEPLOY_RUNBOOK.md`; narrative in git history.
Capture evidence stays with its capture, under
`PR-demo/mac_bisect_matrix/captures/<run>/README.md`.

If you are about to paste a results table into this file, it goes in
`docs/experiments/` and you leave a line here saying what it decided.

---

## Deployed state (2026-08-01)

| Box | Packages | Encoder config | Status |
|---|---|---|---|
| ~~T4 (EC2, Tesla T4 / NVENC, 4 vCPU)~~ **DECOMMISSIONED 2026-07-31** — kept only so its recorded numbers stay attributable to a config | `xrdp-dev 0.10.80+git20260730013346.52b8798839ad` (#45 steps 0–7 + log clock fix), `xorgxrdp-dev 1:0.10.80+git20260729225933.d77d05463e52` (step 6) | `PR-demo/t4_profile/gfx-t4-nvenc-ltr-g240-gate.toml` — `aux_ltr_chain = true`, `intra_refresh_frames = 240` | **#55 E5-2 = 1.67× AMBER**, wire audit 7/7, smoke gate PASS at both sizes. **Box is gone**: #73's re-runs and #60's un-root-caused bimodality need a new reference target, and onscreen (UWP/macOS) was never walked |
| T4 — previous state (2026-07-28, for reference) | `xrdp-dev 2a0279ef3aa1`, `xorgxrdp-dev 5b9650cafbc3` | `gfx-t4-nvenc-ltr.toml` — `-g 30000` | Rendered correctly onscreen on Windows (incl. multimon) and macOS (owner-tested) |
| bisect fleet arm-n | image `34795577580b.xx5b9650c-xfce` | `gfx/arm-n.toml` — `aux_ltr_chain = true`, no `-g` (the runner pins it) | good on Windows multimon + macOS |
| bisect fleet arm-r (2026-07-29) | image `f7acb5979788.xxd77d054` = xrdp #45 steps 0–7 + xorgxrdp step 6 | `gfx/arm-r.toml` — `aux_ltr_chain = true`, `intra_refresh_frames = 240` | #45 gates E1/E2/E3/E6/E7 PASS; its E5 number was payload-clocked (see `docs/experiments/45-intra-refresh-and-pump-set.md`). Kept as the 10 Hz-cadence reference arm |
| bisect fleet arm-s (2026-07-30) | image `52b8798839ad.xxd77d054` = xrdp #45 steps 0–7 + the log clock fix, xorgxrdp step 6 | `gfx/arm-s.toml` (encoder block identical to arm-r), `SESSION_KIND=codeflood` | **#52 E5-2 arm: 29.9 ms mean per send = 2.13× over arm-t** — the #45 E5 gate, GREEN |
| bisect fleet arm-t (2026-07-30) | image `5dae11f63adb.xxd77d054` = xrdp #45 steps **0–4** + the log clock fix, xorgxrdp step 6 | `gfx/arm-t.toml` (identical encoder block), `SESSION_KIND=codeflood` | #52 E5-2 **baseline** arm: 63.6 ms mean per send. Same xorgxrdp as arm-s, so the A/B isolates steps 5+7 |

FR-H264-8 remains **EXPERIMENTAL**; `aux_intra_leaf` remains the shipped
default. Gate status and evidence: `PRD.md` FR-H264-8.

> **`-g 30000` is a known-risky interim, not the target state.** Corrected
> 2026-07-29 (arm-o): it does NOT *remove* mid-stream IDRs, it makes them
> rare — the main child still emits a GOP IDR every 30 000 pairs (~11 min
> at the measured 43.8 pairs/s), each still paying the ~630 ms aux-child
> respawn. It also removes the only mechanism bounding invariant **I3**
> (transitive dependency depth), so an encoder/decoder divergence would
> persist until reconnect. **And it makes the re-key structurally dead:**
> a main IDR resets the shared counter (`xrdp_h264_annexb.c:2450`), so at
> `-g 30000` the counter peaks at 60 000 and can never reach the 65 024
> threshold. The frame_num wrap is therefore being prevented by the GOP
> IDR *by accident*, not by the re-key mechanism designed for it — change
> `-g` and that protection silently changes character. The re-key only
> fires first if `-g > 32512`. **Revert target is fixed** (#45 D7): `-g` =
> `intra_refresh_frames` = 240, so GOP boundaries coincide exactly with
> scheduled refresh indices and an unscheduled IDR ceases to be reachable.

---

---

# Open work

## Execution order (owner directive, 2026-08-03): the backlog is LINEAR

#80 is the first FR-FLOW-1-conforming design and BLOCKS everything
below it. Open items are renumbered #82+ into one line. Historical
numbers stay as "(was #NN)" in each header and body cross-references
keep the old numbers — this table is the map. DONE items keep their
numbers.

1. **#80** — credit frontier (absorbs #79; steps inside the item)
2. ~~**#81** — WAN RTT simulation harness (netem on the fleet netns)~~ **DONE 2026-08-03**
3. **#82** (was #76) — the unreproduced 26.7 ms x015 pump: reproduce or retire
4. **#83** (was #77) — a faster producer
5. **#84** (was #61f) — delivery-loop latency: encoder ack-clocked through a busy main thread
6. **#85** (was #61c) — is the producer the ceiling (re-run on the ring)
7. **#86** (was #61b) — textflood numbers re-established
8. **#87** (was #61d) — codeflood-era ratios under textflood
9. **#88** (was #61g) — oracle client 50–150 ms pauses
10. **#89** (was #70B) — does the emit split buy anything
11. **#90** (was #74) — lever-2 architecture decision
12. **#91** (was #71) — multimon per-monitor ack window + m≥2 serial cost
13. **#92** (was #72) — 4:2:0 in motion / 4:4:4 at rest
14. **#93** (was #73) — T4 re-runs under restored concurrency
15. **#94** (was #53) — arm a monitor only when its pixels changed
16. **#95** (was #54) — capture-side handoff: the remaining 2×
17. **#96** (was #59) — capture share of the bottleneck thread
18. **#97** (was #60) — T4 E5-2 bimodality
19. **#98** — act on the flow-control literature survey (filed 2026-08-04; owner decisions needed on FoM + tier)

## #82 (was #76) — fif = 2 is hiding a bug (owner directive, 2026-08-02) — REFRAMED by #78's runs; the fix moved to #79, since merged into #80

**Status after #78 Runs A/B (see #78, captures
`i78_x017_pumpsplit_20260802` / `i78_x014_fif2_clocks_20260802`):**

* **The pump inflation this item was filed on did not reproduce.**
  fif = 1 pump = 16.40 ms = fif = 2's 16.44, same hour, same host,
  clean fleet, config identical to x015 but for two ring records.
  x015's 26.7 ms is one unreproduced observation; its record carries a
  dated supersede note. Open sub-question: what condition produced it
  (candidates: transient host power/thermal state during the 02:07 run;
  concurrent activity — the fleet-session state was not recorded then,
  it is now, `fleet_sessions_after_runA.txt`). Reproduce-or-retire; a
  rerun of the uninstrumented x015 pod needs owner approval.
* **The fif = 1 defect that DOES reproduce** is the ack-window-gated
  slot release chaining capture to the client round trip — mechanism,
  trace and candidate fix in #78 item 3. That is the remaining
  substance of this item.
* Hypotheses 1 (cold capture pages) and 2 (10 ms poll timeout) were
  refuted from the existing x014/x015 rings before the runs; hypothesis
  3 (duty-driven clocks) is refuted as the steady-state explanation by
  the runs themselves (pump equal at different duties, 52.9 vs 58.8 W).

**Original framing below, kept for the record.**

**The reframing, and it is the whole item.** An earlier draft of this
entry treated "does fif = 2 buy anything?" as the question and filed the
34 % throughput loss at fif = 1 as an unexplained curiosity to be
instrumented later. That is backwards. **If dropping to one frame in
flight costs 34 % of throughput, the pipeline is leaning on a second
in-flight frame to cover a stall — and the stall is the bug.** The design
target is stated in PRD **FR-ACK-3**: *all the concurrency we need, at the
cost of fif = 1.* The client ack window bounds what the CLIENT has
outstanding; it is not a mechanism this server may use to obtain
concurrency for itself.

**Why it cannot be flow control, which is what makes it a bug.** The
encoder's own depth is provably one frame: `pump_pairs` waits for the set
it just submitted and `collect_pair` verifies `desktop_sequence`, so a
second frame is never inside a child. And the worker is never starved —
its `wait` bracket is **0.002 ms/cycle** at fif = 1 (0.515 at fif = 2).
There is no queue for the second credit to fill and no idle worker for it
to feed. It is covering something else.

**Measured** (arms x014/x015, identical image and `gfx.toml` body, one
environment variable apart; mechanism confirmed — all 8056 `send` records
read `fif=1`, `id_server − id_client` = 0 on 8052 of them):

| | x014 fif=2 | x015 fif=1 |
|---|---|---|
| send interval mean / p50 / p99 | 18.5 / 18 / 47 ms | 28.2 / 28 / 32 ms |
| **`pump`** (feed + encode + drain) | **16.6 ms** | **26.7 ms** |
| `coll` (LTR rewrite) | 1.363 | 1.498 |
| `wait` (worker had nothing) | 0.515 | 0.002 |
| capture → egress | 34.8 ms | 55.6 ms |
| egress → client ack | 14.4 ms | 6.0 ms |
| capture → client ack | 49.2 ms | 61.5 ms |

**Where the bug is: 100 % of the regression is inside `pump`** — feed the
NV12 in, wait for the children, drain the coded bytes out. Nothing about
a client ack window has a route into that bracket:
`frames_in_flight` is read in exactly two places, `xrdp_mm.c:1691`
(gating `mod_frame_ack` to xorgxrdp) and `:4234` (the trace line), and
neither is on the encode path. Cycle closure residual is 0.004 ms, so it
is not a mislabelling.

**Ruled out.** Coded bytes per view +0.76 % (1 734 414 → 1 747 572);
producer unchanged (15.94 → 16.26 ms/frame); host dump I/O *lower*
(10.67 → 7.04 GB); all five E2 error counters 0; the knob demonstrably
applied.

**Open hypotheses, in the order to try them.** All concern what changes
inside `pump` when the frame the worker feeds was captured 17.9 ms ago
instead of 7.3 ms ago (the measured fifo residency):

1. **The capture pages are colder.** The children read 27.6 MB/frame of
   borrowed capture shmem by page reference (`vmsplice`, FR-PROC-6). At
   fif = 1 those pages were written 2.5× longer ago. This predicts FEED
   grows and ENCODE does not — which #78's instrument distinguishes in
   one run.
2. **`pump_set`'s poll loop.** Its timeout becomes a flat 10 ms once
   nothing is left to write (`xrdp_encoder_ffmpeg.c`, `timeout =
   want_write_any ? deadline - now : 10`). Worth reading against a
   cadence change before blaming hardware.
3. **CPU/GPU clock behaviour under a slower duty cycle** on this
   shared-memory APU. Last, because it is the least actionable and the
   easiest to reach for.

**Method note:** #78's instrument is the first step, not another arm.
Splitting `pump` into FEED and ENCODE+DRAIN discriminates hypothesis 1
from 2 and 3 immediately, and it is a one-record-per-cycle change.

**Record:** `docs/experiments/76-fif1-costs-throughput-in-a-bracket-it-cannot-reach.md`.
**Capture:** `PR-demo/mac_bisect_matrix/captures/i76_x015_fif1_20260802`.

## #78 — split the opaque `pump` bracket (DONE, 2026-08-02)

**Decided:** the split (`feedend`/`outfirst`, commit `661ff5fc64fa`)
measured FEED 2.61 / ENCODE 13.38 / DRAIN 0.41 ms and **falsified its
own baseline** — fif = 1 pump equals fif = 2's (16.40 vs 16.44 ms), so
x015's 26.7 ms is unreproduced (→ #76's open sub-question) and the
reproducing fif = 1 cost is the ack-gated slot release (→ **#79**, the
fix item). The ~5 ms deployed-vs-probe gap sits inside ENCODE, both fif
modes equally. Duty-driven DVFS refuted as the steady-state
explanation (pump equal at 52.9 vs 58.8 W GPU duty).
**Record:** `docs/experiments/78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md`
(includes the metal-host governor-pinning procedure for chasing the
unreproduced condition).
**Captures:** `i78_x017_pumpsplit_20260802`, `i78_x014_fif2_clocks_20260802`.

## #79 — MERGED INTO #80 (2026-08-03; was TOP PRIORITY). The horizon form is superseded by the credit frontier; layer-1 evidence (step 1, DONE) and the retargeted steps are carried by #80. Body kept for the record.

> **RESCOPED 2026-08-02 (owner directive), and the item's original title
> is now a REJECTED design.** This item was "ungate the eager slot ack".
> Plain ungating — removing the window test from the slot ack with
> nothing in its place — is **unconditionally rejected**, per the
> FR-ACK-3 amendment written the same day: it would leave the encoder
> with no rate control of any kind against a link slower than its
> output, and `trans_write_copy_s()` absorbs the difference into an
> unbounded heap list. **`eager_slot_ack`'s default-false status is not
> a mitigation** — a flag decides who discovers an unbounded queue, not
> whether it exists — so the rejection does not depend on opt-in status
> and there is no "evaluation-only" exemption. What #79 builds is the
> horizon form: `client + H > server` on the SLOT ack, H from the
> pipeline's depth, with the client ack window moving to where it
> belongs (egress). Steps 2–4 below are rewritten accordingly; the
> layer-1 result in step 1 is unaffected.

**The defect** (evidence: `captures/i78_x017_pumpsplit_20260802`,
analysis in its README and #78; **confirmed causally by the layer-1
ack-delay sweep, `captures/i79_x017_ackdelay_20260802_s20` —
step 1 below**). `xrdp_mm_update_module_frame_ack`
(`xrdp_mm.c:1697`) emits BOTH producer acks — the ordinary/region ack
and the #70 eager SLOT_ONLY ack — only while
`xrdp_gfx_ack_window_open(client, server, fif)` is true. PRD #70
specifies the eager ack's emission point as **max(absorb N,
egress N−1)**; the client's ack appears nowhere in it, and the in-tree
safety condition is the absorb frontier alone.

**Correction, 2026-08-02 (this entry previously called the window gate
"an implementation artifact of where the emission code lives" — that is
wrong and reading the emission code says so).** The gate is deliberate
and documented: `xrdp_mm.c:1692` states "the client's own ack window
stays the OUTER gate in both modes: a client that stops acking still
stops the producer". So the gate has a JOB, and ungating the slot ack
removes it. **The throughput defect and the liveness bound are two
separate things sharing one `if`, and the fix must be judged on both** —
hence the validation gate at step 3, whose safety leg exists for exactly
this.

**Second correction, 2026-08-02 (same day): what this entry said would
replace the gate does not exist.** The paragraph above originally
continued: with the slot ack ungated the loop "keeps running until the
transport stops draining, `frame_id_server` stops advancing, and the
`server + 1` cap bites — a bound, but a socket buffer's worth of frames
rather than one". **There is no such backstop.** `frame_id_server`
advances when the frame reaches `trans_write_copy_s()`, which cannot
fail for want of a wire (`common/trans.c:644-676`), and the transport's
one byte throttle never charges GFX frames (details in the side-effects
section below, clauses under "CORRECTION"). So `min(consumed,
server + 1)` bounds frames between absorb and *handoff to xrdp's own
heap*, never frames on the wire, and the plain ungate has no bound at
all. This is what rescoped the item.

**And the gate is on the wrong stage in the first place.** The window is
applied to the producer's slot credit, while EGRESS is ungated
(`xrdp_mm.c:4320` writes unconditionally) — measured: at D = 40, 2/3 of
all sends went out with 1 or 2 frames unacked, under a fif = 1 window
that forbids any. So `frames_in_flight` has never bounded
client-outstanding directly; it bounds capture admission, and bounds the
client only emergently by starving the producer. Recorded as the
FR-ACK-3 amendment in `PRD.md` (2026-08-02), whose clause 1 is what
steps 2–4 now implement: the ack window gates egress, a pipeline horizon
gates the slot credit, and one comparison may not serve both.

**Why fif = 2 masks it, quantified (both #78 runs, same day, same
harness).** Window-open at the absorb instant needs
`cliack(server)` already in. At fif = 2 the needed ack is one FULL
period older (client ≥ N−2, whose egress was ~1.5 periods before
absorb(N)) — the ack race has a period of headroom and loses only on
p99 client hiccups. At fif = 1 the needed ack is `cliack(N−1)`, whose
egress was ~8–10 ms before absorb(N), against an egress→cliack of
p50 8.5 / p90 18.9 ms — a near-fair race. Measured withheld-credit
(credit_emit − absorb(k−2)): **fif = 1: p50 0.04 ms, p90 36.4, >10 ms
in 31.1 %** (782/2513; 336 of 338 worker waits >20 ms id-match these);
**fif = 2: p50 0.02, p90 0.05, >10 ms in 1.5 %** (46/3141, 23 waits
>20 ms). The SAME mechanism at fif = 2 is the long-known p99 send tail
(47 ms in `i75_x014_rewrite_20260801`, 27 ms in Run B; the "66 of 3074
cycles stall on the producer" note in the PRD concurrency table) —
one defect explains both arms' tails, scaled by the window depth.

**The fix.** Emit the SLOT_ONLY ack from
`xrdp_mm_update_module_frame_ack` OUTSIDE the window-open branch
(target unchanged: `xrdp_mm_frame_slot_ack_target` =
min(consumed frontier, server+1) — which IS the PRD emission point;
`target > frame_id_server_sent` monotonicity guard unchanged). The
ordinary ack stays window-gated. NOT in scope: gating egress by the
window (today egress is not gated at all — measured send-time
id_server−id_client reaches 1 in 1566/5030 payload sends and 2 twice
at fif = 1, up to 3 at fif = 2 — so fif's client-facing bound is
currently emergent, not enforced; making the window actually gate
egress, per FR-ACK-3's stated purpose, is a separate decision with its
own risks, filed here as an open question, not bundled).

**Pre-implementation verification (blocking).** The emission-order
comment says the slot ack for a frame "never runs ahead of the region
retirement it depends on". Today's eager design already lets the slot
target run ONE ahead of `frame_id_region_sent`; ungated it can run
2+ ahead while the window is closed. Before coding, read xorgxrdp's
SLOT_ONLY handler (xorgxrdp `10fa3aa23033`, the paired deb — source
outside this tree) and confirm slot release does not consume region
retirement state, or emit any DUE region ack (its own guard,
`server > region_sent`, references egress only) alongside. Whichever
holds becomes a stated invariant in the commit.

**Blast radius.** Reaches only: GFX + encoder + `eager_slot_ack =
true` (config default off in the binary; armed per-arm). Non-eager
configs emit no slot ack — byte-identical behaviour. Non-GFX uses a
different ack path — untouched. At fif = 2 (shipped default) the fix
changes the 1.5 % tail cycles only; expected effect is an IMPROVED
p99, and any change beyond that is a regression to investigate.

**Test method, in ladder order. The defect looks like a heisenbug (a
31 % race tail) but is not one: the race only decides HOW OFTEN the
losing state is entered; the emission decision inside that state is a
pure function of (client, server, consumed, region_sent, server_sent,
fif, eager). Each layer below removes the nondeterminism instead of
sampling it — and layer 1 has now shown this is not a hopeful framing:
under injected ack delay the "tail" resolves into a period-3 cycle with
ZERO exceptions in 871 cycles.**

1. **Deterministic reproduction on UNMODIFIED code — DONE
   2026-08-02, mechanism CONFIRMED by intervention.** Record:
   `docs/experiments/79-layer1-the-ack-delay-sweep-confirms-the-withheld-slot-credit.md`;
   capture `captures/i79_x017_ackdelay_20260802_s20`; harness
   `PR-demo/mac_bisect_matrix/{ack_delay_proxy.c, ack_delay_sweep.sh,
   i79_ack_delay_analyze.py}` + its self-test. Five legs on arm x017
   (deployed HEAD, fif = 1): no-proxy, then D ∈ {0, 10, 20, 40} ms of
   client→server delay, 20 s each. Control OK (proxy at D = 0 within
   4.3 % of no-proxy; both reproduce #78 Run A). Delay confirmed
   in-server, not from the proxy: `egress→cliack` +7.0/+17.8/+38.0 ms.
   **Δwithheld/ΔD = 1.10; period 22.5 → 40.1 ms = 0.44 ms per ms of
   ack delay; and the added period lands ENTIRELY in the withheld
   cycles — the prompt-credit class is flat at 16.3–16.9 ms under a
   40 ms ack delay, `pump` flat at 15.2–15.4.** The rival (egress
   itself ack-gated) is excluded: at D = 40 exactly ⅓ of sends carry
   2 unacked frames and ⅓ carry 1.
   Two predictions were FALSIFIED as written, both from treating a
   closed loop as open: withheld p50 came out 24.1/36.0/57.0 rather
   than ≈ D (right slope, ~13 ms offset — the rate drops too, so the
   opening ack is itself later), and the stall fraction saturates at
   **exactly 2/3**, not 100 %. The reason is the finding: at D ≥ 20
   the system locks into a **deterministic period-3 cycle,
   `SS.SS.SS.`** — two captures withheld, one prompt, zero exceptions
   in 480 and 391 cycles — because the credit target
   `min(consumed, server+1)` releases the pair when the window opens.
   **The 31 % "heisenbug" tail is that same cycle, intermittently
   modulated by the ack race.** Deviation recorded: the item said 5 s
   per leg; at 5 s the gate yields 45–51 usable cycles and the control
   leg failed on noise (kept: `..._s5`), so the legs were rerun at
   20 s — a sample-size correction to the same five legs.
   (After the fix exists, the same sweep separates the builds: fixed
   predicts withheld ≈ 0 and period FLAT in D.)
   **LAN regime SETTLED from these same captures, 2026-08-02, at no
   session cost** (`i79_lan_counterfactual.py`). The "prompt cycles run
   at 16.4 ms" evidence was conditioned on the ack having arrived, so it
   could have been measuring cycles pre-loaded by the stall in front of
   them. Conditioning the prompt period on POSITION within a run of
   consecutive prompt cycles kills that: the `direct` leg contains a run
   of **28 consecutive ungated frames at 16.9 ms**, d0 a run of 22, and
   the period is flat across positions (d0: 17.0 / 17.0 / 16.7 / 17.0 at
   positions 1 / 2 / 3 / 4+). **The unmodified server has therefore
   already been observed running the fixed build's LAN steady state.**
   Limit stated: settled for runs to ~28 cycles (~0.5 s); a permanently
   prompt pipeline is what step 4 measures. The d20/d40 legs contain no
   runs longer than 1 — that is the period-3 lock restated, not a gap.
   The owner's `period = max(acklat/K, compute)` model was checked on
   the same data and does NOT fit at K = 1 (residuals +4.9 / +5.9 /
   +10.8 / +5.3 / −7.9 ms): it misses in both directions because a
   gated cycle is ADDITIVE (wait for the ack, then still capture and
   encode) and because credit arrives in PAIRS at high D
   (`min(consumed, server+1)` releases two), giving 1.2 frames per round
   trip rather than 1. The unmodified loop is a duty cycle, which
   `max()` cannot express — and that is what makes the model a
   prediction for the fix rather than a dead end: a horizon makes every
   cycle uniform, so the fixed build SHOULD fit. Added as a row in step
   4's table.
2. **THE CHANGE — a pipeline horizon on the slot credit, not an
   ungate** (rescoped 2026-08-02; the plain ungate is rejected, see the
   note at the top of this item and PRD FR-ACK-3 amendment clause 3).
   * The eager SLOT_ONLY ack is gated on `client + H > server` with
     **H from the pipeline's depth, not from `frames_in_flight`**.
     **H = 3, decided 2026-08-02.**
     * **CORRECTION, same day: the derivation committed a few hours
       earlier was wrong and is retracted.** It said "client-outstanding
       at #78's ack-latency p90 (18.9 ms) is ~2.1, so H = 2 has no
       headroom and would bind on ordinary client jitter." That was
       arithmetic on a p90, never checked against the quantity the gate
       actually compares. Measured directly
       (`i79_wedge_timeline.py`, the `server − client` value at every
       absorb instant): **direct leg 0 in 68.6 %, 1 in 31.3 %, 2 in
       0.1 %; d0 leg 0 in 61.9 %, 1 in 38.0 %, 2 in 0.1 %** — and the
       single "2" in each leg is the **last frame of the capture**,
       whose ack the trace ended before recording (the only ids never
       acked are the final three of each run, contiguous otherwise). So
       **H = 2 would have gated ZERO times in 745 and 713 genuine
       decision points.** "Binds on ordinary jitter" was false.
     * **What the data can and cannot decide.** It cannot separate
       H = 2 from H = 3, and the reason is the selection effect this
       item keeps re-encountering: HEAD stops the producer the moment
       `server − client` reaches 1, so the trajectory is *prevented*
       from reaching 2. A number measured under H = 1 cannot estimate
       how often H = 2 would bind once H = 1 is gone.
     * **What it can decide, and does.** The intervals are not
       selection-bound. Ack round trip (direct leg): p50 **7.6**, p90
       17.9, p99 20.1, **max 27.3 ms**. Predicted fixed-build period
       ~16.9 ms. `server − client` ≈ ack_latency / period, so the
       observed MAXIMUM round trip gives 27.3 / 16.9 = **1.6 → reaches
       2**: H = 2 would gate at roughly the top 1 % of round trips,
       H = 3 not until ack latency exceeds 2 periods (~33.8 ms), above
       everything observed. **H = 3 clears the observed maximum with
       ~1.5x headroom; H = 2 clears it with none.** That is the whole
       basis for preferring 3, and it is a margin argument, not a
       claim that 2 is broken.
     * #61g's documented 50–150 ms oracle-client pauses exceed both.
       Gating there is correct — the client really did stop.
     * H is a compile-time constant pinned by CI, not tuned against a
       run, and H = 1 must reproduce HEAD exactly (see below).
   * **What this bounds on the wire.** Capture k is admitted only when
     `client + H > server` held at the credit for k−2, so at most H − 1
     frames are unacked when a capture starts and at most **H + 1** are
     unacked when it is sent. That is a hard bound in frames, present in
     both regimes, and it is what amendment clause 2 requires. It is
     approximate at the +1 because egress is still ungated; **#80 makes
     it exact.**
   * The ordinary/region ack and egress keep the CLIENT's window
     (`fif`), which is the quantity that window is for. One
     comparison must not serve both jobs (amendment clause 1).
   * `min(consumed, server + 1)` stays exactly as it is. It is a
     pipeline-internal ordering constraint and this item does not
     touch it; the second correction above is only a statement that it
     was never a *wire* bound and must not be cited as one.
   * **Not in scope, and named so it is not done by accident:** moving
     the ack window onto egress properly (today egress writes
     unconditionally, `xrdp_mm.c:4320`). Amendment clause 1 requires
     it; it is a separate behaviour change with its own risk, and it
     is filed as **#80** rather than folded in here. #79 is complete
     without it — with H in place, the slot credit no longer depends
     on the client at all, so #80 changes only what the client holds.
   * H = 1 must reproduce HEAD's behaviour exactly at fif = 1
     (`client + 1 > server` is the current test). That is the
     regression guard and it is a CI assertion, not a claim.
3. **CI — replay + enumeration of the now-VALIDATED mechanism, and it
   must be RED on HEAD first.** Extract the emission decision into a
   pure helper next to `xrdp_gfx_ack_window_open`. Two test shapes:
   (a) **Replay test**: drive the helper through a captured stall,
   asserting after EVERY event which acks are due. Expected values
   from the PRD #70 emission point (max(absorb N, egress N−1)), not
   from the implementation. **Use the layer-1 D = 40 capture, not the
   #78 p90 stall**: layer 1 showed the withheld cycles are a fixed
   3-frame pattern, so the sequence to replay is the one that repeats
   160 times without exception — absorb 74 → egress 74 → absorb 75 →
   egress 75 → cliack 73 → cliack 74 → cliack 75 (capture 76's stall,
   transcribed in the layer-1 record). A test built on a
   once-observed p90 sample would have been a weaker claim about the
   same defect.
   (b) **Exhaustive interleaving enumeration**: all orderings of
   {cliack, egress, absorb} events over a 3-frame window at fif = 1
   and fif = 2 — the state space is small enough to enumerate
   completely, so no "did we pick the right interleaving" residue.
   **Acceptance criterion for the tests themselves: they FAIL on HEAD
   at the absorb steps** (the withheld emission) before the fix lands.
   A detector that cannot fire on the buggy code confirms nothing
   (2026-07-31 lesson).
4. **VALIDATION GATE — re-run the layer-1 sweep against the FIXED build
   and check it against predictions written NOW (owner, 2026-08-02:
   after implementation and CI, before any performance A/B in the
   wild).** Same harness, same arm config, same five legs
   (`ack_delay_sweep.sh`, D ∈ {none, 0, 10, 20, 40}, 20 s each,
   ~4 min), so HEAD and FIXED differ by the deb and nothing else. The
   point is not "is it faster" — the fleet A/B answers that — it is
   **does the change act on the mechanism layer 1 demonstrated, and
   what does it cost.** Predictions, HEAD measured → FIXED expected:

   | quantity | HEAD (measured) | FIXED (predicted) | falsified if |
   |---|---|---|---|
   | withheld p50 | 0.04 → 57.0 ms with D | ≤ 0.1 ms at every D | it rises with D at all |
   | withheld p90 | 35.4 → 77.1 ms | < 2 ms at every D | > 5 ms in any leg |
   | stall fraction | 30 → 67 % | < 1 % at every D | > 5 % in any leg |
   | `SS.` pattern | period-3 lock at D ≥ 20 | no `S` runs at all | any periodic `S` structure survives |
   | period mean | 22.5 → 40.1 ms | 16.5–17.5 ms, \|Δperiod/ΔD\| < 0.05 | slope > 0.1 (something else consumes cliack) |
   | period vs fif = 2 | 22.5 vs 18.1 | **≤ 18.1** (FR-ACK-3's actual requirement) | above it — fif = 1 still costs throughput |
   | pump | 15.2–15.4 flat | unchanged | it moves — the fix touched encode |
   | model residual, `period = max(acklat/H, compute)` | **+4.9 / +5.9 / +10.8 / +5.3 / −7.9 ms** — misses in both directions | within ~1 ms in all five legs | any leg off by > 3 ms — the horizon did not make the loop uniform |
   | id_server − id_client | capped at 2 by the stall | **rises to min(H, ≈1 + D/period)** and NEVER exceeds H | it stays ≤ 2 at D = 40 (credit still gated elsewhere — the "win" came from somewhere unaccounted for) **or** it exceeds H in any leg (the horizon does not hold — a RED result, stop) |

   The last row is not a bonus metric, it is the cost side and it must
   move — and with the rescope it is now **two** assertions in one, a
   floor and a ceiling. The starvation was *accidentally* enforcing the
   client-outstanding bound; H is what replaces it, so the run must show
   both that the accidental bound is gone and that the deliberate one
   binds. At H = 3 the D = 40 leg is the interesting one: predicted
   ack latency ≈ 48 ms against a ≈ 16.4 ms period gives ≈ 3, i.e. H is
   expected to bind exactly there and the period to rise toward
   ack_latency/H rather than stay flat. **That is not a falsification of
   the fix** — it is the WAN regime of amendment clause 4 appearing on
   schedule, and the period row's "flat in D" prediction therefore
   applies to D ≤ 20 only. Stated before the run, per gate 3.

   **Safety leg (new, and the reason the gate is not just the sweep
   again): the frozen client.** `ack_delay_proxy -F <secs>` stops
   forwarding client→server mid-session while the video direction keeps
   flowing — a client that stops acking but keeps reading, which is what
   `xrdp_mm.c:1692` says the window is there for. Run it on HEAD and on
   FIXED and count frames produced after the freeze instant.
   * HEAD: the producer must stop within ~1–2 frames (window closes,
     no credit, capture stops). This leg also proves the leg itself
     works before it is used to judge the fix.
   * FIXED (rewritten 2026-08-02 by the rescope; **the version of this
     bullet dated the day before predicted the stop would come "via
     egress → transport backpressure → `frame_id_server` stalls → the
     `min(consumed, server + 1)` cap". That path does not exist**, see
     the second correction above — which is precisely why the plain
     ungate is now rejected and this leg is judging the horizon form
     instead.) With H in place the producer must stop **within H frames
     of the freeze**, by the horizon itself and by nothing else. Record
     the frame count, the buffered bytes in the transport's `wait_s`
     chain, and the process RSS — the queue is in the heap, so bytes
     and RSS are the only things that can show it.
   * **Acceptance, decided now, before the run: FIXED stops within H
     frames, and `wait_s` bytes plateau.** More than H frames means the
     horizon is not being applied on the path that matters and the
     result is RED — stop and diagnose, do not reach for a larger H.
     Frames stopping but bytes still growing means something else is
     queueing and the leg has found a second defect.
   * Run HEAD's leg too, unchanged: it is the positive control that
     proves the freeze mechanism works before it is used to judge
     anything (HEAD must stop within ~1–2 frames).

   **Instrumentation needed before this gate runs — two spare trace
   fields, no new mechanism (decided 2026-08-02).** The existing ring
   answers everything else: `send` already carries id_server and
   id_client (that is where the 556/552/552 histogram came from),
   `withheld` is derivable from `ackslot` + `absorb`, `pump` is
   bracketed. Two gaps, both filled by populating fields that are
   already reserved and already zero:
   * **`ackslot` field d ← `frame_id_client`** (`xrdp_mm.c:1733` emits
     `target, server, consumed, 0, 0, 0`). Without it the gate's
     decision is inferred; with it, every emission is exactly
     reconstructable, and after the fix it is what distinguishes "H
     bound" from "the client was slow" — the difference the whole item
     turns on. Costs nothing: the field is written either way.
   * **`egress` field c ← the transport's PENDING BYTES**
     (`xrdp_mm.c:4293` emits `frame_id, displayed, 0, 0, 0, 0`).
     Required by the frozen-client safety leg and by #80: the queue is
     in the heap, so bytes are the only thing that can show it. **It
     must be an O(1) running counter maintained inside `trans` on
     append and drain — NOT a walk of the `wait_s` chain per frame.**
     A per-frame list walk is coding rule 5's exact trap: an instrument
     whose cost grows with the quantity it is measuring, on the path
     being measured.

   **What this gate can no longer settle, and where it went.** Until the
   rescope, this gate carried an open design question — "plain ungate
   versus horizon-H, let the safety leg choose". **It is closed by
   inspection, not by measurement**: the plain ungate has no bound to
   measure, so there was never an experiment that could have chosen it.
   Recorded rather than deleted, because the reasoning that made it look
   like a live question — trusting the `min(consumed, server + 1)`
   comment's word "BACKPRESSURE" without following `frame_id_server` to
   the call that advances it — is the mistake worth remembering. The
   remaining open question is only the VALUE of H (2 or 3), and that is
   a design choice pinned by CI, not a knob to tune against a run.

5. **Fleet A/B on the unmodified harness, gated on the mechanism's own
   telemetry, not the noisy tail.** Owner-sized arms (proposed: fixed
   deb at fif = 1 vs the committed x017 capture; a fif = 2 arm for
   non-regression). Acceptance metric is the **withheld distribution**
   (782/2513 > 10 ms → expect ~0; binomially decisive in one 60 s
   run), with the period p90/p99 improvement reported as the
   downstream effect. fif = 2 non-regression: withheld 1.5 % → ~0,
   p99 improves, mean unchanged. `arm_certify.sh` on deploy; smoke
   gate before any handoff; capture → cliack latency accounting in
   the same runs (the recorded eager-ack tradeoff must be measured,
   not assumed).

**Side effects / regression surface, stated up front.**
* Capture age at fif = 1 rises: capture runs slot-bounded ahead again,
  frames queue on the fifo (x015-shaped residency, ~1 pump time), so
  photon-latency per frame can rise while throughput and tail improve —
  this is the RECORDED #70 tradeoff ("up to one encode-time"), now
  exercised every cycle. Measure, don't assume, in step 4.
* Client-outstanding at fif = 1 no longer has any enforcement. It sat
  at ≤ 2 only because the starvation stopped the capture loop; ungated,
  it becomes rate x client-ack-latency, so a SLOW client widens it
  without limit. Layer 1 measured the proxy analogue of a slow client:
  HEAD capped at 2 under a 40 ms ack delay. The fixed build must be
  measured at the same D values (step 3) and the growth reported as the
  fix's cost. If a hard client-facing bound is wanted, that is either
  the separate egress-gating decision above or the slot-ack horizon
  variant in step 3.
  * **CORRECTION 2026-08-02, read in code, not measured: "until
    transport backpressure" (written the day before) is WRONG, and it
    was the load-bearing half of that sentence.** There is no transport
    backpressure on this path at all.
    `frame_id_server` advances when the frame is handed to
    `trans_write_copy_s()` (`xrdp_mm.c:4320` on `enc_done`), and that
    call **cannot fail for want of a wire**: whatever the socket does
    not take is `malloc`ed into a fresh stream and appended to the
    unbounded singly-linked `self->wait_s` list, and it returns 0
    (`common/trans.c:644-676`). So `min(consumed, server + 1)` bounds
    frames between absorb and *handoff to xrdp's own heap*, never
    frames on the wire.
  * The one byte-level throttle in this transport —
    `si->source[my_source] > MAX_SBYTES` (= **0**), which stops
    `select()`ing a source's input while its bytes sit queued
    (`common/trans.c:219, 376`) — **provably does not apply to GFX
    frames**. Bytes are charged only when
    `si->cur_source != XRDP_SOURCE_NONE` (`trans.c:653`), `cur_source`
    is set only inside `trans_check_wait_objs()` for a *transport*
    (`trans.c:396`), and enc_done arrives on a **wait object**, not a
    transport (`xrdp_mm.c:4061, 4538`). `cur_source` is NONE at that
    instant, so the frame's bytes are charged to nobody and throttle
    nothing.
  * Consequence: **the client ack window is currently the ONLY rate
    control between the encoder and a slow link.** On a link that
    cannot carry the encoder's output the backlog does not settle at
    `rate x ack-latency`; it grows in xrdp's heap until the client
    catches up or the session dies. That is the bufferbloat shape
    PRD:603 forbids, one level further in than a socket buffer.
  * This does not change the layer-1 verdict (LAN, ack latency the only
    variable) and does not change step 3's predictions. It changes what
    step 3's **safety leg** is testing: not "does a bound take over"
    but "is there one at all". Record the frozen-client leg's buffered
    bytes and RSS, not just its frame count.

**Default vs opt-in — SETTLED 2026-08-02 (owner), same day it was
filed.** The question as posed ("default the plain ungate, or make it
opt-in?") has no correct answer, because **neither branch is
acceptable**: an unbounded egress queue is a defect at any default, and
a flag only decides who finds it. The plain ungate is rejected outright
(PRD FR-ACK-3 amendment clause 3) and the question is replaced by: may
the HORIZON form default? That one is answerable — it keeps a stated
bound in both regimes — but it is not answered here; it needs step 4's
numbers and the #80 egress work, and `eager_slot_ack` stays
default-false until then. The reasoning below is kept as filed, with
the third bullet's "if the safety leg is bad" now moot.
* **No new knob, and NOT `xrdp.ini`.** The behaviour already lives
  behind `gfx.toml` `[avc444] eager_slot_ack`, default **false**
  (`xrdp_tconfig.c:430, 452`), and the #79 emission is inside
  `if (encoder->eager_slot_ack)` (`xrdp_mm.c:1724`). #79 is a change to
  how that feature acks, not a new feature; splitting its configuration
  across two files buys nothing. GFX/encoder config is gfx.toml's by
  construction.
* So #79 ships opt-in **for free** and needs no decision to do so. The
  only live question is whether `eager_slot_ack` may later become the
  default, and #79 as specified makes that HARDER: an option whose
  documented meaning is "faster, and on a WAN queue without bound" is
  a footgun that benchmarks turn on and WAN operators never turn off.
* Preferred shape, if the safety leg is bad: **replace the bound, do
  not remove it** — the horizon variant `client + H > server` with H
  sized to the pipeline (2 capture slots / 3 stages), not to `fif`.
  `fif` is doing two jobs today — it sets the latency target AND it
  bounds client-outstanding — and FR-ACK-3 requires job 1 to hold at
  fif = 1, which drags job 2's bound down to 1 with it. Separating them
  is defaultable; plain ungating is not.
* Sequence: implement the plain ungate (that is what CI pins), run
  step 3 including the frozen leg, and let the outstanding row and the
  buffered-bytes number choose. **Do not decide the default before the
  safety leg has a number.**
* Slightly more ack messages to xorgxrdp (one SLOT_ONLY per frame even
  with the window closed) — negligible, noted for completeness.
* Bookkeeping interplay: both branches write `frame_id_server_sent`;
  the unit test in step 1 owns this surface.

## #80 — TOP PRIORITY (owner, 2026-08-03): the credit frontier — FR-FLOW-1's first conforming design (steps 1–3 DONE, step 4 PARTLY DONE 2026-08-03; step 5 OPEN; BLOCKS every item from #82 down)

> **Landed 2026-08-03 (steps 1, 2, 3).** The credit frontier is
> implemented behind `eager_slot_ack`, C is `gfx.toml [avc444_ffmpeg]
> wire_window` (default 2, a placeholder — #81 has not run), and CI is
> green at 197/197 in `tests/xrdp` (422 across the tree). The
> cross-layer gate's absence is asserted, not described:
> `test_joint_machine_enumeration` walks the entire reachable joint
> xrdp/xorgxrdp state space for C ∈ {1,2,3} and RED-on-HEAD was
> **verified** — reinstating the shipped gate inside the planner turns
> 4 cases red, including the D=40 wedge golden replay. Record and the
> failure output: `docs/experiments/80-the-credit-frontier.md`.
>
> **Two findings from implementing, neither in the approved design:**
> (a) the region-disposing ack is a SECOND admission token — it is not
> `SLOT_ONLY`, so `xup_ack_frontier_apply` moves the producer's slot
> frontier with it — and is now clamped by the same window, safe
> because the clamped target never lags the credit by more than one id
> and xorgxrdp's `cap_sent` ring holds slots + 1; (b) the
> `NOT_DISPLAYED` region-return is deliberately NOT clamped (its frame
> never reached the wire and owes pixels back), so the bound is a
> statement about frames that reached the transport. Also: the bound is
> `C + 2·M` at M monitors, not `C + 2` — the producer's budget is per
> monitor.
>
> **What is still red/unknown:** no live measurement of this code
> exists, and the shipped default C = 2 does NOT satisfy FR-FLOW-1
> clause 4's "default chosen with #81's data". Steps 4 and 5 below are
> the remaining work, and #81 is their prerequisite.
>
> **UPDATED 2026-08-03 — the code has now run on a link.** #81's netem
> harness landed and the owner approved two legs: arms x018 (loopback
> baseline) and x019 (40 ms true RTT), one monitor, C = 1, perf trace
> on. Capture `i80_wanpair_20260803_125816_s20`; record:
> `docs/experiments/80-the-credit-frontier.md` §"Step 4".
>
> **LAN head to head against x017 `direct`** (same payload, geometry,
> monitors, client rig and xorgxrdp; old build at fif = 1; nothing in
> the network path on either):
> withheld p90 **35.3 → 10.6 ms**, mean 8.45 → 3.46, stalls 29.7 →
> 18.2 %; period p90 **42.7 → 25.9**, p99 52.2 → 30.4, p90/p50 2.51 →
> **1.49**; throughput 46.5 → **54.1 /s**. The wire bound
> `id_server − id_client ≤ C + 2` held live on both legs.
>
> **CORRECTED SAME DAY — the first 40 ms leg was VOID (instrument on
> the measured path) and its numbers are deleted.** The owner flagged
> that the wan numbers did not make sense; investigating found netem at
> its kernel-default `limit 1000` = an undeclared 73 MB/s bottleneck
> with tail drops (measured 73.5 vs 1614 MB/s unshaped, 64 drops). Leg
> deleted, harness fixed (`limit 25000`, environment declared, selftest
> now asserts zero drops), leg re-run:
> `i80_wan40_fixedlimit_20260803_221910_s20`. Corrected 40 ms numbers,
> C = 1: **11.5 fps, period 86.7 ms, send-to-ack 203.7 ms on a 40.45 ms
> link, queue 6.7 MB mean; wire bound held on every send.** The re-run
> is SLOWER than the voided leg because the accidental bottleneck queue
> had been keeping the pipe full; on the honest link, TCP's
> congestion-window validation pins cwnd far below the BDP for xrdp's
> burst-then-wait shape (~600 KB in a shape-replica probe), so **at 4K
> the WAN constraint is BYTES through one TCP flow, not frames in the
> window** — raising C deepens the queue without buying rate. Detail:
> `docs/experiments/80-the-credit-frontier.md` §"Step 4, corrected".
>
> **The two results that are NOT green.** (a) The predicted stall
> fraction on the LAN leg was ≤ 5 %; it is 18.2 %, and the ack record
> CANNOT attribute it — all three frontier terms are equal at emission
> (157/157 ties), a gate-2b situation. One 20 s leg at C = 2 on the LAN
> arm would settle it (~2 min); not run, not in the approved
> description. (b) The wan-leg prediction ("queue ≈ 0") was wrong twice
> — see the corrected block above. A 4K AVC444 frame is **3 386 KiB
> measured**, so each unit of C is up to ~3.3 MB of standing transport
> queue per monitor — the FR-ACK-3 "queue in front of the display",
> measured rather than argued.
>
> **Still unchosen: the shipped default C.** Two RTT points at one C do
> not make FR-FLOW-1 clause 4's RTT → C table — and the table now has a
> stated prerequisite: the TCP environment (congestion control, buffer
> sizes, pacing) must be held fixed and DECLARED, or the table measures
> TCP, not C. Also not run: the freeze leg, and any old-build leg under
> netem (so there is no A/B at 40 ms — x017's D = 40 used the retired
> proxy, a different instrument, gate 5).
>
> **CAUTION for the return to 2 monitors (owner directive,
> 2026-08-03).** Finding (c) — the producer's capture budget is
> **per monitor**, so the wire bound is `C + 2·M`, not `C + 2` — is
> UNTESTED. Everything measured for #80 so far is single monitor. Do
> not carry any C, any bound and any queue number from this work over
> to a 2-monitor configuration by arithmetic: at M = 2 the bound is
> C + 4 frames and, at the 3.3 MB/frame measured here, the transport
> queue term roughly doubles with it. Revisit this deliberately —
> probably as its own A/B — only **after the single-monitor stall work
> is fully closed**, and re-derive the bound from measurement rather
> than from the multiplication. (Findings (a) two-token clamp and (b)
> unclamped `NOT_DISPLAYED` are accepted as-is by the owner until a
> later test rejects them.)

**The defect, read in code and confirmed by #79's layer-1 sweep.**
`frames_in_flight` is documented (PRD FR-ACK-3) as the bound on what the
CLIENT has outstanding. It is not applied there. `enc_done` hands every
completed frame to `trans_write_copy_s()` unconditionally
(`xrdp_mm.c:4320`); the window is tested only around the PRODUCER acks
(`xrdp_mm.c:1697`). Measured consequence, arm x017 at fif = 1 under a
40 ms injected ack delay — a window that forbids *any* outstanding
frame — sends split **556 / 552 / 552** across 0 / 1 / 2 frames
outstanding: two thirds of sends exceeded the bound. Today the excess is
small only because the producer is starved by the same `if`; #79 removes
that side effect deliberately, so after #79 this item is the only thing
standing between the encoder and the wire.

**And the queue behind egress is unbounded.** `trans_write_copy_s()`
cannot fail for want of a wire — the remainder is `malloc`ed onto the
singly-linked `self->wait_s` list, no length or byte limit, return 0
(`common/trans.c:644-676`). The transport's one throttle,
`si->source[my_source] > MAX_SBYTES` with `MAX_SBYTES` = 0
(`trans.c:35, 219, 376`), charges bytes only when
`si->cur_source != XRDP_SOURCE_NONE` (`trans.c:653`), and `cur_source`
is set only inside a transport's `trans_check_wait_objs()`
(`trans.c:396`). enc_done arrives on a **wait object**
(`xrdp_mm.c:4061, 4538`), so `cur_source` is NONE and a GFX frame's
bytes are charged to nobody. At ~3.4 MB per 4K AVC444 frame this is the
bufferbloat shape PRD FR-CAPTURE-8 forbids, one stage further out and
invisible to every metric this project has built.

**Scope.** (a) Gate egress on `xrdp_gfx_ack_window_open(client, server,
fif)` — the quantity the window is actually for. (b) Decide and state
what happens to a completed frame that may not yet be sent: held (bounded
by #79's H, stale by up to H periods) or dropped-and-recaptured (no
staleness, no concurrency). **This is the real design question and it is
NOT settled** — FR-ACK-3 objects to fif = 2 precisely because "a second
in-flight frame is a queue in front of the display", and holding a frame
server-side is the same queue relocated. (c) A bound on `wait_s` for this
path, in frames, with a test. Note that (a) plus (b)-as-hold makes (c)
implied rather than independent — say which is load-bearing.

**Why it is not folded into #79.** #79 is complete without it: with the
horizon H the slot credit no longer depends on the client at all, so #79
can be measured and landed on its own. This item changes what the client
holds, which is a different risk surface (a bug here stalls the display
rather than the pipeline), and PRD FR-ACK-3 amendment clause 1 requires
both halves — one item per behaviour change.

**Blocked on:** an owner decision on hold-vs-drop in (b). The FR-ACK-3
amendment is already written and needs no further sign-off.

**SUPERSEDED 2026-08-03 (owner axioms) — the egress gate is the wrong
fix and this item is REWRITTEN below.** The owner stated the two
principles as axioms — *lossless backpressure stalls come only from the
immediate next neighbour; the lossy end-to-end guard runs from the
farthest end (client) to the nearest end (capture admission), and its
response is drop-by-coalesce, never a hold* — and observed that under
full enforcement there is no hold-vs-drop decision at egress at all: a
frame that could not be sent should never have been captured, so
nothing intermediate ever holds. That is correct, provable by
induction, and it deletes scope (a)/(b) of the original filing:

* **Induction (no egress hold is reachable).** Admit capture k only
  while `k ≤ frame_id_client + C_eff` for a constant `C_eff`.
  `frame_id_client` only rises, frames are sent in id order, so at the
  instant k is sent, `k − client ≤ k − client_at_admission ≤ C_eff`.
  Every frame that exists is inside the window when it reaches egress;
  an egress gate would never fire; there is nothing to hold or drop
  mid-pipeline. The one precision the induction demands: **the window
  must be counted from the CAPTURE frontier, not the egress frontier**
  — counting from `frame_id_server` (what #79's horizon H did) leaves
  pipeline inventory that can arrive at egress after the window moved,
  which is exactly what would need a hold.
* **The drop path already exists and needs zero new code.** Admission
  denied = no credit = both slots stay busy = xorgxrdp coalesces
  damage into the dirty region (PRD FR-CAPTURE-8 clause 4, "frames are
  dropped before they exist — the only legal drop point"). Content is
  never delayed in a queue; the next admitted capture carries the
  union, so a slow client gets fewer, *fresher* frames. (Little's law
  note: on a WAN nothing raises frame rate above window/RTT — drop
  keeps latency and staleness low, it does not buy throughput.)

**The REWRITTEN item — source admission via the credit frontier
(design proposed 2026-08-03; owner ACCEPTED same day as the first
FR-FLOW-1-conforming design — it blocks every item from #82 down).**
Replace the emission-time window test with a third term in the credit
frontier itself, at the one existing site:

    credit = min(frame_id_consumed,       /* slot fact: children done */
                 frame_id_server + 1,     /* pipeline-inventory cap   */
                 frame_id_client + C)     /* end-to-end wire window   */

and emit unconditionally whenever the frontier advances. Each term now
consults exactly its own layer; the stall signal (slots) is never
suppressed by the wire signal — when `client + C` binds, the producer
is not stalled, it is *dropping* (coalescing) by construction.
* Wire bound that results: capture rides ≤ 2 slots above the credit,
  so unacked-at-send ≤ **C + 2**. C is a server policy constant — EGFX
  has NO client-advertised window (the `max_unacknowledged_frame_count`
  that fde04e80 honoured is the legacy TS frame-ack capset, which is
  why the GFX path hardcodes 2 — re-tethering to the client is not
  possible in GFX), so C's meaning must be stated singularly: "max
  frames unacked on the wire = C + 2". Wedge replay says C = 2 keeps
  the credit for f789 immediate; C's exact value is pinned by the
  step-3 exhaustive enumeration (the frontier is a pure function),
  not tuned against a run.
* Client pathologies already handled: EGFX SUSPEND snaps
  `frame_id_client = frame_id_server` (`xrdp_mm.c:1854-1866`), and
  cliack already re-invokes `xrdp_mm_update_module_frame_ack`
  (`xrdp_mm.c:1867`), so the frontier wakes on ack arrival with no new
  plumbing.
* What remains of the original #80: only the **telemetry** — the O(1)
  pending-bytes counter in `trans` (`egress` field c) stays required,
  because `wait_s ≤ C + 2 frames` is now a claim CI can state but only
  the counter can verify live; and the frozen-client leg verifies the
  whole bound end to end.
* Change surface is enumerated in
  `docs/experiments/79-layer1-...md` §"The change surface under the
  two axioms". xorgxrdp: **no change** — credit semantics on the wire
  are unchanged, only the arithmetic producing the frontier moves.
  Behaviour stays behind `eager_slot_ack` (coding rule 2: default-off
  preserves today's behaviour bit for bit).

**Steps (linear, 2026-08-03; absorbing #79's plan):**
1. **DONE 2026-08-03. Blocking pre-step:** xorgxrdp's SLOT_ONLY
   handler read at `/workUpdateXorgXrdp` (`10fa3aa23033`). Slot release
   does NOT consume region-retirement state: `xup_ack_frontier_apply`
   advances `f->shown` only for a non-SLOT_ONLY ack, retirement is
   driven from `rect_id_ack_shown` and admission from `rect_id_ack`,
   and `rdpClientConReturnFrameRegion` is unreachable for a SLOT_ONLY
   ack. The same read confirmed the drop path needs zero new code
   (`rdpDeferredUpdateCallback:3984` returns early, damage stays in
   `dirtyRegion`). Detail: `docs/experiments/80-the-credit-frontier.md`.
2. **DONE 2026-08-03. The frontier**, plus C as user config in
   `gfx.toml [avc444_ffmpeg] wire_window` (owner confirmed gfx.toml
   2026-08-03), range 1–64, out-of-range REFUSED, default 2 as a
   PLACEHOLDER pending #81. Code: `xrdp_gfx_credit_frontier()`,
   `xrdp_gfx_region_ack_target()`, `xrdp_gfx_plan_acks()` in
   `xrdp/xrdp_encoder.h`; `xrdp_mm_emit_credit_frontier()` in
   `xrdp/xrdp_mm.c`, with the shipped gated emission kept verbatim as
   `xrdp_mm_emit_legacy_frame_ack()` for `eager_slot_ack = false`
   (rule 2). Telemetry: `ackslot`/`ackregion` carry `frame_id_client`
   and C; `egress` field c carries transport bytes queued, in KiB, from
   a new O(1) `struct trans::wait_bytes` counter — never a per-frame
   walk of `wait_s` (coding rule 5). Man page: `gfx.toml(5)`.
3. **DONE 2026-08-03. CI, RED on HEAD verified:**
   `tests/xrdp/test_avc444_credit_frontier.c` (9 cases) plus 2 config
   cases in `test_tconfig.c`. The enumeration walks the entire
   reachable joint xrdp/xorgxrdp state space for C ∈ {1,2,3} asserting
   INV-SENT / INV-WIRE / INV-LIVE / INV-HELD / deadlock freedom, with
   three non-vacuity checks (pipeline runs to the end, the wire bound
   is ATTAINED, the window term is the strict minimum somewhere). The
   D = 40 wedge is replayed at C = 2 and C = 1 with the event ORDER
   measured (`i79_x017_ackdelay_20260802_s20/leg_d40`, frames 406–409)
   and the expected values hand-derived from FR-FLOW-1 clause 3.
   Reinstating the shipped gate inside the planner turns 4 cases red;
   the mutation was reverted and is not committed.
4. **PARTLY DONE 2026-08-03 (2 of the legs, owner-approved arm
   count). #81 harness, then the VALIDATION GATE.** #81 landed;
   `i80_wan_pair.sh` ran the two approved legs with the predictions
   written in its header BEFORE the run. Results and the two red
   findings are in the landed block above and in
   `docs/experiments/80-the-credit-frontier.md` §"Step 4". The
   ack-delay sweep is gone with its proxy (see #81). **Still owed:**
   the freeze leg; an old-build leg under netem so there is an actual
   A/B at 40 ms; a C = 2 LAN leg to attribute the residual 18.2 %
   stalls; and enough RTT points to choose the shipped default.
   Original scope, kept for the record: ack-delay sweep + freeze
   leg + netem RTT legs against the fixed deb, predictions re-derived
   for DROP semantics before the run — period ~flat at ALL D
   (admission drops instead of stalling); withheld ≈ 0 at every D;
   `id_server − id_client` at send never > C + 2; freeze leg stops
   production within C + 2 frames and `wait_s` bytes plateau; the
   drop visible as damage-area growth per admitted frame at high RTT.
   The freeze leg is now readable: `egress` field c is the transport's
   queued KiB, so "`wait_s` plateaus" is a number rather than a claim.
5. **Fleet A/B** (intent unchanged from the superseded #79 step 5).

**Deliberately NOT done in steps 1–3, and why** (written 2026-08-03
before step 4 ran; the first bullet was answered later the same day —
see the UPDATED block at the top of this item):
* **No live run.** Step 4 needs #81, and #81 has not been built. Every
  number quoted for this item is from the pre-change captures or from
  CI; the code has never encoded a frame on a real link.
  *(2026-08-03, later: #81 landed and two legs ran. This bullet is
  superseded — the code has now encoded frames at 0.078 ms and at
  40.4 ms RTT.)*
* **The default C = 2 is not a recommendation.** It matches the legacy
  `frames_in_flight` so short-RTT behaviour is preserved, and
  FR-FLOW-1 clause 4's "default chosen with #81's data" is unmet.
* **xorgxrdp is unchanged**, as the design predicted: the wire's credit
  semantics did not move, only the arithmetic producing the value.
  `/workUpdateXorgXrdp` is now on branch `wip/eager-ack` at the same
  commit, to keep the two repositories' branch names in step (CLAUDE.md
  "The other half of the pipeline").

The paragraphs below are the original egress-gate filing, kept for the
record; its sequencing question is moot (there is no egress gate to
sequence).

**ORIGINAL FILING (superseded).** #80 was filed as "blocked
on #79 landing". Tracing the gate's provenance and drawing the
pipeline's backpressure map suggests the opposite order, and possibly a
simpler #79.

* The rejection of #79's plain ungate was **conditional on egress being
  ungated**. With egress gated (this item), a client that falls behind
  stops `frame_id_server` advancing; the **existing** cap
  `min(frame_id_consumed, frame_id_server + 1)` then stops the slot
  credit within one frame, both capture slots fill, and xorgxrdp
  coalesces and drops (PRD FR-CAPTURE-8 clause 4). That is a bound —
  and it is the *near-end* drop firing because the pipeline is
  genuinely full, which is what the drop guard was always meant to
  mean.
* If that holds, **#79 needs no horizon H at all**: emit the slot
  credit at absorb with only the existing cap, and the client bound
  lives entirely here. One less constant, one less thing pinned by CI,
  and each signal on its own layer — the slot credit consulting only
  its immediate neighbour (are the children done with the pixels), the
  client window consulting only the client.
* Cost of this order: (b)'s hold-vs-drop question must be answered
  first, and holding a completed frame at egress is new machinery in
  the main thread (the assembler produces one buffer per frame; it
  would have to be held rather than written). On a LAN the hold is
  ~0.4 ms — ack round trip 7.6 ms against a 7.9 ms slot-free deadline,
  both measured — so the "queue in front of the display" FR-ACK-3
  objects to is negligible there; on a WAN it is bounded by the same
  cap.
* **Not acted on.** #79's CI and validation gate are written against
  the horizon form. Reordering means rewriting both, and the decision
  is the owner's. Evidence and derivation:
  `docs/experiments/79-layer1-...md`, section "Where the gate came
  from, and the mechanism mismatch it encodes".

## #81 — WAN RTT simulation harness on the container network namespaces (DONE 2026-08-03)

> **Landed 2026-08-03** as
> `PR-demo/mac_bisect_matrix/netem_rtt.sh`: delay split in half and
> applied to BOTH ends of a pod's veth pair, so a round trip picks up
> the whole RTT. `selftest` is GREEN in 22 s and checks four things —
> the requested delay appears as a measured one, `clear` restores the
> exact qdisc lines and the baseline RTT, `apply` REFUSES an interface
> carrying a foreign qdisc, and the delay is on the **RDP port the
> client rig dials** rather than only on ICMP to the pod IP. First use:
> #80 step 4. Record, including the two things that went wrong while
> building it (`nsenter -n` does not swap the mount namespace, so
> `/sys/class/net/eth0/iflink` silently returns the host's answer; six
> stale veths for four pods make name-based resolution unsafe):
> `docs/experiments/81-the-netem-rtt-harness.md`.
>
> **The ack-delay proxy is RETIRED (owner directive, 2026-08-03)** — it
> burns 100 % of a core when idle and overlaps with netem.
> `ack_delay_proxy.c`, its self-test and `ack_delay_sweep.sh` are
> deleted; `i79_ack_delay_analyze.py` is KEPT, because it is the
> measurement layer both builds' numbers are computed by and the #80
> head-to-head imports it. **The i79 results are NOT void**: the sweep's
> own control leg (`d0` through the proxy vs `direct` with no proxy)
> came out 4.3 % apart, which a spinning core would not have left, and
> the #80 head-to-head quotes only `direct`. Three idle states were
> probed on 2026-08-03 and none reproduced the CPU burn, so the state
> that causes it is not identified — if it happens DURING a leg rather
> than between legs, the control-leg argument is what to re-examine.
>
> **What the harness did NOT do, and it is the one that matters:** two
> RTT points is not the RTT ∈ {0, 10, 40, 80, 150} matrix this entry
> specified. That matrix is a multi-leg session run and still needs
> owner approval with an arm count.

**What.** `tc netem` applied from the host to a fleet pod's veth:
delay in BOTH directions (true RTT), optional jitter/loss, optional
`tbf` rate cap. Legs at RTT ∈ {loopback baseline, 10, 40, 80, 150 ms}.
Harness versioned in `PR-demo/mac_bisect_matrix/`.

**Why a second instrument when `ack_delay_proxy` exists.** They answer
different questions and both stay. The proxy delays ONLY client→server
bytes, above TLS — it isolates the ack variable while video delivery
stays instant, which is what made layer 1 a mechanism test. netem
delays both directions below TCP — video delivery, TCP ACK clocking
and frame acks all move together, which is the environment the user's
"set C by your RTT" guidance and the shipped default C must be derived
from. Mechanism instrument vs environment instrument; do not compare
their numbers directly (quality gate 5).

**Requirements.**
* Verify the applied RTT by measurement through the same path before
  each leg (ping through the pod netns) — never trust the knob
  (the 2026-07-31 mode-name lesson).
* Stateless: restore qdiscs on exit; REFUSE to run if a qdisc the
  harness did not create is already present on the interface.
* Server side only; the client rig stays exactly as deployed.

**Output feeds:** #80 step 4's WAN-leg predictions; the shipped
default C (FR-FLOW-1.4); the RTT → suggested-window table for the
config docs.

**2-minute rule:** this entry names the harness build and a ≤60 s
self-check only. A full RTT matrix is a multi-leg session run — owner
approval with the arm count before running.

## #83 (was #77) — A faster producer (TODO — queued behind #76/#78, reprioritised 2026-08-02)

**What it is.** x014 left the FR-BENCH-1 margin at **1.09×** — the
textflood producer at 16.91 ms
against an 18.48 ms pipeline, with the producer's p90 (19.01 ms) inside
the pipeline's p50 (17.96 ms). At that margin an arm measures the payload
as much as the server, and the 66 producer stalls in x014 are already
visible as a p99 that went the wrong way (31 → 46.5 ms) while the mean
improved by 7 ms.

**Scope.** PRD design B: memmove scroll + strip render instead of a full
redraw. Offline it is 7.1 ms/frame against today's ~16 ms, which restores
the margin to ~2.6× at the current pipeline rate and keeps it above 2×
even if the pipeline reaches 14 ms.

**Reprioritised the day it was filed (owner directive, 2026-08-02).**
This item was first written as "NEXT, blocks this path", on the reasoning
that no arm can be read through a payload 9 % from being the limit. That
reasoning was sound for the fif = 2 configuration and does not apply to
the one now under investigation: at fif = 1 the pipeline runs at 28.2 ms
against a 16.26 ms producer, an FR-BENCH-1 margin of **1.74×**, so #76
and #78 are measurable today. **#76 — a 34 % throughput loss that means
the pipeline is leaning on a second in-flight frame — outranks it.**

It still gates everything measured at fif = 2, which is the configuration
every arm before x015 used, and it still gates #74, #61f and #61d. It is
queued, not closed.

**Acceptance.** Producer p90 strictly below the pipeline p10 at
3840×2400, stated in the arm's own capture; FR-BENCH-1 margin ≥ 2.0×
reported beside every ratio thereafter.

## #75 — The LTR rewrite re-serialised a whole picture to edit 30 bytes of slice header (DONE 2026-08-01)

**Why.** #61e measured `collect` at 8.802 ms of a 25.474 ms period, and
`tools/avc444_ltr_rewrite_bench.c` attributes 7.07 ms of it to the
rewriter: **1.90-2.03 ns/byte across runs, against 0.03-0.04 ns/byte for
a plain `memcpy` of the same buffers in the same bench — ~60×.** The
edit itself is the
slice header, tens of bytes. This is the largest piece of xrdp-side CPU
in the frame period and it is not AVC444 overhead in any inherent
sense.

**What costs it** (measured split, `getrusage` on the bench child):
byte-at-a-time passes ~72 %, `mmap`/`munmap` + first-touch faults from
six ~1.7 MB `malloc`s ~19 % (**4228 minor faults per pair measured**
against 4248 predicted), bulk `memset`/`memcpy` ~9 %.

**The four antipatterns**, in cost order:

1. The CABAC payload is unescaped and re-escaped for nothing. It is
   byte-aligned in both input and output (`cabac_alignment_one_bit`
   pads to a byte before it) and copied verbatim, so its escaped bytes
   are invariant whenever the emulation-prevention zero-state entering
   the payload is the same in the old and new headers.
2. Six ~1.7 MB `malloc`/`free` per frame, all above glibc's 128 KB
   `M_MMAP_THRESHOLD`.
3. `memset(newr, 0, nal_len + 16)` zeroes a whole picture buffer when
   only the rewritten header is read before `memcpy` overwrites it.
4. `find_start_code` is a byte-at-a-time triple compare over the full
   payload, once per NAL boundary and twice per packet counting the
   `packet_intra_is_converted` pre-scan.

**Scope.** `xrdp/xrdp_h264_annexb.c` only. No wire change, no protocol
change, no config knob: the emitted bytes must be IDENTICAL. The fast
path is taken only when its precondition holds and falls back to
today's code otherwise.

**Acceptance.** (a) `make check` green with
`tests/xrdp/test_avc444_ltr.c`'s golden byte vectors UNCHANGED — the
test is what proves the output did not move, so touching it would void
the whole exercise; (b) `avc444_ltr_rewrite_bench` ns/byte reported
before and after on the same input; (c) one deployed arm measuring the
end-to-end textflood send interval (owner-approved, 2026-08-01, ONE
arm).

**LANDED.** Offline: **11.027 -> 1.026 ms/pair, 1.90 -> 0.18 ns/byte,
4228 -> 73 minor faults per pair**, against an unchanged 0.03 ns/byte
`memcpy` control. Output proven identical two ways: CI 411/411 with the
golden byte vectors UNTOUCHED, and an FNV-1a digest over 120 whole 4K
pictures identical before and after (`12c16104c46343cb`). Predicted
in-session effect: `collect` 8.8 -> ~2.4 ms, period 25.5 -> ~19.1 ms.
Record: `docs/experiments/75-the-rewrite-was-re-serialising-the-picture.md`.

**ARM RESULT (x014, one arm, owner-approved).** Period **25.474 ->
18.476 ms**; `collect` **8.802 -> 1.362 ms** while `pump` held at
16.585 (the control); 2228 -> 3075 sends, 39.2 -> 54.1 fps. Closure:
7.440 removed minus 0.515 new wait = 6.925 against a measured 6.998.
Certificate 7/7, 0 black frames, 0 rewrite failures over 6150 packets.

**And the limit stated before the run arrived.** The producer's interval
is 16.91 ms against an 18.476 ms pipeline: **FR-BENCH-1 margin 1.09x,
MARGINAL**. 66 of 3074 cycles (2.15%) now stall on the producer and
carry 99.7% of all wait time; **p99 send interval REGRESSED 31 -> 46.5
ms** while mean and p50 improved by ~7 ms. Delivered 18.476 ms (1.38x);
with producer stalls removed 17.96 ms (1.42x), which is the measured
p50 and the number a further optimisation starts from.

Record: `docs/experiments/75-the-rewrite-was-re-serialising-the-picture.md`;
capture `captures/i75_x014_rewrite_20260801`.

**Consequence for what comes next, and it is a blocker, not a note:**
any further arm on this path needs the FASTER PRODUCER first (PRD design
B, memmove scroll + strip render, 7.1 ms/frame offline). At 1.09x the
payload is inside the measurement, so #74 Lever 2 — whose remaining
prize is now `pump`'s 16.6 ms, not `collect`'s — cannot be measured with
today's textflood. This reopens **#61c** with a concrete number.

## #85 (was #61c) — Is the producer the ceiling? REOPENED 2026-08-01 (#61h voided the run that answered it)

**Answered the day it was opened.** The session Xorg runs at **98.9 % of
one core with NO client connected at all** — the payload alone saturates
it. Connecting the client and running the whole capture path moves it to
**96.4 %**, i.e. *down* 2.5 points: the capture does not add load, it
displaces payload drawing inside the same single thread.

Share of that thread, from `tools/avc444_pack_bench.c` on this CPU
(1.3–1.5 ms/frame at 3.686 Mpx × 30.05 fps): **capture ~4 %, everything
else ~92 %** — xterm glyph compositing, scroll blits, Present emulation,
fills. Reproduces the T4's 99.9 % (#59) on different hardware.

**Consequence.** No worker-side change is measurable under this payload;
the producer answers every question first. **Resolved the same day** by
#61b: textflood drops the session Xorg to 25.3 % and #70B then measures
1.12x instead of 0.96x. Ratios taken under codeflood are void as
throughput numbers — see #61d for the ones that need re-running.

`perf` sampling is unavailable on this box (`perf_event_paranoid = 4`,
host-owned, `sysctl -w` silently fails; `perf record` yields 0 bytes) —
hence `/proc/<pid>/stat` deltas plus an offline bench. Same class of
blocker as the seccomp `bpf()` denial in #70B.

**Record:** `captures/i61c_xorg_profile_20260801 (DELETED by #61h, git history only)`.

## #91 (was #71, earlier #65) — multimon capture‖encode: per-monitor ack window + the m≥2 serial cost (TODO — after #70; the global-window arithmetic stands on its own CI pin)

At m≥2 two further issues sit ON TOP of the m=1 serializer (#70):

1. **The xrdp ack window is global while the budget is per-monitor.**
   `xrdp_gfx_ack_window_open` (fif=2, global) admits ~1 outstanding
   per monitor at m=2 and halves the intended per-monitor depth. The
   PRD forbids widening the global pool (bufferbloat, measured
   2026-07-31: 98.1 ms vs 87.0 ms); the spec shape is ≤2 PER MONITOR.
   Pinned in CI: `test_overlap_m2_global_window_pins_each_monitor_to_one`
   documents today's behaviour and flips on the fix.
2. **The m≥2 serial cost is real and unexplained by overlap alone.**
   173.7 ms period with 132.1 ms inside our pipeline at 1.33 of 4
   cores; the 50.1 % cross-monitor interleaving does not make it
   fast. After #70, re-decompose: how much was the ack pacing, how much is
   step 7's whole-set drain (a late monitor holds the set), how much
   is genuinely serial assembly.

Acceptance: per-monitor window (never a pool), CI updated
deliberately, fleet-arm decomposition showing per-monitor depth 2 at
m=2, negative arm gaps on both monitors.

## #92 (was #72, earlier #66/#63) — 4:2:0 while the screen is in motion, 4:4:4 when it settles (blocked by #71 — FR-PROC-7's preemption signal needs the fifo non-empty at pop time, which needs #70/#71 concurrency first)

Motivated by #62's measured decomposition, not by intuition. On the T4 with
the textflood payload the 173.7 ms period is:

| segment | ms | % of period |
|---|---|---|
| capture + AVC444 pack (xorgxrdp) | 63.7 | 36.7 |
| encode + LTR rewrite + EGFX assembly (xrdp) | 68.4 | 39.4 |
| idle, awaiting damage | 41.5 | 23.9 |

76 % of the period is our pipeline, on a box measured at **1.33 of 4 cores**
with nothing pinned. The cost is serial latency, not arithmetic we cannot
afford — so the lever that helps is one that removes WORK FROM THE CHAIN,
not one that makes any single stage faster. The batch already parallelises
the encode and lands `kids_armed=4` in 100 % of cycles; it cannot help the
capture in front of it or the assembly behind it.

**Both big segments are paid twice, once per view.** Measured on the same
run: main P 2.09 MB, aux P 1.69 MB, so the aux view is **44.8 % of the
bytes**, a second full-frame pack inside `a8r8g8b8_to_avc444_box` (26.5 %
self, the top symbol in libxorgxrdp), and a second encode.

**Proposal.** While the screen is in motion, send 4:2:0 only — drop the aux
view — and send the full 4:4:4 pair once it settles. Motion is exactly when
chroma detail is least perceptible and when the period is longest; a still
screen is when subpixel-AA text fringes matter and when there is time to
spare. This attacks capture AND encode AND assembly in one change, which is
what a 1.41x says is needed.

### Open questions to settle BEFORE implementing

1. **What is "in motion"?** Needs a cheap, deterministic signal — damaged
   area per cycle, or consecutive cycles with damage above a threshold.
   It must not flap: oscillating between 420 and 444 every few frames
   would be visible as chroma breathing on static text.
2. **How does the settle transition avoid a visible pop?** The aux chain is
   an LTR chain (#44/#45): resuming it after a gap needs its own intra, or
   the chain has to survive the motion window unreferenced. Interacts
   directly with the intra-refresh schedule (`intra_refresh_frames`) and
   with A1-A7 of the wire audit — those ratchets must still pass.
3. **Does the client tolerate a stream that alternates?** The EGFX
   capability is negotiated once. Verify against the Mac and Windows
   clients before trusting it — this is the class of change that produced
   the wrong-colour bisect.
4. **Is the win real?** Predicted from #62: removing the aux view should
   take ~45 % off the encode segment and roughly half the pack. Measure
   it with textflood and `e52_period_decompose.py`, same pair, same box —
   do not accept a rate number without the decomposition.

### Acceptance criteria

* the motion detector is pure logic with unit tests under `tests/`;
* default OFF, so absent/invalid config reproduces today's behaviour
  exactly (no functional regression);
* smoke gate PASS before any measurement (the #61 precondition);
* wire audit A1-A7 still PASS in both the motion and settled regimes;
* the E5-2 pair re-run and DECOMPOSED, not just rated;
* a still-screen visual check that subpixel-AA text is still 4:4:4 sharp.

## #93 (was #73, earlier #67) — T4 benchmark re-runs under restored concurrency (blocked by #70 + #71; #72 optional but preferred)

The numbers the PR sells, re-measured on the representative box once
the mechanism is proven locally. Requires re-provisioning the T4 from
bare AMI (DEPLOY_RUNBOOK + persistent-harness install — it must reach
measurable with no ad-hoc steps).

* E5-2 m=1 4K and the m=2 A/B, decomposed, with FR-BENCH-1's two
  saturation checks green in the VERDICT (producer stamps are now
  default-on).
* Re-verdict #62's 1.41× (annotated producer-confounded-then-cleared;
  with the serializer fixed (#70) both arms should shift — quote old vs new).
* Owner onscreen walk (T4 protocol §6): UWP + macOS visual pass was
  informally confirmed 2026-07-31 pre-fix; repeat on the fixed build.

---

## #94 (was #53) — Arm a monitor only when its pixels changed (TODO, NEXT)

**Why.** Measured 2026-07-30 (#52 results, first flood pair): with one
active monitor beside an idle one, the batch is **~9 % SLOWER** than the
serialized path (68.5 ms vs 62.6 ms mean per send). The idle monitor
still reported full-monitor damage every cycle — the window spans both
monitors, so a scroll dirties both areas even though only one has
changed pixels — and the shared deadline (D3) then ties the active
monitor's frame to the idle monitor's full-area capture, NV12 upload and
encode. One active monitor next to an idle one is an ordinary desktop,
so this is a real regression in a common case, not a bench artifact.

**Scope.** Decide per cycle whether a monitor joins the set, on evidence
that its pixels changed rather than on damage-rect coverage. Candidates
to evaluate in this order (cheapest first): (a) drop a monitor from the
set when its previous pair coded as all-skip below a byte threshold and
its damage rects are unchanged; (b) a capture-side changed-region test
in xorgxrdp before the slot is handed over; (c) keep the monitor in the
set but give it its own deadline so it cannot hold the healthy monitor
(this is the residual coupling already recorded under #45 step 7).
Whatever lands must NOT drop a real update — a monitor that stops
sending is worse than one that sends all-skip frames.

**Gate.** The benchmark already exists and needs no new scaffolding: the
first flood pair (`SESSION_KIND=codeflood` before the 32× line repeat,
i.e. ink on one monitor only) currently reads 0.91×. Acceptance: **≥
1.0× on that pair** (no regression against serialized) with **the 2.13×
two-monitor result unchanged within noise** — both arms re-measured, not
one. Plus E2 clean and no monitor left un-updated over a 180 s run.

---

## #95 (was #54) — Capture-side handoff: the remaining 2× (TODO)

**Why.** #52 proved the encode side is no longer the constraint: at
2.13× the worker is **32 % busy**, per-pair service is 14.7 ms (encode
collected 2.8 + rewrite/emit 12.3) against a 59.9 ms per-monitor period,
and after a frame's `last=1` the same monitor's next damage arrives
41 ms (p50) to 105 ms later. Flow control never binds (un-acked p50 0 /
max 4 of fif = 2, client `queue_depth` 0) and it is not bandwidth
(495 Mbit/s over loopback with the oracle client). The wait is the
capture handoff.

**Scope** — #45's two deferred capture questions, now with evidence:
1. what sets a monitor's floor period (deferred-update pacing vs
   ack-budget retirement in xorgxrdp), measured per stage rather than
   inferred from the send interval;
2. whether the producer can hand BOTH monitors over in one cycle, so the
   set is armed with two fresh captures instead of one plus a stale
   slot.

**Gate.** Same instrument as E5-2 (arm-s vs a new arm, `codeflood`,
180 s, oracle, `E5_BASE_MS` = arm-s's own 29.9 ms): **≥ 1.5×** on top of
2.13×, worker busy above 60 %, and the `last=1 → next own dmg` wait
below the per-pair service time. Stop rule as #52: under 1.5× the
remainder is attributed, not re-tuned.

---

## #96 (was #59) — The capture is 14 % of the bottleneck thread; the rest is not ours to optimise (TODO — one lever left, see #61b)

Answers "which function is slow despite the vectorized capture, and does
capture dominate the interval?" — profiled on the T4 with
`PR-demo/t4_profile/xorg_perf_probe.sh` plus `xserver-xorg-core-dbgsym`,
full write-up and raw profile in
`PR-demo/mac_bisect_matrix/captures/e52_t4_batched_20260730/CPU_BOTTLENECK.md`.

Of the session Xorg's cycles (it is ~92 % of one core; period ~92 ms):

| path | % | ≈ms/period |
|---|---|---|
| xterm glyphs — `ProcRenderComposite → pixman_image_composite32` | 19.3 | 16 |
| **X Present in software emulation — `present_fake_do_timer → present_execute_copy → pixman_blt`** | **18.8** | **16** |
| xterm scroll — `ProcCopyArea → pixman_blt` | 16.5 | 14 |
| **xorgxrdp capture — `rdpDeferredUpdateCallback → rdpCaptureGfxA2 → a8r8g8b8_to_avc444_box`** | **13.8** | **12** |
| fills — `ProcRenderFillRectangles → fbFill` | 9.1 | 8 |

**Capture does not dominate**: 13.8 % against 44.9 % for the payload's own
X rendering. Two independent confirmations: with no client connected at all
the flood alone holds Xorg at **99.9 %** of a core; and `rdpCopyBoxList`
(the hw→sw staging copy) fires **0 times in 40 s** against `rdpCapture` 825
— there is no redundant copy on this box.

**The AVX2 kernels are not the problem and are not worth optimising**:
`avc444_decode_row.avx2` 9.85 % + `a8r8g8b8_to_avc444_box.avx2` 3.59 %, and
`avc444_pack_bench` predicts 12.6 ms/period against 12 ms profiled — bench
and profile agree to 5 %.

**One lever, and it is ours:**

**Move the pack off the X server thread** (#54's capture-side half). The
12 ms is not slow, but it sits on the single thread the whole session is
queued behind. Hand xrdp a raw XRGB snapshot and pack in the encoder-side
worker: the same arithmetic, off the critical path.

> ### WITHDRAWN (2026-07-31): the `present_fake` lever — we do not own it
>
> This item previously proposed attacking the 18.8 % `present_fake` path
> (X's Present extension running in software emulation) by setting
> `Option "DRI3" "0"` in the shipped `xorg.conf` or passing
> `-extension Present` on the Xorg command line, on the argument that a
> config file xrdp ships is in our scope.
>
> **That argument was wrong and the lever is withdrawn.** Flipping those
> knobs does not make our code faster; it changes how the *X server*
> presents, in the hope its emulation path gets cheaper. That is
> optimising a component we neither own nor ship, measured through a
> benchmark that was itself the problem — and it changes the behaviour of
> every session, for a benefit that was never demonstrated (the one
> attempt, disabling the xfwm4 compositor, did not stick and did not move
> the number).
>
> **#62 supersedes it.** `present_fake` fires on Present requests;
> `textflood` drives the screen with `XShmPutImage` and issues none, so
> the payload stops feeding that path rather than the X server being
> reconfigured to make it cheaper. Whatever remains is a fraction of an
> X-thread cost that fell 7.7x, and is no longer worth a config change.
>
> The rule this leaves behind: **when a profile says the cost is in code
> we do not own, the fix is to stop generating the work, not to retune
> the other component.**

One process note worth keeping: the first reading of this profile, taken
before the dbgsym install when the frames above `rdpCopyArea` were bare
addresses, blamed xorgxrdp's own staging copy and would have sent an
optimisation at code that never runs. The uprobe count killed it. Do not
attribute a stripped stack by inference.

---

## #97 (was #60) — The T4's E5-2 is bimodal and it is not root-caused (TODO)

Sixteen repeats of the batched arm split into two tight clusters — 47–49 ms
(8 runs) and 67–74 ms (8) — with nothing in between, and the baseline
measured 77.3 ms once and 108.9 / 109.9 ms later. That is the whole reason
#55's answer is a band.

The clusters differ in bytes, not just cadence: **580 KB per picture at a
92 ms period** vs **875 KB at 140 ms**, with `encode collected → last=1`
moving 21.3 → 32.6 ms in step (`E5-2_run_to_run_variance.txt`). Encode time
tracks picture size, so the loop has two self-consistent equilibria and
something tips it at session start.

Ruled out, each with evidence: **codec fallback** (no run logged
`Matched RFX mode`; every run's `xrdp.log` checked), **corpus position**
(3 000 lines, per-500-line density varies only 1.17×, fully traversed every
~12 s so even a 60 s window averages five passes), **the profiler** (both
clusters occur with and without perf/top/uprobes attached), **compositing**
(the xfconf change neither stuck nor moved the number), **leftover probes**
(`perf probe -l` empty), and **GPU clocks** (`nvidia-smi dmon` shows the
encoder essentially idle in both).

Still open: what selects the equilibrium. Worth trying — pin the payload's
write rate instead of letting it free-run (a `codeflood` variant with a
fixed bytes/s), and log per-picture size against period from the first
frame of a session to see whether the two branches separate at startup or
drift apart later.

Until it is closed: **E5-2 runs must be ≥180 s, both arms measured in one
sitting, and the mean bytes-per-picture of the two arms must agree** before
a ratio is quoted. A pairing whose arms differ in picture size is measuring
content, not the pipeline.

---

## #86 (was #61b) — textflood wired into the fleet (plumbing DONE; its NUMBERS reopened by #61h)

`SESSION_KIND=textflood` now exists in `banner.sh` and the binary is
built into the fleet image (builder stage in `Containerfile`). Arms
**x003/x004** are the #70B A/B under it at 3840x2400.

**The measured figures below are VOID (#61h): they were taken with the
per-frame trace on log.c, i.e. with ~12 unbuffered writes per frame
inside the period being measured.** They are kept here only so nobody
re-quotes them from memory — Session Xorg 96.4 % -> 25.3 % of one core,
FR-BENCH-1 passing at producer 65.07 fps vs pipeline 24.94 (2.61x). Both
the producer's rate and the pipeline's have to be measured again on a
ring-traced build before FR-BENCH-1 can be called passed.
GLAMOR stays CLOSED-WONTFIX.

**Every throughput number from here uses textflood.** A ratio measured
under codeflood is a measurement of the X server (#61c).

**Record:** the capture was deleted by #61h's garbage collection; it is
in git history only.

## #87 (was #61d) — Re-measure the codeflood-era ratios under textflood (TODO)

**#70 (1.11x) and #70B (0.96x) were both measured against a saturated
producer and are void as throughput numbers.** #70B has been re-run
(1.12x at 3840x2400); #70's eager ack has not, and neither has #70B at
2560x1440, where its projection was computed.

**Work.** Re-run the eager-ack A/B (arm-u/arm-v config) under textflood,
and the emit-split A/B at 2560x1440 under textflood, so the two knobs
have numbers taken against a producer that is not the clock.

**Why it matters beyond bookkeeping.** #70B's prize is
resolution-dependent: `pump` and `coll` scale with pixel count and
`emit` does not, so `emit` was 27 % of the serial chain at 2560x1440 and
17 % at 3840x2400. The ratio at the smaller geometry should be *larger*,
and that is a prediction this item can falsify.

## #84 (was #61f) — Cut the delivery loop's latency: the encoder is ack-clocked through a busy main thread (TODO)

**The shape of the loop is established; every DURATION in it is void
(#61h).** What survives is ordering, which a slow logger cannot
distort: capture is not damage-clocked — it fires some time after the
eager slot ack of frame N−2, the captured rect then waits for the xrdp
main thread to read it, and that same thread is also writing the
previous frame to the client. FR-CAPTURE-8's two slots buy no lookahead
at m=1: the frontier reads `ack = N−2, shown = N−3` at 1524/1533
captures — permanently at cap, re-opened once per encoded frame. When
the client stops draining, the stall echoes at two-frame spacing
through the slotack(N−2) budget re-open.

The numbers that used to be here — 8.0 ms trigger, 16.2 ms transit,
17 ms send window, 1.9 ms margin, 2.249 ms/cycle idle — were all
measured with ~12 unbuffered log.c writes per frame inside them, and
the send window in particular was the interval BETWEEN two of those
writes. They have to be taken again on a ring-traced build before any
of them means anything. Instrument:
`PR-demo/mac_bisect_matrix/i61f_delivery_chain.py`.

**Step 1 — cut the main thread's 16 ms xup service latency.** The
frame exists 16 ms before the thread that must enqueue it reads the
message. THE DEPENDENCY, stated so it cannot be mistaken again:
`xup fd readable -> main thread reads the rect -> fifo_add -> encoder
worker sees it`. A shape qualifies only if it shortens THAT. Candidate
shapes: service the xup fd from inside the egress write loop; or move
the enqueue (or the egress writes) off the main thread. This attacks
BOTH populations. Acceptance: `msgin − cap_sent` p50 drops to low
single digits on a gate run; `wait` > 1 ms cycles fall accordingly.

**Making the egress itself cheaper is NOT this item (owner, 2026-08-01).**
The first attempt batched the ~2400 drdynvc PDUs of a frame into one
buffered write. That is a network-egress optimisation: it does not
appear anywhere in the dependency above, it carries wire-adjacent risk
the item never scoped, and it measured worse at every shape tried
(its own A/B numbers are void under #61h, and the record was deleted
with them). Reverted whole in `b588a954`. The finding is "wrong lever",
not "slow lever" — a faster version of it would have been worse,
because it would have shipped, and no rate number could have told us
that.

**MEASURED 2026-08-01 on the ring-traced build, and the answer retires
Step 1's whole premise.** The delay this item exists to cut is
enqueue → submit, and it is **15.316 ms** (p50 15.035, p90 18.199) —
60 % of a 25.474 ms period. Of that, the worker was idle for
**0.0019 ms**: a recoverable share of **0.01 %**. The frame sits on the
fifo for 15 ms because the worker is still encoding the previous one.
That is serial work in progress, not scheduling slack, so **no earlier
wake-up, no re-ordering and no cheaper handoff recovers it** — the
entire class of fix Step 1 was reaching for is ruled out, and the cork
is confirmed unrelated rather than merely slow.

What is left is arithmetic, and it belongs to #74: `pump` (16.654 ms,
waiting for the two ffmpeg children) and `collect` (8.802 ms, popping
NALs and rewriting both views' LTR refs) are serial with each other
within a frame and across frames. Overlapping them is Lever 2, and the
prize is now sized: up to 8.8 ms of 25.5 ms.

**Record:** `docs/experiments/61e-the-period-is-encode-and-rewrite.md`;
capture `captures/i61e_x013_eager_m1_4k_20260801`.

**Step 2 — capture depth, per monitor only** (PRD forbids a global
pool). A third slot adds one frame of real lookahead and absorbs
client hiccups up to a full period. Only after Step 1: with a 16 ms
service latency any extra slot drains into the same queue.

**Falsifiable.** If Step 1 lands and the tail persists at unchanged
amplitude, the client-forcing story is wrong and the stall origin is
inside the server after all. Note the forcing is client-dependent:
this client is the oracle harness (its dump goes to tmpfs, so disk
writes are ruled OUT as its pause source — the pause is unattributed,
see #61g); a real client's hiccup spectrum differs, but the 1.9 ms
margin is ours.

**Why it matters beyond 6.1 %.** Any further reduction of worker-side
serial time now converts into more of this wait rather than into rate.
The worker is no longer the only ceiling.

## #88 (was #61g) — The oracle client's 50–150 ms pauses: NOT scheduling; what they are is still open (TODO)

**Step 1 is ANSWERED, and it rules the planned fix out.** One 60 s gate
run on x006 with per-thread `/proc/<tid>/schedstat` sampling
(2026-08-01; the capture and its record were garbage-collected by #61h
and are in git history only — the finding is a RATIO of the client's own
counters, which the server's logger does not touch): run-delay
summed over every client thread is 0.326 s in 57.0 s — **0.5 % of the
client's CPU time**, and no higher inside the ack holes (6.0 ms/s) than
outside (5.4 ms/s). The client runs at ~0.65 cores throughout, on a
32-core box at load ~1. **Host CPU contention is not what pauses it**,
so the pin/nice fix that was queued as Step 2 is withdrawn — it would
have targeted a mechanism that is not operating.

The dump was already ruled out (`/tmp` is tmpfs). What the client is
actually doing during a pause is unattributed, and the honest position
is that it stays unattributed until someone has a cheap way to ask.

**Do not build another sampler for it.** The one used here was a custom
wheel next to `common/perf_trace`; it cost 36 % of a core and moved the
rate it was measuring (37.6 vs 36.8 ms on the same arm), and its whole
yield was the single number above. It is reverted. Next candidate
instruments, cheapest first: the client's own `--log-level` timing on
the DVC receive path; `perf record` on the client pid for 5 s; a
one-line `/proc/<pid>/stat` delta before/after. None of them is a new
tool in `PR-demo/`.

**Explicitly out of scope without owner sign-off:** enlarging the
loopback socket buffers to hide client pauses from the server. That
would change what E5 *means* (it removes the client-forcing the
server currently absorbs), and buffering-away a symptom is the
fallback shape the honesty rule exists to catch.

**Not a substitute for #61f.** A perfect client removes the forcing,
not the vulnerability: the 1.9 ms margin and the 16 ms main-thread
service latency stay ours.

**Caution for anyone re-measuring here.** x006 measured 36.8 ms in the
morning and 41.2–41.8 ms the same evening — same arm, same image, same
payload, cause unestablished (host load 1.0 → 1.9). Quote a treatment
only against a control from its own pass.

## #61h — Per-frame trace off log.c and into the perf ring (DONE 2026-08-01)

Every `GFX_TRACE` / `ACK_TRACE` record was a `LOG(LOG_LEVEL_INFO, …)` —
global mutex, unbuffered `write()`, ~12 per frame, nine of them on the
xrdp main thread. The instrument sat on the path it measured, and the
"17 ms send window" the whole first #61f attempt was designed against is
the interval between two of those log lines.

Fixed in `66a60311`: records go to `common/perf_trace` (payload widened
2 → 6 ints so a GFX send fits in one event), the sink formats through
one schema function, the file opens with a `# perfbase` line carrying
both clocks, and a trace knob armed without `XRDP_PERF_TRACE` warns once
and disarms instead of silently recording nothing.
`perf_trace_lines.py` renders the ring back into the line shapes the
existing analyses read. CLAUDE.md rule 5 now requires the ring for
anything at frame rate.

Verified on a 60 s run of the fixed build: **zero** `GFX_TRACE` lines in
the pod's `xrdp.log`, records present in the ring.

**The replacement is now measured, not just argued (2026-08-01).** The
armed ring costs a producer thread **~7 µs per frame** (p99 50 µs, worst
frame observed 91 µs) for the twelve records the hot path writes — 0.02 %
of a frame period, worst frame 0.37 % — plus 0.5–0.6 % of one core in
total across both threads. Disarmed, which is what ships, a
`PERF_TRACE6` costs ~30–45 ns. **Timing taken with the ring armed can be
quoted as if the ring were not there.** Instrument:
`tools/perf_trace_bench.c`, two arms, 20 s each, linking the shipped
`common/perf_trace.c`.

Open and small: 29 % of frames carry a minor page fault (first touch of
the 393 KB ring and the sink's 1 MB stdio buffer), which is inside the
worst case above. A `memset` of both at `perf_trace_open()` would move
it off the producer thread. Not applied — it is shipped-code change and
a third arm.

**Records:** `docs/experiments/61h-the-logger-was-in-the-measurement.md`
(the defect and what it voided), `docs/experiments/61h-what-the-ring-costs.md`
(what the replacement costs).

**Consequence, and it is large: every timing number measured with the
trace armed is void.** Twenty-five capture directories and three
experiment records were deleted from the tree (they remain in git
history). The items that had closed on them are reopened below.

## #61e — DONE 2026-08-01 (redone on the ring): the period is encode + LTR rewrite, and `capture ‖ encode` HOLDS at m=1

**Answered on arm x013**, one monitor at 3840×2400, textflood, eager ack
+ emit split, 2227 sends, 0 trace drops. The frame period is **25.474 ms
and 100 % of it is the encoder worker running serially**: 16.654 ms
waiting for the two ffmpeg children to encode the frame (65.4 %),
8.802 ms popping the encoded NALs and rewriting both views' LTR
references (34.6 %), 0.018 ms for everything else (0.1 %). Per-cycle
closure residual max |0.000000| ms; the 25.46 ms send-to-send interval
agrees independently.

`capture ‖ encode` at m=1 **HOLDS**: worker `wait` 0.0019 ms mean, 0 of
2226 cycles over 1 ms, 100 % of frames already enqueued, fifo depth 0 at
all takes — with gate 2b run first to prove the bracket could have shown
a stall. Read the condition, though: capture is hidden *because encode
is slow*, so a materially faster encoder reopens the question.

**No ratio is claimed** — one arm, no control. The gate's
`2.01x` line is against the stale `SESSION_KIND=code` default baseline
and is void as a comparison.

**Record:** `docs/experiments/61e-the-period-is-encode-and-rewrite.md`.
Open follow-up recorded there: `collect` at 8.8 ms/frame is 5× an
offline bench figure of 1.75 ms/pair that states no resolution.

## #61e — the reopening this replaced (2026-08-01)

Previously marked DONE with a period closing to 0.007 ms unattributed, a
worker idling 2.249 ms/cycle (35 % of cycles stalled), and PRD's
`capture ‖ encode` row declared falsified with the emit split on.

**All of that is void (#61h).** Those runs carried ~12 unbuffered log.c
writes per frame, nine on the xrdp main thread, inside the period being
attributed — and the stage brackets were being differenced against a
period the logger inflated. The tracer-transparency argument that
covered this is void for the same reason: its controls (armed vs
disarmed ring, new vs old build) had the log.c lines in BOTH arms, so it
could not see them.

**What is still standing** is one ordering fact, which a slow instrument
cannot distort: capture is ack-clocked, not damage-clocked — the
xorgxrdp frontier reads `ack = N−2, shown = N−3` at essentially every
capture, so FR-CAPTURE-8's two slots buy no lookahead at m=1.

**To redo, on a ring-traced build:** the period attribution and the
`capture ‖ encode` verdict, control and treatment in ONE pass. Do not
compare anything to a pre-#61h number.

## #89 (was #70B) — REOPENED 2026-08-01: does the emit split buy anything?

Previously marked DONE at 1.12x under textflood (and 0.96x under
codeflood, already void as producer-bound). **The 1.12x is void (#61h)**
— measured on x001–x004, all with the per-frame trace on log.c.

The code ships and stays default-off; its prerequisite refactor fixed a
real use-after-free and that is unaffected. What has to be measured
again is whether splitting the emit off the worker moves the rate at
all, and by how much. PRD FR-ACK-2 still names it as #70's completion.

## #90 (was #74) — Lever 2 architecture: DECISION OPEN (owner discussion next iteration; was task "#40 implement FR-PROC-7")

Lever 2 was queued as "implement FR-PROC-7's submit/collect
construction + the three policies" on the implicit shape of ONE worker
thread. The #61e/#61f decomposition reopened the shape question, and
the owner has argued (2026-08-01) for a different one. **Do not start
implementation until this is decided.** The candidates:

- **A. Depth reorder, one thread** (the shape #40 assumed):
  `subm(N+1) → coll(N)+book+rel(N) → pump(N+1)`. Period
  `subm + max(encode, tail)` ≈ 21 ms at m=1 — but only while
  `tail ≤ encode` (13.7 vs 16.7 ms today, 3 ms margin), and the fixed
  service order idles the children whenever encode finishes early.
  The owner's objection: capture-ready and encode-done are independent
  events; a hardwired order is a bet that worker CPU stays faster than
  ffmpeg, and it stops paying exactly where serialization hurts most
  (tail ~27 ms at m=2 > encode ~17 ms under the set-pump).
- **B. Submit/collect stage threads** (owner proposal): a submit side
  owning stdin fds + schedule cadence, a collect side owning stdout
  fds + the NUT demux/LTR rewrite, bounded one-frame SPSC queue
  between them; event-driven on whichever upstream fires first.
  Period `max(subm, tail, encode)` — generalizes to m≥2 (~27 ms vs
  A's ~36 ms). Each child's stream is touched by exactly one thread,
  so per-child ordering is structural; shared state shrinks to handle
  lifecycle + error stop-the-line.
- **C. Resumable-coll state machine on one thread** — rejected in
  discussion: hand-rolled coroutines in C to slice an 8.5 ms
  demux+rewrite is strictly heavier than a thread, and the codebase
  precedent (emit thread, FR-TRACE-1 ring) already buys threads with
  narrow queues for exactly this problem.

Constraints that survive whichever shape wins: **#61f Step 1 first**
(a 21–27 ms pipeline behind a ~32 ms delivery loop converts the whole
gain into `wait`); the bounded queue is the contract (the bufferbloat
prohibition applies as it did to the global pool); PRD's "exactly one
worker thread / a measurement a set-pump cannot reach" paragraph
(concurrency section) stands until superseded by a dated amendment —
option B requires that amendment, and the m≥2 tail arithmetic above is
the candidate measurement, currently *projected*, not measured (#71
owns the m≥2 numbers).

---

# Closed — records in `docs/experiments/`

Each line is what the work decided. The conditions, tables and
retractions are in the linked file; the code is in git.

| item | outcome | record |
|---|---|---|
| **#45** intra refresh, one-thread `pump_set`, per-monitor capture budget | DONE. E5 resolved by #52 at 2.13×. | [`45-intra-refresh-and-pump-set.md`](docs/experiments/45-intra-refresh-and-pump-set.md) |
| **#52** E5-2 saturated-payload frame interval | DONE. 2.13× GREEN; retired the 51.1 ms cadence baseline as a reading of a 10 Hz metronome. | [`52-e5-2-saturated-payload.md`](docs/experiments/52-e5-2-saturated-payload.md) |
| **#55** E5-2 on the T4 | DONE. 1.5×–2.3× AMBER, attributed to a saturated Xorg. T4 now decommissioned. | [`55-e5-2-on-the-t4.md`](docs/experiments/55-e5-2-on-the-t4.md) |
| **#59/#60** T4 attribution corrections | Recorded. Capture is 13.8 % of the bottleneck thread; the E5-2 bimodality was never root-caused and the box is gone. | [`59-61-corrected-attribution.md`](docs/experiments/59-61-corrected-attribution.md) |
| **#61** GLAMOR on NVIDIA | CLOSED-WONTFIX — renders black; the campaign built on it is void. Payload half continues as #61b above. | [`61-glamor-on-nvidia.md`](docs/experiments/61-glamor-on-nvidia.md) |
| **#62** textflood payload | DONE. 1.41× RED, then annotated producer-confounded. | [`62-textflood-payload.md`](docs/experiments/62-textflood-payload.md) |
| **#64** rect_id ack "ghost" | CLOSED. Root cause REFUTED — the ack is an echo and never drifted. Machinery survived into #70. | [`64-rect-id-ack-ghost.md`](docs/experiments/64-rect-id-ack-ghost.md) |
| **#70** eager slot-release ack | DONE, shipped default-off. 1.11×, encode‖tail 4.8 → 8.5 ms. Step 0 answered NO. Incomplete without #70B per PRD FR-ACK-2. | [`70-eager-slot-release-ack.md`](docs/experiments/70-eager-slot-release-ack.md) |

## #98 — act on the flow-control literature survey (TODO — filed 2026-08-04; OWNER DECISIONS NEEDED before any code)

**What.** The owner suspected we were re-deriving known theory; a
web-verified literature survey (15 years of SIGCOMM/NSDI/MobiCom/CoNEXT
+ deployed cloud-gaming systems) confirms it and is filed at
`docs/research/flow-control-survey-2026-08.md`. Summary of what it
settles about our measured 40 ms equilibrium:

* The measured closed form (`ack_latency = 50.5 ms + queue/43.1 MiB/s`,
  standing queue = window_bytes − BDP, drain = cwnd/RTT) is textbook —
  it is BBR's window-limited operating-region equation plus Little's
  law, and the multiple-equilibria property is Kleinrock (1979) /
  Jaffe (1981). The cwnd pinning is RFC 7661 behaving as specified,
  and its own recommended mitigation is pacing.
* **The bound to chase:** send-to-ack ≥ RTT + frame_size/bandwidth
  ≈ 60–100 ms on our 40 ms link, vs 203.7 measured.
* **The field's figures of merit** (every deployed system: Stadia/GCC,
  Google SQP, Tencent Pudica NSDI '24, Salsify NSDI '18): p95/p99
  per-frame delivery delay at a quality floor, stall rate, and the
  quality×delay Pareto — NOT frames/s at a given RTT. Industry latency
  budgets: <100 ms end-to-end for gaming-class, <150 ms RTC-class.
* **Remedies by implementation weight**, with the survey's expected
  effect on our numbers:
  - Tier 0 (config, hours): BBR + fq pacing on the server egress,
    `tcp_slow_start_after_idle=0`, buffers ≥ BDP + 2 frames.
  - Tier 1 (socket code, days): `TCP_NOTSENT_LOWAT` (~128 KB) so the
    standing queue lives in xrdp where damage-coalescing can eat it,
    plus userspace frame pacing (Trickle ATC '12 / SQP): spread each
    frame over the period at ~1.2× the needed rate instead of a
    3.4 MB burst. Tier 0+1 expected: 203.7 → ~60–100 ms send-to-ack,
    11.5 → ~25–45 fps at 40 ms.
  - Tier 2 (app rate control, weeks): encoder per-frame byte budget =
    delivered_rate × period — the direction EVERY deployed system
    converged on; the only tier that survives a genuinely slow link;
    what 60 fps at 40 ms requires (≤750 KB/frame at 43 MiB/s). Also:
    size the credit C to ⌈RTT/period⌉ + 1 rather than a constant.
  - Tier 3 (transport, months): QUIC / RDPEUDP2-style UDP. The survey's
    read: most of the latency win is already captured at Tier 2;
    Tier 3 buys loss-resilience (irrelevant on our loss-free testbed,
    relevant on real WANs).

**Decisions this item needs from the owner, in order:**
1. Adopt the field's FoMs in the PRD (p99 frame delay at a quality
   floor + stall rate + quality×delay Pareto) in place of / beside
   fps-at-RTT? This changes what every future WAN gate asserts.
2. Authorize a Tier 0+1 experiment (the cheapest lever the survey
   predicts moves 203.7 ms materially; needs an arm count + wall time
   stated before running, per the 2-minute rule).
3. Whether Tier 2 (encoder rate adaptation) enters the roadmap as its
   own item — it is a new control loop touching the encoder config
   surface, not a tweak.

**Interaction with #80:** none of this reopens #80's steps 1–3 — the
credit frontier is about WHO stalls WHOM inside the server, and its
LAN result stands. It reframes step 4's remaining work: the C-table's
prerequisite ("declared TCP environment") now has a concrete shape —
declare CC algorithm, pacing, and buffer sizes per leg — and C's own
sizing rule has a candidate closed form (⌈RTT/period⌉ + 1) to test
instead of a table search.