# Slice #127 — Compile-time performance trace foundation

## Commit boundary

This commit adds a generic performance telemetry facility. It contains no
AVC event schema and changes no normal-build behavior.

Target files are `configure.ac`, `common/perf_trace.c`,
`common/perf_trace.h`, `xrdp/xrdp_listen.c`, `xrdp/xrdp.c`, and
`tests/common/test_perf_trace.c`. Register source and suite in
`common/Makefile.am`, `tests/common/Makefile.am`,
`tests/common/test_common.h` and `tests/common/test_common_main.c`.
The disabled-footprint script is
`tests/common/test_perf_trace_disabled.sh`.

## Requirements

* S127-R1: configure shall expose `--enable-perf-trace`, disabled by default.
  When it is off, trace macros expand to statements that evaluate none of
  their arguments and no trace implementation is linked.
* S127-R2: disabled binaries and objects shall contain no event names, trace
  path, trace global, trace function or trace-only branch.
* S127-R3: enabled records shall be complete newline-terminated ASCII records
  with `schema=1`, `mono_ns`, `pid`, `tid`, `event` and event-specific named
  signed 64-bit values. Names and format strings are compile-time constants;
  untrusted text is never a format string. The `XRDP_PERF_TRACE` environment
  value names the output prefix and arms only an enabled build.
  The accepted formatter subset shall be documented and reject positional
  conversions, dynamic width/precision, floating-point, pointer and dynamic
  string conversions; event keys and static token values come from the
  literal format, while dynamic fields are bounded integers.
* S127-R4: producers shall use bounded `snprintf`-compatible local formatting
  directly into their own byte ring. The Linux ring shall be double-mapped so
  a wrapped record is contiguous; wrap shall not require a second record copy.
* S127-R5: a producer shall publish only a complete record. Overflow drops a
  whole record and increments a counter; it never exposes a partial line.
  Each ring is 512 KiB, a record is at most 512 bytes including newline, and
  at most eight producer rings may be claimed. Failure to claim a ring is
  counted and reported at shutdown.
* S127-R6: initialization shall be explicit after fork and before measured
  work. The sink owns file I/O. Shutdown shall stop new publication, wake and
  join the sink, drain complete records, report drops/failures once, and be
  idempotent.
  `perf_trace_init()` returns disabled, armed or error explicitly;
  `perf_trace_on()` is then only an atomic state read and shall not call
  `pthread_once`, allocate or open. `perf_trace_close()` transitions armed to
  closing before it waits and leaves a terminal closed state. Initialization
  failure uses a terminal failed state and cannot be retried from an event.
* S127-R7: the output shall use secure exclusive mode-0600 creation, no
  symlink following and close-on-exec. Open/write/drain failure shall disable
  tracing safely and remain observable at human rate.
* S127-R8: no event call may allocate, lock a global mutex, perform file I/O,
  call normal logging or initialize the facility lazily.
* S127-R9: an armed file shall begin with `clock_base`, relating monotonic and
  realtime nanoseconds. Shutdown diagnostics are `perfdrop` with ring/drop
  count, `perfformat` with ring/failure count, and `perfnoring` with the count
  of producer threads that could not claim a ring. These names and fields are
  part of schema 1.

## Required tests and gate

`tests/common/test_perf_trace.c` shall cover named formatting, 64-bit and
negative values, malformed/truncated record rejection, disarmed default,
FIFO bytes, boundary wrap, whole-record drop, null rejection, secure-create
failure, concurrent per-thread rings, explicit start/stop, final drain,
post-fork child initialization, proof that an event cannot initialize lazily,
double close, and sink write failure. Run
`CK_RUN_SUITE=PerfTrace tests/common/test_common` in the enabled build.

The disabled-footprint gate shall compile a call whose arguments have side
effects, prove the effects do not occur, and inspect the linked test binary
with `nm` and `strings` for the implementation and sentinel event name. Both
must be absent. Run the README gate in disabled and enabled builds.
