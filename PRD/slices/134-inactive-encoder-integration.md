# Slice #134 — Inactive server encoder integration

## Commit boundary

This commit gives the existing xrdp encoder worker ownership of an internal
external-ffmpeg transaction, but leaves all capability and configuration
entry points disconnected.

Target files are `xrdp/xrdp_encoder.c`, `xrdp/xrdp_encoder.h`,
`xrdp/xrdp_mm.c`, `xrdp/xrdp_types.h`, and
`tests/xrdp/test_xrdp_egfx.c`.

## Requirements

* S134-R1: one worker-owned context shall carry selected mode, visible/coded
  geometry, capture identity, child handles, parser state and output buffers.
  Ownership transfer shall be explicit at submit, consume, complete and free.
* S134-R2: internal AVC420 shall submit the main view to one child. Internal
  AVC444 shall submit matched main and auxiliary views and shall not publish a
  partial pair.
* S134-R3: every failure shall return the capture ownership or preserve dirty
  damage according to existing semantics, tear down the affected process
  state and surface an error. It shall not invoke x264/OpenH264 as fallback.
* S134-R4: the existing x264 and OpenH264 dispatch, queue ownership, surface
  creation and completion behavior shall remain unchanged.
* S134-R5: there shall be no parser/config key, codec-order value, capability
  response or runtime branch by which a session can select this integration.
* S134-R6: lifecycle calls shall be safe for a never-started, partially
  started, completed and failed context.
* S134-R7: trace-enabled builds shall record damage geometry (`dmg`), encoder
  submitted/returned/ready state (`enc`), worker enqueue/take FIFO identity
  (`enq`, `take`) and worker waiting/draining brackets (`wait_beg`,
  `wait_end`, `drain_beg`, `drain_end`). Every frame-bearing event shall carry
  the explicit surface or frame identity used to join it. Schema-1 fields are
  `dmg`: `surface`, `num_rects`, `x1`, `y1`, `x2`, `y2`; `enc`:
  `submitted_seq`, `returned_seq`, `ready`, `inflight`, `center_y`; `enq` and
  `take`: `frame_id`, `fifo_depth`; wait/drain begin: `n_items`, `drain_full`;
  drain end: `n_items`, `drain_full`. `dmg` and `enc` carry static
  `class=GFX_TRACE`; `wait_end` has no dynamic field.

## Required tests and gate

Add deterministic EGFX/encoder tests for an internal AVC420 transaction, a
matched AVC444 transaction, partial-pair suppression, failure ownership and
unchanged legacy dispatch. The test shall call the internal seam directly;
it shall also prove live capability selection cannot reach it. Run
`CK_RUN_SUITE=test_xrdp_egfx_base_functions tests/xrdp/test_xrdp`, the legacy
H.264 suites, and the README gate.
