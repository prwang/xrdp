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

## #61c — The producer is the ceiling, and it is the payload's own drawing (ANSWERED 2026-08-01; the lever is #61b)

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

**Record:** `PR-demo/mac_bisect_matrix/captures/i61c_xorg_profile_20260801/README.md`.

## #71 (was #65) — multimon capture‖encode: per-monitor ack window + the m≥2 serial cost (TODO — after #70; the global-window arithmetic stands on its own CI pin)

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

## #72 (was #66, earlier #63) — 4:2:0 while the screen is in motion, 4:4:4 when it settles (blocked by #71 — FR-PROC-7's preemption signal needs the fifo non-empty at pop time, which needs #70/#71 concurrency first)

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

## #73 (was #67) — T4 benchmark re-runs under restored concurrency (blocked by #70 + #71; #72 optional but preferred)

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

## #53 — Arm a monitor only when its pixels changed (TODO, NEXT)

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

## #54 — Capture-side handoff: the remaining 2× (TODO)

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

## #59 — The capture is 14 % of the bottleneck thread; the rest is not ours to optimise (TODO — one lever left, see #61b)

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

## #60 — The T4's E5-2 is bimodal and it is not root-caused (TODO)

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

## #61b — textflood wired into the fleet (DONE 2026-08-01)

`SESSION_KIND=textflood` now exists in `banner.sh` and the binary is
built into the fleet image (builder stage in `Containerfile`). Arms
**x003/x004** are the #70B A/B under it at 3840x2400.

Session Xorg **96.4 % -> 25.3 %** of one core; FR-BENCH-1 **PASSES**
(producer 65.07 fps vs pipeline 24.94 = 2.61x margin) — the check #62
lacked. GLAMOR stays CLOSED-WONTFIX.

**Every throughput number from here uses textflood.** A ratio measured
under codeflood is a measurement of the X server (#61c).

**Record:** `PR-demo/mac_bisect_matrix/captures/i61b_x004_ab_20260801/README.md`.

## #61d — Re-measure the codeflood-era ratios under textflood (TODO)

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

## #61e — Close x004's period to ≤0.5 ms unknown, and settle capture‖encode at m=1 (IN PROGRESS)

**The claim under test is PRD's, not a ratio.** The `capture ‖ encode =
YES for m = 1, shipped` row asserts the frame period *equals* the encode
duration and the capture is *fully hidden*. It was measured at 1600×912
in a different era. At 3840×2400 on x004 the worker's serial chain is
**29.07 ms** against a **35.95 ms** period: **6.88 ms/frame belongs to
no stage**, and PRD's claim is exactly the claim that none of it is the
encoder waiting for a frame.

**Step 1 (DONE, no new code).** Attributing every consecutive pair of
worker events, not just the bracketed stages, closes x004 to
**−0.063 ms** and names the two holes: `join_end -> coll_beg`
**2.38 ms** and `coll_end -> drain_beg` **4.49 ms**. So the 6.88 ms is
real elapsed time on the worker thread, not a pairing artefact — but it
is named by *tag*, not by *code*, which is not an attribution.

**Step 2 (this item).** Five new brackets under the existing
`XRDP_PERF_TRACE` sink, shipped disarmed:

| bracket | names |
|---|---|
| `book_beg/end` | the counters + `LOG(LOG_LEVEL_DEBUG)` + `GFX_TRACE` block — the whole of hole A |
| `rel_beg/end` | `gfx_batch_release_slots()`, the #70 CONSUMED ack |
| `wait_beg/end` | **`g_obj_wait` — the encoder holding nothing to encode** |
| `enq` (main thread) | a frame becoming available, keyed by its echoed id |
| `take` (worker) | the same id lifted off the fifo |

**What decides it.** `wait` is the per-frame millisecond count by which
capture is NOT hidden behind encode; PRD's claim is that it is zero.
`enq -> take` residency is the same statement in its positive form — a
residency that is consistently positive says the data was already in
hand and the worker was busy, i.e. ffmpeg could not have seen frame N+1
any sooner. The two are complementary and must agree: every cycle is
either worker-bound (residency > 0) or capture-bound (wait > 0), never
both, never neither.

**Gate 4 is mandatory before reading anything.** The instrumented build
is not the build that measured 1.12×. New arms **x005/x006** (not a
rebuild of x003/x004, whose images stay as measured) must reproduce
40.25 / 35.95 ms, or the attribution is of a different system.

**Falsifiable.** If `wait` is ~0 on both arms, PRD's row survives at
3840×2400 and the 6.88 ms is all worker overhead we own. If `wait` is
several ms, the row is wrong at this geometry and the capture pipeline —
not the encoder — is the next lever.

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
| **#70B** emit split | Built 2026-08-01. 0.96x under codeflood (producer-bound, void as a throughput number) then **1.12x under textflood at 3840x2400**. Shipped default-off; its prerequisite refactor fixed a real use-after-free. | [`70B-perf-trace-sink.md`](docs/experiments/70B-perf-trace-sink.md), [codeflood](PR-demo/mac_bisect_matrix/captures/i70b_x001_ab_20260801/README.md), [textflood](PR-demo/mac_bisect_matrix/captures/i61b_x004_ab_20260801/README.md) |
