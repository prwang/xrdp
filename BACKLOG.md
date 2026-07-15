# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

Only **upcoming** work lives here. Completed work is recorded in `PRD.md` §25
("Delivered"), with detailed root-cause writeups under `tests/xrdp/avc444/`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

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
