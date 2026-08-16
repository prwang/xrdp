# Slice #131 — Annex-B validation and parameter-set policy

## Commit boundary

This xrdp-only leaf commit parses and validates bounded H.264 Annex-B access
units and provides the interoperability transforms required by supported
hardware profiles. It does not implement LTR or auxiliary leaf rewriting.

Target files are `xrdp/xrdp_h264_annexb.c`,
`xrdp/xrdp_h264_annexb.h`, and the base cases in
`tests/xrdp/test_avc444_h264.c`. Register source and suite in
`xrdp/Makefile.am`, `tests/xrdp/Makefile.am`,
`tests/xrdp/test_xrdp.h` and `tests/xrdp/test_xrdp_main.c`.

## Requirements

* S131-R1: accept three- and four-byte Annex-B start codes and enumerate NAL
  units without reading beyond the supplied buffer. Reject empty, truncated,
  forbidden-bit and structurally malformed data.
* S131-R2: a main reset access unit shall have exactly the required SPS, PPS
  and IDR relationship. Missing or conflicting parameter sets and multiple
  pictures in one output packet shall fail.
* S131-R3: auxiliary non-reset output shall contain one permissible picture
  and shall not smuggle parameter sets that conflict with the main stream.
* S131-R4: `sanitize_hrd` shall remove unsupported HRD signalling while
  preserving all unrelated SPS syntax; `strip_pic_struct` shall clear only
  `pic_struct_present_flag`. Each transform shall be idempotent and shall fail
  on a truncated SPS rather than emitting a guessed result.
* S131-R5: all bit readers, Exp-Golomb values, output growth and NAL counts
  shall be bounded. Input and output buffers may alias only where the API
  explicitly permits it.

## Required tests and gate

The base `Avc444H264` cases shall cover reset SPS/PPS/IDR, duplicate SPS,
missing PPS, auxiliary VCL, malformed input, four HRD cases and three
`pic_struct` cases. Run `CK_RUN_SUITE=Avc444H264 tests/xrdp/test_xrdp`, then
the README gate. The four auxiliary-leaf cases belong to #137 and shall not be
enabled here.
