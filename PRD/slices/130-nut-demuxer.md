# Slice #130 — Bounded standard-NUT demuxer

## Commit boundary

This xrdp-only leaf commit adds a streaming parser for the standard NUT subset
emitted by the supported ffmpeg command. It has no server caller.

Target files are `xrdp/xrdp_nut.c`, `xrdp/xrdp_nut.h`,
`tests/xrdp/test_avc444_nut.c`,
`tests/xrdp/avc444/fixture_4frame.nut`, and
`tests/xrdp/avc444/PROVENANCE.md`.
Register source and suite in `xrdp/Makefile.am`,
`tests/xrdp/Makefile.am`, `tests/xrdp/test_xrdp.h` and
`tests/xrdp/test_xrdp_main.c`.

## Requirements

* S130-R1: the parser shall accept the standard main header, stream header,
  syncpoint, frame-code and frame payload forms required by the fixture. It
  shall operate incrementally at every byte boundary.
* S130-R2: it shall validate start codes, file identifier, variable-length
  integers, header checksum/CRC where present, stream identity and declared
  packet sizes before consuming a payload.
* S130-R3: configured per-packet and aggregate byte ceilings shall be checked
  without overflow. Truncated, malformed, unknown-stream and over-limit input
  shall return a stable error and free partial state. Zero-valued constructor
  limits select defaults of 1 MiB header metadata, 128 MiB per picture and
  256 MiB total buffered input.
* S130-R4: a completed packet shall be returned once, in input order, with no
  bytes from a later packet. Allocation failure shall be explicit.
* S130-R5: the parser shall contain no ffmpeg, libnut or other upstream parser
  source. The fixture provenance and exact generation command shall be kept.

## Required tests and gate

The `Avc444Nut` suite shall have six independent cases: four valid packets,
fragmentation at every byte, every truncation point, bad CRC, bad file ID and
aggregate ceiling. Run `CK_RUN_SUITE=Avc444Nut tests/xrdp/test_xrdp`, prove no
production caller exists, then run the README gate.
