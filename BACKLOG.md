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

## ~~Graceful degradation on persistent encoder failure~~ — WITHDRAWN (2026-07-17)

Withdrawn by explicit owner decision: an automatic RFX fallback would *mask*
persistent encoder failure instead of surfacing it, and masking is exactly the
failure mode that prolonged the AVC444 lag investigation (see the honesty rule
in `CLAUDE.md`). A persistently failing encoder must fail loudly (per-frame
ERROR lines, visible breakage) so the root cause gets fixed — on this project,
do not re-add any silent codec fallback without explicit owner sign-off.

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

### Base the clean-room branch on FRESH `origin/devel`, not local `devel`
Local `devel` (21d38d0c, 2026-06-17) is **25 commits behind** `origin/devel`.
Because of that staleness, `git diff devel..HEAD` currently shows a set of
changes that are **upstream, not ours**, and must NOT appear in the PR:
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

### Acceptance
- PR branch = fresh `origin/devel` + the slices below; `git diff` touches only
  AVC444 feature files + `CC_GFX_AVC444`; no CVE/vnc/sesman/submodule noise.
- Every slice builds and `make check` passes on its own (bisectable).
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
   one-frame `-probesize`, NUT read loop, **synchronous** encode + sequence
   verification, resize lifecycle. Built correct from the start (no desync/
   deadlock to “fix later”); the test carries both regression guards. Depends
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
