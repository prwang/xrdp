# Slice #219 — AVC420 and AVC444 wire serialization

## Commit boundary

This xrdp-only commit serializes completed internal transactions. It does not
advertise the backend.

Target files are serializer portions of `xrdp/xrdp_encoder.c`,
`xrdp/xrdp_encoder.h`, and `tests/xrdp/test_avc444_metablock.c`.
Register the suite in `tests/xrdp/Makefile.am`,
`tests/xrdp/test_xrdp.h` and `tests/xrdp/test_xrdp_main.c`.

## Requirements

* S219-R1: AVC420 shall produce the existing RDPGFX AVC420 metadata and one
  validated H.264 bitstream for its visible region.
* S219-R2: an AVC444 frame shall be serialized as one luma PDU with LC=1 and,
  when auxiliary output is due, one following chroma PDU with LC=2. Both PDUs
  shall carry the same frame/surface identity and ordered region set.
* S219-R3: LC=0 shall not be serialized or admitted to clean history. A reset
  starts with a validated LC=1 main reset; auxiliary state is joined without
  creating an LC=0 reference relationship.
* S219-R4: visible dirty rectangles shall map to metablock rectangles with
  even origin and even extent, clipped to coded bounds. Empty output shall be
  omitted; unions and counts shall be overflow-checked.
* S219-R5: serializer capacity shall be checked before each header, rectangle
  and payload. Oversize output shall fail without emitting a partial command.
* S219-R6: codec IDs shall match #216 and LC selection. Region order shall be
  deterministic across monitors and input list ordering.

## Required tests and gate

The six `Avc444Metablock` golden cases shall cover even and odd origins,
mixed/edge rectangles, LC=1 luma bytes, LC=2 chroma bytes and a single-region
command. Add two required cases for LC=0 rejection and insufficient output
capacity. Run
`CK_RUN_SUITE=Avc444Metablock tests/xrdp/test_xrdp`, then the README gate.
