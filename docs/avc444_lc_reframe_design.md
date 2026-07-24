# AVC444 LC=1/LC=2 temporal reframing — ground truth + fix design

## Problem
The macOS Windows App (Apple VideoToolbox) renders a **black screen** on xrdp's
AVC444 stream, while mstsc and UWP render it fine. AVC420 (single-stream) renders
everywhere including the Mac.

## Ground truth (measured, not assumed)
Owner provided a real Windows host that emits real AVC444 auxiliary chroma:
Windows Server 2022 (build 20348) + NVIDIA A10-4Q vGPU (hardware NVENC), with
`AVC444ModePreferred=1` + `AVCHardwareEncodePreferred=1`. Captured 803 AVC444
frames with a patched FreeRDP rdpgfx wire dumper across two chroma-rich payloads.

**The macOS Windows App rendered that real Windows session cleanly for 2 minutes**
— a session that carries frequent `LC=2` auxiliary-chroma frames and rare `LC=0`.
This **refutes** "the Mac can't do AVC444 aux/`LC=0`" and **confirms** the defect
is in xrdp's emission, not the client.

### What real Windows emits (AVC444v2, `0x000F`)
- **Bootstraps luma-only**: first frame is `LC=1` with an IDR (`[AUD,SPS,PPS,IDR]`);
  the chroma/aux view is not initialized at connect.
- **Cadence**: `LC=1` ~93%, `LC=2` ~7%, `LC=0` ~0.25% (2/803).
- **Aux is ALWAYS `[AUD,P,...]`** — 58/58 aux instances; never its own IDR/SPS.
  Exactly one IDR/SPS/PPS in the whole session. One shared H.264 decode context;
  the aux "view" is temporally interleaved as ordinary P-frames, routed to
  main-vs-aux by the `LC` field.
- **`LC=0` streams tile DISJOINT rects** (main = new-content tile, aux =
  complementary catch-up tiles). Windows never emits a same-region `LC=0`.

### What xrdp emits today (`xrdp_encoder.c`)
- **`LC=0` on every frame, hardcoded** (serializer at `gfx_wiretosurface1_avc444`
  / `avc444_serialize_*`), both sub-streams covering the **same** full region.
- **First frame is `LC=0` dual-stream**: main IDR + aux **P-slice** in one PDU,
  same region — a construction real Windows never produces. Apple VideoToolbox
  (stricter than mstsc/UWP's DXVA) rejects it → black.

## Fix: reframe the existing pair as LC=1 then LC=2 (same bytes, same traffic)
xrdp already produces the two H.264 sub-bitstreams (main YUV420 + aux chroma),
interleaved into one continuous stream with monotonic `frame_num`
(IDR main → P aux → P main → P aux …). The fix changes only the **RDPGFX wire
framing**, not the codec bytes:

    today:     frame N -> one PDU  [ LC=0 | main | aux ]   (same region)
    proposed:  frame N -> PDU A    [ LC=1 | main ]
                          PDU B    [ LC=2 | aux  ]

This satisfies every observed Windows invariant while keeping chroma **dense**
(refreshed every frame — we are not implementing Windows' sparse `LC=2` heuristic,
only its semantic structure):
- First PDU is `LC=1` carrying the main **IDR** → luma-first bootstrap.
- Aux is always an `LC=2` P-slice → never inline, never same-region `LC=0`.
- Decode order unchanged (`main[N]` then `aux[N]`) → byte-identical decode,
  references resolve (main[N] is in the DPB before aux[N]).
- Same total traffic — a pure re-framing, not deferral/drop.

The design's one dependency — that the client keeps H.264 decoder state across
separate `LC=1`/`LC=2` PDUs — is **proven** by the 2-min Mac render of the real
Windows session (which does exactly that).

### Wire layout reused (MS-RDPEGFX 2.2.4.5 / 2.2.4.4)
`RFX_AVC444_BITMAP_STREAM` = `avc420EncodedBitstreamInfo` (UINT32: bits0-29 =
`cbAvc420EncodedBitstream1`, bits30-31 = `LC`) + sub-stream(s):
- `LC=1`: `cb1` = len(bitstream1); only bitstream1 (main) follows.
- `LC=2`: `cb1` = 0; only bitstream2 (aux) follows.
Each sub-stream is a self-contained `RFX_AVC420_BITMAP_STREAM` (numRegionRects,
rects, quantQualityVals, then the H.264 Annex-B bytes) — identical to the two
halves we already serialize inside the `LC=0` PDU.

### Scope
- `xrdp/xrdp_encoder.c`: split the single `LC=0` `wiretosurface1` emission into
  two emissions (`LC=1` main, then `LC=2` aux), reusing the existing
  metablock/bitstream serialization for each half. Keep codec id `0x000F` (v2)
  when the client advertised v2; fall back to `0x000E` (v1) otherwise.
- Unit test: extend the AVC444 serializer test to assert the two-PDU
  (`LC=1`+`LC=2`) layout and header words, replacing/augmenting the `LC=0` case.
- No change to the ffmpeg converter or encoder; the H.264 pair is unchanged.

### Validation
Backstop: in-tree serializer unit test (`tests/`). End-to-end: mstsc/UWP must
still render (no regression), and the decisive check is a macOS Windows App render
of the reframed stream. Smoke-gate per `PR-demo/tail_flush_ab/smoke.sh` at
multiple session sizes before any handoff.

Local (uncommitted) artifacts: `vm/gfxwin_anim`, `vm/gfxwin_scroll` (real-Windows
dumps), `vm/gfxdump_*` (our dumps), `vm/parse444.py`, `vm/scan444.py`,
`vm/GROUND_TRUTH_win2022_avc444.md`.
