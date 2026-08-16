# Slice #223 — One-thread multi-monitor pump set

## Commit boundary

This xrdp-only commit generalizes the encoder worker to all active monitors
and assembles ready output inline. It shall not create an emit thread.

Target files are multi-monitor portions of `xrdp/xrdp_encoder.c`,
`xrdp/xrdp_encoder.h`, `xrdp/xrdp_encoder_ffmpeg.c`,
`xrdp/xrdp_encoder_ffmpeg.h`, `xrdp/xrdp_mm.c`, `xrdp/xrdp.h`,
`tests/xrdp/test_avc444_multimon.c`,
`tests/xrdp/test_avc444_emit_split.c`,
`tests/xrdp/test_avc444_ffmpeg.c`, and
`tests/xrdp/test_xrdp_egfx.c`. Register `Avc444EmitSplit` in
`tests/xrdp/Makefile.am`, `tests/xrdp/test_xrdp.h` and
`tests/xrdp/test_xrdp_main.c`.

## Requirements

* S223-R1: each monitor shall own its geometry, main/aux children, parsers,
  frame identity, LTR state, pending input and completed output. No state may
  be indexed only by global frame order.
* S223-R2: one worker shall build one poll set containing all live child input
  and output descriptors and pump them under one monotonic batch deadline.
  A blocked child shall not prevent service of a ready child.
* S223-R3: completion shall be matched by monitor, view and submission
  identity. A monitor pair becomes publishable only when its required views
  are complete and valid.
* S223-R4: ready monitors shall be assembled inline into one EGFX batch in
  deterministic monitor order. Assembly shall not mutate encoder ownership,
  advance LTR state, acknowledge capture or perform normal per-frame logging.
* S223-R5: sequence zero, an unarmed monitor, a failed submission and a
  teardown/restart shall clear stale readiness and shall not publish old
  bytes.
* S223-R6: failure of one monitor shall return/preserve that monitor's damage
  and tear down its process state without aliasing another monitor's buffers.
* S223-R7: trace-enabled builds shall bracket submit (`subm_beg`, `subm_end`),
  polling (`pump_beg`, `pump_end`), bookkeeping (`book_beg`, `book_end`),
  per-monitor collection (`coll_beg`, `coll_end`), release (`rel_beg`,
  `rel_end`) and inline serialization (`emit_beg`, `emit_end`). `batch` shall
  carry cycle, armed-monitor/view counts and result. Schema-1 fields are
  `subm_beg`: `set_n`; `subm_end`: `n_handles`; pump begin/end: `n_handles`,
  `kids_armed`, `monitor_mask`, `credit_mask`, `credit`; book begin/end:
  `n_handles`; `batch`: `cycle`, `set_n`, `monitors_armed`, `kids_armed`,
  `max_kids`, `rv`; collection: `monitor`; release: `set_n`; emission:
  `frame_id`, `monitor`. `batch` carries static `class=GFX_TRACE`.

## Required tests and gate

Enable the five `Avc444EmitSplit` publication-state tests and
`test_ffmpeg_pump_set_four_views_one_thread`. `Avc444Multimon` and EGFX tests
shall cover unequal monitor geometry, independent identity/LTR state,
deterministic batch order, one-child stall, failure isolation and two-monitor
resize. Run all four targeted suites, then the README gate.
