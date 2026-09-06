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
* S134-R3: the first backend failure shall latch a terminal state before
  teardown, return capture ownership or preserve dirty damage according to
  existing semantics, tear down the affected process state and surface one
  error. Every later submit, collect, event or damage entry shall observe the
  latch before child creation and shall not restart the runner. It shall not
  invoke x264/OpenH264 as fallback. The integration remains unselectable here;
  #142 connects this already-tested terminal result to connection hangup.
* S134-R4: the existing x264 and OpenH264 dispatch, queue ownership, surface
  creation and completion behavior shall remain unchanged during normal
  operation. The shared retirement safety requirements below apply to all
  encoders using the worker and its queues.
* S134-R5: there shall be no parser/config key, codec-order value, capability
  response or runtime branch by which a session can select this integration.
* S134-R6: lifecycle calls shall be safe for a never-started, partially
  started, completed and terminally failed context. Terminal notification and
  teardown shall be idempotent.
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
* S134-R8: accepted input shall have an explicit owner independent of queue
  membership. Ownership shall persist while input is queued, held by the
  worker, or referenced by completion fragments, including failure to allocate
  the final completion. Normal terminal completion or retirement shall release
  each input, command, compressed output and capture mapping exactly once.
  Queue removal alone shall not transfer or destroy the last ownership record.
* S134-R9: retirement shall prevent new submissions, request worker shutdown
  and confirm that the worker can no longer access shared state before
  destroying children, buffers, queues, events or synchronization objects.
  Child shutdown shall close descriptors and terminate/reap every child before
  releasing its context. A bounded worker-stop failure shall be terminal:
  retain an explicit owner for the encoder and every enclosing object it can
  still access, prohibit replacement and report failure. Neither session
  cleanup nor module unloading may bypass that ownership. Retained state may
  be reclaimed only after confirmed quiescence or process exit; a timeout is
  never evidence of quiescence. Partial initialization shall use the same
  ownership rules without waiting for a worker which was never started.
* S134-R10: retirement shall discard queued encoded output without sending it.
  Every accepted capture shall receive one terminal ownership disposition.
  Capture-slot reuse and visible-region disposition remain separate: an early
  slot release does not mean the pixels were displayed. Discarded captures
  shall restore their damage under the producer contract, without releasing
  memory before its last reader stops or duplicating terminal disposition.
  Disposition shall preserve capture order where acknowledgements are
  cumulative. Normal completion and retirement shall share input cleanup.

## Required tests and gate

Add deterministic EGFX/encoder tests for an internal AVC420 transaction, a
matched AVC444 transaction, partial-pair suppression, failure ownership and
unchanged legacy dispatch. Inject one child failure, then repeat work and
damage entries; assert one terminal notification, one teardown, zero later
child creations and no codec substitution. The test shall call the internal
seam directly; it shall also prove live capability selection cannot reach it.
Exercise the production retirement path with queued inputs, worker-held input,
multiple completion fragments, a missing final completion, an already released
slot, and partial initialization. Assert exact input/output frees, capture
unmaps and terminal dispositions by identity, with no old output sent.
Hold the worker at a deterministic barrier: a failed bounded stop shall leave
its full ownership graph intact, signal terminal failure and permit no new
encoder. Release the barrier and prove eventual cleanup exactly once. Include
enclosing session destruction in that test, not only the encoder destructor.
Run
`CK_RUN_SUITE=test_xrdp_egfx_base_functions tests/xrdp/test_xrdp`, the legacy
H.264 suites, and the README gate.
