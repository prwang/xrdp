# Slice #212 — Paired AVC capture and diagnostic wire contract

## Commit boundary

This paired commit defines, but does not activate, the full-chroma capture
contract and trace timestamp transport.

Target files are xrdp `common/xrdp_client_info.h`,
`common/xup_client_info.h`, `xup/xup.c`,
`tests/xrdp/test_avc444_multimon.c`; and xorgxrdp
`configure.ac`, `module/Makefile.am`, `module/rdpClientCon.c`,
`module/rdpClientCon.h`.

## Requirements

* S212-R1: `CC_GFX_AVC444` shall identify packed full-chroma capture and shall
  carry capture format, width alignment, monitor count, visible geometry,
  region offsets and region sizes.
* S212-R2: the format shall distinguish AVC444v1, AVC444v2 and main-only
  full-range BT.709 NV12. Unknown values shall be rejected.
* S212-R3: the structure shall be fixed-width, versioned and checked for exact
  agreement before either peer uses offsets or monitor metadata.
* S212-R4: monitor count and every dimension, stride, offset and length shall
  be range- and overflow-checked. Regions shall be ordered, disjoint and
  contained in the mapped allocation. At most 16 monitors are accepted; a
  capture width or height above 16384 is rejected. At most 15 dirty rectangles
  are transported per monitor; a more complex region is coalesced to its
  checked extent.
* S212-R5: in a paired trace-enabled build, the per-frame paint message shall
  append capture-begin, packing-complete and send timestamps from
  `CLOCK_MONOTONIC`, plus monitor/frame identity and the producer's slot and
  displayed-region acknowledgement frontiers. The exact extension and
  trace-build bit shall participate in the version agreement. xorgxrdp shall
  expose the matching `--enable-perf-trace` configure option, disabled by
  default. In that build `XRDP_ACK_TRACE=1` arms the timestamp reads; without
  it the fields are zero and the producer does not read the clock.
* S212-R6: a default build shall contain no timestamp fields in the paint
  message and shall perform no trace-only clock read. Mixed trace modes shall
  fail version agreement rather than parse different layouts.
* S212-R7: the consumer shall emit the transported values as the `capture`
  event with `frame_id`, `monitor`, `begin_ns`, `packed_ns`, `sent_ns`,
  `slot_ack` and `region_ack`, and static `class=ACK_TRACE` through the one
  #211 sink. The producer shall not write a per-frame normal log.
* S212-R8: no capability response or encoder dispatch shall select this
  capture code in this commit.

## Required tests and gate

`tests/xrdp/test_avc444_multimon.c` shall cover exact serialization, version
mismatch, all three formats, monitor limits, invalid and overflowing
geometry, disjoint containment, exact trace extension and rejection of mixed
trace-build contracts.
Run `CK_RUN_SUITE=Avc444Multimon tests/xrdp/test_xrdp`, then both trace modes
and the paired README gate.
