# Slice #128 — Paired AVC capture wire contract

## Commit boundary

This paired commit defines, but does not activate, the full-chroma capture
contract.

Target files are xrdp `common/xrdp_client_info.h`,
`common/xup_client_info.h`, `xup/xup.c`,
`tests/xrdp/test_avc444_multimon.c`; and xorgxrdp
`module/rdpClientCon.c`, `module/rdpClientCon.h`.

## Requirements

* S128-R1: `CC_GFX_AVC444` shall identify packed full-chroma capture and shall
  carry capture format, width alignment, monitor count, visible geometry,
  region offsets and region sizes.
* S128-R2: the format shall distinguish AVC444v1, AVC444v2 and main-only
  full-range BT.709 NV12. Unknown values shall be rejected.
* S128-R3: the structure shall be fixed-width, versioned and checked for exact
  agreement before either peer uses offsets or monitor metadata.
* S128-R4: monitor count and every dimension, stride, offset and length shall
  be range- and overflow-checked. Regions shall be ordered, disjoint and
  contained in the mapped allocation. At most 16 monitors are accepted; a
  capture width or height above 16384 is rejected. At most 15 dirty rectangles
  are transported per monitor; a more complex region is coalesced to its
  checked extent.
* S128-R5: the paired producer shall not read a diagnostic clock, emit a
  per-frame normal log or append trace-only timestamps to the xup contract.
  Performance characterization uses xrdp's receive, encode and egress
  brackets with explicit frame and monitor identities.
* S128-R6: no capability response or encoder dispatch shall select this
  capture code in this commit.

## Required tests and gate

`tests/xrdp/test_avc444_multimon.c` shall cover exact serialization, version
mismatch, all three formats, monitor limits, invalid and overflowing
geometry and disjoint containment. Run
`CK_RUN_SUITE=Avc444Multimon tests/xrdp/test_xrdp` and the paired README gate.
