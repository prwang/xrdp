# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

Only **upcoming** work lives here. Completed work is recorded in `PRD.md` §25
("Delivered"), with detailed root-cause writeups under `tests/xrdp/avc444/`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

## H.265 / HEVC via ffmpeg — OUT OF SCOPE / BLOCKED (2026-07-14)

**Decision: not pursued.** The encode side is nearly free (`-c:v libx265` /
`hevc_nvenc` in `encoder_args`; the converter and NUT demux are codec-agnostic;
only a new HEVC Annex-B validator — 2-byte NAL header, VPS/SPS/PPS 32/33/34,
IDR 19/20 — would be genuinely new). **The wire is the wall.** Research against
the current MS-RDPEGFX spec (rev 19.0, 2026-05-11): **no HEVC codecId, no HEVC
caps flag** — the public codecId enum ends at AVC444V2 `0x000F`, all H.264. The
AVD *feature* is documented (a dedicated **"Configure H.265/HEVC hardware
encoding"** GPO, but in the **AVD** admin template `terminalserver-avd.admx`,
not in-box RDS; client shows "Codecs Used: HEVC", event 162 "HevcProfile"), yet
the *protocol carriage* (codecId, enabling capset, container framing) is
undocumented. The FreeRDP-labelled "Azure undocumented" capsets `0x000B0101/
0200/0300` are pinned by the spec as behaviorally == 10.7 with no HEVC
semantics. FreeRDP has an experimental AV1 custom codec but **no HEVC decoder at
all**. So a server emitter would need packet-capture reverse-engineering of a
live AVD↔Windows App session (three unknown values) and would only reach recent
Microsoft clients with the HEVC Video Extension + capable GPU. Revisit only if
those values get documented or captured; the spec-grounded, interoperable
ceiling for xrdp remains AVC444/AVC444v2 (shipped).

*Update 2026-07-23 (revisit-trigger watch):* MS-RDPEGFX v20260511 Appendix A
note <5> now acknowledges capsets `0x000B0101/0200/0300` — behaving as
VERSION107 only on builds *without* KB5089573 (24H2/25H2) / KB5089570 (26H1),
i.e. real v11 features ship behind those KBs; FreeRDP maintainers suspect
HEVC (FreeRDP#12846, nightly probes Azure hosts). We captured `0x000B0101`/
`0x000B0300` flags `0x1a2` live from the Android Windows App (gap analysis
§2a). Still no public codecId/capset semantics — item stays BLOCKED; the
watch condition is those KB-gated semantics or a FreeRDP decode landing.

## ~~Graceful degradation on persistent encoder failure~~ — WITHDRAWN (2026-07-17)

Withdrawn by explicit owner decision: an automatic RFX fallback would *mask*
persistent encoder failure instead of surfacing it, and masking is exactly the
failure mode that prolonged the AVC444 lag investigation (see the honesty rule
in `CLAUDE.md`). A persistently failing encoder must fail loudly (per-frame
ERROR lines, visible breakage) so the root cause gets fixed — on this project,
do not re-add any silent codec fallback without explicit owner sign-off.

## macOS Windows App AVC444 validation — TODO (2026-07-22, highest-value test)

The Mac "Windows App" is the stated blocker that killed the prior
out-of-tree AVC444 rollout (Nexarian: FreeRDP and MSTSC were fine, "But
Mac OS is important enough that it blocked the rollout"; no screenshot,
capture, or root cause exists upstream — see
`PR-demo/UPSTREAM_GAP_ANALYSIS.md` §2a). Our stream lacks the fork's F1
(pair split across frames — now unit-guarded by the avc444_wire tests) and
F2 (no caps gating) defects and is mstsc-verified, so this run is decisive
whichever way it goes.

**Setup:** Mac + Windows App (record app + macOS versions) over the tunnel
to `127.0.0.1:3389`; `avc_mode = "auto"`; capture regardless of outcome:
the `xrdp_mm_egfx_caps_advertise` version/flags lines (the capsets the
Windows App offers — undocumented anywhere), the negotiated-mode log line,
and a screenshot. Optional: dev build + `XRDP_GFX_TRACE=1` for send/ack.

**Expected outcome matrix — interpretation and action:**

1. **Caps ≥ v10, AVC444 v2 negotiated, render clean** (incl. colorkey
   drive and an odd-origin high-contrast edge): historical blocker
   REMOVED. Strongest PR line. Record evidence; done.
2. **Clean until resize, garbled after**: generation/reset handling on
   reconnect-resize. Retest at fixed size via fresh login; if it
   reproduces, treat as OUR bug candidate (reset keyframe / caps redo),
   trace before blaming the client.
3. **Immediate full-frame chroma garble on v2** (Nexarian-symptom):
   client fault isolated (our stream is spec-conformant + mstsc-clean).
   Retest `avc_mode = "420"` — expected clean. If a v1 (0x000E) trial is
   wanted, add a small caps-classifier override knob (config-only,
   follow-up). Ship policy: per-client negotiate-down documented in
   gfx.toml docs; PR narrative = "fault isolated, contained by caps
   gating + config".
4. **Garbled even on AVC420**: NOT a 444 defect — baseline H.264 issue
   (our stream or Mac decoder). Capture and root-cause before any claim;
   do not paper over with RFX (honesty rule).
5. **Client advertises only CAPVERSION_81 or AVC_DISABLED**: classifier
   already serves AVC420/RFX — confirm session works stock-like; the
   captured capsets are themselves the deliverable (nobody upstream has
   them documented). **OBSERVED on the Android Windows App (SM-S936U,
   2026-07-23):** `AVC_DISABLED` on all v10 capsets, no `AVC420_ENABLED`
   on 8.1, undocumented `0x000B0101`/`0x000B0300` flags `0x1a2`; session
   correctly ran RFX (capture in `UPSTREAM_GAP_ANALYSIS.md` §2a).
   **Counter-observation (Windows desktop Windows App, 2026-07-23):** the
   desktop variant advertises NO `AVC_DISABLED` (flags 0x0 through 10.7)
   — so the Mac variant plausibly allows AVC too, making outcomes 1/3
   more likely than 5. Reminder: run the Mac test SINGLE-monitor, or the
   multimon gate skips H.264 before any negotiation (as happened in the
   dual-monitor Windows session).
6. **No garble but stalls/frozen frames**: pacing/ack issue, not chroma.
   Dev build + trace; compare `frame_id` ack cadence vs mstsc run.
7. **Fails before GFX negotiation** (TLS/transport): environment, not
   codec — fix tunnel/cert first, outcome not attributable to AVC444.

**Acceptance:** verdict + capsets + screenshot recorded in
`UPSTREAM_GAP_ANALYSIS.md` §2a (required-test #2 closed either way), and
the PR narrative updated ("blocker removed" or "fault isolated + policy").

## Port AVC444 wire-layout serializer + test to clean branch — TODO (2026-07-22)

Delivered on dev: the RFX_AVC444_BITMAP_STREAM body serialization was
extracted from `gfx_wiretosurface1_avc444` into an exposed
`out_RFX_AVC444_BITMAP_STREAM()` (`xrdp_encoder.{c,h}`, no behavior change —
identical byte sequence, placeholder/backfill included) and unit tested in
`test_avc444_metablock.c` (`avc444_wire` tcase): ONE PDU, LC=0 in info-word
bits 30..31, cb == metablock+luma length, chroma sub-stream immediately
after, both metablocks over the same rects, stream ends after chroma. This
is the direct regression guard against the prior fork's pair-split-across-
frames defect (luma LC=1 / chroma LC=2 in separate GFX frames).

- Porting rule: NO separate fix commit — fold the serializer extraction
  into slice 5 (metablock emission, same file/pattern) or slice 8 if 5
  stays folded into 8; the `avc444_wire` tests travel with it; the
  `gfx_wiretosurface1_avc444` call-site change lands in slice 8.
- Re-run the per-slice bisectability walk for rewritten slices after.
- Acceptance: clean branch `make check` includes the avc444_wire tests;
  `git diff` dev-vs-clean for these files stays scaffold-only.

## Probe must log child stderr — TODO (2026-07-22)

`xrdp_ffmpeg_avc444_probe()` drains and discards the child's stderr, so a
probe failure logs only `ffmpeg probe FAILED` with no reason. The T4/NVENC
global-header failure (PRD §25, 2026-07-22) took a live shim + off-box NUT
replay to diagnose; child stderr in the log would not have named this
particular cause (the child was silent) but eliminates the largest suspect
class (bad args / missing device / missing encoder) in one glance.

- Scope: probe loop only — feed `err_fd` reads through the existing
  `log_child_line()` (as the runtime path does) instead of discarding.
- Also log WHICH internal check failed (timeout / EOF / NUT error /
  non-monotonic pts / reset-keyframe validation) at WARNING.
- Also log WHY the H264 candidate was skipped when no probe runs at all
  (client caps refusal vs multimon gate vs config) — the 2026-07-23
  dual-monitor Windows App session matched RFX with no probe line and
  the reason was only inferable from code reading.
- Acceptance: a probe failure line is followed by the child's stderr (if
  any) and the failing-check name; unit tests unaffected.
- Lands on the dev branch first; ports to the clean branch only by folding
  into slice 7 (same rule as the dump_extra fix — no separate fix commits
  on the clean branch).

## Upstream clean-room preparation — TODO (2026-07-17)

Transition from the dev branch to a reviewable upstream PR against `devel`.
The dev branch stays as-is (history + scaffold); the PR is rebuilt clean.

### Owner decisions (locked)
- **Strip `XRDP_GFX_TRACE` diagnostics from the PR.** All three layers:
  the send/ack trace in `xrdp_mm.c` (`gfx_trace_on`, the send/ack log lines)
  and the damage-bbox + enc `submitted_seq/returned_seq/inflight/centerY`
  trace in `xrdp_encoder.c` (`gfx_enc_trace_on`, `gfx_trace_rects`). Safe
  because the invariant it revealed is already asserted deterministically at
  the API: `test_avc444_ffmpeg.c` requires every encode call to return
  `READY` with `desktop_sequence == submitted` and `flush_next` → `DONE`
  (commit 4eb72b0c), and the fail-loud `sequence mismatch` / `restarting
  encoder` ERROR path is exercised by the smoke gate. Stripping the trace
  removes a debugging aid, **zero** regression coverage.
- **Strip `tail_flush` from the PR.** It was only ever a workaround for the
  runner desync that the synchronous encode fixed; it is now a structural
  no-op (nothing is ever in flight). Remove the ini knob and both arming
  sites: `xrdp_tconfig.{c,h}` (`avc444_ffmpeg_tail_flush` field + parse),
  `xrdp_encoder.h` (`avc444_flush_enabled`), the arming blocks in
  `xrdp_encoder.c`, and the `tail_flush` docs in `gfx.toml` / `gfx.toml.5`.
  Keep `flush_next` — that is the teardown/resize drain, unrelated to the
  spammer.

### Base the clean-room branch on `origin/devel`, not local `devel`
Cut the clean branch from `origin/devel` (currently 8812646d, 2026-07-16;
remote cache is synced — do not run `git fetch`, this env has no push/fetch
creds). Against that ref our branch is **41 ours-only / 3 origin-only**,
merge-base `3af31df3` (Jul 2). Do NOT use the local `devel` ref (21d38d0c,
Jun 17) as the base or comparison — it is ~a month stale, and that staleness
is why `git diff devel..HEAD` shows a set of changes that are **upstream, not
ours**, and must NOT appear in the PR:
- `libxrdp/xrdp_caps.c`, `xrdp_rdp.c`, `xrdp_sec.c` — upstream CVE fixes
  (CVE-2026-55639 GCC OOB read, and merged fork hardening).
- `vnc/vnc.c`, `vnc/vnc.h` — CVE-2026-41252 heap overflow + desktop-size
  symbols.
- `sesman/sesexec/session.c` — CVE-2026-55626 (Xvnc UDS TCP disable).
- `librfxcodec` submodule pointer bump.
Rebasing the AVC444 layers onto a freshly fetched `origin/devel` drops all of
these automatically (they are already upstream). After fetch, sanity-check:
the only non-AVC444 file the PR touches should be `common/xrdp_client_info.h`
(`CC_GFX_AVC444 = 6`).

### Excluded from the PR (dev-branch scaffold, keep in dev branch only)
`PR-demo/**`, `tests/xrdp/avc444/repro_mbparity/**`,
`tests/xrdp/avc444/FINDINGS_*.md`, `repro_*.py`, `tools/gen_isoluma.py`,
`PRD.md`, `BACKLOG.md`, `CLAUDE.md`, `*_config.md`, `scripts/build_dev_deb.sh`,
`dist/` debs, and all untracked scratch (burr/partialGreen PNGs, `tester_key`,
`xrdp-PR.tar`, `iptables.rules`, `*.Po`, …). Add a `.gitignore` hygiene pass.
**Keep** `tests/xrdp/avc444/PROVENANCE.md` (the NUT independent-implementation
/ licensing attestation) — fold it into the NUT slice and the PR cover letter;
maintainers will ask.

### Divergence risk: none textual, one semantic touchpoint to verify
The only commits on `origin/devel` past our merge-base (3af31df3..8812646d)
are the 3-commit DYNVC multi-chunk reassembly fix (#3829), touching a single
file, `libxrdp/xrdp_channel.c` — which our branch never touches. Zero conflict
surface, so **do not rebase the dev branch to "derisk"**: there is nothing to
resolve, and the clean-room slices apply onto `origin/devel` (which already
has the fix) as a clean textual apply. One semantic note: large full-screen
AVC444 frames are chunked over drdynvc, and #3829 corrects multi-chunk
reassembly — a correctness fix we *inherit* by basing on `origin/devel`.
Confirm during clean-room smoke that large AVC444 frames reassemble cleanly on
the new base (expected: fine / better; not a risk, just a checkpoint).

### Acceptance
- PR branch = fresh `origin/devel` + the slices below; `git diff` touches only
  AVC444 feature files + `CC_GFX_AVC444`; no CVE/vnc/sesman/submodule noise.
- Every slice builds and `make check` passes on its own (bisectable).
  Re-verified 2026-07-22 after the dump_extra rewrite for the four
  rewritten commits (`04e43ee2` → tip `c74a09e7`): per-slice `make` +
  `make check` green, plus the gated real-ffmpeg suite (64/64) against
  ffmpeg 7.1 and 8.1 at every slice. Slices 1–6 are untouched by the
  rewrite (identical hashes).
- No `XRDP_GFX_TRACE`, no `tail_flush` anywhere in the diff.
- astyle (pinned 3.4.14) + cppcheck clean; `/* */` comments only.

## Commit reorganization plan (clean-room slices) — DRAFT (2026-07-17)

Rebuild the feature as ~10 modular commits, each one subfeature, bottom-up so
every commit compiles and tests green (leaf utilities first, wire integration
last). Each slice carries its own `Makefile.am` / test-registration hunk so it
is self-contained. Suggested order:

1. **caps enum plumbing.** `common/xrdp_client_info.h` (`CC_GFX_AVC444`),
   `xrdp/xrdp_types.h`. No behavior change; the capture-capability constant
   everything else references.
2. **RGB→NV12 dual-plane converter + 16/32 chroma alignment.**
   `xrdp_avc444_convert.{c,h}` + `test_avc444_convert.c`. Pure/deterministic;
   includes the mstsc chroma-split (“burr”) fix via `chroma_align` and its
   `test_avc444_width_align` / `odd_dims` guards. Self-contained leaf.
3. **H.264 Annex-B validator.** `xrdp_h264_annexb.{c,h}` +
   `test_avc444_h264.c`. Pure leaf (NAL header / SPS-PPS-IDR checks).
4. **NUT demuxer.** `xrdp_nut.{c,h}` + `test_avc444_nut.c` +
   `fixture_4frame.nut` + `PROVENANCE.md`. Pure leaf; independent-impl note
   ships with it.
5. **AVC420/AVC444 metablock emission.** The `out_RFX_AVC420_METABLOCK` +
   region-rect serialization in `xrdp_encoder.c` + `test_avc444_metablock.c`.
   (If not cleanly separable from dispatch, fold into slice 8.)
6. **AVC444/AVC420 caps negotiation.** `xrdp_avc444_caps.{c,h}` +
   `test_avc444_caps.c`. Pure logic: pick v2 / 420 from the client capset.
7. **External stock-ffmpeg runner (synchronous encode).**
   `xrdp_encoder_ffmpeg.{c,h}` + `test_avc444_ffmpeg.c`. Spawn/argv incl.
   one-frame `-probesize` and the `dump_extra,h264_mp4toannexb` bsf chain
   (global-header muxer vs encoders with no in-band SPS/PPS repeat —
   h264_nvenc; folded in 2026-07-22 after the T4 finding, branch rewritten,
   slice now `04e43ee2`), NUT read loop, **synchronous** encode + sequence
   verification, resize lifecycle. Built correct from the start (no desync/
   deadlock to “fix later”); the test carries both regression guards plus
   the global-header-encoder probe regression. Depends
   on 2–4. **No tail_flush, no trace.**
8. **Encoder integration / dispatch.** `xrdp_encoder.{c,h}`: select the ffmpeg
   backend, feed converter output, emit metablock + bitstream. Depends on
   2–7. **No trace.**
9. **eGFX caps advertise + wire-to-surface send.** `xrdp_mm.c`: advertise the
   AVC444 capset, connect-time encoder probe, send path. Depends on 6/8.
   **No send/ack trace.**
10. **Config + docs.** `xrdp_tconfig.{c,h}` (`[avc444_ffmpeg]` path/avc_mode/
    encoder_args parse) + `test_tconfig.c` + `tests/xrdp/gfx/*.toml`,
    `gfx.toml`, `xrdp.ini.in`, `docs/man/gfx.toml.5.in`. **No tail_flush.**

Notes: build glue travels with its slice (do not defer Makefile edits to a
trailing commit, or intermediate commits won’t build). The synchronous-encode
+ probesize design is baked into slice 7 as the *initial* implementation — the
dev branch’s desync/deadlock/fix archaeology is intentionally not replayed;
its rationale belongs in the PR description, with `PRD.md` §25 as the
long-form reference.
