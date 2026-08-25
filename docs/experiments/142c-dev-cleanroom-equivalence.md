# #142C development and clean-room equivalence audit

## Scope and identities

This audit compares development xrdp `6f5b90311b1f831b72369a13951d7a2269d4f27f`
and xorgxrdp `c190343ff28a61e45b8307993f0fdd54fb596b29` with
clean-room xrdp `b38c63473c5297250af325d2770c5219ede69d48` and
xorgxrdp `3dc52da1321644bda7678fb246d815dc27bd9bef`. It covers every
normative requirement and required gate in slices #126 through #142. Source
shape and test names are not compared as product behavior.

Classification numbers are those in
`PRD/gates/142c-dev-cleanroom-equivalence.md`. Category 1 means equivalent
behavior implemented differently, category 2 means a clean-room-only
normative behavior which must be added to development, and category 4 means
development residue which is not part of the product contract. No category-3
or category-5 divergence was found.

## Requirement matrix

| slice | behavior and owning seams | result | independent gate |
|---|---|---|---|
| #126 | Disjoint, page-aligned per-monitor AVC capture regions with checked aggregate arithmetic in the shared xup header, xrdp layout consumer and xorgxrdp allocator/capture path. | Category 1. Development computes offsets with `xup_cap_h264_shmem_layout`; clean-room carries a typed layout. Both preserve the one-monitor bytes and prevent cross-monitor aliasing. | Development `Avc444Multimon` layout/overflow/disjoint cases; clean-room exact-layout cases and paired producer tests. |
| #127 | Compile-time optional, per-thread, double-mapped text trace ring; explicit lifecycle, secure sink and disabled footprint. | Category 1. The implementations differ in formatter decomposition but expose the same schema, bounds and no-trace binary contract. | Default disabled-footprint script; enabled `PerfTrace` suite; source inspection of init/close and producer macros. |
| #128 | Fixed-width full-chroma capture metadata, exact peer version, format/alignment/geometry bounds and per-frame monitor/slot identity. | **Category 2.** Development had only recomputed offsets plus an untyped `shmem_offset`; it did not carry or validate the complete layout or marked slot identity. Reconciliation adds the typed layout, exact size/version check, marked identity and independent consumer validation. | The new development resize-contract test derives both mapping sizes and rejects a previous geometry; the existing clean-room mutation/format/limit cases remain the independent exhaustive contract gate. |
| #129 | BT.709 full-range conversion, v1/v2/main-only packing, 16/32 width and 16-row height alignment, edge replication and non-aliasing vector/scalar paths. | Category 1 for pixel behavior. The reconciled producer consumes #128's typed offsets and coded geometry, which is the category-2 integration seam rather than a second converter. | Both conversion suites use specification vectors; paired xorg scalar/vector golden test; nonaligned resize-stride cases. |
| #130 | Incremental bounded standard-NUT parser with stable malformed/truncated/ceiling failures and retained fixture provenance. | Category 1. | Six-case `Avc444Nut` suites, including every-byte fragmentation and every truncation point. |
| #131 | Bounded Annex-B parsing, parameter-set policy, HRD sanitation and pic-struct removal. | Category 1. | Base `Avc444H264` golden and rejection cases in both trees. |
| #132 | Pure RDPGFX capability classifier, including the v10.1 distinction, forced modes and no fallback. | Category 1. | Seven table-driven classifier groups in both trees. |
| #133 | Shell-free ffmpeg runner, nonblocking `vmsplice` pump, 64-KiB pipe gate, 16-row geometry, topology-aware probe, stable terminal classes and bounded private forensics. | Category 1. Development decomposes forensic writers differently but has the same first-failure, permission, topology and no-retry behavior. | Live stock-ffmpeg runner suites; fake hang; probe topology; post-confirm terminal-once test; clean-room permission/exclusive forensic cases. |
| #134 | Worker-owned inactive ffmpeg integration, matched pair publication, terminal latch, damage return and no fallback/restart. | Category 1. | EGFX terminal and lifecycle cases plus development post-confirm repeated-damage test. |
| #135 | Exact AVC420/AVC444 LC=1/LC=2 serialization, metablock bounds, no LC=0 and checked capacity. | Category 1. | Specification-derived `Avc444Metablock` and EGFX command parsing/capacity cases. |
| #136 | Exactly two capture slots per monitor, stable borrowed snapshots, fail-early fully backed allocation and cleanup/recreate on geometry change. | Category 1 before the #128 integration. Development already reserves backing and recomputes its old layout; clean-room allocates from the typed login layout. The latter difference is deliberately retained for the 40059 red arm and is not accepted as final behavior. | Slot/budget identity and shared-memory reservation tests; ffmpeg resize/reap test; real resize arm required below. |
| #137 | CABAC auxiliary-IDR-to-leaf transform with typed rejection and production-topology probe. | Category 1. | Auxiliary-leaf golden/rejection cases and live two-child probe in both trees. |
| #138 | Independent LTR chains, re-key, frame-wrap handling and independent intra schedules. | Category 1. | Twenty-eight independent DPB/bitstream/schedule cases and live re-key/cut probes in both trees. |
| #139 | One worker poll set for all monitor children, identity-matched completion, inline deterministic batch assembly and isolated failure. | Category 1. | Pump-set, publication-state, unequal-monitor and EGFX batch suites. |
| #140 | Per-monitor slot plus global wire-credit admission, split slot/region frontiers, damage return, cumulative monotonic acknowledgements and frozen-client bound. | Category 1 for flow behavior. Development uses echoed rectangle identities and held-region maps; clean-room uses typed slot owners. #128's missing marked monitor/slot identity is the sole category-2 validation gap and is reconciled there. | Thirteen credit-frontier cases, development region-loss/eager-ack models, paired producer admission tests and trace-disabled counter-footprint gate. |
| #141 | Dense equivalence at zero, bounded time-only sparse cadence, independent auxiliary state and one-shot full-chroma convergence after quiescence. | Category 1. | Ten cadence/deadline cases, sparse DPB/live runner cases and the retained final-convergence interactive result. |
| #142 | Opt-in activation, exact config-to-probe transfer, documented defaults/ranges/tradeoffs, one terminal hangup, copy-safe software block and operator-only public prose. | **Category 2** for three removed development-only configuration keys: current development still accepted `tail_flush`, `fault_aux_delay` and `fault_strip_mmco`; clean-room correctly makes H.264 unavailable when they appear. Reconciliation rejects them while retaining internal deterministic fault APIs. Other invalid values differ only within the PRD's allowed unavailable-or-documented-safe-default policy. **Category 4** consists of historical/internal labels in development comments and unexposed diagnostic fields; none is copied into clean-room. | `GfxLoad`, operator-surface gate, complete config-to-probe transfer, terminal-once post-confirm test, default/trace builds and live arm. |

## Reconciliation boundary

The development reconciliation therefore changes only two product contracts:

1. the xup pair gains the complete fixed-width layout and marked slot identity,
   xorgxrdp validates it before use, and xrdp validates each mapped snapshot
   against current client geometry; and
2. the three removed development-only configuration keys disable H.264 instead
   of activating diagnostic behavior.

For 40059, xorgxrdp intentionally allocates and packs from the typed layout
received at login. Its own display geometry still changes on a RandR update,
but that cached layout does not. This is the actual clean-room behavior, not a
fault-injection switch. The consumer independently rebuilds the expected
layout from current client geometry, so growth outside the original alignment
class must be rejected.

Before deployment, the reconciled default xrdp tree passed 214/214 tests and
the trace-enabled tree passed 215/215 (the extra case is the enabled trace
suite). The paired xorgxrdp build and its two ordinary test programs passed.
The deployable package is rebuilt with tracing compile-time absent.
