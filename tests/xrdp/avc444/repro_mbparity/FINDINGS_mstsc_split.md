# AVC444 v2 "resize comb" burr — mstsc chroma split alignment (CONFIRMED)

Confirmed root cause of the resize-triggered comb burr that appears on **mstsc**
(not FreeRDP). Verified against live captured data (user visually approved the
reproduction `conv44_mstsc` matches their screen).

## Root cause

The ChromaV2 aux view packs the aux "luma" plane as `[ U-half | V-half ]`, split
at `packedWidth / 2`. xrdp packs at `packedWidth = round_up_16(width)`
(`xrdp_avc444_convert.c`). The decoder must read the V-half from the same split.

- **FreeRDP** derives the split from `round_up_16(width)` (prim_YUV.c /
  yuv.c: `alignedWidth = round_up_16`), so it matches our packing — always clean.
- **mstsc** derives it from `round_up_32(width)` (32-aligned). When
  `round_up_16(width)` is NOT already 32-aligned — i.e. the **coded macroblock
  count is odd**, `(round_up_16(width)/16) % 2 == 1` — mstsc's split is 16 screen
  pixels past ours. Every odd-column V sample is read 16px displaced ->
  - a **period-2 comb ~16px LEFT of high-contrast vertical edges**, and
  - the tail reads run into the padding -> a **variable-width right-edge strip**
    (width tracks `round_up_32 - round_up_16` at that width).

Height is irrelevant (the split is purely horizontal). Failure rate ~1/2 because
odd vs even coded-MB-count is a coin flip over widths. Fresh connects were only
"clean" when they happened to land on even-MB widths.

## Reproduction (this directory)

`mstsc_decode.c` is the FreeRDP-faithful Luma + ChromaV2 combine + reverse-filter
RGB decoder, with the ONE knob that matters exposed: the split width
(`nTotalWidth`, argv[6]). `burr_full.c` runs the real converter
(`xrdp_avc444_convert.o`) on an XRGB source and dumps the main/aux NV12.

    # build (needs the converter object + libcommon from a configured tree)
    gcc -O2 -I../../../../xrdp burr_full.c \
        ../../../../xrdp/xrdp_avc444_convert.o \
        ../../../../common/.libs/libcommon.a -o burr_full
    gcc -O2 mstsc_decode.c -o mstsc_decode
    gcc -O2 burr_decode.c  -o burr_decode      # standalone (no split knob)

    # saturated test source at some width W, through the real converter:
    python3 gen_w.py 1776                       # -> /tmp/w.xrgb  (green bars)
    ./burr_full /tmp/w.xrgb 1776 256 /dev/null main.nv12 aux.nv12 dump

    # decode both ways: split at round_up_16 (FreeRDP) vs round_up_32 (mstsc)
    ./mstsc_decode main.nv12 aux.nv12 1776 256 ok.rgb  1776   # clean
    ./mstsc_decode main.nv12 aux.nv12 1776 256 bad.rgb 1792   # BURR

`to_xrgb.py` converts a captured PNG (e.g. an xwd of the live session) to the
B,G,R,X byte layout the converter expects, so a real frame can be run through
the same pipeline.

## Confirming sweep (green bars, decode split16 vs split32)

| width | round_up_16 | coded MB | split16 (FreeRDP) | split32 (mstsc) |
|-------|-------------|----------|-------------------|-----------------|
| 1760  | 1760        | 110 even | clean             | clean           |
| 1776  | 1776        | 111 ODD  | clean             | **BURR**        |
| 1792  | 1792        | 112 even | clean             | clean           |
| 2176  | 2176        | 136 even | clean             | clean           |
| 2184  | 2192        | 137 ODD  | clean             | **BURR**        |
| 2192  | 2192        | 137 ODD  | clean             | **BURR**        |
| 2208  | 2208        | 138 even | clean             | clean           |

Predictor: `hasBurr(mstsc) = (round_up_16(width) / 16) % 2`.

## Fix tension (why a naive round_up_32 is wrong)

Packing at `round_up_32` would fix mstsc but **break FreeRDP symmetrically**
(FreeRDP would then read V 16px short). The split each decoder uses is derived
from the width, and mstsc rounds to 32 while FreeRDP rounds to 16 — they only
agree when the width is **already 32-aligned**. So the correct fixes are:

1. **Force the AVC444 coded/surface width to 32-alignment** (round_up_32), so
   `round_up_16 == round_up_32` and BOTH decoders land on the same split. No
   client detection needed; costs up to 16px of edge-replicated right padding
   (or snap the desktop width to a 32-multiple).
2. Or a **client-type flag / detection** (ini) to choose the packing alignment
   per client (round_up_16 for FreeRDP-like, round_up_32 for mstsc-like).

Any candidate fix must be validated with this harness by decoding at BOTH
split16 and split32 and requiring **both** clean.
