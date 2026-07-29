# BACKLOG

**What this file is:** the open work list. Nothing else.

**What it is not:** a lab notebook. Persistent decisions, contracts,
invariants, measured performance baselines and acceptance evidence live in
`PRD.md`; operational procedure lives in `DEPLOY_RUNBOOK.md`; working rules
live in `CLAUDE.md`. Incident narratives and campaign logs live in **git
history** — that is what it is for. If an entry here is still true after the
task closes, it belonged in the PRD; move it and delete it from here.

Rewritten 2026-07-28: this file had grown to 3268 lines of superseded
investigation records. The durable content was moved into `PRD.md`
(concurrency baseline, FR-H264-8 invariants + gate status, clean-room port
spec) and `DEPLOY_RUNBOOK.md` (install hazards, credential handling, GPU
checks, gfx.toml knobs, performance triage). Everything else is recoverable
with `git log -p -- BACKLOG.md`.

---

## Deployed state (2026-07-28)

| Box | Packages | Encoder config | Status |
|---|---|---|---|
| T4 (EC2, Tesla T4 / NVENC) | `xrdp-dev 0.10.80+git20260728184709.2a0279ef3aa1`, `xorgxrdp-dev 1:0.10.80+git20260728175938.5b9650cafbc3` | `PR-demo/t4_profile/gfx-t4-nvenc-ltr.toml` — `aux_ltr_chain = true`, `-g 30000` | **Renders correctly onscreen on both Windows (incl. multimon) and macOS** (owner-tested) |
| bisect fleet arm-n | image `34795577580b.xx5b9650c-xfce` | `gfx/arm-n.toml` — `aux_ltr_chain = true`, `-g 30000` | good on Windows multimon + macOS |

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
> fires first if `-g > 32512`. MUST be reverted to a sane GOP once #45
> lands.

---

## #45 — Scheduled paired intra refresh + race-free `main‖aux` (NEXT)

Immediate next step. Implements the revised **FR-H264-6** (PRD) and unblocks
the parallel encode. Steps are ORDERED: 1–4 are correctness and ship on their own; 5 is
pure throughput and must come last, because the IDR race it would otherwise
inherit is only removed by 1–4.

**0. Ratchets first** (must FAIL against today's code before step 1):
   extend the pure-C DPB simulator in `tests/xrdp/test_avc444_ltr.c` with a
   scheduled paired-cut AU sequence in both 1-context and 2-context decode
   modes; add byte-exact goldens from `ltr_splice_ref.py`; add an assertion
   mode to `tools/avc444_ltr_wire_audit.py` — it already parses every fact
   needed (slice type, nal type, frame_num, rplm target, mmco6 slot, per
   view) but asserts none of them and always exits 0. What each
   verification layer can and cannot prove is in PRD FR-H264-6, "Verify,
   never assume".

**1. Rewriter must accept BOTH intra input shapes, in both views.**
   `slice_ltr_rewrite()` today handles exactly two inputs: IDR-carrying-I,
   and P. Both scheduled shapes are rejected as `unsupported`:
   - **non-IDR I** (what `h264_nvenc` emits at a forced key frame without
     `-forced-idr`) hits the `B/SP/SI or non-IDR I` reject at
     `xrdp_h264_annexb.c:2128`. Needs a third branch: parse the child's
     `dec_ref_pic_marking`, emit no `ref_pic_list_modification` (I slices
     have none), replace marking with the constant mmco6 self-mark, skip
     `cabac_init_idc` (P-only).
   - **mid-stream IDR** (what `h264_vaapi` emits, always) is accepted but
     mishandled for the main view: `ltr_rewrite_walk` resets `cur_fn = 0`
     and clears `aux_seeded` (`:2447–2457`, `:2527–2531`) — the DPB flush
     we are trying to abolish. Generalize the existing `to_seed_i`
     conversion (proven in production for the aux seed) to view 0 whenever
     `st->started`, so the shared counter continues and LT1 survives.
   Both backends must be supported; neither shape is optional.

**2. Schedule + observed-vs-requested check.** Both children spawned with
   an identical frame-indexed `-force_key_frames`; interval from a new
   gfx.toml knob. `xrdp_h264_ltr_state` carries the expected refresh index;
   a picture that parses P where intra was scheduled fails the pair loudly
   (same class as an aux P with LT1 unseeded) — this is the runtime ratchet,
   the only check that runs before the client sees the frame.

**3. Drop the SPS/PPS-alongside clause from FR-H264-6** unless a reason
   appears. A non-IDR I is not a decoder entry point, the stream never
   seeks, and EGFX is reliable — repeated parameter sets buy nothing and
   cost bytes at every refresh. Child-emitted SPS/PPS must still pass
   through on the main view (already handled, `:2350–2392`).

**4. Delete the aux respawn path** (`encode_pair` `:1184–1197`,
   `ltr_aux_fresh`) and restore a sane refresh interval on the T4 profile
   and arm-n (≈240 costs ≈ +4 % bandwidth; PRD FR-H264-6).

**5. `main‖aux` — MOVED to #51** (converged with monitor concurrency; the
   two are the same serialization and the same fix). Retained here for the
   ordering constraint only: #51 needs steps 1-4 first. Original analysis:
   submit both, collect both — the two children are
   independent processes and genuinely encode concurrently. The ONE
   constraint (and the whole of the correction to PRD's "mechanical
   refactor"): **`submit` must transfer, not merely enqueue.**
   `in_iov_push()` performs no I/O — it appends to an iov array — and
   `feed_vmsplice()` has exactly one caller, inside `pump()`
   (`xrdp_encoder_ffmpeg.c:730`, `:794`, `:856`). So a split where
   `submit_single()` is just the push half sends nothing to the aux child
   until `collect_aux()` pumps it, and the pair stays serial.
   `submit_single()` must therefore pump until `!in_iov_pending()`,
   without waiting for output.

   With that, sequential writes already buy most of the win, because a
   write is cheap against an encode: serial `2(w+e)` becomes `2w+e`
   (4K: `e` = 19.6 ms measured, `w` = a ~15 MB vmsplice ⇒ ~43 ms → ~24 ms).

   The union poll must be **set-shaped, not pair-shaped**:
   `pump_set(kids[], n, deadline)` with the caller owning the completion
   predicate, so `n = 1` (LC=1 main-only) and `n = 2` (pair) are the same
   code — a `pump2(main, aux)` would hard-wire "two issue, two retire" and
   break under FR-PROC-7's variable shape. Do NOT thread main/aux: see PRD
   "Threads: not on the main/aux axis". Under ONE shared deadline, it is a
   robustness upgrade, NOT a prerequisite — it is not needed to avoid a deadlock,
   since the parent is never blocked on the child it is not draining.
   What it buys: `F_SETPIPE_SZ` is applied only to the INPUT pipe (1 MB,
   `:528`); the output pipe keeps the 64 KB default, and a 4K intra packet
   is several times that, so an undrained child stalls mid-write and
   erodes the overlap exactly when packets are largest. Measure the simple
   form first and only add the union poll if the measurement demands it.
   Re-check the FR-PROC-6 borrowed-pointer contract per child at collect.

Acceptance:
- **Ratchets verify the schedule is OBSERVED, not requested** — at every
  scheduled index both views parse `slice_type == I`; a silent skip fails
  the test, never degrades quietly.
- No IDR mid-stream; frame_num strictly +1 per picture across a refresh;
  every P's list-modification resolves to its own view's LT slot; both
  chains' transitive depth ≤ the configured interval.
- **Fault-injection recovery check (client harness, dev box only):** an
  oracle-client run drops/corrupts exactly one P, then measures pixel
  re-convergence — must heal within ≤ N frames on the scheduled-cut
  stream and must NOT converge on a `-g 30000` control. This is the only
  test that makes I3 *observable* (a healthy screen shows identical
  pixels either way); the wire ratchets remain the proof of the
  invariant itself.
- `make check` green; topology 1/2/3 identity unchanged; bandwidth gate
  re-run so the refresh cost is recorded, not assumed.
- T4 re-measured as **frame period**, not encoder ms — see the gain model
  in PRD "Concurrency state of the encode pipeline".

---

## #51 — Converge `main‖aux` + monitor concurrency into ONE `pump_set`

Supersedes #45 step 5 as a standalone item: the two are the SAME
serialization with the same fix, and splitting them would build the
mechanism twice. Everything is serialized behind one worker thread
blocking in `pump()` on one child — main before aux, monitor 1 before
monitor 2 — so m monitors cost `2m(w+e)`.

**Justified by measurement, not assumption** (dev-box VAAPI, 2026-07-29,
`PR-demo/vaapi_concurrency_bench.sh`; full table in PRD):
N=2 concurrent encodes are FREE (0.90×/0.94× per stream), N=4 costs only
1.39×/1.64×. At 4K that is 13.9 ms → 6.5 ms for one monitor (2.1×) and
27.9 ms → 11.4 ms for two (2.4×). The local N=2 ratio (0.94×) independently
corroborates the T4's recorded 0.96×, so "two concurrent encodes are free"
now holds on two vendors' hardware.

Scope:
1. Factor `pump()` into `pump_arm()` (build this child's pollfd slots)
   and `pump_service()` (dispatch revents: `feed_vmsplice` / stderr /
   stdout). `pump()` keeps its signature — every existing caller is
   untouched.
2. `pump_set(kids[], n, deadline)`: arm all, poll once, service all. The
   CALLER owns the completion predicate, so shape lives with policy —
   `n=1` LC=1, `n=2` a pair, `n=2m` m monitors. NOT `pump2()`: a
   pair-shaped helper breaks under FR-PROC-7's variable shape.
3. `submit_single()` must PUMP until `!in_iov_pending()`, not merely
   `in_iov_push()` — `feed_vmsplice()` has one caller, inside `pump()`,
   so a push-only submit sends nothing and the pair stays serial.
4. ONE shared deadline across the set (otherwise a stalled child gets
   `n × pair_timeout_ms`), and the FR-PROC-6 borrowed-pointer check per
   child at collect.
5. `kids[]` is REBUILT every cycle from live `avc444_ffmpeg_handle[]`, never
   memoized — a monitor dropped mid-session (UWP windowed mode) must
   simply stop contributing a child.

Ordering: needs #45 steps 1-4 first. Until the aux-respawn path is gone,
whether the aux child must be REPLACED depends on main's rewrite result
(`encode_pair` `:1184`), so aux cannot be submitted before main is
collected — a data dependency no poll restructuring removes.

Do NOT thread main/aux (shared `ltr`, pair contract, zero CPU to win).
Threading monitors is safe but unnecessary once `pump_set` lands — the
per-monitor state is already partitioned; they just join the set. This
is FR-PROC-7's BREADTH policy.

**Cheap local measurement to do FIRST, before any cloud spend:** the T4
pair cost 67.5 ms while two 4K encodes account for 39.2 ms — a ~28 ms
remainder that is NOT encode and that concurrency cannot touch. The dev
box can attribute it for free: run a fleet arm at a fixed resolution and
compare its frame period against the 6.97 ms raw-encode floor measured
here. Whatever the difference is (capture, vmsplice feed, NUT demux, LTR
rewrite, EGFX assembly) is the real ceiling, and it should be attributed
BEFORE re-provisioning a GPU box, not after.

## #40 — FR-PROC-7 preemptive aux (sparse aux cadence)

Submit/collect construction plus all three policies (preempt, breadth,
depth) with per-policy unit tests. Spec: `PRD.md` FR-PROC-7. Prerequisite
FR-CAPTURE-8 is shipped; the submit/collect split is shared with #45, so
sequence #45 first.

## #41 — Deploy FR-PROC-7 + measure

Smoke gate, colour-edge check, combined fps on the T4 and the fleet.

## #46 — Clean-room upstream port

Rebuild the feature as reviewable slices against fresh `origin/devel`.
Locked decisions, exclusions, base-ref rules and acceptance criteria are in
`PRD.md` §17 "Clean-room upstream port". Note the pre-existing astyle drift
in files this branch does not own (`xrdp_avc444_caps.c`, the rfx block of
`xrdp_encoder.c`, `xrdp_types.h`, `xup_client_info.h`,
`tests/.../repro_mbparity/*`) must be resolved in that pass —
`scripts/run_astyle.sh -v 3.4.14`, never the system astyle 3.1.

---

## #49 — CI never runs the ffmpeg-path tests; nine of them report PASS anyway

Found 2026-07-29 while checking whether CI gated the frame_num-wrap re-key
timing (PRD FR-H264-8). It does not, and the problem is not specific to
that test.

`tests/xrdp/test_avc444_ffmpeg.c` gates **nine** tests on
`XRDP_TEST_FFMPEG_PATH`. The CI `unittests` step runs a bare `make check`
(`.github/workflows/build.yml:180`) and never sets it, so all nine return
early. Check has no skip verdict, so each is reported `ok N ... Passed`.
CI has therefore been green on this file while executing none of it —
including `test_ffmpeg_encode_pair`, the standing regression guard for the
content/region desync that froze mstsc on stale frames, and
`test_ffmpeg_single_sps_per_keyframe`, the guard for the duplicated-SPS
config that rendered black on the macOS Windows App. A green suite that
proves nothing is exactly the failure mode the strict-honesty rule exists
to prevent.

Done already: `have_ffmpeg()` now logs a WARNING on every skip naming the
file and saying the PASS proves nothing, so `test-suite.log` (which CI
uploads on failure) shows it.

Scope: make CI actually run them. The runner is `ubuntu-latest`, which
ships ffmpeg with libx264, so the likely fix is one line — set
`XRDP_TEST_FFMPEG_PATH=/usr/bin/ffmpeg` on the unittests step, after
confirming the build dependency script installs a usable ffmpeg on every
matrix leg (and skipping the variable on legs where it does not, rather
than failing them). Measure the added runtime first: the LTR re-key test
alone drives 32 pair encodes.

Open question for the owner: whether CI should HARD FAIL when ffmpeg is
absent (no silent inert leg anywhere) or keep an explicitly-reported skip.

## Owner-blocked

- **Owner sign-off** on making `aux_ltr_chain` the default (after #45
  lands and the gates are re-run).
