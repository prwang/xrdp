# #120 -- `common/perf_trace` shipping boundary

Recorded 2026-08-17.

## Decision

`common/perf_trace` is a compile-time opt-in facility. A default build links
no implementation, contains no event or sink strings, and its macros evaluate
no arguments. An enabled session initializes after fork, before measured
work, and closes by stopping publication, joining the sink and draining every
complete record. Security, lifecycle, format, wrap, overflow, concurrency,
fork, final-drain and sink-failure cases have deterministic tests.

The xorgxrdp `ACK_TRACE cap` endpoint was introduced by this development
branch in xorgxrdp commit `10fa3aa2`; it is absent from the pinned upstream
base `49bf2dd`. The surviving report obligations need xrdp-side capture
arrival, encode and egress brackets plus explicit frame/monitor identities.
None needs producer packing duration. The endpoint was therefore removed.
No producer timestamp fields, xup trace-build version or xorgxrdp tracer link
is added.

## Qualification

The retained local source-cost runs are in
`PR-demo/mac_bisect_matrix/captures/i120_perf_trace_shipping_20260817/`.
Twelve named text events per frame cost 11.825--14.198 us mean, 13.709--14.460
us p50, 14.229--14.919 us p90 and 23.529--24.369 us p99. Those are source-path
costs, not projected frame-rate gains. The capture README records counts,
conditions and quality checks.

Default and enabled builds, the common test suite and the disabled-footprint
gate are the deterministic acceptance anchors. The paired xorgxrdp build
proves removal of its synchronous producer logger.
