# Slice #136 — Two-slot capture and fail-early shared memory

## Commit boundary

This paired commit expands each AVC capture monitor region to two slots and
reserves its complete backing store before the session can use it. It does not
change client wire credit.

Target files are xrdp `common/os_calls.c`, `common/xup_client_info.h`,
`xrdp/xrdp_encoder.c`, `xrdp/xrdp_encoder.h`, `xrdp/xrdp_mm.c`,
`tests/xrdp/test_avc444_multimon.c`; and xorgxrdp
`module/rdpClientCon.c`, `module/rdpClientCon.h`, `module/rdpMisc.c`.

## Requirements

* S136-R1: every active monitor shall own exactly two equal-sized slots inside
  its disjoint region. Slot bases shall derive from checked region and slot
  sizes, never from a global rotating index.
* S136-R2: admission and rotation are per monitor. Two captures for one
  monitor may be outstanding; a third shall be refused until that monitor
  receives a matching ownership acknowledgement.
* S136-R3: each slot shall carry the capture/frame identity needed to reject a
  stale, duplicate or wrong-monitor acknowledgement. Wrap of the identity
  counter shall preserve unambiguous live ownership.
* S136-R4: xrdp shall retain a stable slot snapshot until input consumption is
  complete. The producer shall not modify an owned slot.
* S136-R5: the complete shared-memory length shall be allocated and physically
  reservable before the session starts. Allocation or backing-store failure
  shall be reported before mapping use and shall never become a later SIGBUS.
* S136-R6: cleanup shall unmap/close partial allocations and clear ownership.
  Existing single-slot capture codes shall retain their old layout.
* S136-R7: a geometry or monitor-layout change shall derive a new two-slot
  layout from the new display description before capture resumes. The producer
  shall allocate or reuse backing memory only against that new byte length and
  shall publish offsets, slot identity and mapping length from the same layout.
  Derivation and validation shall complete in temporary state before the
  published layout, backing allocation, screen geometry or slot ownership is
  changed. If the replacement is invalid, the resize shall fail without
  partially publishing it. No capture from the old layout may cross the resize
  completion boundary.

## Required tests and gate

`Avc444Multimon` shall cover session fallback, one/two/unequal monitors,
two-slot strides, empty budget, capacity one and two, refused third capture,
per-monitor isolation, alternating slot identity, acknowledge-all, stale
acknowledgement, growth/shrink resize, monitor-layout replacement and
overflow/allocation failure. A paired test shall prove the producer cannot
acquire a live slot and that a growth resize publishes the new slot offsets and
mapping length before its first capture. The pure layout test shall also prove
that a failed replacement leaves the previously valid layout byte-identical.
Run the targeted suite and both paired builds, then the README gate.
