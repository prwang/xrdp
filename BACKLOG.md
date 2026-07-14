# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

Only **upcoming** work lives here. Completed work is recorded in `PRD.md` §25
("Delivered"), with detailed root-cause writeups under `tests/xrdp/avc444/`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

## Hardware H.264 via ffmpeg encoder_args (VAAPI/nvenc/qsv) — TODO (2026-07-14)

**Rationale.** The verbatim `encoder_args` passthrough is meant to express
hardware encoders with **no xrdp code change** — the same win proven for AVC444
v2 (which never touched the ffmpeg interface). VAAPI is the in-scope hardware
case: it emits **H.264**, so the wire codecId (AVC444/AVC444v2 `0x000E/0x000F`)
is unchanged and mstsc/FreeRDP decode it natively. (Contrast H.265 — BLOCKED,
see below.)

**Scope.** No code change for the happy path. Deliver: (1) a VAAPI recipe in
`docs/man/gfx.toml.5.in` (sibling to the nvenc example) and (2) validation +
the deployment prerequisites documented. Candidate config (drop-in):

```
[avc444_ffmpeg]
path = "/usr/bin/ffmpeg"
encoder_args = [
  "-vaapi_device", "/dev/dri/renderD128",
  "-vf", "format=nv12,hwupload",
  "-c:v", "h264_vaapi",
  "-rc_mode", "CQP", "-qp", "20",
  "-bf", "0", "-async_depth", "1",
]
```

**Validated so far (this box, 2026-07-14).** Ran xrdp's *exact* argv contract
(rawvideo NV12 on fd3 → encoder_args → `h264_mp4toannexb`/NUT on pipe:1) with
the block above, and a real-video encode of `/root/test.avi` (848x480 H.264):
- **As root: works.** Clean exit, valid H.264 High, NAL stream `SPS·PPS·SEI·IDR·
  nonIDR…` — satisfies `xrdp_h264_main_reset_ok()` (SPS+PPS+IDR on reset). The
  built-in `xrdp_ffmpeg_avc444_probe()` replicates this spawn+verify at real
  coded dims.
- xrdp's raw-input case needs the **hwupload** form, not the `-hwaccel …
  -hwaccel_output_format vaapi` (decode-side) form — there is no decode step,
  frames are already raw. hwupload tokens all sit **after** `-i`, so they are
  expressible in `encoder_args`; `-vaapi_device` also works post-`-i`. This is
  why the fixed pre-`-i` region (the NV12 input contract) does not block VAAPI.

**Deployment prerequisites (the real gate — not code):**
1. **Render-node permission for the ffmpeg child's uid.** The child is forked
   from the xrdp front-end and inherits its uid/gids (fork path just dup2s fds +
   execve, no setuid — `xrdp_encoder_ffmpeg.c`). `/dev/dri/renderD128` is
   `root:root 0660`; the service user must be in the node's group. Verified the
   fix: `chgrp render /dev/dri/renderD128` + adding the user to `render` flips a
   non-root user from "cannot open" to "can open".
2. **Scrubbed env.** The child gets `PATH=/usr/bin:/bin` only (no `HOME`/`XDG`/
   `LIBVA_*`). libva autodetected the driver fine here, so usually OK; if a GPU
   needs `LIBVA_DRIVER_NAME` there is currently **no way to pass it** — that
   would need a small code change (forward an allowlisted `LIBVA_*` set, or a
   `gfx.toml` env map). Probe first.
3. **`-bf 0 -async_depth 1`** keep the one-in/one-out cadence the pipelined
   runner expects; hardware min encode size is 128x128 (xrdp 16-aligns near
   full-screen, so not an issue in practice).

**OPEN ISSUE / environment caveat (this box).** As the unprivileged session
user (in `render`, node chgrp'd) the encode **hangs in `drm_read`** — the
process opens the render node but GPU submit never completes; only **root**
completes the encode. This is a container GPU-passthrough privilege limit, not
an xrdp or env problem (reproduced regardless of `HOME`). Acceptance therefore
requires validating on a host where the render node is fully usable by the
unprivileged xrdp service user (bare metal or a container with proper GPU
delegation), not just by root.

**Possible follow-up (only if needed): a `hwaccel_args` slot before `-i`.** The
fixed pre-`-i` tokens are the input contract (raw NV12 at runtime-computed coded
dims, BT.709 full range) and must match the converter's bytes exactly, so they
are deliberately not admin-editable. The one thing an encoder choice might
legitimately want there is device init (`-init_hw_device` for QSV, or `-hwaccel`
if compressed input were ever fed). `-vaapi_device` avoids it (post-`-i`), so no
slot is needed today; if it becomes necessary, add a **narrow** second
passthrough injected immediately before `-i`, scoped so it cannot override
`-f`/`-pixel_format`/`-video_size`/`-i` (which would corrupt the contract).

## AVC420 (YUV420, single view) via the external ffmpeg backend — TODO (2026-07-14)

**Rationale.** Route plain AVC420 (RDPGFX codecId `0x000B`) through the same
external-ffmpeg IPC child, so a deployment can drop the linked H.264 library
(x264/OpenH264) entirely and still serve non-AVC444 clients — and inherit the
hardware-encoder passthrough above for 420 too. AVC420 is **AVC444 minus the aux
view**: one YUV420 picture, one H.264 substream, no ChromaV1/V2, no LC word.

**Scope (all subsets/variants of existing AVC444 code — a few hundred lines):**
- **Converter: ~0.** AVC420 needs only the main NV12 view. Better: xorgxrdp
  already delivers native NV12 (`CC_GFX_A2` / `XRDP_nv12_709fr`, the current
  linked path's capture), so the child can be fed that directly and skip the
  RGB→NV12 converter. Decide reuse-main-view vs. feed-native-NV12 (prefer the
  latter — less code, already the format on hand).
- **IPC runner:** an `encode_single()` = `encode_pair()` with the aux half
  removed. Same `pipe:3` input, same NUT demux, same H.264 annexb validator,
  same poll loop (~100–150 lines).
- **Wire serializer:** `gfx_wiretosurface1_avc420` = the avc444 one minus the
  second metablock, second substream, and the LC info word; emit `0x000B`
  (~60 lines).
- **Negotiation:** `XRDP_GFX_AVC420` is already classified in
  `xrdp_avc444_caps.c`; add an `avc420_ffmpeg` selection block in `xrdp_mm.c`
  mirroring the avc444 one, plus a branch in `xrdp_encoder_create` (~40 lines).
- **Tests:** single-view serializer + a caps case. No new IPC contract, demux,
  or validator — all reused.

**Acceptance:** a client offered AVC420 (no AVC444) renders a correct desktop
via the ffmpeg child with `libh264` unloaded; no regression when AVC444 is
available (AVC444 still preferred).

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
