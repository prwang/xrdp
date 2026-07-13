# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

## AVC444 encoding — MVP WORKING (deployed + validated on this box)

**End-to-end AVC444 is live on `127.0.0.1:3389` (2026-07-13).** With
`h264_encoder = "ffmpeg"` in `gfx.toml`, an `xfreerdp3 /gfx:AVC444` client
renders a correct XFCE desktop decoded from the `RFX_AVC444_BITMAP_STREAM`
(codec `0x000E`) produced by the external stock-ffmpeg backend. Server log:
`ffmpeg AVC444 probe OK` → `Matched H264/AVC444 (ffmpeg) mode` →
`xrdp_encoder_create: starting ffmpeg AVC444 gfx session` →
`xrdp_ffmpeg: spawned ffmpeg pid … coded 1280x720`. Colors are correct
(validates the no-U/V-swap split). Child reaped on disconnect (no zombies/
leaks). Regression verified: a non-AVC444 (`/gfx:RFX`) client still gets a
desktop (`Matched RFX mode`) — the ffmpeg backend cleanly declines and codec
order falls through. Deployed: `/usr/sbin/xrdp` + `/usr/lib/xorg/modules/
libxorgxrdp.so` (backups in `/root/avc444_backup/`).

Remaining polish/hardening (below "Remaining for on-screen AVC444"): full
FR-CAP-0 no-x264 build de-guarding (only the AVC420 metablock helper was
moved out so far), unit tests for the new integration seams, PR9
resize/reset/failure-threshold hardening, Windows `mstsc` interop, and a
commit (nothing committed yet).

## AVC444 encoding — IN PROGRESS (superseded by the section above)

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
- `PRD.md` (v2) landed and verified (see `PRD.md` §18 "Review pass 8").

### Implemented and unit-tested (self-contained encoding engine — the algorithmic core)

All new files build **without** `XRDP_X264`/`XRDP_OPENH264`/FFmpeg libs (FR-CAP-0),
are compiled into `xrdp` unconditionally, and are covered by the `test_xrdp` Check
suite (`make check` green; 47 xrdp-suite tests, +24 new). No functional/security
regression — the code is dormant until wired into the live GFX path behind config.

- **PR3 — color + AVC444 two-view reconstruction** — `xrdp/xrdp_avc444_convert.{c,h}`.
  Exact full-range BT.709 integer matrix (spec vectors) + MS-RDPEGFX 3.3.8.3.2 main/
  Chroma420 split into two NV12 views at 16-aligned coded dims; edge-replicated
  padding. **Round-trip proven**: an independent reimplementation of the reference
  decoder recovers the source YUV444 chroma exactly. Split verified against the
  FreeRDP reference (no U/V swap — the FreeRDP *encoder* B2/B3 swap is a known
  self-inconsistency; we match its decoder + the spec + xrdp's AVC420 path).
  Tests: `tests/xrdp/test_avc444_convert.c`.
- **PR5 — NUT demuxer** — `xrdp/xrdp_nut.{c,h}`. Bounded streaming parser (file id,
  main header + frame-code table, stream header, syncpoints, frames, MSB-first
  CRC-32 poly 0x04C11DB7/init-0). Overflow-checked varint arithmetic vs FR-NUT-3
  ceilings before any alloc (FR-NUT-7). Validated against a real stock-ffmpeg
  fixture (`tests/xrdp/avc444/fixture_4frame.nut`, provenance recorded): 4 packets,
  exact sizes/PTS, first keyframe SPS/PPS/IDR; plus byte-fragmentation, truncation
  (NEED_MORE not error), CRC-corruption, bad-file-id, and total-ceiling tests.
- **PR6 — H.264 Annex-B validation** — `xrdp/xrdp_h264_annexb.{c,h}`. Bounded NAL
  scan; first-packet SPS/PPS/IDR + aux VCL checks (FR-H264-5). Tests present.
- **PR1 (partial) — capability classification** — `xrdp/xrdp_avc444_caps.{c,h}`.
  Pure FR-CAP-2 table (v8/v8.1/v10.0/v10.1-excluded/v10.2–10.7); table-driven tests.
  *Not yet wired into `xrdp_mm_egfx_caps_advertise()`.*
- **PR4 — secure ffmpeg process runner + probe** — `xrdp/xrdp_encoder_ffmpeg.{c,h}`.
  fork/execve (no shell, minimal env, fd3 raw-in / stdout-NUT / stderr, close-on-exec,
  child fd hygiene), nonblocking `poll()` loop, stderr logged only via constant
  format string with control chars stripped (NFR-SEC-8), SIGTERM→grace→SIGKILL reap.
  Four-picture behavioral probe. **Validated end-to-end against real ffmpeg 7.1.5**
  (converter → child → NUT → H.264 → ordered pairs); no zombies/fd leaks. The live
  test is gated on `XRDP_TEST_FFMPEG_PATH` so default CI skips it
  (`tests/xrdp/test_avc444_ffmpeg.c`).

### CRITICAL FINDING — stock-ffmpeg pipe latency invalidates the synchronous model

The PRD's synchronous "write pair → read pair" transaction (§8.6) was validated in
the PRD's review passes only with **EOF-terminated** input (a file / closed pipe),
which flushes everything. Empirically, a stock ffmpeg reading a **persistent** pipe:
- emits **zero** output until it has buffered several input frames (rawvideo AVIO
  buffering; ~3 frames for realistic sizes), and
- **never flushes the final picture(s) without EOF**.
Low-latency flags do not fix this cleanly: `-fflags nobuffer` **breaks** the encode
path ("No filtered frames for output stream"); `-probesize 32` corrupts the rawvideo
demuxer. So the design was adapted: the runner is **pipelined** — `encode_pair()`
submits a pair and returns the *oldest completed* pair (a submitted pair is available
~1–2 desktop updates later); `flush_next()` closes input and drains the remainder at
reset/teardown. Consequence for the MVP: a static final frame is not displayed until
the next update or an explicit flush (acceptable per NFR-LAT-1, documented). This is
a genuine deviation from PRD §8.6/§11 and should be reflected there when the live
path lands.

### Remaining for on-screen AVC444 (not yet done)

- **PR1 wiring** — split `best_h264_index` into avc444-v1/avc420 candidates in
  `xrdp_mm_egfx_caps_advertise()`, gate on `monitorCount <= 1` + probe.
- **PR2 — xorgxrdp `CC_GFX_AVC444` XRGB full-chroma capture** (separate repo
  `/workUpdateXorgXrdp`; needs version-matched rebuild + deploy).
- **PR7/PR8 — pair backend into the encoder worker + `RFX_AVC444_BITMAP_STREAM`
  `LC=0` serializer** (codec id 0x000E via `xrdp_egfx_wire_to_surface1()`), pipelined
  frame-ack mapping via `result->desktop_sequence`.
- **Config** — `gfx.toml [avc444_ffmpeg]` + typed parsing in `xrdp_tconfig.c`;
  `h264_encoder = "ffmpeg"`.
- **Validation harness — PROVEN on this box (2026-07-13).** `Xvfb :99` (installed;
  virtualizes fine in this userspace Incus container) + `xfreerdp3 3.15.0`
  (`/gfx[:AVC444]`, the reference AVC444 decoder) + `xdotool` (submit the
  tester/blank login) + `xwd` + `ffmpeg` (xwd→png). Verified end-to-end: connect →
  full XFCE desktop over the current GFX H.264 (AVC420/x264) path → PNG screenshot.
  Reusable script: `/tmp/avc444_harness/shoot.sh <out.png> [AVC444|AVC420|RFX]`.
  So on-screen AVC444 validation IS possible here — no Windows client needed.

- **SECOND FINDING — pipeline latency vs xrdp's synchronous per-frame GFX model.**
  `gfx_wiretosurface1()` (`xrdp_encoder.c:790`) is synchronous: it parses the GFX
  command, encodes THIS frame, serializes, and returns the bitmap for immediate
  send + per-frame ack (flow control via `frames_in_flight`/`gfx_ack`). The
  pipelined ffmpeg backend cannot return THIS frame's encoded pair synchronously
  (it's ~1 update behind). The AVC444 serializer path must therefore submit the
  current pair and emit the oldest *completed* pair (stale by ~1 update), mapping
  frame acks via `result->desktop_sequence`, and emit a no-op/late frame while the
  pipeline primes — without stalling GFX flow control. This is a real deviation
  from the PRD's synchronous assumption and the main remaining integration risk;
  do it with the harness, validating on-screen, not blind.

---
