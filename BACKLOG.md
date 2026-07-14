# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

## AVC444 encoder-args passthrough — DONE (architecture, 2026-07-14)

Behavior-preserving refactor of the ffmpeg backend config. The previous design
enumerated each ffmpeg flag as a typed field (`tune`/`quality_crf`/
`gop_pictures`), parsed by name in `xrdp_tconfig.c`, stored in three structs and
re-emitted in `build_argv`. That is a hard-coded allow-list: it does not scale
and cannot express the hardware encoders (nvenc/qsv/vaapi) the PRD plans.
Replaced with a single verbatim passthrough:

- `struct xrdp_avc444_encoder_args` (`xrdp_encoder_ffmpeg.h`): fixed array of up
  to 64 tokens × 255 chars + count. `build_argv` inserts these tokens verbatim
  between the fixed **input** contract (raw NV12 on `pipe:3`) and the fixed
  **output** contract (Annex-B in NUT on `pipe:1`); it emits no `-c:v`/tuning of
  its own. `FF_MAX_ARGV` 80 → 128 for headroom.
- `xrdp_ffmpeg_avc444_default_encoder_args()` is the single source of the
  built-in default block, which **reproduces the historic hard-coded argv
  exactly** (libx264 ultrafast zerolatency crf 18 g 240 x264-params
  repeat-headers=1) — so absent/empty config is a strict no-op vs. before.
  Shared by `config_default()` and the tconfig loader so they never diverge.
- `gfx.toml [avc444_ffmpeg]`: `tune`/`quality_crf`/`gop_pictures` **dropped**;
  new `encoder_args = [ … ]` array parsed in `xrdp_tconfig.c` (bounds-checked,
  truncation warns, an empty array falls back to the default). `path` kept.
  Plumbed as one struct copy through `xrdp_encoder` (create +
  `gfx_wiretosurface1_avc444`) and the `xrdp_mm_egfx_caps_advertise` probe.
- Tests (`test_tconfig.c`): defaults assert the libx264/zerolatency/crf-18
  block; override (`gfx_avc444_ffmpeg.toml`, selecting `h264_nvenc`) asserts the
  default does not leak; new empty-array fallback test. `make check` = **53/53**
  (incl. the real-ffmpeg probe/encode/resize tests, which exercise the new
  passthrough end-to-end).
- Docs: `docs/man/gfx.toml.5.in` gains a full `[avc444_ffmpeg]` section (input/
  output contracts, `encoder_args` semantics, the no-shell one-token-per-element
  rule, the Annex-B/`repeat-headers` requirement, and an nvenc example).
- Security: `gfx.toml` is root-owned admin config (trusted like `sshd_config`);
  tokens go straight to `execve` with no shell, so there is no injection surface.
  Bounds are enforced on count and per-token length.

## AVC444 magenta burr — ROOT CAUSE FOUND: it's AVC444 v1; fix = emit v2 (2026-07-14)

**Symptom.** Client captures (`wierd_red_burr.png`, `red_burr_v2.png`) show
magenta speckles on saturated green terminal text.

**Root cause (proven with a faithful offline harness — see
`tests/xrdp/avc444/FINDINGS_magenta_burr.md`).** The burr is **inherent to
AVC444 v1 (codecId 0x000E) chroma reconstruction in the FreeRDP v3.15 client
decoder**, not
an xrdp packing or encoder-quantization defect:
- reproduces losslessly (`-qp 0`) and all-intra (`keyint=1`) → not H.264
  quantization, not a P-frame effect (both earlier theories DISPROVEN);
- reproduces when **FreeRDP's own** encoder feeds FreeRDP's own decoder — the
  dedicated `RGBToAVC444YUV` (v1) → ChromaV1 combine still yields ~100 burr px on
  a 192x64 green-text crop. Since the reference encoder+decoder pair burrs on its
  own, **no server-side v1 packing removes it.**
- Mechanism: the decoder's YUV444->RGB step re-derives every block's (even,even)
  chroma with an **always-on** reverse filter `4*U00 - neighbours` +
  `CONDITIONAL_CLIP` (threshold 30) — FreeRDP `prim_YUV.c:358`,
  `sse/prim_YUV_sse4.1.c:212,356`, `prim_internal.h:215`; runs for both v1 and
  v2. In v1 the (even,even) chroma is a subsampled main-view value, so the filter
  is a genuine extrapolation and overshoots past neutral into the complementary
  hue (magenta) at sharp green/black edges. Measured through the faithful SSE
  decoder, the burr is invariant to the server's v1 packing choice (point=109,
  2x2-avg=100, pre-distort=153) — so it cannot be tuned away on the v1 wire.

**Fix — emit AVC444 v2 (codecId 0x000F, ChromaV2).** v2 uses a different chroma
transport (ChromaV2 aux + block-average main chroma) that feeds the same
always-on reverse filter consistent data, so it reconstructs the true value
instead of overshooting. Verified end-to-end through FreeRDP's own v2
encoder+decoder: **v1 = 100 burr / v2 = 0 burr**, visibly clean, ~4x lower mean
error. Spec MS-RDPEGFX 3.3.8.3.3; FreeRDP `general_ChromaV2ToYUV444`
(`prim_YUV.c:172`). v1/v2 is chosen by the **codecId**, not the LC bit: FreeRDP
`avc444_decompress` (`h264.c:646`) sets ChromaV1/V2 from
`codecId == RDPGFX_CODECID_AVC444` (0x0E) vs `...V2` (0x0F); the LC/`op` field
stays 0 (both streams present) for v2 exactly as for v1. This is a new feature
(produce the ChromaV2 aux packing, emit codecId 0x000F, gate on client v2
support with v1 fallback) — TODO below. The ffmpeg interface and encoder_args
are unchanged: the child still gets two NV12 pictures per frame in both modes.

**Protocol-level cross-check (not FreeRDP-specific):** the reverse filter +
cutoff-30 threshold is the spec's own optional decode step (MS-RDPEGFX 3.3.8.3.2
v1 / 3.3.8.3.3 v2). Reproducible against a real Windows RDP server (Server 2022
Eval, "Prioritize H.264/AVC 444" GPO, Event ID 162 confirms 4:4:4) with
`xfreerdp3 /gfx:AVC444` and `WLOG_FILTER=...rdpgfx.client:DEBUG,...gdi:TRACE` to
read the negotiated `RDPGFX_CODECID_AVC444` (0x0E, v1) vs `...V2` (0x0F). See the
"Independent validation" section in `tests/xrdp/avc444/FINDINGS_magenta_burr.md`.

**Faithful repro method (the earlier light repro was unfaithful and misled us):**
capture pristine source via `x11grab :10`; pack with the real
`xrdp_avc444_convert.o`; decode with a harness that calls FreeRDP's runtime
`primitives_get()` **SSE4.1** path (the general/C path gives a false 0-burr).

### TODO — AVC444 v2 emission
- `xrdp_avc444_convert.c`: add a ChromaV2 packing (aux plane layout per
  3.3.8.3.3) alongside the existing v1 path; unit-test the split against a known
  vector.
- Capability negotiation: advertise/select v2 only when the client advertises
  `RDPGFX_CAPVERSION_101`; fall back to v1 otherwise (no regression when absent).
- Serializer: emit `codecId = RDPGFX_CODECID_AVC444V2` (0x000F) for v2 frames via
  `xrdp_egfx_wire_to_surface1()`; keep the LC/`op` field at 0 (two-stream
  layout, same as v1). No ffmpeg-interface change.

### Secondary correctness fixes (independent of the burr; do alongside v2)
- v1 main-view chroma should be the 2x2 **average** (FreeRDP's canonical
  `RGBToAVC444YUV`), not the current point sample (lowers mean error; not a burr
  cure). No U/V swap (ours already matches; FreeRDP's Split-path swap is a
  FreeRDP defect, correctly not reproduced).
- Colorspace: our converter + ffmpeg flags use BT.709 full range but FreeRDP's
  `YUV444ToRGB` is hard-coded BT.601 full range (`prim_internal.h:229`), causing
  a mild desaturation/hue shift. Match BT.601 for color accuracy.

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

### Resize / 16-alignment lifecycle — TESTED (2026-07-14)

Added coverage for the client-resize path that recycles the ffmpeg child at
new 16-aligned coded dimensions (task: "resize to odd width and height"):

- **Converter alignment (deterministic, always runs):**
  `tests/xrdp/test_avc444_convert.c` — `test_avc444_odd_dims_alignment`
  (odd visible sizes 1/15/17/1281×721/1366×769 round coded dims up to the next
  multiple of 16; nv12_size matches) and `test_avc444_odd_padding_edge_replicated`
  (1281×721 → 1296×736: padding columns/rows edge-replicate the last real
  pixel, so the encoder never reads uninitialized memory).
- **ffmpeg recycle lifecycle (gated on `XRDP_TEST_FFMPEG_PATH`):**
  `test_avc444_ffmpeg.c` — `test_ffmpeg_resize_recycle` walks even→odd→odd→odd→
  even sizes, recreating the child each time exactly as
  `gfx_wiretosurface1_avc444()` does on a size change; each generation spawns at
  the correct 16-aligned coded dims, encodes, and is fully reaped before the
  next spawns (N resizes leak no processes/fds). `make check` green (50 xrdp
  tests with a real ffmpeg; the two ffmpeg tests skip without one).
- **Live on-box validation (2026-07-14):** `xfreerdp3 /dynamic-resolution
  /gfx:AVC444`, resized mid-session via `xdotool windowsize` to an odd target.
  Server aligns the desktop to even and the encoder to /16: 900×605 → desktop
  900×604 → ffmpeg `coded 912x608 generation 1` (old child reaped, new pid
  spawned); post-resize XFCE desktop renders correctly with correct colors.
  Log evidence: two distinct `spawned ffmpeg pid … coded WxH` lines per resize.

### CRITICAL FINDING refined — the latency is x264 encoder-side, `-tune zerolatency` fixes it

Offline repro `tests/xrdp/avc444/repro_ffmpeg_latency.py` + writeup
`FINDINGS_ffmpeg_latency.md` pin the root cause definitively (measured, not
recalled). Feeding one NV12 frame at a time to a persistent pipe:
- **production (no tune):** per-frame output `[304,0,0,…]`, i.e. only the NUT
  header, then nothing — all encoded pictures withheld until EOF.
- **`-tune zerolatency`:** per-frame `[3967,337,193,…]`, tail-after-EOF 5092→26
  bytes — one encoded picture out per input frame.

So the withholding is **entirely x264 encoder-side output delay**, not ffmpeg
input/AVIO/demux/probe buffering, and it has two additive parts: (1) lookahead
/ B-frame reordering (`rc-lookahead`, `sync-lookahead`, bframes) and (2)
threaded frame-parallelism (~threads−1 frames; `-threads 1` alone doesn't fix
it, but zerolatency's sliced-threads does). The prior "low-latency flags break
it" note was about *input-side* flags (`-fflags nobuffer` breaks encode,
`-probesize 32`/`-avioflags direct` corrupt the demuxer) — the repro reproduces
those failures too. **Consequence:** adding `-tune zerolatency` to the encoder
argv would allow the simpler synchronous write-pair/read-pair model and remove
the ~1-update pipeline lag; it is also the correct tune for interactive remote
desktop (the in-tree x264 GFX path already uses it). Deferred as a follow-up to
validate on-screen with the harness rather than change the working runner blind.

### `-tune zerolatency` — now a config default (deployed + re-validated 2026-07-14)

`-tune zerolatency` was already present in the encoder argv (hard-coded literal
since 13f59ae8, alongside `-preset ultrafast -bf 0`), so the deployed child was
already streaming per-frame. This change makes it a **real config knob** rather
than a magic string:
- `struct xrdp_ffmpeg_avc444_config` gains `char tune[16]`;
  `xrdp_ffmpeg_avc444_config_default()` sets it to `"zerolatency"`; `build_argv`
  emits `-tune <tune>` only when non-empty (set `tune = ""` to omit it).
- `gfx.toml [avc444_ffmpeg] tune` parsed in `xrdp_tconfig.c`
  (`avc444_ffmpeg_tune`, default `"zerolatency"`), plumbed through
  `xrdp_encoder` (create + `gfx_wiretosurface1_avc444`) and the
  `xrdp_mm_egfx_caps_advertise()` probe so both probe and live child use it.
- Tests: `test_tconfig.c` gains `…avc444_ffmpeg_defaults` (no table → default
  zerolatency) and `…avc444_ffmpeg_override` (explicit table overrides every
  field incl. tune) with stub `gfx/gfx_avc444_ffmpeg.toml`. `make check` = 52/52.
- Deploy + re-validate: rebuilt `/usr/sbin/xrdp`, restarted, reconnected
  `/gfx:AVC444`. Live child argv (`/proc/<pid>/cmdline`) shows
  `… -preset ultrafast -tune zerolatency -crf 18 -g 240 …`; full XFCE desktop
  renders with correct colors; child reaped on disconnect (no leak). Deployed
  `/etc/xrdp/gfx.toml [avc444_ffmpeg]` now carries `tune = "zerolatency"`.

This does not by itself remove the pipelined runner's ~1-update lag — that is a
separate follow-up (make the runner return the just-submitted pair now that
output is reliably per-frame). The tune is the enabler; the runner change is
not yet done.

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
