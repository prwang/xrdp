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
> fires first if `-g > 32512`. **Revert target is fixed** (#45 D7): `-g` =
> `intra_refresh_frames` = 240, so GOP boundaries coincide exactly with
> scheduled refresh indices and an unscheduled IDR ceases to be reachable.

---

## #45 — Scheduled paired intra refresh, one-thread 4-view `pump_set`, per-monitor capture budget (NEXT)

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

**0. Ratchets first — must FAIL against today's code before step 1.**
   Extend the pure-C DPB simulator in `tests/xrdp/test_avc444_ltr.c` with a
   scheduled paired-cut AU sequence in both 1-context and 2-context decode
   modes; byte-exact goldens from `ltr_splice_ref.py`; an assertion mode
   for `tools/avc444_ltr_wire_audit.py` (it parses every needed fact —
   slice type, nal type, frame_num, rplm target, mmco6 slot, per view —
   and today asserts none and always exits 0). Each ratchet is demonstrated
   RED against the current build and the RED output recorded in the commit
   message. A validator that cannot fail is worthless; one of ours asserted
   a bug as correct (2026-07-29, `rekey_boundary_audit.py` check B).

**1. Rewriter accepts BOTH intra input shapes, in BOTH views.**
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

**2. Schedule + observed-vs-requested check.** Both children spawn with an
   identical frame-indexed `-force_key_frames` schedule derived from
   `intra_refresh_frames` (D6). `xrdp_h264_ltr_state` carries the expected
   refresh index; a picture parsing P where intra was scheduled **fails the
   pair loudly** (same class as an aux P with LT1 unseeded). The only check
   that runs before the client sees the frame; it fails the pair rather
   than shipping it.

**3. No parameter sets at a cut** (D5, unconditional). A non-IDR I is not a
   decoder entry point, the stream never seeks, EGFX is reliable.
   Child-emitted SPS/PPS still passes through on the main view (`:2350–2392`).

**4. Delete the aux respawn path; retire the `-g 30000` interim.** Remove
   `encode_pair` `:1184–1197` and `ltr_aux_fresh`. Set `-g` equal to
   `intra_refresh_frames` (D7) on the T4 profile, arm-n and arm-p — every
   intra picture lands on a scheduled index by construction, "unscheduled
   IDR" is unreachable, and the frame_num-wrap re-key becomes the live wrap
   mechanism instead of being masked by the GOP IDR.

**5. `pump_set` (xrdp).**
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

**6. Per-monitor capture budget, depth 2 (xorgxrdp; lands before E5).**
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
