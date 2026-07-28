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

> **`-g 30000` is a known-risky interim, not the target state.** It removes
> mid-stream IDRs, which is the only thing currently preventing the ~630 ms
> aux-child respawn stall — but it also removes the only mechanism bounding
> invariant **I3** (transitive dependency depth), so an encoder/decoder
> divergence would persist until reconnect. It MUST be reverted to a sane
> GOP once task #45 lands.

---

## #45 — Scheduled paired intra refresh + race-free `main‖aux` (NEXT)

Immediate next step. Implements the revised **FR-H264-6** (PRD) and unblocks
the parallel encode.

Scope:
1. Spawn both children with an **identical frame-indexed**
   `-force_key_frames` schedule (measured working on both `h264_nvenc` and
   `h264_vaapi`), so refresh indices are known before submission.
2. Rewrite each scheduled index as a **paired cut** — main → non-IDR I
   self-marking LT0, aux → non-IDR I self-marking LT1, SPS/PPS alongside,
   no IDR and no DPB flush. First picture of the session stays a real IDR.
3. Delete the aux-child respawn path (it becomes unreachable) and restore a
   sane GOP / refresh interval on both the T4 profile and arm-n
   (≈240 frames costs ≈ +4 % bandwidth; see PRD FR-H264-6).
4. Split `encode_single()` into `submit_single()` / `collect_single()` — the
   submit/collect structure already exists internally — and submit main and
   aux concurrently, collecting in order (the LTR rewrite stays serial;
   it is microseconds of CPU). With (1) the IDR race disappears entirely.

Acceptance:
- **Ratchets must verify the schedule is OBSERVED, not requested.** At every
  scheduled index the slice must parse as `slice_type == I` in **both**
  views; a P where intra was expected is a **loud failure**, same class as
  an aux P with LT1 unseeded. Unit test + a wire-capture assertion via
  `tools/avc444_ltr_wire_audit.py`; a stream that silently skips a scheduled
  refresh must fail the test, not degrade quietly.
- Ratchet that no IDR appears mid-stream and that both chains' transitive
  depth never exceeds the configured interval.
- `make check` green; topology 1/2/3 identity unchanged; bandwidth gate
  re-run so the refresh cost is recorded, not assumed.
- 4K fps re-measured on the T4 (expect the pair to approach one encode's
  cost, ~20 ms, rather than two).

---

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

## #47 — Reconnect after codec switch renders black (suspected real bug)

Raised 2026-07-23, never root-caused, carried forward deliberately rather
than dropped in the rewrite. Reproduce or retire it explicitly.

---

## Owner-blocked

- **macOS across a re-key boundary.** The topology-3 epoch rule assumes a
  two-context client re-initialises its aux decoder when the chain
  re-keys. Windows/macOS render correctly today, but nobody has watched a
  re-key transition onscreen. Needed before FR-H264-8 can lose EXPERIMENTAL.
- **Owner sign-off** on making `aux_ltr_chain` the default.
