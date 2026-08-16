# #88 — Oracle-client pauses are not a server-port gate

## Historical observation

The oracle client showed intermittent 50–150 ms pauses. Host CPU contention
and dump I/O were ruled out, but the client's activity during a pause was not
attributed. A proposed `/proc` sampler was rejected after the earlier sampler
cost 36% of one core and moved the rate it was intended to measure.

## Withdrawal — 2026-08-16

This is external-client diagnosis, not an xrdp implementation requirement.
The port qualification uses pinned client identities and same-sitting,
interleaved controls so cross-day client drift cannot stand in for a server
effect. Root-causing the pause would not change or qualify the clean-room
series, so the active item is withdrawn.

If a later product specification adopts client p95/p99 delivery latency as a
normative figure of merit, it must open a new item with a suitable client-side
instrument. This record does not authorize a new sampler or socket-buffer
change.
