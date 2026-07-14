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

Mechanism: AVC444 **v1** (LC=0) does not transmit every chroma sample. The
decoder reconstructs the missing (even,even) chroma by extrapolation —
`avg = 4*U00 - U01 - U10 - U11`, then `CONDITIONAL_CLIP(avg, U00)` (uses the
extrapolated value when it differs from the stored sample by >= 30, else the
stored sample). See FreeRDP `libfreerdp/primitives/prim_internal.h:215` and
`sse/prim_YUV_sse4.1.c:212`. At a sharp saturated green/black text edge this
extrapolation overshoots past neutral into the complementary hue (green's
opposite is magenta), producing the fringe.

## The fix — emit AVC444 **v2** (LC=1, ChromaV2)

AVC444 v2 transmits the actual sampled chroma for every position (no
extrapolation), so the decoder does not overshoot:

| encode→FreeRDP-decode (192x64 green-text crop) | burr px | mean abs diff |
|---|---|---|
| v1 `RGBToAVC444YUV` → ChromaV1 combine | 100 | 3.71 |
| **v2 `RGBToAVC444YUVv2` → ChromaV2 combine** | **0** | **0.87** |

v2 is visibly clean (no magenta) and ~4x more accurate. Reference: v2 aux
packing in FreeRDP `prim_YUV.c:172` (`general_ChromaV2ToYUV444`) /
`sse/prim_YUV_sse4.1.c:1579`; v2 encoder `prim_YUV.c:2149,2304`. Spec:
MS-RDPEGFX 3.3.8.3.3 (YUV420 combination for YUV444v2).

Emitting v2 is a real feature: advertise/emit LC=1 and produce the ChromaV2
auxiliary packing, gated on the client advertising v2 support (fall back to v1
otherwise). Tracked in BACKLOG.md.

## Secondary correctness notes (not the burr cure)

- Our v1 main-view chroma is a **point sample**; FreeRDP's canonical
  `RGBToAVC444YUV` uses the **2x2 average** (no U/V swap). Switching to the
  average lowers mean error (4.19→3.71) but does NOT remove the v1 burr.
- Colorspace: our converter and the ffmpeg flags use **BT.709 full range**, but
  FreeRDP's `YUV444ToRGB` is hard-coded **BT.601 full range**
  (`prim_internal.h:229`). This causes a mild desaturation/hue shift
  (e.g. green (63,192,63) decodes to (100,175,100)) independent of the burr.
  Matching BT.601 would improve color accuracy.

## Repro assets

`repro/` scripts are kept out-of-tree (they depend on a local FreeRDP checkout
and its shared libraries). The faithful decoder harness links FreeRDP's real
`primitives_get()` SSE4.1 path; the packer mirrors `xrdp_avc444_convert.c`.
