# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

## AVC444 encoding — TODO

**Goal.** Add an external stock-`ffmpeg` AVC444 (RDPGFX `0x000E`) H.264 backend
to xrdp — encode the two Microsoft AVC444 views through one persistent stock
`ffmpeg` child, demux its NUT stdout in-tree, and serialize `RFX_AVC444_BITMAP_STREAM`
`LC=0` to MSTSC — with no compile-time FFmpeg dependency. Full spec in `PRD.md`.

**Scope.** Per the `PRD.md` §17 PR decomposition (PR1 capability/build guards …
PR9 resize/reset + MSTSC acceptance). MVP: single monitor, AVC444 v1 only,
`libx264`, standard NUT, complete-view reconstruction, restart-on-resize. Keep
changes controlled per `CLAUDE.md`: no functional/security regression, treat the
external process's NUT/H.264/stderr as untrusted (bounds + format-string safety),
tests for new logic (capability classification, color, NUT demuxer, state machine).

**Status notes.**
- Fresh branch `dev/ipc_avc444` off `origin/devel`; build/packaging + docs scaffold.
- `PRD.md` (v2) landed and verified: a multi-agent pass cross-checked every concrete
  claim against the real codebase (`/work` + `/workUpdateXorgXrdp`), the installed
  stock `ffmpeg 7.1.5` (FR-PROC-5 argv re-run end-to-end), and MS-RDPEGFX. The PRD
  was highly accurate; 15 precision/security corrections applied (see `PRD.md` §18
  "Review pass 8"). No code yet.

---
