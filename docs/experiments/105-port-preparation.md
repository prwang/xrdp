# #105 — upstream-port preparation decisions

Recorded 2026-08-10 through 2026-08-16. The completed preparation portion
moved from `BACKLOG.md` on 2026-08-16; #105 remains open only as the port
umbrella.

## Decisions already made

* The work is one paired main change, internally divided into bisectable
  commits. The earlier five-PR proposal was withdrawn because its early PRs
  could not stand alone.
* The xrdp cleanup base is pinned to
  `fe850a22c08a624c66bbac07e310251782e6f828`. A later remote advance does not
  silently change it.
* The pinned xrdp base adds incoming-channel dechunking, bounds hardening and a
  dynamic-resize ordering fix. It introduces no breaking AVC API,
  configuration or xup contract. The clean-room port must preserve the new
  resize-before-surface-and-encoder ordering and must not undo the upstream
  dechunker work.
* Standard external perf/trace tooling cannot replace 13 of the 34 semantic
  frame records without forbidden time-window joins. A completed,
  compile-time-opt-in `common/perf_trace` therefore ships in the paired
  change. Ordinary builds must contain no trace source, call, argument
  evaluation, string, state, counter or diagnostic xup payload.
* #107 selected versioned named key/value text produced directly into a Linux
  double-mapped byte ring. Fixed anonymous objects, six positional integers
  and the one-off positional parser are retired.
* The shipped `wire_window` default is 1. Value 2 is documented with its
  measured one-monitor benefit and its additional frame of latency/memory
  exposure, and is raised for maintainer feedback.
* `tail_flush` is not part of the port. The synchronous runner made it inert.
  The explicit fault-injection keys remain dev-only. The semantic perf-trace
  events supersede the old per-frame `XRDP_GFX_TRACE` logger rather than
  preserving logging on the measured path.

## Preparation that was not complete at this record's close

The dev tracer still needed explicit process lifecycle, final drain, complete
atomic state, visible failures, default-off build erasure and the paired
xorgxrdp timestamp bridge. The xorgxrdp base had not been owner-pinned. The
old eleven-commit cleanup branch predated most of the current frontier and was
not a valid starting branch.

Those open actions are now BACKLOG #200 and #201. The actual clean-room
implementation is split into numbered BACKLOG items #210 and above.
