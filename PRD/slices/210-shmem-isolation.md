# Slice #210 — Paired H.264 shared-memory isolation

## Commit boundary

This paired commit fixes an already reachable AVC420 defect independently of
the new backend. It changes the capture layout only for `CC_GFX_A2`; no
AVC444 capability, process or configuration is introduced.

Target files are xrdp `common/xup_client_info.h`,
`xrdp/xrdp_encoder.c`, `tests/xrdp/test_avc444_multimon.c`; and xorgxrdp
`module/rdpCapture.c`, `module/rdpClientCon.c`,
`module/rdpClientCon.h`. Register the new xrdp suite in
`tests/xrdp/Makefile.am`, `tests/xrdp/test_xrdp.h` and
`tests/xrdp/test_xrdp_main.c`.

## Requirements

* S210-R1: each active monitor shall receive a disjoint, page-aligned NV12
  region computed from that monitor's coded dimensions.
* S210-R2: layout arithmetic shall reject invalid counts, dimensions,
  overflow and aggregate sizes that cannot be represented by the transport.
* S210-R3: both peers shall use one bumped exact structure version. A peer
  with the old version shall fail before dereferencing the new layout.
* S210-R4: the one-monitor layout and existing encoder output shall remain
  byte-compatible apart from the new region base calculation.
* S210-R5: no monitor may overwrite another monitor's plane while captures
  are outstanding.

## Required tests and gate

`tests/xrdp/test_avc444_multimon.c` shall independently cover the no-monitor
session fallback, one monitor, two unequal monitors, page alignment,
degenerate input rejection, disjoint ranges and overflow. Run
`CK_RUN_SUITE=Avc444Multimon tests/xrdp/test_xrdp`, then the README gate in
both repositories. This commit is invalid if it contains any AVC444
activation or if either repository builds against the other wire version.
