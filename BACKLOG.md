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

## Deployed state (2026-07-30)

| Box | Packages | Encoder config | Status |
|---|---|---|---|
| T4 (EC2 `100.24.126.48`, Tesla T4 / NVENC, 4 vCPU) | `xrdp-dev 0.10.80+git20260730013346.52b8798839ad` (#45 steps 0–7 + log clock fix), `xorgxrdp-dev 1:0.10.80+git20260729225933.d77d05463e52` (step 6) | `PR-demo/t4_profile/gfx-t4-nvenc-ltr-g240-gate.toml` — `aux_ltr_chain = true`, `intra_refresh_frames = 240` | **#55 E5-2 = 1.67× AMBER**, wire audit 7/7, smoke gate PASS at both sizes. Payload disarmed, sessions off, `XRDP_GFX_TRACE=1` drop-in and `nvidia-smi -pm 1` in place. **Onscreen (UWP/macOS) not yet walked** |
| T4 — previous state (2026-07-28, for reference) | `xrdp-dev 2a0279ef3aa1`, `xorgxrdp-dev 5b9650cafbc3` | `gfx-t4-nvenc-ltr.toml` — `-g 30000` | Rendered correctly onscreen on Windows (incl. multimon) and macOS (owner-tested) |
| bisect fleet arm-n | image `34795577580b.xx5b9650c-xfce` | `gfx/arm-n.toml` — `aux_ltr_chain = true`, no `-g` (the runner pins it) | good on Windows multimon + macOS |
| bisect fleet arm-r (2026-07-29) | image `f7acb5979788.xxd77d054` = xrdp #45 steps 0–7 + xorgxrdp step 6 | `gfx/arm-r.toml` — `aux_ltr_chain = true`, `intra_refresh_frames = 240` | #45 gates E1/E2/E3/E6/E7 PASS; its E5 number was payload-clocked (see #45 GATE RESULTS). Kept as the 10 Hz-cadence reference arm |
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

## #45 — Scheduled paired intra refresh, one-thread 4-view `pump_set`, per-monitor capture budget (DONE — E5 resolved by #52 at 2.13×)

ONE plan across two repos: xrdp (steps 0–5, 7) and xorgxrdp (step 6).
Implements the revised **FR-H264-6** (PRD) and the multimon
encode-and-capture concurrency together, because measuring one without the
other produces a false number (step 6/7 rationale). The former #51
(`pump_set`) and the short-lived #52 (capture budget) are absorbed here and
no longer exist as items.

Everything below is either a **decision** (D1–D17, binding), a **fact**
(verified in source or git history, with the location), or a **required
recon gate** (R1–R2: fleet measurements that must reach their acceptance
gate BEFORE the step that uses the property lands). Nothing in this item
is open.

### End state — all SEVEN hold on ONE deployed pair (xrdp deb + xorgxrdp deb)

**E1 — the non-IDR-I wire format is fully functional and smoke-gated.**
`PR-demo/smoke_gate/smoke.sh` passes at BOTH its sizes against the
package-installed binaries and config, with `aux_ltr_chain = true` and the
schedule live. Same build, not a separate config or arm.

**E2 — a long payload renders, ≥ 1000 frames.** The code/scroll corpus
(`SESSION_KIND=code`, the FR-H264-8 gate baseline) for **≥ 1000 consecutive
encoded pairs** at `intra_refresh_frames = 240`, i.e. **≥ 4 scheduled cuts**
in one uninterrupted stream. All four required:
  - `oracle_black_frame_check.py`: every picture decodes, **zero** black
    frames anywhere (the owner's decode gate, 2026-07-29 — nothing is
    handed over onscreen before it passes);
  - `tools/avc444_ltr_wire_audit.py --assert` exits non-zero on any
    violation (step 0 gives it teeth);
  - server log: zero rewrite failures, zero `unsupported`, zero pair aborts;
  - at each cut index both views parse `slice_type == I`.

**E3 — offscreen dual monitor at the real target geometry.** The E2 run at
**2560×1440 + 3840×2400**, headless on the dev box
(`PR-demo/multimon_offline/`, parameterised from its hardcoded 2×1024×768;
its fail-loud "exactly 2 monitors or abort" behaviour kept). Both monitors
audited independently.

**E4 — ONE thread drives FOUR views concurrently.** With both monitors
damaged in the same cycle, the single `proc_enc_msg` worker issues **one**
`pump_set(kids[], 4, deadline)` covering main₁, aux₁, main₂, aux₂. Proven
by an instrumented per-cycle set-size counter asserted in the E3 run, never
inferred from a wall-clock improvement.

**E5 — the speed-up is measured locally on VAAPI as the ORACLE FRAME
INTERVAL, WITH step 6 landed.** The quantity #45 chases is the oracle
client's frame interval: the server-side interval between successive sends
when the client acks before decode and present, so nothing but the server
is in the number (never encoder ms — PRD "Concurrency state of the encode
pipeline"; never the rendering client's rate, which is a client
measurement). Dev-box VAAPI, E3 geometry and payload, against the recorded
pre-#45 oracle frame interval for the identical arm, payload, geometry.
**Baseline measured 2026-07-29, before any of steps 5–7:** 51.1 ms mean
per send (p50 50 ms, p90 103 ms) = 19.57 sends/s = 9.79 pairs/s per
monitor at 2560×1440 + 3840×2400
(`PR-demo/mac_bisect_matrix/captures/ab_oracle_215330/`). **≥ 2.0×
therefore means ≤ 25.6 ms mean per send** on that arm and payload.
Step 6 must be deployed in the same measurement, or the batching
gain is silently paid for out of lost capture overlap and the number is a
lie (step 7 rationale). Prediction from the measured concurrency table
(N=2 ≈ free, N=4 at 1.39×/1.64×): **≥ 2.0×** on the dual-monitor oracle
frame interval. **Stop rule:** under 1.5× the item is RED and the remainder is
attributed (capture, vmsplice feed, NUT demux, LTR rewrite, EGFX assembly)
before anything ships — not re-tuned until it looks better, not reported as
a partial win.
**Why the oracle interval is the target and not the end-to-end rate —
measured 2026-07-29.** Clean A/B on one fresh
arm-q pod at E3 geometry, 60 s each, back to back
(`PR-demo/mac_bisect_matrix/captures/ab_render_215222/`,
`ab_oracle_215330/`): distro `xfreerdp3` decoding and presenting delivers
**5.94 sends/s = 2.97 pairs/s per monitor** (send gap p50 103 ms, p90
331 ms, mean 169 ms), the oracle client — which acks before decode and
present — delivers **19.57 sends/s = 9.79 pairs/s per monitor** (p50
50 ms, p90 103 ms, mean 51 ms). The session is **client-bound by 3.29×**:
the client needs ~117 ms per surface frame on top of the server's 51 ms,
the same software 4:4:4 reconstruction cost recorded on 2026-07-26 (~65 ms
at the owner layout), now measured at target geometry. A 3.3× client
ceiling swallows a 2× server improvement whole: chase the end-to-end rate
and #45 would measure xfreerdp's YUV444 reconstruction, show ~1.0×, and
call correct server work a failure. **So E5 chases the oracle frame
interval.** The rendering client's rate is recorded beside it every time
as the end-to-end figure — it is context, never the gate, and it will not
move until the client side is addressed (out of #45 scope). Fidelity gates
(E1, E2's black-frame check, E7) keep using the real rendering client — the
oracle proves no fidelity and must never be smoke-gated on. Both runs sat
at outstanding depth 2 for about half their sends, so the difference is
pace, not budget contention.
The non-concurrency remainder is characterised HERE, locally, before any
cloud spend: the T4 pair cost 67.5 ms while two 4K encodes account for
39.2 ms — a ~28 ms remainder that is NOT encode and that concurrency
cannot touch. Run a fleet arm at fixed resolution against the 6.97 ms
dev-box raw-encode floor and attribute the difference (capture, vmsplice
feed, NUT demux, LTR rewrite, EGFX assembly) BEFORE re-provisioning a GPU
box; this attribution is the number the stop rule reaches for if the
speed-up lands short.

**E6 — nothing else regressed.** `make check` green; topology 1/2/3
identity unchanged; FR-H264-8 bandwidth gate re-run so the refresh cost is
recorded, not assumed (predicted ≈ +4 % at N=240, PRD FR-H264-6).

**E7 — the dual-monitor drag gate.** Extend the orbiting-thunar load
driver (`PR-demo/t4_profile/profile_owner_load.sh`, xdotool windowmove)
with an **up/down sweep that crosses the monitor boundary**, driven inside
the offscreen xfreerdp dual-monitor rig (E3's rig, on the dev box) — the
drag pattern of the `251bc4d` incident, made deterministic. The gate
asserts **per-monitor frame progress** — each monitor's rect stream
advances during the sweep; overall liveness is not sufficient, because the
original bug froze one monitor while the other flowed and a whole-session
check would have passed it.

### The capture-side state machine (facts; the frame for step 6)

Verified in `/workUpdateXorgXrdp/module/rdpClientCon.c`, 2026-07-29:

- **One `dirtyRegion` for the whole virtual desktop** — damage from all
  monitors lands in one region. `rect_id`/`rect_id_ack` are **global**
  counters: `rect_id++` per send, one send = one monitor's frame, the ack
  is cumulative.
- **One event per monitor per frame, carrying BOTH views.** xorgxrdp emits
  one `STARTFRAME+WIRETOSURFACE_1+ENDFRAME` per monitor (`:3357`); the
  shmem slot holds `[main NV12][aux NV12]` packed (FR-CAPTURE-6) and xrdp
  splits it (`xrdp_encoder.c` `main_view`/`aux_view = main_view +
  aux_offset`). Queue states like "m1C1main, m1C1aux" are not
  representable; m monitors damaged = m FIFO items.
- **The scan** (`rdpDeferredUpdateCallback`): snapshot `rotation =
  rect_id`; visit each monitor once in rotation order; per monitor,
  intersect its rect with `dirtyRegion` — empty ⇒ no send, else capture ⇒
  send ⇒ `rect_id++`; subtract the served region; **BREAK when the budget
  gate trips**. After the loop, **only if every monitor was visited**
  (`monitor_index == monitor_count`), `dirtyRegion` is cleared — the
  "nothing left that any monitor covers" clear. A pass that breaks early
  leaves the remainder in `dirtyRegion` and reschedules.
- **The load-bearing invariant:** any path that reaches the completed-scan
  clear while some monitor was NOT visited destroys that monitor's damage.
  That is exactly the `251bc4d` incident (live rotation base revisited the
  just-sent monitor, skipped another, clear ate its damage — frozen bottom
  4K monitor during dual-monitor drags). Any change to this loop is judged
  against this invariant first.
- **Today's budget is global 2** (`MaxOutstandingRects`, `:130`) over
  per-monitor slots (`cap_offsets[mon] + slotIndex * cap_slot_bytes[mon]`).
  Consequences at m = 2: (a) the two-slot alternation is inert — `rect_id`
  advances by m between one monitor's consecutive sends, so `(rect_id+1)&1`
  is constant per monitor and each monitor is pinned to one slot
  (**measured, R1: same slot on 1079/1079 full-pass consecutive sends;
  per-monitor two-slot pipelining fired once in 1100 sends**);
  (b) a batched 4-view encode holds both budget slots for its whole
  duration, so no capture can overlap it. The overlap that exists today at
  m = 2 is *cross-monitor* interleaving via the per-item ack
  (`xrdp_mm.c:4088`), not the two-slot mechanism. FR-CAPTURE-8 was designed
  and measured at m = 1 (PRD clause 3 has no `monitorCount` term); multimon
  was never covered, so this is a gap being closed, not a regression being
  repaired.

### Steps (ordered; 0–4 correctness, 5–7 throughput; 6 lands before E5 is measured)

**0. Ratchets first — must FAIL against today's code before step 1.
   DONE 2026-07-29, RED demonstrated and recorded** in
   `PR-demo/mac_bisect_matrix/captures/ratchets_red_20260729/` (three new
   C ratchets fail at `cd856fad`; the wire audit's new `--assert` mode
   fails 5 of 7 checks on the pre-#45 arm-o capture whose descriptive
   verdict is PASS). Four corrections to this item's own text came out of
   it, recorded at the end of this section.
   Extend the pure-C DPB simulator in `tests/xrdp/test_avc444_ltr.c` with a
   scheduled paired-cut AU sequence in both 1-context and 2-context decode
   modes; byte-exact goldens from `ltr_splice_ref.py`; an assertion mode
   for `tools/avc444_ltr_wire_audit.py` (it parses every needed fact —
   slice type, nal type, frame_num, rplm target, mmco6 slot, per view —
   and today asserts none and always exits 0). Each ratchet is demonstrated
   RED against the current build and the RED output recorded in the commit
   message. A validator that cannot fail is worthless; one of ours asserted
   a bug as correct (2026-07-29, `rekey_boundary_audit.py` check B).

**1. Rewriter accepts BOTH intra input shapes, in BOTH views. DONE
   2026-07-29 (`9653bd1f`).** The reject at `xrdp_h264_annexb.c:2128`
   plus the three walker gates that keyed on `ntype == 5` (C1); the
   emitter needed no change at all.
   `slice_ltr_rewrite()` today handles exactly IDR-carrying-I and P:
   - **non-IDR I** (`h264_nvenc` at a forced key frame without
     `-forced-idr`) hits the reject at `xrdp_h264_annexb.c:2128`. Third
     branch: parse the child's `dec_ref_pic_marking`, emit no
     `ref_pic_list_modification` (I slices have none), replace marking with
     the constant mmco6 self-mark, skip `cabac_init_idc` (P-only).
   - **mid-stream IDR** (`h264_vaapi`, always) is accepted but mishandled
     on the main view: `ltr_rewrite_walk` resets `cur_fn = 0` and clears
     `aux_seeded` (`:2447–2457`, `:2527–2531`) — the DPB flush this FR
     abolishes. Generalise the production-proven `to_seed_i` conversion to
     view 0 whenever `st->started`, so the shared counter continues and LT1
     survives.
   Both backends ship in the same build or neither does (D11): the dev box
   (VAAPI) and the T4 (nvenc) each exercise exactly one shape, so a
   single-shape build passes half the fleet silently.

**2. Schedule + observed-vs-requested check. DONE 2026-07-29
   (`9653bd1f` rewriter half, `a0d9e773` schedule + plumbing).**
   `intra_refresh_frames` plumbed through all four hops; both children
   spawned with `-force_key_frames expr:not(mod(n,N))` (no backslash --
   the escaped form is rejected by ffmpeg, verified) and `-g N`; the
   rewriter fails the pair on a mismatch in EITHER direction. Proven on
   two real ffmpeg children by `test_ffmpeg_scheduled_paired_cut_live`:
   77 pairs at period 24, cuts only on scheduled ordinals in both views,
   zero mid-stream IDRs. Both children spawn with an
   identical frame-indexed `-force_key_frames` schedule derived from
   `intra_refresh_frames` (D6). `xrdp_h264_ltr_state` carries the expected
   refresh index; a picture parsing P where intra was scheduled **fails the
   pair loudly** (same class as an aux P with LT1 unseeded). The only check
   that runs before the client sees the frame; it fails the pair rather
   than shipping it.

**3. No parameter sets at a cut** (D5, unconditional). **DONE
   2026-07-29 (`9653bd1f`), resolved as D18 after finding the item
   contradicted itself (C2).** A non-IDR I is not a
   decoder entry point, the stream never seeks, EGFX is reliable.
   Child-emitted SPS/PPS still passes through on the main view (`:2350–2392`).

**4. Delete the aux respawn path; retire the `-g 30000` interim. DONE
   2026-07-29 (`a0d9e773`).** `-g` is REMOVED from arm-n/p/q and both T4
   LTR profiles rather than lowered: the runner pins it, so a `-g` in
   `encoder_args` would be silently overridden. Remove
   `encode_pair` `:1184–1197` and `ltr_aux_fresh`. Set `-g` equal to
   `intra_refresh_frames` (D7) on the T4 profile, arm-n and arm-p — every
   intra picture lands on a scheduled index by construction, "unscheduled
   IDR" is unreachable, and the frame_num-wrap re-key becomes the live wrap
   mechanism instead of being masked by the GOP IDR.

**5. `pump_set` (xrdp). DONE 2026-07-29 (`3ceed31d`).** `pump()` is
   now the n = 1 case, argued equivalent path by path;
   `submit_pair`/`pump_pairs`/`collect_pair` is the construction #40
   shares; `test_ffmpeg_pump_set_four_views_one_thread` asserts
   `kids_armed == 4` over four real children.
   1. Factor `pump()` into `pump_arm()` (build this child's pollfd slots) +
      `pump_service()` (dispatch revents: `feed_vmsplice`/stderr/stdout).
      `pump()` keeps its signature; existing callers untouched
      (`pump_set(&self, 1, ...)`).
   2. `pump_set(kids[], n, deadline)`: arm all, poll once, service all.
      The CALLER owns the completion predicate — `n=1` LC=1, `n=2` a pair,
      `n=4` two monitors. Never `pump2()`: pair-shaped hard-wires "two
      issue, two retire" and breaks under FR-PROC-7's variable shape.
   3. `submit_single()` **pumps until `!in_iov_pending()`**, not merely
      `in_iov_push()` — `feed_vmsplice()`'s only caller is inside `pump()`
      (`xrdp_encoder_ffmpeg.c:730`, `:794`, `:856`); a push-only submit
      sends nothing and the pair stays serial.
   4. **ONE shared deadline** across the set (D3) — else a stalled child
      costs `n × pair_timeout_ms` — and the FR-PROC-6 borrowed-pointer
      contract re-checked per child at collect.
   5. `kids[]` rebuilt every cycle from live `avc444_ffmpeg_handle[]`,
      never memoized — a monitor dropped mid-session (UWP windowed mode)
      simply stops contributing a child.

**6. Per-monitor capture budget, depth 2 (xorgxrdp; lands before E5).
   DONE 2026-07-29 — xorgxrdp `d77d054` + xrdp `6f80b0fe`.** Deb:
   `dist/xorgxrdp-dev_1%3a0.10.80+git20260729225933.d77d05463e52_amd64.deb`
   (verified recon-free). Three adversarial reviews ran against the
   diff; two real findings were fixed before the commit and three
   consequences are recorded below (see "What step 6 changed that the
   item did not predict").
   In sub-order:
   - **6a. Make the completed-scan clear structurally safe FIRST.** Replace
     "clear `dirtyRegion` when all monitors were visited" with the thing it
     actually means: `dirtyRegion ∩= union(all monitor rects)`, computed
     independently of the scan. Damage no monitor covers (layout gaps) is
     dropped explicitly; damage a monitor covers can never be destroyed by
     any scan-order bug again — the entire `251bc4d` hazard class is
     removed structurally rather than avoided. Own commit, gated by E7's
     harness, before any budget change.
   - **6b. Budget becomes PER MONITOR: ≤ 2 outstanding per monitor, never
     any global pool** (D13). Accounting: per monitor, a ring of its ≤ 2
     sent-and-unacked rect_ids, checked against the cumulative
     `rect_id_ack` (the ack already being cumulative is what makes a
     2-entry ring sufficient). The in-loop gate tests the capacity of **the
     monitor about to be served** and **BREAKs** when it is at cap (D15) —
     identical stall behaviour to today's global gate (today the whole pass
     returns), so no fairness regression, and with 6a landed the early exit
     cannot cost damage. The top-of-callback gate returns only when **no**
     monitor has capacity. Never skip-and-continue past a capped monitor
     (D15): a skipped monitor reaching a completed-scan state is the
     `251bc4d` shape by a second route.
   - **6c. Slot index becomes per monitor** (D17): a per-monitor counter
     incremented on that monitor's send replaces global `rect_id` parity.
     Restores two-slot alternation at even m; the two outstanding frames of
     a monitor always land in different slots again. **No layout or
     contract change**: the layout is already 2 slots per monitor and the
     slot rides the explicit per-frame `shmem_offset`, so the encoder side
     is untouched.
   - **6d. Overflow stays drop-never-queue, restated per monitor** (D14).
     At a monitor's cap, its next change coalesces into `dirtyRegion`
     (union + extents collapse) and the next capture packs the union —
     newest state wins, the intermediate frame is dropped before it exists
     (FR-CAPTURE-8 clause 4, currently global wording). The loud budget
     assertion and "no third capture" are asserted per monitor, or the
     global assertion fires on every legal multimon frame.
   - Re-derive the `/dev/shm` floor at 2560×1440 + 3840×2400 and record it
     (R2). No new shmem is expected (cap 2 = existing 2 slots/monitor), but
     the 512 Mi figure is re-checked, never assumed — an undersized tmpfs
     SIGBUSes Xorg mid-session (2026-07-28 incident).

**7. Batch both monitors into one set (xrdp).** At the top of each worker
   cycle, drain the input FIFO **non-blockingly**; group queued AVC444
   `WIRETOSURFACE_1` items into one set, **at most one per `mon_index`** —
   a second item for a `mon_index` already in the set belongs to the next
   frame and ends the batch. The worker **never waits** for a monitor that
   has not queued damage: `n = 2` when one monitor is damaged, `n = 4` when
   both are; no timer, no speculative wait (it would add latency on the
   common single-monitor path and cannot be bounded correctly). Each item
   keeps its own STARTFRAME/ENDFRAME framing, ack and shmem lifetime —
   batching changes when children are FED, never per-monitor PDU order.
   Scale the `fifo_to_proc_depth > 2` bound in `server_egfx_cmd`
   (`xrdp_mm.c:4819`) to `2m`, or it is a permanent false ERROR in every
   dual-monitor log.
   **Why 6 must precede the E5 measurement:** a batched set holds each
   monitor at 1 outstanding for the whole 4-view encode; under the global
   budget of 2 that saturates the gate and no capture overlaps the encode —
   frame period degrades from `max(capture, encode)` toward
   `capture + encode_set`. With 6b's per-monitor depth 2, each monitor has
   exactly one free slot during the set, capture overlap survives, and E5
   measures the real gain.

### Decisions (binding; the previously ambiguous language they replace is dead)

| # | Decision |
|---|---|
| D1 | **Zero threads.** Exactly one encoder worker (`proc_enc_msg`) before and after. Main/aux AND multimon are poll-set problems. A future threading proposal must first show a measurement `pump_set` cannot reach. |
| D2 | **`pump_set` is required.** E4 (`n=4`) is the end state; sequential-submit-only is not an acceptable stopping point. |
| D3 | **One shared deadline** across the set. `F_SETPIPE_SZ` is INPUT-pipe-only (1 MB, `:528`); the 64 KB output pipe stalls an undrained child exactly when packets are largest. |
| D4 | **#51 and #52 are folded in here.** One item, one approval. |
| D5 | **No SPS/PPS at a cut**, unconditionally. |
| D6 | gfx.toml `intra_refresh_frames`; C field `avc444_ffmpeg_intra_refresh_frames`; **default 240**; range **[24, 4096]** (loader refuses, runner clamps); effective only when `aux_ltr_chain = true`. **No 0/off value** — an off switch would keep the deleted respawn path alive as a shadow fallback. |
| D7 | **`-g` = `intra_refresh_frames`.** GOP boundaries coincide with scheduled indices; unscheduled IDR unreachable. |
| D8 | Fault-injection recovery bound: **≤ `intra_refresh_frames` + 1 pairs** (241 at default) from the injected corruption; the `-g 30000` control must NOT converge. |
| D9 | **E5 (dev-box VAAPI ORACLE frame interval — baseline 51.1 ms mean at E3 geometry, measured 2026-07-29) is the measurement gate.** The rendering client's end-to-end rate is recorded alongside but is not the gate: it is client-bound by 3.29× and would hide any server gain. The T4 pack-bench still runs per CLAUDE.md and is recorded, but T4 availability does not gate #45; T4 frame-period confirmation gates the `aux_ltr_chain` default flip (Owner-blocked). |
| D10 | Four children reach one set via step 7's batching rule over per-monitor FIFO items (one event per monitor, fact section). |
| D11 | **Both backend intra shapes ship in the same build**; neither is optional. |
| D12 | **1:1 main/aux pairing is a precondition** of the shared schedule. FR-PROC-7's sparse aux cadence breaks it — **#40 may not land before #45** and must re-derive the aux schedule from the aux child's own index when it does. |
| D13 | **Capture budget is ≤ 2 outstanding PER MONITOR — never a global pool of `2m`.** A pool lets one damaged monitor take all of it: 4-deep on 2 slots at m = 2, i.e. bufferbloat, +2 frames of latency, and slot aliasing. The aggregate 2m is a consequence of m independent caps, never a drawable quantity. |
| D14 | **Beyond a monitor's cap: drop-and-coalesce, never queue.** Newest state wins via the dirty region; frames are dropped before they exist. Depth 2 is the pipeline itself (one encoding + one capturing, the recorded ≤ 1 frame of latency), not a queue. Accepted bounded cost: at most ONE stale frame can be encoded (C2 already sent when C3 arrives); C2 cannot be dropped after send — its enc item holds borrowed pointers into the slot (FR-PROC-6), and overwriting under a queued item is the content/region-desync class that froze mstsc. |
| D15 | **BREAK on a capped monitor; skip-and-continue is forbidden.** A skipped monitor that lets the scan complete re-creates the `251bc4d` damage-destruction shape by a second route. Fairness comes from the rotation snapshot plus the bounded ack (same stall as today's global gate), not from skipping. |
| D16 | **The completed-scan clear is replaced by an explicit coverage intersect** (`dirtyRegion ∩= union(monitor rects)`), landed first with its own E7-harness gate (step 6a). |
| D17 | **Per-monitor slot index** replaces global `rect_id` parity. No shmem layout change, no xup contract bump (`shmem_offset` is explicit per frame). |
| D18 | **D5 resolved against step 3's parenthetical (see C2): at a CONVERTED cut the child's SPS/PPS/SEI are DROPPED on the main view; at a real stream-start IDR they still pass through.** A converted cut is not a decoder entry point, so parameter sets there buy nothing and cost 206 B every refresh on VAAPI. Guarded, because a swallowed *changed* SPS is silent whole-picture corruption: the dropped set must be byte-identical to the cached one, and a differing SPS/PPS **fails the pair loudly** instead of being dropped. (A parameter-set change cannot happen without a fresh encoder object today — a geometry change destroys and recreates it, so `started` is 0 and the pass-through path is taken — which is why the guard is cheap and why it must still be there.) |

### Recon gates (required; each reaches its gate BEFORE the property is used)

- **R1 — even-m slot pinning, measured on the fleet. GATE REACHED
  2026-07-29: CONFIRMED, with one correction to the gate's own wording.**
  Run: arm-q (`PR-demo/mac_bisect_matrix/k8s/arm-q.yaml`) — arm-n's
  encoder config byte for byte, on xorgxrdp `957fa79` = `5b9650c` plus
  one recon-only INFO line per AVC444 send; 2 × 1024×768 client from the
  host dummy-X rig, `SESSION_KIND=code`, 60 s, **1100 sends**. Harness
  `PR-demo/mac_bisect_matrix/r1_slot_recon.sh` + `r1_slot_report.py`;
  evidence `PR-demo/mac_bisect_matrix/captures/r1_slot_recon_20260729_191020/`.
  - **Full passes (both monitors sent; `rect_id` gap 2): 1079 of 1079
    consecutive sends reused the same slot — zero changes.** Monitor 1
    used exactly ONE slot for the entire run; its second slot was never
    written.
  - **Correction to the gate wording.** It said "neither monitor ever
    changes slot". Monitor 0 changed slot **18** times — every one of
    them on a *partial* pass (gap 1: it sent while monitor 1 had no
    damage), a case the wording did not anticipate. Those flips do not
    rescue the mechanism: 17 of the 18 second sends happened with the
    monitor's previous frame already acked, so nothing was pipelined.
  - **The property 6c depends on**, measured directly: a monitor holding
    **two outstanding frames in two different slots** occurred **once in
    1100 sends (0.09 %)**. Per-monitor two-slot pipelining is inert at
    m = 2. The overlap the session does get is cross-monitor, and the
    global budget is saturated — max (`rect_id − rect_id_ack`) = 2, with
    543 of 1100 sends issued at depth 2.
  - **Re-run at the E3 TARGET geometry (2560×1440 + 3840×2400), same
    conclusion** — `r1r2_target_geometry.sh 90`, evidence
    `captures/r1r2_target_20260729_193624/`. 528 sends over 87 s:
    full-pass same-slot **479/479**, monitor 1 again pinned to a single
    slot all run, per-monitor two-slot pipelining **2 of 528 (0.38 %)**,
    max depth 2 with 244/528 sends at depth 2. Slot offsets confirm the
    layout arithmetic exactly (monitor 0: 0 and 11 059 200 =
    2560·1440·1.5·2 views; monitor 1 based at 22 118 400).
  - **6c may proceed on this basis.** The recon instrumentation is not
    part of step 6: revert xorgxrdp `957fa79` and retire arm-q when the
    step lands.
  - Two observations from these runs, recorded because they are real and
    neither is a gate result: (a) the target-geometry session delivered
    only **3.03 pairs/s per monitor** (6.06 sends/s) — **attributed
    2026-07-29: client-bound, not a server limit** (see the A/B below);
    (b) FreeRDP logged `YUV decoder: intersecting rectangles, aborting`
    48 times in the target run and 16 times at 2×1024×768, in both cases
    clustered in a few seconds around session start and then absent for
    the rest of the run. Not investigated here; it belongs to whoever
    picks up the region-construction path, and it is not caused by the
    recon build (the instrumentation is a log statement).
- **R2 — the `/dev/shm` floor at 2560×1440 + 3840×2400, measured on the
  fleet. GATE REACHED 2026-07-29: PASS at a flat 1 GiB.** Measured on the
  same target-geometry session as R1 (`captures/r1r2_target_20260729_193624/`),
  by sampling the pod's tmpfs for the whole run and reading back the
  arena xorgxrdp actually reserved at connect:
  - **capture arena reserved: 77 414 400 B (73.8 MiB)** — exactly
    `w·h·1.5 × 2 views × 2 slots` summed over monitors (22 118 400 +
    55 296 000). The model is confirmed, not assumed: the same log line
    read 9 437 184 B at 2×1024×768, also exact.
  - **peak `/dev/shm` used across the session: 77 414 400 B** — the
    capture arena is the only consumer; nothing else in the session
    touched the tmpfs.
  - **tmpfs configured: 1 GiB → 13.9× headroom**, 950 MiB unused at peak,
    no SIGBUS, no budget-exceeded log. **Owner directive 2026-07-29: 1 GiB
    flat, no micro-tuning.** The failure mode is a hard mid-session SIGBUS
    and the value is a tmpfs *ceiling* rather than an allocation, so
    headroom costs nothing and a tight bound buys nothing. `k8s/arm-q.yaml`
    carries it with the arithmetic in the comment; other arms keep 512Mi
    (6.9× at this geometry) until they run E3-sized sessions.

### What step 6 changed that the item did not predict (2026-07-29)

Found by three adversarial reviews of the step-6 diff. Two were fixed
before it was committed; three are consequences that must be MEASURED,
not assumed away.

**Fixed before the commit:**

- **A pass that serves nobody used to stamp the frame clock and arm the
  timer.** With a per-monitor budget the top gate is permissive (it only
  refuses when NO monitor has capacity) while the in-loop gate BREAKs on
  the first capped monitor (D15) — so at m = 2 roughly every other
  wake-up serves nobody. Stamping `lastUpdateTime` there paced the next
  pass off a frame that never happened; arming `updateScheduled` made
  the ack's own `rdpScheduleDeferredUpdate()` a no-op, so a freed
  monitor waited out a whole frame interval; and rescheduling turned it
  into a ~250 Hz loop doing a full region intersect per tick and sending
  nothing — a direct tax on the quantity E5 measures. Now: no send, no
  clock stamp, no reschedule; the ack re-arms (both ack handlers do so
  unconditionally).
- **The budget must NOT be reset when the capture arena is re-laid
  out.** `rdpClientConAllocateSharedMemory` REUSES the mapping whenever
  the byte total is unchanged, and the total is an order-independent sum
  over monitors — so a layout update that only permutes or moves
  monitors keeps the same shm object. Clearing the ring there pointed
  the next capture at slot 0 of memory a still-unacked frame's enc item
  borrows by pointer (FR-PROC-6), i.e. the encoder reading a slot being
  rewritten. Nothing needs resetting instead: `rect_id`/`rect_id_ack`
  are never reset either, and a real re-map is preceded by xrdp deleting
  its encoder and acking everything with `rect_id_ack = INT_MAX`.

**Consequences to measure, recorded because they change what a gate
means:**

- **6c switches ON a per-frame re-pack that R1 measured as INERT.**
  Under the dead parity rule each monitor was pinned to one slot, so
  `cap_slot_missing[mon][other]` was never read and the re-pack union in
  `rdpCapRect` contributed nothing at m = 2. With a per-monitor counter
  the slot alternates on every send, so every capture now additionally
  packs the previous frame's damage for that monitor — and that union is
  also what goes on the wire as the frame's dirty rects. This is
  REQUIRED by FR-CAPTURE-8 clause 9 (a slot that sat idle is stale), it
  is the price of real two-slot pipelining, and it lands in the same
  measurement as the concurrency gain. So: **E5's 51.1 ms baseline never
  paid it, and E6's bandwidth gate is now measuring a different capture
  region.** Both must be re-read with that in mind, and the aux/main
  KB/frame numbers recorded beside the interval rather than compared to
  the pre-#45 bandwidth figures as if nothing changed.
- **6d's per-monitor "no third capture" assertion is UNREACHABLE by
  construction.** The in-loop gate evaluates the same predicate
  immediately upstream and `rect_id_ack` cannot move in between
  (single-threaded, monotone). It is a tripwire for future edits, not
  present coverage, and it is not counted as evidence for anything.
- **6a's coverage intersect also runs on the single-monitor path**,
  which previously had no clear at all: damage beyond the client canvas
  is now dropped instead of retained forever. Benign in both directions
  (that damage is unreachable, and both resize paths re-damage the full
  screen) and it removes a pre-existing permanent-reschedule loop — but
  it IS a behaviour change outside `CC_GFX_AVC444`, so it is stated
  rather than folded into "replacing the clear".

### GATE RESULTS — measured 2026-07-29 on the deployed pair

Pair under test: xrdp-dev `f7acb5979788` (steps 0–7) + xorgxrdp-dev
`d77d05463e52` (step 6), arm-r, `aux_ltr_chain = true`,
`intra_refresh_frames = 240`, VAAPI CQP 444, offscreen 2560×1440 +
3840×2400. Evidence:
`PR-demo/mac_bisect_matrix/captures/e_gate_oracle_20260729_233749/`
(+ `e_gate_render_*`, `e7_drag_20260729_234903/`).

| gate | result |
|---|---|
| **E1** smoke gate | **PASS** at BOTH sizes against the package-installed pair: 8/8 keypresses rendered the right colour, zero lag, edge fidelity 0.994 / 0.992 (floor 0.50), zero encoder errors. Run with the new `SMOKE_TARGET=pod` path — **the T4 is gone** (no `/root/.t4_host`, no key), so this is a fleet-pod pass, NOT a T4 pass. |
| **E2** ≥ 1000 pairs | **PASS**: 1688 pairs per view, **8 scheduled cuts** at ordinals 0/240/…/1680, wire audit `--assert` 7/7 clean, black-frame check 3376/3376 decoded with **zero** black frames, and zero rewrite failures / `unsupported` / pair aborts / budget assertions / fifo-depth errors in either server log. |
| **E3** target geometry | **PASS** — the E2 run IS the E3 run; both monitors audited independently. |
| **E4** one thread, four views | **PASS.** The mechanism was proven here (4 children in ONE poll set, asserted counter + a once-per-run INFO line) but the premise was rare under this payload — 21 of 3346 cycles (0.6 %). Under E5-2's saturating payload it is the common case: **2013 of 3889 cycles (52 %)**. |
| **E5** oracle frame interval | **RESOLVED by E5-2 (#52) on 2026-07-30: 2.13× GREEN.** The 0.97× first measured here (52.5 ms vs a 51.1 ms baseline) was metronome-against-metronome — both sides read the payload's own 10 Hz clock, not the server (attribution below). Re-run under a saturating two-monitor payload against a re-measured baseline: **63.6 ms → 29.9 ms per send, 2.13×**, `kids_armed=4` in 52 % of cycles. Evidence: `captures/e52_flood2_arm-s_20260730/`. |
| **E6** no regression | `make check` green: xrdp 152/152, libcommon 157, libipm 35, libxrdp 13, memtest 1, zero failures. Refresh cost measured on the gate corpus: a paired cut adds 210 308 B over a 47 614 B pair, i.e. **+1.84 % at N = 240** (PRD predicted ≈ +4 %). The arm-n/arm-m `bandwidth_bench.sh` A/B was NOT re-run: arm-n is `SESSION_KIND=xfce`, a different payload, so it is not comparable to this corpus. |
| **E7** dual-monitor drag | **PASS** on the sweep window: 551 and 812 sends over 60 s of sweeping across the boundary, worst per-monitor gap 702 ms / 406 ms (threshold 2000 ms). |

**Why E5 is red — attribution REASSESSED 2026-07-30** on repaired
timestamps (see the instrument note; analysis script committed beside
the capture as `reanalyze_repaired.py`). The 2026-07-29 attribution
("the limit is capture/ack-side") over-reached; the first version is in
git history. Corrected:

- **The gate's payload is a 10 Hz metronome, and it clocks the whole
  run.** `SESSION_KIND=code` is a `sleep 0.1` scroll loop
  (`banner.sh`): per-monitor send period p50 102 ms (mean 104.7), the
  two monitors 26 ms apart in phase, and ALL 121 steady-state gaps
  > 150 ms are exactly ONE skipped 102 ms beat (~204 ms = 2× the
  period). The only larger gaps are two session-startup transients
  (4.3 s / 9.0 s). The pipeline is never full.
- **The server is nearly idle.** Service per pair **11.8 ms** (encode
  collect 4.2 + rewrite/emit 7.6 — the previously recorded "~26 ms
  encode-and-emit" was a log-clock artifact), oracle ack 2.1 ms, then
  ~93–95 ms waiting for the same monitor's next handoff. Worker busy
  22 % of wall clock. Nothing waits on encode, rewrite or ack.
- **The mean is not tail-driven.** 52.5 ms is the harmonic of two
  ~105 ms payload periods: the 70–150 ms wait-gaps contribute 35.2 ms
  of it, the entire > 150 ms tail only 7.9 ms; a tail-free run would
  still be 46.3 ms (1.10×). The p50 of ~26 ms measures burst spacing
  between the two monitors' frames, not throughput.
- **So 0.97× compares metronome to metronome.** The 51.1 ms baseline
  ran the SAME 10 Hz payload (its capture was not kept — E5-2
  re-measures both sides), so E5 as designed could not show any
  encoder-side gain — and equally cannot convict the capture path.
  The rare batching premise (kids_armed=4 in 21 of 3346 cycles) is a
  property of the payload cadence, not of the capture code: with items
  landing 26 ms apart and 11.8 ms service, two items almost never
  coexist. Capture pacing is UNPROVEN either way until the producer
  outruns the pipeline — that is **#52 (E5-2)**.
- **Corroboration kept from 2026-07-29:** with the rendering client
  (slower consumer) the batch fired in 11 % of cycles instead of 0.6 %,
  and the end-to-end rate was **2.96 pairs/s per monitor** against 2.97
  before #45 — unchanged, as predicted for a client-bound session.

**Instrument note — every ms-level number read from xrdp logs is
suspect (upstream bug, found 2026-07-30).** `common/log.c:1159`
computes `millisec = (tv.tv_usec + 500 / 1000)` — integer `500/1000`
is 0 — and snprintf-truncates the µs count into `char[4]`: whenever the
true sub-second part is < 100 ms (10.4 % of lines in this capture) the
printed fraction is the LEADING DIGITS of the µs value, up to ~0.9 s
late within its own second (proof: acks stamped 896 ms before the send
they acknowledge). File order is causal, so a right-running-minimum
repair is exact on the ~90 % of correct lines. Means (window/count)
are robust — the 0.97× verdict stands — but raw percentiles and any
two-line timing delta are untrustworthy until the one-line fix lands
(**#52 step 0**).

**What this means for the item — CLOSED 2026-07-30.** Steps 0–7 are
implemented, tested and deployed; E1/E2/E3/E6/E7 passed on the deployed
pair; E4 and E5 are answered by **#52 (E5-2)**, which replaced the 10 Hz
metronome with a saturating two-monitor payload and re-measured BOTH
sides: **2.13×** (63.6 ms → 29.9 ms mean per send), with the batch
arming four children in 52 % of cycles instead of 0.6 %. The two
capture-side questions from 2026-07-29 (why a monitor's period is what
it is; whether the producer can hand both monitors over together) are
now answerable and carry evidence — see #52's results: the worker is
still only 32 % busy and the wait is on the next capture handoff, so the
remaining headroom is capture-side, and that is the next item, not a
blocker on #45.

**One residual coupling recorded from step 7's review, not fixed:** with
the shared deadline across a set (D3), a child that withholds its picture
delays the HEALTHY monitor's frame — and so its ack and capture-slot
release — by up to `pair_timeout_ms`. The healthy monitor does not LOSE
the frame (each handle is collected on its own merits and only failing
handles are torn down), so this is added latency, not frame loss. D3
specified one deadline for the two views of ONE frame; extending it
across monitors couples independent frames, and E7 is the gate that
would catch it.

**One startup transient recorded, not investigated:** at session start the
3840×2400 monitor sends ONE frame and then nothing for ~4.7 s while the
2560×1440 monitor streams — its encoder pair is spawned later. E7 scores
the sweep window only and reports this separately; a first version of the
gate scored it as a stall, which is how it was found.

### Corrections to this item, found by reading the source (2026-07-29)

Recorded rather than silently fixed, because each one changes what a
step has to do:

- **C1 — step 1 is bigger than the `:2128` reject.** Fixing only
  `slice_ltr_rewrite()`'s reject does NOT make the rewriter accept a
  non-IDR I "in both views": three walker-level gates key on
  `ntype == 5` rather than on the picture being intra —
  `xrdp_h264_annexb.c:2466` rejects any non-IDR aux picture while
  `!aux_seeded` (so an nvenc aux cut is still refused), `:2481` derives
  `to_seed_i` from `ntype == 5`, and `:2532` sets `aux_seeded` only on
  an IDR. The emitter, by contrast, needs **no change at all**: the
  existing non-IDR arm already emits the exact required bytes for both
  new shapes (no rplm, constant `mmco6 ltfi=view`, `out[0] = 0x61`,
  `cabac_init_idc` neither read nor written).
- **C2 — step 3 as written is self-contradictory, measurably.** D5 says
  "no SPS/PPS at a cut, unconditionally"; step 3's parenthetical says
  child-emitted SPS/PPS still pass through on the main view. On VAAPI
  the cut IS a child IDR carrying parameter sets, so both cannot hold:
  the shipped arm-o capture spends **206 B per cut** (SPS 29 + PPS 4 +
  SEI 173) on exactly those. **Resolved 2026-07-29 (D18):** D5 wins for
  a CONVERTED cut, with a guard — see D18.
- **C3 — stale line references.** The aux respawn is
  `xrdp_encoder_ffmpeg.c:1244–1257` (with `ltr_aux_fresh` at `:149`,
  `:1267`, `:1712`), not `:1184–1197`. `feed_vmsplice()`'s single call
  site is `:915`, not `:730/:794/:856`. `F_SETPIPE_SZ` is `:587`, not
  `:528`. The counter reset is `:2452`, not `:2450`.
  `submit_single()`/`pump_set()`/`pump_arm()` **do not exist** — step 5
  creates them; the existing submit half is inline in `encode_single`.
- **C4 — the wire audit was not toothless, it was incomplete.** Its
  default mode already exits non-zero on cross-view refs, missing list
  modification, non-self-marking pictures and frame_num gaps. What it
  lacked was an `--assert` mode and, specifically, any check of the
  properties #45 adds: it *skipped* the gap check at a mid-stream IDR
  (`if b_['idr']: continue`), i.e. it was blind to the exact defect step
  2 removes. Both fixed in step 0.
- **C5 — the `fifo_to_proc_depth > 2` scaling belongs to step 6, not
  step 7.** Step 6b alone raises the legal in-flight count from 2 to
  2m, so at m = 2 the assertion at `xrdp_mm.c:4819` becomes a permanent
  false ERROR the moment step 6 deploys, batching or no batching — and
  E2's "zero errors in the server log" would be unreachable, with
  relaxing the criterion as the tempting next move. Landed with step 6.
- **C6 — after step 2 the re-key is the ONLY frame_num-wrap
  protection.** Today a main GOP IDR resets the shared counter, which
  masks the wrap by accident; `xrdp_ffmpeg_avc444_ltr_counter_cap()` and
  `warn_if_rekey_unreachable()` (plus their test at
  `tests/xrdp/test_avc444_ffmpeg.c`) model exactly that reset and become
  FALSE at the D7 target `-g 240`. They are corrected in the same commit
  as step 1/4, not left to warn about a mechanism that no longer exists.
- **C7 — one existing test asserts the abolished behaviour.**
  `test_ltr_emitter_epoch_restart_byte_exact` asserts
  `st.aux_seeded == 0` and `st.frame_num == 1` after a mid-stream main
  IDR. That is the DPB flush step 2 removes, so the test is re-scoped to
  the `!started` epoch case (re-key restart, where the encoder object is
  destroyed and the state is memset) rather than deleted — and the
  re-scope is recorded here so it cannot be mistaken for weakening a
  gate to keep it green.

### Out of scope

- Threads of any kind (D1).
- The frame_num-wrap re-key mechanism (shipped 2026-07-29; PRD FR-H264-8).
- FR-PROC-7's three policies (#40), subject to D12.
- Instrumenting the ~1 s re-key pause (owner: not a priority).

### Acceptance

E1–E7, each with its evidence recorded in the commit message or `PRD.md` —
not asserted. Plus:

- **The schedule is OBSERVED, not requested**: at every scheduled index
  both views parse `slice_type == I`; a silent skip fails the test.
- No IDR mid-stream; frame_num strictly +1 per picture across a refresh;
  every P's list-modification resolves to its own view's LT slot; both
  chains' transitive depth ≤ `intra_refresh_frames`.
- **Fault-injection recovery (dev-box client harness):** an oracle-client
  run drops/corrupts exactly one P and measures pixel re-convergence —
  heals within D8's bound on the scheduled-cut stream, does NOT converge on
  a `-g 30000` control. The only test that makes I3 observable; the wire
  ratchets remain the proof of the invariant itself.

---

## #52 — E5-2: saturated-payload frame interval (DONE 2026-07-30 — 2.13× GREEN)

**Why.** E5's payload (`SESSION_KIND=code`) is a `sleep 0.1` scroll
loop: 10 Hz damage per monitor, server 78 % idle, pipeline never full —
both the 51.1 ms baseline and the 52.5 ms measurement were readings of
the payload's own clock (#45 GATE RESULTS, reassessed 2026-07-30). A
throughput gate needs the producer strictly FASTER than the pipeline,
so damage coalesces (forced frame loss) and the send interval measures
the server. Scope: fleet/bench only — no change to shipped defaults,
no change to the cadence-deterministic bandwidth payloads.

**Step 0 — fix the instrument first.** `common/log.c:1159`:
`millisec = (tv.tv_usec + 500 / 1000);` → `(tv.tv_usec + 500) / 1000`
(round µs → ms; upstream bug, introduced by `db962399` "log: quit
using lrint and -lm"). Without it, ~10 % of trace stamps are up to
~0.9 s late and every E5-2 percentile would need offline repair.
Add a unit test under `tests/common` — factor the µs→ms conversion so
it is testable pure (assert 999 499 µs → 999, 45 123 µs → 45, 999 µs
→ 1, 0 → 0; the buggy code returns the leading digits instead), and
assert the formatted field is exactly three digits. Candidate for a
separate upstream PR — it fixes every xrdp log timestamp, not just
ours.

**Step 1 — the payload: minimal change.** New `SESSION_KIND=codeflood`
in `PR-demo/mac_bisect_matrix/banner.sh`, inside the existing
`code|codeline|codefast` case: `STEP=25`, `DELAY=0` (others keep
`DELAY=0.1`), i.e. the same deterministic 3000-line corpus scroll with
the metronome removed — self-clocked by consumption, still failing
LOUD if the corpus mount is missing. No WM runs in these sessions, so
the single `xterm -maximized` spans the full virtual screen and BOTH
monitors receive damage continuously (the E5 trace proved one repaint
damages both surfaces, 26 ms apart). `code`/`scroll`/`gray`/`chroma`
are untouched: they are the FR-H264-8 BANDWIDTH baselines, where
cadence determinism is the point. Optional second bound, same
one-liner pattern: `grayflood` (full-screen bands, no sleep) as the
max-encode-cost/full-frame-damage worst case.

**Step 2 — arms (fleet discipline; never mutate a deployed arm).**
`banner.sh` is baked into the image, so rebuild via
`build_and_deploy.sh` and stand up TWO arms simultaneously:
- **arm-s** — the #45 pair (xrdp `f7acb5979788` + xorgxrdp
  `d77d05463e52`), the current arm-r software;
- **arm-t** — the baseline: xrdp at `a0d9e773` (steps 0–4, i.e.
  pre-pump_set/pre-batching) + the SAME xorgxrdp `d77d05463e52`, so
  the producer side is identical and the A/B isolates exactly steps
  5 + 7. (The original 51.1 ms baseline build's capture was never
  committed; E5-2 does not reuse its number for anything.)
Both arms `SESSION_KIND=codeflood` via pod env. arm-r stays up as the
10 Hz reference; arm-q retires (its gate is answered).

**Step 3 — measure.** `e_gate_run.sh` oracle mode, 180 s, E3 geometry
(2560×1440 + 3840×2400), against EACH arm back to back. Harness
change: the hardcoded 51.1 ms baseline becomes `E5_BASE_MS`
(env-overridable); the E5-2 ratio is **flood-vs-flood** (arm-t mean ÷
arm-s mean). Record per arm: mean/percentiles of the send interval
(now trustworthy per step 0), `kids_armed` histogram, worker busy %
(batch→last=1 vs wall clock), per-monitor period, and the rendering-
client end-to-end rate beside it as usual.

**Gate.** ≥ 2.0× GREEN, ≥ 1.5× AMBER, else RED — and the stop rule
carries over verbatim: under 1.5× the remainder is attributed
(capture pacing/budget, vmsplice feed, NUT demux, LTR rewrite, EGFX
assembly) before anything ships; no re-tuning to make the number look
better.

**Predictions, recorded before running (each is checkable in the
capture):**
1. under flood, `set_n=2 / kids_armed=4` becomes the COMMON case
   (it was 0.6 % at 10 Hz) — step 7's premise finally exercised;
2. worker busy % rises sharply; if arm-s approaches saturation the
   2.0× target is genuinely in reach, since the serialized baseline
   pays `2m(w+e)` where the set pays ~`w+e`;
3. if instead the interval pins at a producer period ≫ service time
   with the worker still mostly idle, the constraint IS capture-side —
   then #45's two capture questions (deferred-update pacing / ack
   budget retirement; handing both monitors over together) become the
   next item, now with real evidence instead of a payload echo.

**Acceptance:** log.c fix merged with its unit test (astyle 3.4.14 +
CI green); `codeflood` in-tree; both arms deployed from clean debs and
recorded in the deployed-state table; E5-2 ratio + attribution
committed beside the captures and summarized in PRD FR-H264-6/#45
(the old E5 numbers stay, relabeled as the 10 Hz-cadence measurement);
smoke gate run against any arm handed to a human.


### #52 RESULTS (2026-07-30) — **E5-2: 2.13×, GREEN**

Two arms, one payload, 180 s each, E3 geometry, oracle client. Evidence:
`PR-demo/mac_bisect_matrix/captures/e52_flood2_arm-s_20260730/` (README
carries the tables; `../e52_flood2_arm-t_20260730/` is the baseline arm).

| | arm-t baseline (steps 0–4) | arm-s (steps 0–7) |
|---|---|---|
| xrdp-dev | `+git20260730013437.5dae11f63adb` | `+git20260730013346.52b8798839ad` |
| xorgxrdp-dev | `d77d05463e52` | **the same** |
| mean per send | 63.6 ms | **29.9 ms** |
| p50 / p90 / p99 | 61 / 80 / 86 ms | 31 / 53 / 64 ms |
| sends/s | 15.72 | **33.40** |
| per-monitor period | 127.4 ms | **59.9 ms** |
| pictures pushed | 4 938 MiB (234 Mbit/s) | **10 434 MiB (495 Mbit/s)** |
| worker busy | 16 % | 32 % |
| `kids_armed=4` | n/a | **52 % of 3 889 cycles** |

**Ratio 2.13× — GREEN.** Predictions 1 and 2 from the spec hold:
`kids_armed=4` went from 0.6 % of cycles at 10 Hz to 52 %, and the
parallel set is worth ~2× once both monitors have work. E2 also holds
under the flood (7/7 assertions, zero black frames in 991 pictures at
~0.93 MB per picture, zero rewrite failures / budget assertions).

**Steps as landed.** Step 0: `common/log.c` rounds µs→ms via a factored
`log_usec_to_msec()`, with `tests/common/test_log.c` (8 cases) — full
`make check` 366/366. Verified in situ: arm-t has ZERO out-of-order log
stamps and a flat histogram of fractional parts; arm-s has 1.1 % of
lines out of order by ≤ 8 ms, which is step 5's worker thread
interleaving with the main thread, not clock corruption. Step 1:
`codeflood` + `grayflood` in `banner.sh`; cadence kinds untouched. Step
2: arms `arm-s` (:40018) and `arm-t` (:40019), both from clean debs,
both `SESSION_KIND=codeflood`, same xorgxrdp so the A/B isolates steps
5+7. Step 3: `E5_BASE_MS` replaces the hardcoded 51.1, and the harness
now records the `kids_armed` histogram, worker busy %, per-monitor
period and `SESSION_KIND` per run.

**The first flood pair was RED at 0.91×, and it is kept.**
`captures/e52_flood_arm-{s,t}_20260730/`: removing the metronome was not
enough. A corpus line is ~27 visible columns and the xterm is 6400 px
wide, so the ink sat on the primary monitor only — the oracle dumps
measured **167 MB of pictures on the 2560×1440 monitor against 0.75 MB
on the 3840×2400 one**, which still took full-monitor damage every cycle
because the window spans both. A batch has nothing to overlap when one
of its monitors is blank, while the shared deadline still couples the
active monitor to the idle monitor's full-area capture and upload:
68.5 ms against the baseline's 62.6 ms. `codeflood` now repeats each
corpus line 32× so every row wraps past the right edge of monitor 2.

**Two findings this leaves open (new items, not blockers):**
1. **Idle-monitor coupling is a real ~9 % regression.** One active
   monitor beside an idle one is an ordinary desktop, and there the
   batch loses. The fix is to arm a monitor only when it has changed
   pixels, rather than because a window overlaps it — cheap to test
   with the first flood pair as the ready-made benchmark.
2. **The remaining headroom is capture-side, now with evidence.** At
   2.13× the worker is still only 32 % busy: per-pair service is
   14.7 ms (encode collected 2.8 + rewrite/emit 12.3) against a 59.9 ms
   per-monitor period, and after a frame's `last=1` the same monitor's
   next damage arrives 41 ms (p50) to 105 ms later. Flow control never
   binds (un-acked p50 0 / max 4 of fif=2, client `queue_depth` 0) and
   it is not bandwidth (495 Mbit/s over loopback). #45's two deferred
   capture questions — deferred-update pacing / ack-budget retirement,
   and handing both monitors over together — are the next lever.

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

## #55 — E5-2 on the T4 (DONE 2026-07-30 — **1.5×–2.3×, AMBER**, attributed)

> ### CORRECTION, same day: the T4 number is a band, not 1.67×
> Repeating both arms found the box bimodal. The batched arm splits into
> **47–49 ms** (8 runs) and **67–74 ms** (8 runs); the baseline, re-measured
> later, gave **108.9 / 109.9 ms** rather than 77.3 ms. Three pairings:
> 77.3/46.3 = **1.67×**, 109.4/72.5 = **1.51×**, 108.9/48.2 = **2.26×**.
> The drift is common-mode and every pairing clears 1.5×, so the conclusion
> holds and the single number does not. **Quote 1.5×–2.3×, centred ~1.7×.**
> Not root-caused (**#60**); the 180 s pair below is still the best single
> sample because both arms pushed the same bytes per picture (602.7 vs
> 594.0 KB) and its byte-rate ratio (1.64×) matches its frame-rate ratio.
> Where the saturated core goes: **#59** and
> `PR-demo/mac_bisect_matrix/captures/e52_t4_batched_20260730/CPU_BOTTLENECK.md`.

### RESULTS (2026-07-30, `ubuntu@100.24.126.48`, Tesla T4 / 4 vCPU Xeon 8259CL)

Both arms measured on the T4 itself under `codeflood`, 180 s each,
2560×1440 + 3840×2400, oracle client, identical xorgxrdp and identical
`gfx.toml` — so the A/B isolates xrdp steps 5+7 exactly as on the dev box.

| | baseline (0–4) `5dae11f63adb` | batched (0–7) `52b8798839ad` |
|---|---|---|
| mean per send | 77.3 ms | **46.3 ms** |
| sends/s | 12.94 | 21.61 |
| per-monitor period | 154.6 / 155.1 ms | 91.6 / 93.2 ms |
| pair service dmg→last=1 | 13.9 ms | 23.5 ms |
| worker busy | 18 % | 45 % |
| `kids_armed=4` | n/a (pre-step-5) | **93 %** of 1 965 cycles |
| pictures pushed | 1 330 MiB / 63.6 Mbit/s | 2 183 MiB / 104.5 Mbit/s |
| **E5-2 ratio** | | **1.67× — AMBER** |

Evidence: `PR-demo/mac_bisect_matrix/captures/e52_t4_{baseline,batched}_20260730/`
(READMEs, `VERDICT.txt`, gzipped `gfx_trace.txt`, decomposition, CPU
samples). A 100 s repeat reproduced the batched arm at 47.6 ms.

**Attribution — the remainder is capture-side and it is one saturated
thread.** The session Xorg runs at **92 % of a core**, measured by reading
`/proc/<xorg>/stat` twice 60 s apart during a clean run (a `top` sampler
perturbs a 4-vCPU box enough to move the rate from 47.6 ms to 71.3 ms, so
that run is kept only as CPU evidence, never as a rate). Against that: the
four NVENC children cost ~7 % of a core **each** (0.28 core total), the
worker is idle 55 % of the time, service is 23.5 ms inside a 91.6 ms
period, flow control never binds (un-acked p90 = 1 of a cap of 2,
`queue_depth` 0 throughout), and the box overall sits at ~2.5 of 4 cores.
The T4 is not out of CPU; it is out of *one* CPU. That is **#54**, and it
is why the same code gives 2.13× on the 32-core dev box and 1.67× here.

Pack bench on this CPU (CLAUDE.md obligation), 3840×2400:
**old planar 7.14 · scalar packed 53.09 · shipped vectorized 9.03 ms/frame**
— i.e. ~10 % of a 91.6 ms period spent inside the same saturated thread.

Also clean on the T4: all six E2 counters zero on both arms; wire audit
`--assert --intra-refresh 240` **7/7 PASS** (3 824 pictures batched,
2 274 baseline, 0 frame_num gaps); smoke gate as the last step **PASS** at
1920×1080 and 1024×768, 8/8 keys, edge 1.000, 0 encoder errors.

The black-frame check FAILs on both arms and is the **login paint-in**, not
a dropout: main-view luma is 0 through picture 28, 0.77 at 30, 118.9 by 40,
and there are zero black pictures afterwards in 3 824. The T4 runs XFCE,
which takes seconds to paint; the fleet pods have no desktop and paint
immediately, which is why this never appeared before. `startup_frame40_
painted.jpg` in the batched capture is the painted frame. The FAIL is left
standing in `VERDICT.txt`; see #58 for fixing the checker rather than the
evidence.

**Onscreen (§6 of the protocol) is still open** — the box is left on the
batched build with the payload disarmed, smoke-gated, ready to connect.

### Four things this run found that the protocol did not predict

1. **A freshly booted T4 fails AVC444 negotiation and silently falls back
   to RFX.** First connection after boot: `xrdp_ffmpeg: probe TIMEOUT ...
   elapsed=4008 ms, packets=0` → "ffmpeg verification FAILED (TIMEOUT);
   removing external AVC candidate" → "Matched RFX mode". Cold
   `h264_nvenc` at 3840×2400 measured 3.95 s to first output vs 1.40 s
   warm — the 4 s probe deadline is right on top of the cold path, and
   with NVIDIA persistence mode off the driver unloads whenever no CUDA
   process is running, so *every* first connection is cold. Worked around
   for this campaign with `nvidia-smi -pm 1` (recorded in the capture
   READMEs); the real fix is **#56**.
2. **`apt-get install` of an xrdp-dev deb stops at a conffile prompt**
   (`/etc/xrdp/cert.pem`, `rsakeys.ini` — modified on the box) and leaves
   xrdp-dev half-configured with `dpkg: error processing package`. Every
   T4 deb install needs `-o Dpkg::Options::=--force-confold` alongside
   `--allow-downgrades`. Folded into the protocol.
3. **The T4 runs a window manager and the fleet does not**, so the payload
   inked one monitor. `xterm -maximized` spans the root on a bare X server
   but means *the current monitor* to xfwm4 — the first probe measured 251
   damage events on surface 1 against 44 on surface 0, i.e. #53's regime
   wearing the E5-2 label. Fixed in `e52_payload.sh` by sizing the window
   to the root geometry from `xwininfo -root` (`xdotool getdisplaygeometry`
   returns the *primary monitor*, which is the same bug again) after
   removing the maximized state, and re-asserting it for the life of the
   session — a one-shot resize held on one run and lost on the next, and a
   6400×2400 window at **X=2570** covers exactly one monitor while looking
   correct in every size check.
4. **The harness could not tell.** Both of those produced ordinary-looking
   runs. `e_gate_run.sh` now prints per-surface damage coverage on every
   run and says loudly when one monitor carried it (**gate G5**):
   `COVERAGE WARNING: one monitor carried the run (1376 vs 28) ... an E5
   ratio from it is not an E5-2 result`. Both committed T4 runs pass it
   (1.01× and 1.06× imbalance).

---

## #55 (original scope) — E5-2 on the T4

The dev-box 2.13× is a VAAPI number on a 32-core box. The T4 (Cascade Lake
+ Tesla T4/NVENC) is the representative low-to-average old-CPU target, so
its ratio is the one that belongs in the PR.

**Protocol: `PR-demo/t4_profile/E5-2_T4_PROTOCOL.md`** — written before the
launch, ~40 min of instance time, every step scripted. It covers the
single-instance A/B (both debs named, and the version-sort trap: the
baseline deb sorts NEWER than the batched one, so `--allow-downgrades` and
a hash check before every measurement), the four gates a number must pass
to count (xorgxrdp still installed, deployed hash is the intended arm,
payload declared, session freshly logged off), the artifact inventory with
sizes (the oracle dumps are 5–11 GB per run and are audited on a prefix
then deleted; everything else is committed), the pack-bench and smoke-gate
obligations, and cleanup on both boxes.

Machinery that landed with it: `e_gate_run.sh` grew `E_TARGET=ssh` so the
same harness and the same analysis run against a real box over an ssh
port-forward with the client side still on the dev box;
`PR-demo/t4_profile/e52_payload.sh` + `e52-payload.desktop` +
`e52_t4_payload.sh` are the persistent, checksum-gated, autostart-armed
T4 payload (arming is a marker file plus a session logoff — never an ssh
launch into a live session); `PR-demo/mac_bisect_matrix/sessions_off.sh`
logs every fleet session off afterwards.

**Acceptance.** Both arms measured on the T4 under `codeflood`, ratio and
decomposition committed beside the captures, pack-bench ms/frame recorded
next to the deployed hashes, smoke gate PASS before the owner connects, and
the §6 onscreen checklist walked on both the Windows App (UWP) and macOS —
that list is also what the owner watches, with the mid-stream non-IDR I
refresh cadence (item 1) and the idle-monitor coupling (item 2) as the two
genuinely new risks since the 2026-07-28 T4 test.

---

## #59 — The capture is 14 % of the bottleneck thread; the rest is not ours to optimise (TODO — one lever left, see #62)

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

## #61 — GLAMOR is not available to us on NVIDIA; the "different benchmark" needs another route (CLOSED-WONTFIX for GLAMOR, the payload half is TODO)

**The ask.** #59 showed the E5-2 benchmark is not touching a ceiling we
own: two thirds of the saturated Xorg thread is the xterm payload's own
software rendering (44.9 %) plus X's Present emulation (18.8 %), against
13.8 % for the whole capture. The proposed fix was **GLAMOR** (move X's
drawing onto the GPU) **+ alacritty** (move glyph rasterisation out of the
X process entirely).

**GLAMOR half: RED, and it is an upstream limitation, not a
misconfiguration.** Enabling it on the T4 (`nvidia` added to
`Option "DRMAllowList"` in `/etc/X11/xrdp/xorg.conf`, `ubuntu` added to
`render`/`video`) *looked* like it worked — the session Xorg logged
`glamor X acceleration enabled on Tesla T4/PCIe/SSE2` and logged in — but
it renders **black**:

```
smoke[1920x1080]: EDGE FAIL (0.000 < 0.50)  ok=0 lag=8   (every key got=black)
Xorg: (EE) XRDPDEV(0): Failed to make 1024x768x32bpp pixmap from GBM bo
```

This is **neutrinolabs/xrdp#1697**, open since 2020-10-06: *"Latest xrdp
can use glamor to accelerate X drawing and make use of hardware 3D
rendering but it only works well with Intel or AMD hardware."* The last
comment on it (2024-11-18, unanswered) reports our exact symptom —
xorgxrdp `--enable-glamor` on NVIDIA, RDP login, black screen.

Both escape routes in that thread are closed for us:

* the only working NVIDIA path is jsorg71's **`nvidia_hack` branch** plus
  `xorg_nvidia.conf` with a hardcoded PCI BusID — a different driver stack
  (the real NVIDIA X driver), not a toggle on the `xorg.conf` we ship;
* and on that branch **dynamic resolution and the virtual monitor do not
  work** ("nvidia proprietary driver does not support virtual monitor"),
  because it drives a real GPU head. E5-2 is a two-monitor
  2560×1440 + 3840×2400 benchmark driven by the RDP monitor layout, so
  `nvidia_hack` cannot host it even if we adopted it.

Related detail worth keeping: with GLAMOR on, alacritty's own stderr
showed `glx: failed to create dri3 screen` / `failed to load driver:
nvidia-drm` and Mesa falling back to **zink** — xorgxrdp calls
`glamor_init(..., GLAMOR_USE_EGL_SCREEN | GLAMOR_NO_DRI3)`, so clients
cannot get DRI3 through this screen regardless.

**Everything measured while GLAMOR was on is VOID.** xterm 46.5 ms;
gpuflood 35.0 / 35.1 / 35.2 / 38.5 ms; gpuflood baseline 60.3 / 61.2 ms;
the re-profile in which `avc444_decode_row.avx2` became the #1 symbol at
14.17 % and libpixman vanished. All of it was a black screen: the capture's
own check reports **1160 of 1160 main-view pictures black**. Archived, with
every rate-bearing file renamed, under
`PR-demo/mac_bisect_matrix/captures/e52_t4_glamor_VOID_20260731/`.

**Process failure, recorded so it is not repeated.** The config under test
was changed and then a full measurement campaign — six rate runs, a perf
profile and a uprobe run — was executed *before* the smoke gate was run
against it. CLAUDE.md already requires the smoke gate against the exact
deployed binary **and config**; the missing rule is the ordering: **prove
basic functionality first, measure second.** A config change that alters
the render path is a deployment, and an unproven deployment produces
numbers that cost more to unwind than the gate costs to run. Added to
`PR-demo/t4_profile/E5-2_T4_PROTOCOL.md` as a hard precondition.

**What is still open (the payload half).** Alacritty does not need GLAMOR
to be useful here: it rasterises glyphs in **its own process** (Mesa
llvmpipe/zink on this box) and hands X finished buffers, which should
remove the 19.3 % glyph-composite and 9.1 % fill paths from the X thread
and replace them with one large blit — and the box has ~1.5 idle cores for
it to run on. That is a real, testable change to what the benchmark
measures, on the render path we already smoke-gate. Measure it as: arm
`gpuflood`, confirm the smoke gate passes first, then a paired ≥180 s
baseline/batched pair, and re-profile to show the pixman paths shrank.
Cross-check the black-frame count on every run — it is what caught this.

**Resolved by #62.** The payload half was built: `PR-demo/textflood/`
renders the same corpus with cairo in its own process and blits it with
`XShmPutImage`, cutting the payload's X-thread cost 7.7x (99.0 % -> 12.8 %
of a core on Xvfb, against a 13.7 % idle floor) with the residual being the
SHM memcpy itself. The `Option "DRI3" "0"` / `-extension Present` idea that
was listed here as a second lever is **withdrawn** — see the box under #59:
it retunes the X server rather than our code, and textflood issues no
Present requests, so the path stops being fed instead.

---

## #63 — 4:2:0 while the screen is in motion, 4:4:4 when it settles (**BLOCKED on #65** — was TODO, NEXT; its gate numbers would be producer-confounded until FR-BENCH-1 passes, and FR-PROC-7's preemption signal needs a saturating producer to be testable at all)

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

---

## #64 — WITHDRAWN 2026-07-31: the "capture/encode never overlap" finding was a measurement error, twice over

**This item was filed on evidence that does not support it, and both
pieces of that evidence have now been shown to be artefacts. Overlap is a
PRD requirement, the PRD already states where it does and does not hold,
and the corrected measurement agrees with the PRD.** What survives is a
harness defect list and one genuinely open question (below), not a bug in
the pipeline. Left in full rather than deleted, because how it went wrong
is the reusable part.

### Error 1 — `inflight` does not measure capture/encode overlap

`inflight` in `GFX_TRACE enc` is
`pairs_submitted - pairs_returned` **inside one ffmpeg child**
(`xrdp_ffmpeg_avc444_inflight`, `xrdp_encoder_ffmpeg.c:2038`), logged on
the same call that submitted the frame. The shipped args are
`-tune zerolatency` (libx264) / `-async_depth 1` (VAAPI), which by design
return the coded picture on the submitting call — the accessor's own
comment says so: *"zero with the shipped low-latency args"*. Confirmed on
the archived trace: **`rv=READY` on 2018/2018 samples**, i.e. every
submission returned synchronously.

So `inflight == 0` is the DESIGNED value. It cannot be anything else, at
any monitor count, at any ack-window size, on any box. It is a statement
about one encoder child's internal queue depth, **not** about whether
xorgxrdp's capture runs concurrently with xrdp's encode. Hypothesis 1
below was therefore probed against a metric that was incapable of moving,
which is why it "did not respond" to `fif=4`.

*Quality-gate check 2 exists for exactly this and still did not catch it:
the check asks "did the intervention change the mechanism it targets", and
the answer was correctly "no" — but the conclusion drawn was "a serialiser
is blocking it" when the available conclusion was "this metric cannot
show it". Amend the check: when a mechanism's telemetry does not move,
rule out "the telemetry cannot move" BEFORE reaching for a blocker.*

### Error 2 — the timestamp check paired events by cycle window

The fallback evidence was a timestamp gap: `last=1` of cycle N to the
batch arm of cycle N+1, positive on 1010/1010 cycles. That pairing is
wrong. The `last=1` falling inside cycle N is the close of the frame
captured in cycle **N-1**, because the next cycle arms *before* the
previous frame's final send goes out. Visible directly in the raw trace:

```
batch cycle=2                                    .161
send last=1 frame_id=1 id_server=0               .168   <- closes frame 0
send last=0            id_server=1               .171   <- frame 1 starts
enc submitted_seq=1                              .179
batch cycle=3                                    .192   <- ARMS 3 ms BEFORE
send last=1 frame_id=2 id_server=1               .195   <- frame 1 closes
```

The arithmetic said so before the trace did: on the single-monitor run the
window pairing produced **`encode + assembly = -3.3 ms`**, a negative
segment, because service (8.2 ms) came out *less than* capture (11.5 ms).
A negative duration is the pairing confessing, and it was the thing that
stopped the run from being reported.

`e52_period_decompose.py` now pairs by frame identity (`id_server`) and
measures `capture arm of frame N+1 - last=1 of frame N`. Re-run on the
same archived traces:

| run | overlapping cycles | median gap |
|---|---|---|
| 2 monitors, 4K+1440p, textflood | **1010 / 2015 (50.1 %)** | −11 ms |
| 1 monitor (geometry void, see below) | **4634 / 6684 (69.3 %)** | −2 ms |

**Capture and encode DO overlap.** The negative gap is the overlap.

### What the PRD already said — and it matches

PRD §"Concurrency state of the encode pipeline" specifies:

> `capture ‖ encode` — **YES for m = 1, shipped** (FR-CAPTURE-8);
> **partial and accidental for m ≥ 2** … the overlap that occurs is
> *cross-monitor* interleaving, while the two-slot mechanism itself is
> inert — `rect_id` advances by m between a monitor's consecutive sends

The measured 50.1 % at m=2 is precisely "cross-monitor interleaving":
consecutive `id_server` values alternate between the two monitors, so
every second pair overlaps by construction. The spec predicted the number
before it was measured. **Nothing here is a regression against the PRD.**

### Methodology failure this exposed (owner directive, 2026-07-31)

A PRD-required property was chased with a 180 s remote run on the T4 as
the FIRST experiment, with no CI evidence presented and no local ladder.
Correct order, binding from now on:

1. **CI first.** `make check` — run it and quote it before any live claim
   about a specified property. *(Run 2026-07-31: **152/152 PASS**,
   including `test_cap_budget_cap2_reaches_depth_two`,
   `test_cap_budget_slot_alternates_per_monitor`, and
   `test_ffmpeg_pump_set_four_views_one_thread`. The accounting that
   makes overlap possible is covered; wall-clock overlap is not — see the
   open item below.)*
2. **Local, short, cheap.** 5 s on the dev box (AMD VAAPI) at 1080p, then
   5 s at 4K. Same analysis script, same assertion.
3. **Only then the T4**, and only at the duration the question needs.

A 180 s run is for a *rate*; a binary property like "do these two stages
ever overlap" is answered by seconds of trace. Escalate resolution and
duration only when the cheap rung passes and the question survives.

### CI assertion — DONE 2026-07-31 (`7058ab97`)

`xrdp_gfx_ack_window_open()` extracted from `xrdp_mm_update_module_ack`
(by name, unchanged) into `xrdp_encoder.h` so the joint flow control is
testable, and `drive_pipeline()` in `test_avc444_multimon.c` models the
loop over BOTH real predicates. Four assertions, three of them controls:

| config | depth | meaning |
|---|---|---|
| m=1 cap=2 fif=2 | **≥ 2** | the requirement |
| m=1 cap=1 | 1 | no slots, no overlap |
| m=1 cap=2 fif=1 | 1 | slots free, window shut — the cross-process coupling |
| m=2 cap=2 fif=2 | 1 | the PRD's "inert per monitor" at m≥2 |
| m=2 cap=2 fif=4 | ≥ 2 | what a per-monitor window would restore |

`make check` **156/156 PASS**. Two modelling errors were caught by the
controls while writing it (acking in the same tick as the send; a 1-tick
encode) — both would have made every configuration read depth 1.

### #64b — m=1 at 4K on the T4 does not overlap AT ALL (RESOLVED 2026-07-31: H-c — the PRODUCER is the serialiser; fix tracked as #65, which BLOCKS everything downstream)

**Resolution.** Read the shipped gates instead of running more arms
(escalation ladder rung 0, in hindsight the right first move):

- xorgxrdp's `rdpDeferredUpdateCallback` DOES admit a second capture at
  depth 1 — per-monitor budget of 2, checked per monitor with a
  break-not-skip scan (`rdpClientCon.c`, verified 2026-07-31 in the
  `d77d054` tree). The flow-control layer is not the serialiser.
- `rdpScheduleDeferredUpdate` paces at `msFrameInterval` = 16 ms — a
  16 ms timer cannot produce an 87 ms serialisation.
- `budget exceeded` / `third capture` = 0 on every run: the gate was
  never even ASKED for a second slot.

So the second slot is never exercised because **no second frame of
damage ever exists during an encode: textflood produces at 8.19 fps —
equal to the pipeline's own period.** Its loop is
`cairo render (~100 ms at 8.3 Mpx) → XShmPutImage → XSync`, single
thread, zero internal pipelining. H-c, with the mechanism named. H-a
and H-b are moot for this observation (the resolution sweep would have
measured textflood's render cost against pixels, not the pipeline).

The m=1 T4 row therefore convicts the producer, not the pipeline, and
the finding moved into the PRD as **FR-BENCH-1** (the
saturating-producer contract, FAILING). Fix is **#65**.

Ladder run 2026-07-31, capture
`captures/e52_t4_textflood_m1_4k_20260731/`:

| rung | m | geometry | payload | overlap |
|---|---|---|---|---|
| CI | 1 | n/a | n/a | **admitted** (156/156) |
| local arm-s | 1 | 1920×1080 | codeflood | 7/86 = **8.1 %** |
| local arm-s | 1 | 3840×2160 | codeflood | 4/53 = **7.5 %** |
| **T4** | 1 | 3840×2160 | textflood | **0/205 = 0.0 %** |

**This is a regression against a number already in the PRD** (quality
gate check 4). PRD "Concurrency state of the encode pipeline" records
`capture ‖ encode` = **"YES for m = 1, shipped"**, measured at 1600×912
where *"capture is fully hidden"* — frame period equalled encode
duration while `cap->enc_entry` added 23.8 ms that never reached the
period. At 3840×2160 on the same box, nothing is hidden: 0 of 205
frames, and the gap's **minimum is +7 ms — never once negative**.

Period decomposition (122.6 ms mean): capture+pack 46.7, encode+assembly
40.7, idle 35.6. Closes to 122.9 ≈ 122.6.

**Not the capture budget.** `budget exceeded` and `third capture` are 0
on the T4 run and both local runs — the capture side never hit its own
capacity limit. It never had a second frame to capture while the first
was encoding, so the serialiser is upstream of the slot accounting.

**Open hypotheses**, in order of fit to `min = +7 ms never crossed`:

- **H-a: resolution-dependent, and the PRD's m=1 evidence is stale.**
  The "fully hidden" measurement was at 1600×912 = 1.46 Mpx; this is
  8.29 Mpx. If capture cost scales past encode cost, the two stages stop
  interleaving. Test: sweep m=1 on the T4 at 1600×912, 1920×1080,
  2560×1440, 3840×2160 with ONE payload and plot overlap % against
  pixels. Cheap — 5–10 s per point, and it either reproduces the PRD's
  own number at its own resolution or falsifies it.
- **H-b: the damage/deferred-update timer gates it.** 29 % of the period
  is idle awaiting damage, and the m=2 run's minimum gap was exactly
  4.0 ms = `MIN_MS_TO_WAIT_FOR_MORE_UPDATES`. If the next capture cannot
  arm until a timer fires, overlap is structurally impossible regardless
  of slots. Test: the `rdpDeferredUpdateCallback` vs `rdpCapRect` uprobe.
- **H-c: payload, not pipeline.** textflood damages the whole root every
  frame; there may be no second frame's damage pending to capture. Test:
  run the T4 m=1 with codeflood so the local and T4 rows share a payload
  — the one comparison this ladder is currently missing.

**Do H-a and H-c first**: both are short local-then-T4 runs, and H-c is
the confound that currently prevents attributing the local-8 %/T4-0 %
difference to anything at all (different payload AND different hardware —
CLAUDE.md stand-in rule forbids reading it as nvenc-vs-VAAPI).

### Still open (the m≥2 serial cost)

The 2-monitor period is 173.7 ms with 132.1 ms of it inside our pipeline
on a box at 1.33 of 4 cores. The 50.1 % overlap there is cross-monitor
interleaving, exactly as the PRD describes, and it does not make that
fast. The serial-cost question is real; the "never overlaps" framing was
not.

### Superseded observation (kept for the record)

Across two 180 s T4 runs, `GFX_TRACE enc` reported
`inflight=0` on **every single sample** — 2018/2018 at `fif=2` and
2084/2084 at `fif=4`. Per Error 1 this is the designed value, not a
finding.

The measured cost: the period is a strictly serial chain —
capture+pack 63.7 ms, then encode+assembly 68.4 ms, then 41.5 ms idle —
132.1 ms of 173.7 ms (76 %) inside our pipeline, on a box running at
**1.33 of 4 cores with nothing pinned** (`session_cpu_split.txt`).

### Hypothesis 1 — the global ack window. Probed against an invalid metric; verdict WITHDRAWN.

*(The probe below was run before Error 1 was known. Its `inflight`
readings prove nothing either way — `inflight` cannot move. The
rate/idle regressions it recorded are still real numbers and still
reproduce the bufferbloat the PRD predicted for a global pool, so the
"do not ship a global pool" conclusion stands on that evidence alone.)*

xrdp releases xorgxrdp's capture slots only via
`xrdp_mm_update_module_ack`: `if (frame_id_client + fif > frame_id_server)`
with `fif = DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT = 2`, a **global** counter,
while xorgxrdp's `xup_cap_budget` is **per monitor**. At 2 monitors
`frame_id_server` advances 2 per cycle, so a global window of 2 admits
1 outstanding frame per monitor and cancels the second slot.

**This is exactly the defect PRD § already flagged on 2026-07-29** ("the
budget is global while the slots are per-monitor, so at m monitors this
allows only one frame in flight per monitor"), and the PRD's remedy is
**≤ 2 outstanding PER MONITOR — never a global pool of 2m**, because a
pool "lets one damaged monitor run 4-deep on 2 slots: bufferbloat, +2
frames latency, slot aliasing". Assigned to #45 step 6; xorgxrdp's half
was implemented (per-monitor `xup_cap_budget`), xrdp's ack window was not.

Probed with the existing `XRDP_GFX_FRAMES_IN_FLIGHT=4` knob — **the
forbidden global-pool shape, run only as a diagnostic** and reverted the
same session (capture:
`captures/e52_t4_fif4_PROBE_REVERTED_20260731/`):

| | fif=2 | fif=4 (global pool) |
|---|---|---|
| `inflight` | 0 / 2018 | **0 / 2084 — unchanged** |
| mean per send | 87.0 ms | **98.1 ms (worse)** |
| idle window | 41.5 ms | **63.2 ms (worse)** |
| final ack -> next batch | 23.5 ms | 19.2 ms |

The window demonstrably took effect (`fif=4` on all 8328 send lines) and
acks did return sooner, yet **no concurrency appeared and latency rose** —
the PRD's predicted bufferbloat, reproduced. So the global ack window is
**not** the binding constraint, and making it per-monitor (the correct fix
per the PRD) is necessary for correctness but is **not sufficient** to
close this item. Do not implement it and declare victory.

### Open hypotheses, in the order to test them

**H2 — the deferred-update callback is never re-entered while an encode is
outstanding.** `rdpDeferredUpdateCallback` captures BOTH monitors in one
pass (step 7 batching) and returns; a second capture requires the timer to
re-arm via `rdpScheduleDeferredUpdate`, which early-returns while
`updateScheduled` is TRUE. If the re-arm is coupled to the send/ack rather
than to damage, capture can never run ahead regardless of budget.
**Decisive test:** uprobe counts of `rdpDeferredUpdateCallback` entries vs
`rdpCapRect` calls over 40 s. If entries >> captures, the callback is
firing and being denied at the capacity gate (-> H3). If entries ~= captures
(~1 per period), the callback is not being scheduled and H2 is confirmed.
`PR-demo/t4_profile/xorg_capture_uprobe.sh` already does this shape; add
the two symbols.

**H3 — the capacity gate denies despite free slots.** `xup_cap_budget`
retires on `rect_id_ack`; if `rect_id_ack` only advances once per cycle,
`count[mon]` never drops below the cap in time. Distinguished from H2 by
the same uprobe, plus logging `rect_id`/`rect_id_ack` per pass.

**H4 — the batch itself serialises.** Step 7 arms all 4 children on ONE
deadline and waits for the set. If the pump set is drained synchronously
before the callback returns, then by construction nothing is in flight
when the next submission happens — which would explain `inflight=0`
independently of any budget, and would mean #45 step 7 traded pipelining
for batching. **This is the hypothesis most consistent with the evidence**
(it predicts `inflight=0` exactly, under any window size) and should be
tested first if the uprobe shows the callback is firing normally.

### Why this blocks #63

#63 (4:2:0 during motion) removes work from the serial chain — worth
~45 % of the encode segment and ~half the pack. But if the chain is serial
because of H4, the same serialisation will bound the result afterwards,
and #63's measured win will be smaller than its arithmetic predicts.
Establish the cause here first, so #63 is measured against a pipeline
whose concurrency behaviour is understood.

### Acceptance criteria

* the serialiser NAMED, with a uprobe or trace count that distinguishes
  H2/H3/H4 — not an inference from a stripped stack (#59's lesson);
* the fix keeps **per-monitor** accounting on both sides of the xup
  boundary; a global pool of 2m is a spec violation regardless of what it
  measures;
* `inflight > 0` observed on a material fraction of samples — the
  mechanism check, and the thing that decides whether the fix worked
  (a rate improvement without it is not evidence);
* E5-2 pair re-run and DECOMPOSED with `e52_period_decompose.py`;
* unit tests under `tests/` for the budget/scheduling logic changed.

---

## #56 — A cold GPU makes the first login fall back to RFX, silently (TODO, HIGH)

Found on the T4 on 2026-07-30 (#55). On the **first RDP connection after
boot**, `xrdp_mm_egfx_caps_advertise`'s ffmpeg verification times out and
xrdp drops H.264 entirely:

```
xrdp_ffmpeg: probe TIMEOUT: no verdict within the deadline (cold encoder/device
  init? child still starting?) (dump_extra=1, 3840x2400, packets=0, elapsed=4008 ms)
  ffmpeg verification FAILED (TIMEOUT); removing external AVC candidate
Codec search order is H264, RFX
Matched RFX mode
```

Measured on the same box: `h264_nvenc` at 3840×2400 takes **3.95 s** cold
and **1.40 s** warm. The probe deadline is ~4 s, so the cold path lands on
top of it. With NVIDIA persistence mode off (the default) the driver is
unloaded whenever no CUDA process is running, so this is not a
once-per-boot event — it is *every* connection that follows an idle gap.

Why it matters beyond the benchmark: the session comes up **looking fine**.
The user gets RFX, at RFX quality, with no error anywhere except one WARN
line in `xrdp.log`, and no retry — the AVC candidate is *removed* for the
life of the connection. This is exactly the failure shape CLAUDE.md's
strict-honesty rule is about: a fallback that converts a loud failure into
a silent degradation.

Options, none chosen yet (needs owner sign-off — a shipped fallback change):
* make the probe deadline resolution- and encoder-aware rather than a flat
  4 s, and log at ERROR when it fires;
* warm the encoder once at xrdp start instead of at first connect;
* re-probe on the next connection rather than removing the candidate for
  the session;
* document `nvidia-smi -pm 1` as a deployment requirement (what #55 did as
  a workaround, recorded in the capture READMEs).

Acceptance: a cold-boot first connection negotiates AVC444, proven by
`xrdp.log` + a wire audit, with no persistence-mode workaround; and a probe
timeout that *does* happen is loud.

---

## #57 — xrdp-dev debs stop at a conffile prompt in non-interactive installs (TODO)

`apt-get install -y /tmp/xrdp-dev_*.deb` on the T4 (2026-07-30) halted at
`Configuration file '/etc/xrdp/cert.pem' ... What would you like to do about
it ?` and then `dpkg: error processing package xrdp-dev (--configure): end of
file on stdin at conffile prompt`, leaving xrdp-dev unpacked but not
configured. `/etc/xrdp/rsakeys.ini` does the same. Both files are legitimately
modified on any box that has ever generated its own key/cert, i.e. every
deployed box.

Workaround in use: `-o Dpkg::Options::=--force-confold`, now in the T4
protocol and DEPLOY_RUNBOOK. Real fix: these are generated-at-install
artifacts, not admin configuration — they should not be shipped as
conffiles at all. Small, self-contained packaging change; belongs with the
clean-room port (#46) rather than the dev branch.

---

## #58 — The black-frame check cannot tell a login paint-in from a dropout (TODO)

`oracle_black_frame_check.py` flags any black main-view picture with
`0 < i < n-2` as mid-stream and FAILs the run. On the T4 that fires on
every capture, because the session is XFCE and takes ~1.3 s to paint after
`E_COLD=1` logs the previous one off: main-view luma is 0 through picture
28, 0.77 at picture 30, 118.9 by picture 40, and zero black pictures in the
remaining ~3 800. The fleet pods never showed it because they run the
payload with no desktop and paint immediately.

A check that FAILs every good run gets ignored, which is worse than not
having it. Fix: treat a **leading contiguous run** of black pictures that
ends in a monotone ramp as startup (report it, do not fail on it), and keep
failing on any black picture after the first painted one. Needs a unit test
built from both shapes — a synthetic startup ramp and a synthetic
mid-stream dropout — so the distinction is pinned.

Until then the #55 captures carry the FAIL in `VERDICT.txt` with the
decode evidence beside it, rather than an edited verdict.

---

## #40 — FR-PROC-7 preemptive aux (sparse aux cadence)

Submit/collect construction plus all three policies (preempt, breadth,
depth) with per-policy unit tests. Spec: `PRD.md` FR-PROC-7. Prerequisite
FR-CAPTURE-8 is shipped; the submit/collect split is shared with #45, so
sequence #45 first.

**Hard ordering, not a preference (#45 D12).** #45's paired refresh gives
both children an IDENTICAL frame-indexed `-force_key_frames` schedule, which
assumes 1:1 main/aux pairing. A sparse aux cadence breaks that assumption:
the aux child no longer sees the same frame indices, so its schedule must be
re-derived from its own index or its cuts land on the wrong pictures — and
the observed-vs-scheduled runtime check would then fail every pair. #40 may
NOT land before #45, and this FR's spec must be amended when it does.

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

---

## #65 — textflood violates the saturating-producer contract (TODO, **BLOCKING** — owner directive 2026-07-31; blocks #63, #64b re-run, FR-PROC-7, and every 4K E5-2 verdict)

**The core bug moved to the producer.** PRD **FR-BENCH-1** (added with
this item) states the design intent textflood was built to and now
fails: CPU text rendering, representative subpixel-AA colored pixels,
and — the load-bearing clause — **strictly faster than the pipeline
under test**, so the measured interval is pipeline-limited and a
successor frame is physically in the fifo at pop time. Measured
2026-07-31 (T4, m=1, 3840×2160): textflood at **8.19 fps ≈ the
pipeline's own period**; fifo empty at all 205 completions. Every
number gated on this producer is producer-confounded until fixed —
which is why this blocks everything downstream, per the owner
directive.

Root cause in the producer: the serialized
`draw_frame → XShmPutImage → XSync` loop, one thread, zero internal
pipelining. The XSync comment ("without it the client races ahead of
the server") is FR-BENCH-1 inverted — the producer MUST race ahead, up
to the 2-slot capture bound.

**Step 1 recon DONE 2026-07-31 (`ring_recon.c`, run ON the T4,
offline) — and it corrected this item's own first draft.** The draft
claimed "~100 ms cairo render, ≤10 fps regardless of buffering"; that
was an unmeasured inference and it is FALSIFIED: the verbatim shipped
render loop computes **24.1 ms/frame = 41.4 fps** on the T4 at 4K
(2.5× over the 16.4 fps floor). Scroll+strip renders at 7.1 ms
(141 fps, 8.6×); a ring memcpy at 6.8 ms (147 fps); two concurrent
frame copies sustain 8.8 GB/s — neither compute nor bandwidth is the
wall. **Open anomaly (quality gate check 1): the producer computes
41 fps offline but delivered 8.19 fps deployed — a 5× gap no offline
bench explains.** Leading hypothesis: each `XSync` pays the X thread's
per-frame work (blit copy, damage accounting, the ~20 ms capture pack
when the timer lands inside the sync) and the loop never renders
during the wait. Step 0 decides it.

- **Step 0 — instrument first.** `--selftest N` (offline fps, now
  effectively answered by ring_recon) plus in-session per-frame
  timestamps split into render / blit / XSync-wait. No redesign lands
  before the 5× gap is attributed. Cheap: ~15 lines in textflood.
- **Step 2 — decouple, keep live rendering (design B).** memmove
  scroll + strip render (live per-frame CPU text rendering kept —
  the owner's stated intent), double-buffered so the next frame
  renders during the previous frame's sync, at most 2 outstanding
  blits. At a 25 fps target this costs ~18 % of one core
  (footprint clause). Full-frame damage is preserved: every pixel
  moves every frame.
- **Step 2-fallback — pre-rendered frame ring** (steady state = one
  6.8 ms memcpy, R=16 ≈ 0.5 GB) ONLY if in-session measurement shows
  design B still producer-limited — per the strict honesty rule the
  fallback is not wired in silently; switching to it is a recorded
  decision.
- **Step 3 — harness enforcement.** `e_gate_run.sh` VERDICT prints the
  producer's selftest rate and the in-run saturation observable
  (negative overlap-gap fraction from `e52_period_decompose.py`) and
  stamps the run **VOID (producer-limited)** when either check fails —
  the same loud-invalid pattern as the geometry guard.
- **Step 4 — re-run what the confound taints**: the m=1 4K overlap run
  (re-adjudicates #64b's 0/205 with a compliant producer) and the m=2
  A/B (re-issues #62's 1.41×, which may UNDERSTATE the batch if both
  arms were producer-paced).

Acceptance: selftest ≥ 2× the oracle pipeline rate at 3840×2160 on the
T4; a gate run whose VERDICT shows both saturation checks green; #62's
and #64b's numbers re-issued or explicitly re-confirmed under the
compliant producer.

## #62 — textflood: a payload whose X-side cost is a memcpy (DONE 2026-07-31 — deployed, A/B run, **1.41x RED, attributed**; **2026-07-31 verdict annotated: producer-confounded, re-run under #65** — the 1.41× may understate the batch if both arms were paced by the same 8 fps producer, per FR-BENCH-1)

Closes the instrument half of #61. #59 established that the xterm payload
makes E5-2 measure the X server rather than our pipeline; `PR-demo/textflood/`
renders the **same** corpus (`code_corpus.ansi`, real xrdp source highlighted
by pygments + clangd in solarized-dark) with cairo **in its own process** and
hands X one finished image per frame over MIT-SHM.

Measured on the dev box, Xvfb 6400x2400, 30 s per arm, two `/proc/<pid>/stat`
reads:

| arm | X-server CPU | payload's X-side cost |
|---|---|---|
| idle Xvfb | 13.7 % of a core | — |
| xterm codeflood (server-side XRender glyphs) | **99.0 %** | 85.3 points |
| textflood (client-side cairo + MIT-SHM) | **12.8 %** | ~0, at the idle floor |

**7.7x less X-thread cost for the same content.** textflood's own
rasterization (~87 % of a core) runs in a different process on a different
core; the 3.85 s of X CPU that remains is the SHM copy — ~1050 frames x
61 MB in 3.85 s = 16.6 GB/s, the memcpy floor. Total system work rises, the
bottleneck thread is freed. That is the right trade where Xorg is
single-threaded with idle cores beside it.

Rendering verified from a decoded `xwd`, not assumed: 8/9 solarized entries
present (magenta is on 43 of 3000 corpus lines, so a sample missing it is
expected), 3650 distinct colours, and **21.5 % of pixels are subpixel-AA
fringes** (non-palette, R!=G or G!=B: `#002b37`, `#012b36`, `#165d83`).
Subpixel AA is requested explicitly rather than inherited from the session's
fontconfig — it is what a real desktop renders, and it is the maximal AVC444
stressor, so a silent fallback to greyscale AA would flatter the 4:2:0 arm.

Also removes the failure mode that invalidated three T4 runs: the window is
override-redirect over the whole root, so xfwm4 cannot re-snap it to one
monitor (#53's one-active-one-idle regime) and the xdotool span-fixer loop
is no longer needed.

**Scope note.** That table measures the *payload's* X-side cost on Xvfb — a
property of the payload, not an xrdp measurement, and not a substitute for
one. The E5-2 number still has to come from the T4 with xorgxrdp in the loop.

### T4 RESULT (2026-07-31) — the payload works; the ratio is RED

Deployed to the T4 and the A/B run, both arms 180 s in one sitting, each
deb smoke-gated BEFORE measuring (the #61 precondition):

| | baseline (steps 0-4) | batched (steps 0-7) |
|---|---|---|
| mean per send | 122.9 ms | **87.0 ms** |
| damage coverage | 715/714 = **1.00x** | 1009/1009 = **1.00x** |
| `kids_armed=4` | n/a (cannot batch) | **100 % of 1011 cycles** |
| black frames | 0 of 1424 | 0 of 2014 |
| **ratio** | | **1.41x — RED** |

Captures: `PR-demo/mac_bisect_matrix/captures/e52_t4_textflood_{baseline,batched}_20260731/`.

**The instrument did its job.** Against the same box under the old xterm
payload: session Xorg **92 % of a core -> 28.9 %**; libpixman self
**71.6 % -> 6.3 %**; our capture path **13.8 % -> 37.4 %** of Xorg cycles
(`avc444_decode_row.avx2` 26.5 % self, the top symbol in libxorgxrdp).
Monitor coverage was 1.00x on both arms with no span-fixer — the
override-redirect window removed that whole failure class.

**The ratio is RED and it is NOT a CPU ceiling.** `session_cpu_split.sh`
during the run: Xorg 28.9 %, payload 79.7 %, xrdp encoder side 24.4 % =
**1.33 of 4 cores**. Nothing pinned, 2.7 cores idle. `e52_period_decompose.py`
on the run's own trace says the 173.7 ms period is:

| segment | ms | % | whose |
|---|---|---|---|
| batch arm -> first enc submit (capture + AVC444 pack) | **63.7** | 36.7 | **ours** |
| enc submit -> last=1 (encode + LTR rewrite + EGFX assembly) | **68.4** | 39.4 | **ours** |
| last=1 -> next batch arm (idle, awaiting damage) | 41.5 | 23.9 | payload |
| **our pipeline** | **132.1** | **76.1** | |

**76 % of every period is our own pipeline running serially on a box that
is 33 % busy.** The remaining cost is latency we have not parallelised,
not arithmetic we cannot afford.

Why less than codeflood's 1.5x-2.3x: textflood damages the whole root every
frame, so a pair is ~3.8 MB (main 2.09 + aux 1.69) against codeflood's
~0.6 MB. The batch parallelises the encode of the four children and does
nothing for the 63.7 ms of capture in front or the assembly behind. Amdahl,
measured. The batch is not regressing — it is being measured against a
workload heavy enough to expose what is not batched. This motivates **#63**.

### Harness faults found and fixed during the run

1. `E_COLD` waited only for the Xorg process to die, not for sesman to
   finish teardown (~600 ms more); a client connecting inside that window
   is dropped with `freerdp_post_connect failed`. Baseline won the race,
   batched lost it — a flake that reads as "the batched deb cannot start a
   session". Fixed in `e_gate_run.sh`.
2. A deb install invalidates `xrdp.service` and its drop-ins; restarting
   without `systemctl daemon-reload` silently dropped `XRDP_GFX_TRACE=1`
   and produced a run with zero trace records. `t4_install_arm.sh` now
   reloads and verifies the variable is in the unit environment.
3. A dead ssh port-forward is reported by the harness as
   `connected; recording for 180s`, producing a VERDICT against an empty
   log. Two runs lost before it was spotted. **Not yet fixed** — see below.
4. The gpuflood supervised-alacritty loop never exits when its session
   ends: four orphaned shells were found respawning alacritty 5781 times.
   The `gpuflood` kind is being deleted outright (alacritty is abandoned:
   without a GPU it is llvmpipe/zink, which would be the new bottleneck).

### Remaining, in order

1. DONE — deployed, smoke-gated first, measured.
2. DONE — it does not starve Xorg: 1.33 of 4 cores, nothing pinned.
   (Predicted ~1.6; measured 1.33.)
3. DONE — pixman self 71.6 % -> 6.3 %, capture 13.8 % -> 37.4 %.
4. DONE — E5-2 pair re-run: 1.41x RED, attributed above.
5. TODO — make `e_gate_run.sh` FAIL LOUDLY when the RDP port is not
   reachable or the run produced zero GFX_TRACE records, instead of
   printing `connected` and emitting a VERDICT against an empty log.
6. TODO — delete the `gpuflood` kind and its orphan-prone restart loop.
7. Then use textflood as the instrument for FR-PROC-7 (#40/#41): its
   preempt/breadth/depth policies need a payload that loads the aux view,
   which subpixel-AA text does maximally (aux is 44.8 % of the bytes here).
