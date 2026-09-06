# Slice #135 — AVC420 and AVC444 wire serialization

## Commit boundary

This xrdp-only commit serializes completed internal transactions. It does not
advertise the backend.

Target files are serializer portions of `xrdp/xrdp_encoder.c`,
`xrdp/xrdp_encoder.h`, and `tests/xrdp/test_avc444_metablock.c`.
Register the suite in `tests/xrdp/Makefile.am`,
`tests/xrdp/test_xrdp.h` and `tests/xrdp/test_xrdp_main.c`.

## Requirements

* S135-R1: AVC420 shall produce the existing RDPGFX AVC420 metadata and one
  validated H.264 bitstream for its visible region.
* S135-R2: an AVC444 frame shall be serialized as one luma PDU with LC=1 and,
  when auxiliary output is due, one following chroma PDU with LC=2. Both PDUs
  shall carry the same frame/surface identity and ordered region set.
* S135-R3: LC=0 shall not be serialized or admitted to clean history. A reset
  starts with a validated LC=1 main reset; auxiliary state is joined without
  creating an LC=0 reference relationship.
* S135-R4: visible dirty rectangles shall map to metablock rectangles in
  visible-destination-relative coordinates. Origins shall round down to even
  coordinates; right/bottom edges shall round up to even coordinates and then
  clip to the visible destination width/height. An odd extent is permitted
  only at an odd visible right/bottom boundary. No region may address coded
  padding outside that destination. The serializer shall receive visible
  bounds separately from coded dimensions; neither padding nor alignment may
  enlarge the outer destination or allocated visible surface. Empty output
  shall be omitted; unions and counts shall be overflow-checked.
* S135-R5: serializer capacity shall be checked before each header, rectangle
  and payload. Oversize output shall fail without emitting a partial command.
* S135-R6: codec IDs shall match #132 and LC selection. Region order shall be
  deterministic across monitors and input list ordering.

## Required tests and gate

The six `Avc444Metablock` golden cases shall cover even and odd origins,
mixed/edge rectangles, LC=1 luma bytes, LC=2 chroma bytes and a single-region
command. Add two required cases for LC=0 rejection and insufficient output
capacity. Both LC=1 and LC=2 shall independently cover full damage at even
sizes, odd width, odd height and both odd dimensions, including a nonzero
outer destination origin. Expected far edges are the visible width/height,
not the padded encoder dimensions. Keep the outer destination unchanged.
Run
`CK_RUN_SUITE=Avc444Metablock tests/xrdp/test_xrdp`, then the README gate.
