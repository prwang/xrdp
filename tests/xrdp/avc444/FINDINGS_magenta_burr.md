# AVC444 magenta/red burr on colored text — root cause

## Symptom

Client captures (`wierd_red_burr.png`, `red_burr_v2.png`) show magenta/purple
speckles on and around saturated green terminal text. It appears only on
high-contrast colored text/edges, accumulates as text is drawn, and persists.

## How it was reproduced (faithful, deterministic)

The first offline attempt (a hand-written recombine) was NOT faithful and gave a
wrong answer (it blamed x264 chroma quantization; `crf`/`chroma-qp-offset`
tuning did not fix the live artifact). The faithful repro pipeline is:

1. Grab the pristine source framebuffer from the Xorg session (`x11grab :10`) —
   source has zero burr.
2. Pack it with xrdp's real converter object (`xrdp_avc444_convert.o`) into the
   main + aux NV12 the server feeds ffmpeg.
3. Decode with FreeRDP's **actual runtime primitives** — a standalone harness
   that calls `primitives_get()` (selecting the SSE4.1 path on this CPU, exactly
   what `xfreerdp3` v3.15.0 runs): `YUV420CombineToYUV444` (LUMA then CHROMAv1)
   + `YUV444ToRGB_8u_P3AC4R`.

Using the general/C primitives instead of the runtime SSE ones gave a false
"0 burr" — the SSE and C `YUV444ToRGB` differ, so the decoder harness MUST use
`primitives_get()` to match the client. With that fixed, the offline burr count
tracks the live client.

## Root cause — AVC444 **v1** chroma reconstruction in the FreeRDP decoder

The burr is **not** an xrdp packing mistake and **not** encoder quantization:

- It reproduces losslessly (`-qp 0`) and with all-intra (`keyint=1`) — so it is
  neither H.264 quantization nor a P-frame effect.
- It reproduces when **FreeRDP's own** encoder feeds FreeRDP's own decoder:
  - `RGBToYUV444` + `YUV444SplitToYUV420` (v1) → **103 burr**
  - dedicated `RGBToAVC444YUV` (v1, the real-server path) → **100 burr**
  Since the reference encoder+decoder pair burrs on its own, **no server-side v1
  packing can remove it.**

Mechanism: when converting the reconstructed YUV444 buffer to RGB, the decoder
re-derives the (even,even) chroma of every 2x2 block with a reverse filter —
`cand = 4*U00 - U01 - U10 - U11`, then `CONDITIONAL_CLIP(cand, U00)` (uses the
filtered value when it differs from the block's stored sample by >= 30, else the
stored sample). This filter is **always on** (`filter = TRUE`) for AVC444 decode
regardless of v1/v2 — see FreeRDP `libfreerdp/primitives/prim_YUV.c:358`
(`general_YUV444ToRGB_DOUBLE_ROW`) and the byte-identical SSE4.1 path
`sse/prim_YUV_sse4.1.c:212,356` (`calcavg`/`sse41_filter`), with
`CONDITIONAL_CLIP` in `prim_internal.h:215`. (The general/C and SSE4.1 paths use
the same `4*U00 - neighbours` formula; the runtime harness must still use
`primitives_get()` to match the client for the rest of the conversion.)

What differs between v1 and v2 is the *chroma the combine step feeds that
filter*. In **v1** (ChromaV1) the (even,even) chroma is a subsampled main-view
value, so `4*U00 - neighbours` is a genuine extrapolation: at a sharp saturated
green/black text edge it overshoots past neutral into the complementary hue
(green's opposite is magenta), producing the fringe. This is a property of the
v1 chroma transport, not of any one server packing choice — measured through the
faithful SSE decoder (lossless combine, no H.264), every server-side v1 packing
still burrs:

| v1 main (even,even) packing | burr px |
|---|---|
| point sample of (2cx,2cy) (current) | 109 |
| 2x2 block average | 100 |
| pre-distort `4*U00 - neighbours` | 153 |

So the burr cannot be tuned away on the v1 wire; it is inherent to v1.

## The fix — emit AVC444 **v2** (codecId 0x000F, ChromaV2)

Note: v1 vs v2 is selected by the **codecId** in the WireToSurface1 PDU
(`RDPGFX_CODECID_AVC444` = 0x000E vs `RDPGFX_CODECID_AVC444V2` = 0x000F), not by
the stream's LC field. LC (the `op` byte, 0/1/2) only says which sub-streams are
present and is orthogonal — both v1 and v2 use LC=0 (luma stream 1 + chroma
stream 2). FreeRDP `avc444_decompress` keys the ChromaV1/V2 choice purely off
codecId (`libfreerdp/codec/h264.c:646`). So emitting v2 keeps today's LC=0
two-stream framing and only changes the codecId.

AVC444 v2 uses a different chroma transport (ChromaV2): the aux views carry the
odd-column chroma for every row plus the remaining even-row samples, and the
v2 encoder pre-conditions the main-view (even,even) chroma to the block average.
The always-on reverse filter therefore receives consistent data and reconstructs
the true value instead of overshooting, so the fringe disappears:

| encode→FreeRDP-decode (192x64 green-text crop) | burr px | mean abs diff |
|---|---|---|
| v1 `RGBToAVC444YUV` → ChromaV1 combine | 100 | 3.71 |
| **v2 `RGBToAVC444YUVv2` → ChromaV2 combine** | **0** | **0.87** |

v2 is visibly clean (no magenta) and ~4x more accurate. Reference: v2 aux
packing in FreeRDP `prim_YUV.c:172` (`general_ChromaV2ToYUV444`) /
`sse/prim_YUV_sse4.1.c:1579`; v2 encoder `prim_YUV.c:2149,2304`. Spec:
MS-RDPEGFX 3.3.8.3.3 (YUV420 combination for YUV444v2).

Emitting v2 is a real feature: produce the ChromaV2 auxiliary packing and emit
codecId 0x000F (keeping LC=0), gated on the client advertising v2 support via
`RDPGFX_CAPVERSION_101` (fall back to v1 codecId 0x000E otherwise). The ffmpeg
interface is unchanged — the child still receives two NV12 pictures per frame
regardless of v1/v2. Tracked in BACKLOG.md.

## Secondary correctness notes (not the burr cure)

- Our v1 main-view chroma is a **point sample**; FreeRDP's canonical
  `RGBToAVC444YUV` uses the **2x2 average** (no U/V swap). Switching to the
  average lowers mean error (4.19→3.71) but does NOT remove the v1 burr.
- Colorspace: our converter and the ffmpeg flags use **BT.709 full range**, but
  FreeRDP's `YUV444ToRGB` is hard-coded **BT.601 full range**
  (`prim_internal.h:229`). This causes a mild desaturation/hue shift
  (e.g. green (63,192,63) decodes to (100,175,100)) independent of the burr.
  Matching BT.601 would improve color accuracy.

## Independent validation against a real Microsoft RDP server

The offline proof uses FreeRDP's encoder+decoder, so a skeptic could argue the
burr is a FreeRDP artifact rather than a protocol property of AVC444 v1. It can
be cross-checked against a genuine Windows RDP server (the reference AVC444
implementation), which settles whether it is fundamental to v1:

- Codec IDs are distinct on the wire: `RDPGFX_CODECID_AVC444` = 0x000E (v1),
  `RDPGFX_CODECID_AVC444V2` = 0x000F (v2). v1 is implied by advertising
  `RDPGFX_CAPVERSION_10` (0x000A0002); v2 is implied by additionally advertising
  `RDPGFX_CAPVERSION_101` (0x000A0100). MS-RDPEGFX 1.7 / 2.2.2.1.
- The reverse filter with the cutoff-30 threshold is the spec's own optional
  decode step (MS-RDPEGFX 3.3.8.3.2 for v1, 3.3.8.3.3 for v2): "If the reverse
  filter ... changes the value by less than a given threshold, then the
  nonreversed value SHOULD be used ... A cutoff threshold of 30 is used." So the
  overshoot mechanism is protocol-defined, not an xrdp/FreeRDP invention.

Concrete repro (cheapest path):

1. Host: **Windows Server 2022 Evaluation** (180-day, no GPU/CAL/RDSH needed;
   AVC444 works on the plain 2-session admin RDP). A **Server 2016 Evaluation**
   VM is the interesting "v1-era" comparison host.
2. Enable AVC444: `gpedit.msc` -> Computer Config > Admin Templates > Windows
   Components > Remote Desktop Services > RD Session Host > Remote Session
   Environment > **"Prioritize H.264/AVC 444 Graphics mode for Remote Desktop
   connections"** = Enabled; `gpupdate /force`. Confirm on the server via Event
   Viewer -> `RemoteDesktopServices-RdpCoreTS`/Operational -> **Event ID 162**
   (`Avc444FullScreenProfile`).
3. Client + observe negotiated mode (FreeRDP always advertises both v1 and v2 —
   the server picks; there is no client CLI knob for v1-vs-v2):
   ```
   WLOG_LEVEL=TRACE \
   WLOG_FILTER=com.freerdp.channels.rdpgfx.client:DEBUG,com.freerdp.gdi:TRACE \
   xfreerdp3 /v:HOST /u:USER /gfx:AVC444
   ```
   `RecvCapsConfirmPdu: version:` shows the negotiated capability tier; the
   per-frame `codec=RDPGFX_CODECID_AVC444` vs `...AVC444v2` line shows the
   actual mode. Force v1 by masking out CAPVERSION_101 (`/gfx:mask:`) or by
   using the Server 2016 host.
4. Content: full-screen saturated green (#00FF00) thin text on pure black.
   Inspect edges at 4-8x for magenta fringing under a confirmed-v1 frame, then
   under a confirmed-v2 frame.

Falsification: if a **confirmed-v1** frame from real Windows shows **no** fringe
on saturated green text, the "fundamental to v1" claim is wrong (it would then be
FreeRDP-encoder-specific). Caveat: the reverse filter is spec-optional, so a
decoder that skips it will not fringe either — that is why the check must use
FreeRDP, whose decode applies it (`filter = TRUE`, above).

## Repro assets

`repro/` scripts are kept out-of-tree (they depend on a local FreeRDP checkout
and its shared libraries). The faithful decoder harness links FreeRDP's real
`primitives_get()` SSE4.1 path; the packer mirrors `xrdp_avc444_convert.c`.
