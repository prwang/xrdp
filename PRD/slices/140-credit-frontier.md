# Slice #140 — Paired credit frontier and split acknowledgement

## Commit boundary

This paired commit separates capture-slot ownership from visible-region
completion and enforces bounded client wire credit.

Target files are xrdp `common/xup_client_info.h`, `common/trans.c`,
`common/trans.h`, `xup/xup.c`, `xrdp/xrdp_encoder.c`,
`xrdp/xrdp_encoder.h`, `xrdp/xrdp_mm.c`, `xrdp/xrdp_types.h`,
`tests/xrdp/test_avc444_credit_frontier.c`,
`tests/xrdp/test_avc444_multimon.c`, `tests/xrdp/test_xrdp_egfx.c`,
`tests/common/test_trans.c`, `tests/common/Makefile.am`,
`tests/common/test_common.h`, `tests/common/test_common_main.c`; and
xorgxrdp `module/rdpClientCon.c`, `module/rdpClientCon.h`.
Register `Avc444CreditFrontier` in `tests/xrdp/Makefile.am`,
`tests/xrdp/test_xrdp.h` and `tests/xrdp/test_xrdp_main.c`.

## Requirements

* S140-R1: admission shall be computed per monitor from a free local capture
  slot and global wire credit. The permitted-monitor mask shall name exactly
  the monitors satisfying both conditions.
* S140-R2: after xrdp has consumed a stable capture slot, it may send a
  slot-only acknowledgement carrying the exact monitor/frame identity. This
  releases storage but shall not retire the producer's dirty region.
* S140-R3: a separate cumulative monotonic region frontier shall advance only
  for frames actually covered by client acknowledgement. Gaps shall hold the
  frontier; duplicate or stale acknowledgements shall not move it backward.
* S140-R4: dropped, failed or unencoded regions shall be returned to dirty
  state. A slot-only acknowledgement shall not make lost pixels clean.
* S140-R5: client wire credit `C` is the configured `wire_window`; the shipped
  value is 1. With two slots per monitor, total outstanding work shall never
  exceed `C + 2M`. A frozen client eventually closes admission without
  overwriting live state.
* S140-R6: the old combined acknowledgement behavior shall remain for capture
  codes not using this contract. `wire_window` has no effect when eager slot
  acknowledgement is disabled.
* S140-R7: transport queue counters used to explain this frontier are trace
  fields only. Their disabled build shall add no reads or branches.
  Maintaining the byte count shall not weaken the pinned base's
  overflow-safe `trans_force_read_s()` validation or change send semantics.
  The `trans::wait_bytes` field and its increment/decrement operations shall
  be conditionally absent when performance trace is disabled.
* S140-R8: the paired message format shall be versioned and bounds checked;
  unknown acknowledgement flags or monitor identities shall be rejected.
* S140-R9: trace-enabled builds shall record `submit`/`absorb` by frame and
  monitor; `msgin` by message identity/bytes; `send` by transport fragment
  with server/client frontiers, bytes, terminal-fragment field and in-flight
  count; `egress` by
  identity, shown state, queued bytes and client frontier; and frontier `ack`
  by acknowledgement kind and all slot/region/client frontiers. Client
  decode `ack` shall carry frame identity, queue depth, decoded count, server
  frontier and acknowledgement offset. Pump events shall include
  permitted-monitor mask and remaining credit. Default builds shall not read
  trace-only transport counters.
  Frontier `ack`, `submit`, `absorb`, `egress` and `msgin` carry static
  `class=ACK_TRACE`; client decode `ack` and `send` carry static
  `class=GFX_TRACE`. A `send` record is transport-fragment evidence and shall
  not be used as the current video-command identity; #141 supplies that
  identity at command construction.

## Required tests and gate

All 13 `Avc444CreditFrontier` cases shall pass: each binding term,
monotonicity, region/window relation, bounded lag, shipped gate, earned
credit, joint enumeration, both wedge replays, producer contract, frozen
client and permitted mask. `Avc444Multimon` shall cover echo identity,
dropped-region return, flag serialization, slot-only hold, cumulative
frontier, overlap restoration and bufferbloat visibility. Add paired producer
tests for masks and both acknowledgement meanings. `TransWaitBytes` shall
cover append, partial drain, complete drain and multiple queued streams in the
enabled build; the disabled footprint test shall prove the field and updates
are absent. Run the targeted suites, both paired builds and the README gate.
