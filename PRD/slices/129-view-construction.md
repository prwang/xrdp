# Slice #129 — Full-chroma views and producer packing

## Commit boundary

This paired commit defines the complete pixel mapping, dimensions and packed
shared-memory representation. It remains unadvertised.

Target files are xrdp `xrdp/xrdp_avc444_convert.c`,
`xrdp/xrdp_avc444_convert.h`, `tests/xrdp/test_avc444_convert.c`; and
xorgxrdp `module/rdpCapture.c`, `module/rdpClientCon.c`,
`module/rdpYuvVectorize.h`, `tests/yuv444/test_yuv444_pack.c` and
`tests/yuv444/Makefile.am`. Register them in xrdp `xrdp/Makefile.am`,
`tests/xrdp/Makefile.am`, `tests/xrdp/test_xrdp.h`,
`tests/xrdp/test_xrdp_main.c`; and xorgxrdp `configure.ac` and
`tests/Makefile.am`.

## Requirements

* S129-R1: XRGB8888 shall convert to full-range BT.709 Y, Cb and Cr with
  deterministic integer rounding and saturation.
* S129-R2: AVC444v1 and AVC444v2 main/auxiliary views shall implement the
  Microsoft reconstruction mapping. Main-only mode shall produce conventional
  full-range BT.709 NV12 and intentionally lose isoluminant chroma detail.
* S129-R3: coded width shall round up to 16 or 32 as selected; coded height
  shall round up to 16. Plane strides and offsets shall obey #128.
* S129-R4: odd visible sizes and dirty rectangles shall not read outside the
  source. Right and bottom padding shall replicate the last visible sample.
* S129-R5: the producer's scalar and vectorized paths shall be byte-identical.
  Loops shall have explicit non-aliasing/stride preconditions and a scalar
  remainder; alignment shall never be assumed from client geometry.
* S129-R6: xrdp's independent converter is a test oracle and standalone
  utility, not a second conversion or fallback on the shipped packed-capture
  path.

## Required tests and gate

`tests/xrdp/test_avc444_convert.c` shall contain the eleven specification
vectors for color primaries, exact round-trip, v1/v2 packing, default mapping,
main-only mode, isoluminant loss, coded dimensions, odd sizes, edge padding,
both width alignments and nonaligned resize strides. Run
`CK_RUN_SUITE=Avc444Convert tests/xrdp/test_xrdp`.

The xorgxrdp test shall feed independently specified XRGB rows through scalar
and vector packing, compare both with golden packed views, and exercise a
non-vector tail. Then run the paired README gate.
