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
| bisect fleet arm-n | image `34795577580b.xx5b9650c-xfce` | `gfx/arm-n.toml` — `aux_ltr_chain = true`, no `-g` (the runner pins it) | good on Windows multimon + macOS |
| bisect fleet arm-r (2026-07-29) | image `f7acb5979788.xxd77d054` = xrdp #45 steps 0–7 + xorgxrdp step 6 | `gfx/arm-r.toml` — `aux_ltr_chain = true`, `intra_refresh_frames = 240` | **#45 gates E1/E2/E3/E6/E7 PASS, E5 RED at 0.97×** (see #45 GATE RESULTS) |

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

## #45 — Scheduled paired intra refresh, one-thread 4-view `pump_set`, per-monitor capture budget (OPEN on E5 — next work is #52)

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
| **E4** one thread, four views | **Mechanism PROVEN, premise rare.** The worker armed 4 children in ONE poll set 21 times (asserted counter + a once-per-run INFO line), so the construction demonstrably works — but that is **21 of 3346 cycles (0.6 %)**. See E5. |
| **E5** oracle frame interval | **RED — 0.97×** (52.5 ms mean vs the 51.1 ms baseline). **The stop rule applies; nothing was re-tuned.** Reassessed 2026-07-30: BOTH numbers are readings of the payload's own 10 Hz clock, not of the server — see the attribution below and **#52 (E5-2)**. |
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

**What this means for the item.** Steps 0–7 are implemented, tested and
deployed; E1/E2/E3/E6/E7 pass on the deployed pair; E4's mechanism is
proven. **#45 stays open on E5**, and the reassessment redirects the
next work: not capture-side archaeology under a 10 Hz payload — a
saturating benchmark first (**#52, E5-2**). The two capture-side
questions recorded on 2026-07-29 (why a monitor's period is ~105 ms;
whether the producer can hand both monitors over together) are kept
under #52's predictions: they are only answerable, and only meaningful,
once the payload outruns the pipeline.

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

## #52 — E5-2: saturated-payload frame interval (TODO, NEXT — the benchmark E5 should have been)

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
