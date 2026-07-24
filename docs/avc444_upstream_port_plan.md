# AVC444 LC=1/LC=2 reframe — upstream port plan (GATED)

Status: **PLAN ONLY — do not execute until all gates below are green.** Owner
directive (2026-07-24): write the port plan but gate it on two outstanding
backlog items; keep AUD out of the PR; confirm the aud-less interleave on macOS
first.

## What ships upstream
The AVC444 wire framing is **interleaved LC=1 luma → LC=2 chroma, two PDUs per
gfx frame**, aud-less. Same H.264 bytes and traffic as the old single-PDU LC=0;
only the framing differs. This is the macOS-Windows-App fix (Apple VideoToolbox
rejects our former same-region LC=0; it accepts luma-first LC=1 + deferred LC=2,
the real-Windows structure). Rationale + ground truth:
`docs/avc444_lc_reframe_design.md`, `vm/GROUND_TRUTH_win2022_avc444.md`.

Dev-branch commit to port: `aa894917` (re-authored, NOT cherry-picked — the clean
branch is a re-slice with different structure).

## GATES (ALL must be green before porting)
1. **macOS confirms the aud-less interleave renders. ✅ GREEN (2026-07-24).** Dev
   box deployed with the reframe, `avc_mode=444`, **AUD removed** from
   `/etc/xrdp/gfx.toml` (wire verified: `LC=1 [SPS,PPS,SEI,IDR]` → `LC=2 [P]`, no
   NAL 9). **Owner confirmed the macOS Windows App AND UWP both render clean with
   no AUD** — the `LC=1`/`LC=2` interleave is the whole fix; AUD is a red herring
   and stays out of the PR.
2. **NVENC-on-Linux test regression fixed** (owner-flagged). Ties to the clean
   branch's slice-7 issue: `avc444-ffmpeg-upstream @ c74a09e7` carries the
   BLANKET `dump_extra` (slice 7 `04e43ee2`), a regression fixed on dev by
   `7927efa7`; the cleanroom artifacts from `c74a09e7` are POISONED for the
   macOS client. The adaptive `dump_extra` re-fold (BACKLOG "Re-fold slice 7…")
   AND the NVENC-Linux path must be green before porting.
3. **Multi-monitor done** (BACKLOG "Multimonitor AVC444…", §formerly a follow-up
   PR). Owner now wants it landed before/with this port: drop `monitorCount<=1`
   eligibility, probe at the largest single-monitor coded size, per-monitor
   encoder/converter/ffmpeg state, dual-monitor live matrix.

## Slice strategy (owner decision: FOLD, no separate fix commit; AUD excluded)
Fold the reframe into slice **`239d8d0e` "avc444: wire the external ffmpeg
backend into the GFX encoder"** — the slice that introduces
`gfx_wiretosurface1_avc444` and the LC serializer — so the serializer is *born*
emitting LC=1/LC=2. No LC=0 ever appears in the PR; no "introduce then rewrite"
churn. The LC-framing rationale lives in the code comment on
`out_RFX_AVC444_BITMAP_STREAM_view` + a concise note in `239d8d0e`'s message.

### Exact changes to re-author against the PR branch (files in slice 239d8d0e)
- `xrdp/xrdp_encoder.c`
  - Replace `out_RFX_AVC444_BITMAP_STREAM` (LC=0, both views one PDU) with
    `out_RFX_AVC444_BITMAP_STREAM_view(..., int lc)`: LC=1 → cb = metablock+main
    len, main sub-stream only; LC=2 → cb = 0, aux sub-stream only.
  - `gfx_wiretosurface1_avc444`: emit two PDUs in ONE frame — queue the LC=1
    luma PDU inline via `gfx_send_done(..., is_last=0)`, then return the LC=2
    chroma PDU to the dispatch loop (FIFO preserves luma→chroma; both land
    between the surrounding STARTFRAME/ENDFRAME so region-strict clients stay
    atomic — no luma-only intermediate).
  - `avc444_flush_build_wts1` (+ caller): same two-PDU framing for the opt-in
    tail-flush; AVC420 stays single-PDU (chroma_out=NULL).
- `xrdp/xrdp_encoder.h`: prototype `out_RFX_AVC444_BITMAP_STREAM` →
  `out_RFX_AVC444_BITMAP_STREAM_view`.
- `tests/xrdp/test_avc444_metablock.c`: replace the LC=0 single-PDU wire test
  with `test_avc444_wire_luma_lc1` (LC=1, cb = metablock+bitstream) and
  `test_avc444_wire_chroma_lc2` (LC=2, cb = 0, aux only); keep the single-rect
  minimal case exercising both views.

### Explicitly NOT ported (dev scaffolding)
`BACKLOG.md`, `docs/*`, `vm/*`, `PR-demo/*`, the `.gitignore vm/` line, the
`aud=1` default-arg change, and the dev-branch AUD comment edits in
`xrdp_encoder_ffmpeg.c`. The upstream default encoder args stay
`repeat-headers=1` (no `aud`).

## Mechanics (git rebase -i is unavailable in this env)
Only `c74a09e7` sits on top of `239d8d0e`, so this is a one-commit manual rebase:
1. In `/work-PR` (worktree of `avc444-ffmpeg-upstream`), tag a backup:
   `git branch backup/avc444-ffmpeg-upstream-preframe`.
2. `git checkout 239d8d0e` (detached); apply the re-authored reframe to
   `xrdp_encoder.{c,h}` + `test_avc444_metablock.c`; `git commit --amend`
   (extend the message with the LC=1/LC=2 rationale) → `239d8d0e'`.
3. `git cherry-pick c74a09e7` onto `239d8d0e'` → `c74a09e7'` (should apply clean;
   it touches caps advertisement, not the serializer).
4. `git branch -f avc444-ffmpeg-upstream c74a09e7'` and re-check out the branch
   in the worktree.
5. Verify per-slice bisectability: `git rebase --exec 'make -C xrdp' <base>` (each
   slice still builds), then `make check` and `astyle --options=astyle_config.as`
   at the tip.
6. Owner pushes (this env is excluded from origin creds).

## Validation before push
- All xrdp unit tests pass; astyle clean (CI pins astyle 3.4.14).
- Bisectable: every slice builds.
- Fresh cleanroom build renders on: mstsc, UWP, and the macOS Windows App
  (aud-less interleave) — the `c74a09e7` "POISONED" note is cleared only after
  the adaptive dump_extra re-fold (gate 2).
- Multi-monitor live matrix (gate 3).

## Note on residual macOS 1px-chroma softness (NOT a gate)
On the macOS Windows App HiDPI display, the isoluminant 1px-stripe bands render
softened/blended (mstsc/UWP show crisp grid+checkerboard, i.e. true 4:4:4 on the
wire). This is a client-side artifact of the macOS Windows App's opaque HiDPI/DSP
pipeline (likely a 4:2:2 downscale or Nyquist-boundary attenuation at non-1:1
scaling), not reachable from the server. Accepted as-is; does not block the port.
