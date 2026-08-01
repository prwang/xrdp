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

## #61c — Is the producer the ceiling? REOPENED 2026-08-01 (#61h voided the run that answered it)

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

## #61b — textflood wired into the fleet (plumbing DONE; its NUMBERS reopened by #61h)

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

## #61f — Cut the delivery loop's latency: the encoder is ack-clocked through a busy main thread (TODO)

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

**#61h is DONE, so this is unblocked — but it must START by
re-measuring.** The service latency this item exists to cut has never
been measured with an instrument that was off the path. Take
`msgin − cap_sent` on a ring-traced build first; the target for Step 1
follows from that number, not from the void 16 ms.

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

## #61g — The oracle client's 50–150 ms pauses: NOT scheduling; what they are is still open (TODO)

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

## #61e — REOPENED 2026-08-01: close the period, and settle `capture ‖ encode` at m=1

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

## #70B — REOPENED 2026-08-01: does the emit split buy anything?

Previously marked DONE at 1.12x under textflood (and 0.96x under
codeflood, already void as producer-bound). **The 1.12x is void (#61h)**
— measured on x001–x004, all with the per-frame trace on log.c.

The code ships and stays default-off; its prerequisite refactor fixed a
real use-after-free and that is unaffected. What has to be measured
again is whether splitting the emit off the worker moves the rate at
all, and by how much. PRD FR-ACK-2 still names it as #70's completion.

## #74 — Lever 2 architecture: DECISION OPEN (owner discussion next iteration; was task "#40 implement FR-PROC-7")

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
