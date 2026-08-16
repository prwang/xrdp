# BACKLOG

This is the open work list. Each open item states the unresolved question or
implementation, why it matters, its acceptance condition, and a durable
pointer. Completed experiments live in `docs/experiments/`; normative product
requirements live in `PRD/`; per-run evidence stays with its capture under
`PR-demo/mac_bisect_matrix/captures/`.

Do not add result tables or completed narratives here. A closed item keeps one
stub at the end of this file and points to its record.

## Current repository state — 2026-08-16

* `/work` and `/workUpdateXorgXrdp` are both on
  `dev/avc444_metablock_checkpoint`; xrdp is at `356a5e1d`, xorgxrdp at
  `10fa3aa`.
* The xrdp clean-room base is pinned to
  `fe850a22c08a624c66bbac07e310251782e6f828`. Its compatibility audit is
  complete and found no breaking AVC API, configuration or wire change.
* The current xorgxrdp upstream base resolves to
  `49bf2dd3546dc48b9d5bae62022762fde11793d0`, but owner pinning and the paired
  compatibility audit remain #201.
* The old `/work-PR` branch at `c74a09e7` is an abandoned reference. It
  diverges from the pinned xrdp base and predates the current frontier.
* The five-arm x031–x035 fleet exists on one paired image and all five arms
  certify. Timing claims from runs carrying xorgxrdp's per-frame
  `ACK_TRACE cap` logger remain quarantined by #108.
* T4 is decommissioned. Re-provisioning is conditional work in #93.

## Execution order

1. Complete the paired tracer shipping gate (#200).
2. Finish the unique normative slice specifications and pin the xorgxrdp base
   (#201).
3. Author the clean-room series in order (#210–#226). A red slice never
   becomes a commit and the next slice does not start.
4. Repair the evidence instrument and invalid records (#99, #108), then close
   only the acceptance claims retained by the PR (#80, #92, and conditionally
   #93).
5. Post-port performance work follows (#88, #94–#96, #98, #103).

---

# Upstream port

## #105 — paired upstream port umbrella

**Status: IN PROGRESS.** One xrdp PR and its matching xorgxrdp change are
re-authored from pinned upstream bases as a bisectable commit series. The
completed base, scope, tracer-format and default decisions are recorded in
`docs/experiments/105-port-preparation.md`; they are not repeated here.

**Open scope only:** complete #200 and #201, then #210–#226 in order. Every
slice must name its exact files, dependencies, behavior boundary, targeted
tests, full-suite gates and paired-repository compatibility gate in its
normative `PRD/slices/` file before implementation begins.

**Acceptance:** every commit is green in both repositories in every build
mode that exists at that commit; the ordinary xrdp build contains no trace
footprint; no intermediate commit advertises or selects an incomplete
backend; the final paired branches pass CI-equivalent checks and retained live
client gates. Nothing is pushed by the agent.

## #200 — complete `common/perf_trace` on the dev pair

**Status: TODO; blocks cutting the clean-room worktree.** #107 selected and
installed the named text byte-ring representation. This item owns the still
open shipping work.

**Dev source:** `common/perf_trace.{c,h}`, `common/trans.{c,h}`,
`xrdp/xrdp_listen.c`, `xrdp/xrdp_process.c`, the 34 call sites in
`xrdp/xrdp_encoder.c`, `xrdp/xrdp_encoder_ffmpeg.c` and `xrdp/xrdp_mm.c`,
`tests/common/test_perf_trace.c`, and xorgxrdp
`module/rdpClientCon.{c,h}`.

**Acceptance:** explicit post-fork initialization before the measured path;
quiescent close, join and final drain; complete atomic lifecycle/ring state;
visible one-shot open/write/flush failures; default-off configure and build
erasure; disabled macros evaluate no arguments; enabled lifecycle, security,
format, concurrency and overhead tests pass; xorgxrdp's per-frame logger is
removed and its producer timestamps cross the versioned xup contract into
xrdp's single ring. Exact rationale and failure modes:
`docs/experiments/107-private-tracer-is-pr-scope.md`.

## #201 — freeze the paired bases and normative commit contract

**Status: IN PROGRESS.** Refactor the monolithic, stale `PRD.md` into a
self-contained `PRD/` tree. `PRD/README.md` defines the product and invariants;
one file under `PRD/slices/` is the unique normative source for each
clean-room commit. BACKLOG items point to dev source only as porting inventory.

**Acceptance:** owner-pinned full xorgxrdp base; paired compatibility audit;
one-to-one mapping between #210–#226 and `PRD/slices/`; exact target files and
tests per slice; explicit activation boundary; no stale LC=0, single-monitor,
window-2-default, tail-flush, JSON-trace or stripped-tracer language; all
in-tree PRD links resolve.

## #210 — paired latent GFX H.264 shared-memory isolation fix

**Status: TODO; blocked on #200/#201.** Port the pre-existing AVC420/H.264
multi-monitor plane-overwrite fix before any AVC444 feature.

**Dev source:** xrdp `common/xup_client_info.h`, `xrdp/xrdp_encoder.c`,
`tests/xrdp/test_avc444_multimon.c`; xorgxrdp `module/rdpCapture.c`,
`module/rdpClientCon.c`, `module/rdpClientCon.h`.

**Acceptance:** standalone behavior for upstream-reachable `CC_GFX_A2`; each
monitor owns a disjoint region; protocol version and both repositories agree;
single- and multi-monitor layout tests plus paired builds are green. Normative
specification: #201 will install `PRD/slices/210-shmem-isolation.md`.

## #211 — generic compile-time performance tracer foundation

**Status: TODO; blocked on #200/#201.** Re-author the completed generic
facility without AVC events or diagnostic xup payloads.

**Dev source:** `common/perf_trace.{c,h}`, `common/trans.{c,h}` and
`tests/common/test_perf_trace.c`; lifecycle shape comes from #200.

**Acceptance:** the slice is independently usable; default builds contain no
trace symbols, strings, state or argument evaluation; enabled builds pass all
generic lifecycle/security/format/concurrency tests. Normative specification:
`PRD/slices/211-perf-trace-foundation.md` after #201.

## #212 — paired AVC capture and diagnostic wire contract

**Status: TODO; blocked on #211.** Add the capture capability, versioned xup
layout/geometry primitives and optional producer-timestamp fields. No AVC
backend is selectable.

**Dev source:** `common/xrdp_client_info.h`, `common/xup_client_info.h`,
`xup/xup.c`, `tests/xrdp/test_avc444_multimon.c`; xorgxrdp
`module/rdpClientCon.{c,h}`.

**Acceptance:** bounds/overflow and serialization tests cover both trace build
modes; older peers reject incompatible structure versions cleanly; paired
headers and builds agree. Normative specification:
`PRD/slices/212-capture-wire-contract.md` after #201.

## #213 — full-chroma view construction and producer packing

**Status: TODO; blocked on #212.** Port v1, v2 and main-only AVC420 view
construction, coded alignment, padding and the vectorized xorgxrdp producer.
The backend remains unadvertised.

**Dev source:** `xrdp/xrdp_avc444_convert.{c,h}`,
`tests/xrdp/test_avc444_convert.c`; xorgxrdp `module/rdpCapture.c`,
`module/rdpClientCon.c`, `module/rdpYuvVectorize.h`.

**Acceptance:** specification-derived color, layout, odd-size, padding,
alignment and AVC420-loss vectors pass; xorgxrdp packed bytes are checked
against the independent xrdp oracle; scalar and vector paths are identical.
Normative specification: `PRD/slices/213-view-construction.md` after #201.

## #214 — bounded standard-NUT demuxer

**Status: TODO; blocked on #201.** Port the independent standard-NUT subset as
a pure leaf.

**Dev source:** `xrdp/xrdp_nut.{c,h}`, `tests/xrdp/test_avc444_nut.c`,
`tests/xrdp/avc444/fixture_4frame.nut` and `PROVENANCE.md`.

**Acceptance:** valid fixture, byte fragmentation, truncation, CRC, file-id
and total-ceiling cases pass; no server caller exists. Normative specification:
`PRD/slices/214-nut-demuxer.md` after #201.

## #215 — Annex-B validation and parameter-set policy

**Status: TODO; blocked on #201.** Port bounded Annex-B inspection plus the
recorded SPS/SEI interoperability transforms used by current hardware
profiles. LTR rewriting is not in this slice.

**Dev source:** base functions in `xrdp/xrdp_h264_annexb.{c,h}` and base cases
in `tests/xrdp/test_avc444_h264.c`.

**Acceptance:** reset packet, missing/duplicate parameter sets, malformed NAL,
HRD sanitization and `pic_struct` vectors pass. Explicit `fault_*` injections
remain dev-only. Normative specification:
`PRD/slices/215-annexb-and-parameter-policy.md` after #201.

## #216 — pure AVC capability classification

**Status: TODO; blocked on #201.** Port client-capability classification and
server mode choice as pure logic. Do not advertise a live backend.

**Dev source:** `xrdp/xrdp_avc444_caps.{c,h}` and
`tests/xrdp/test_avc444_caps.c`.

**Acceptance:** table-driven v8/v8.1/v10.0/v10.1/v10.2–10.7, AVC-disabled,
unknown-version and v2-support cases pass. Normative specification:
`PRD/slices/216-capability-classifier.md` after #201.

## #217 — secure ffmpeg runner and behavioral probe

**Status: TODO; blocked on #214/#215.** Port spawn, descriptor layout,
nonblocking pipe pump, bounded collection, termination/reaping, static
`dump_extra` verification, one-frame `probesize`, pipe-size negotiation and
failure classification. LTR and multi-monitor pump-set behavior are later.

**Dev source:** base portions of `xrdp/xrdp_encoder_ffmpeg.{c,h}` and
`tests/xrdp/test_avc444_ffmpeg.c`, plus
`tests/xrdp/gfx/fake_encoder_hang.sh`.

**Acceptance:** pure and real-ffmpeg gates cover probe success, header-policy
mismatch, duplicate headers, timeout, synchronous pair/single identity,
resize/reap and small-geometry startup. Normative specification:
`PRD/slices/217-ffmpeg-runner.md` after #201.

## #218 — inactive server encoder integration

**Status: TODO; blocked on #213/#216/#217.** Add internal encoder ownership,
mode state and dispatch without making the backend selectable or advertising
an AVC capability.

**Dev source:** relevant portions of `xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_mm.c`, `xrdp/xrdp_types.h` and `tests/xrdp/test_xrdp_egfx.c`.

**Acceptance:** existing x264/OpenH264 paths are unchanged; internal
AVC420/444 transactions are unit-testable; no configuration or capability can
reach the new path. Normative specification:
`PRD/slices/218-inactive-encoder-integration.md` after #201.

## #219 — LC=1/LC=2 wire serialization

**Status: TODO; blocked on #218.** Add AVC420 and AVC444 serializers. AVC444
is born as luma LC=1 followed by chroma LC=2; LC=0 never enters history.

**Dev source:** serializer portions of `xrdp/xrdp_encoder.{c,h}` and
`tests/xrdp/test_avc444_metablock.c`.

**Acceptance:** exact metablock, even-origin/even-extent, region, codec-id and
two-PDU byte vectors pass; the backend remains unadvertised. Normative
specification: `PRD/slices/219-avc-wire-serialization.md` after #201.

## #220 — two-slot capture and fail-early shared memory

**Status: TODO; blocked on #212/#213.** Port two slots per monitor, per-monitor
slot rotation, snapshot ownership and up-front backing-store reservation.

**Dev source:** `common/os_calls.c`, `common/xup_client_info.h`, capture portions
of `xrdp/xrdp_encoder.{c,h}` and `xrdp/xrdp_mm.c`,
`tests/xrdp/test_avc444_multimon.c`; xorgxrdp `module/rdpClientCon.{c,h}` and
`module/rdpMisc.c`.

**Acceptance:** layout, alternation, capacity, failure-before-session and
paired ownership tests pass; allocation failure cannot become a later SIGBUS.
Normative specification: `PRD/slices/220-two-slot-capture.md` after #201.

## #221 — reference-safe AVC444 topology

**Status: TODO; blocked on #215/#217/#219.** Port the mandatory decode-topology
invariant: main pictures never reference auxiliary pictures; auxiliary IDRs
become non-IDR intra leaves on the shared chain.

**Dev source:** relevant functions in `xrdp/xrdp_h264_annexb.{c,h}` and
`xrdp/xrdp_encoder_ffmpeg.{c,h}`; leaf cases in
`tests/xrdp/test_avc444_h264.c`.

**Acceptance:** golden leaf vectors, malformed/truncated rejection and both
decoder-topology simulations pass. Normative specification:
`PRD/slices/221-reference-safe-topology.md` after #201.

## #222 — long-term-reference chain, re-key and scheduled intra refresh

**Status: TODO; blocked on #221.** Port per-view LT0/LT1 rewriting, bounded
frame-number re-key without surface churn, and independent scheduled main/aux
intra refresh.

**Dev source:** LTR portions of `xrdp/xrdp_h264_annexb.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_encoder.{c,h}` and
`xrdp/xrdp_mm.c`; `tests/xrdp/test_avc444_ltr.c`, its two golden headers, and
LTR/re-key/intra cases in `test_avc444_ffmpeg.c` and `test_tconfig.c`.

**Acceptance:** golden bytes, Windows-field cross-check, both decode modes,
sparse cadence, wrap/restart, observed-vs-requested cuts and live real-ffmpeg
cut cases pass. Normative specification:
`PRD/slices/222-ltr-rekey-intra.md` after #201.

## #223 — one-thread multi-monitor pump set and batch emission

**Status: TODO; blocked on #220/#222.** Port one encoder pair per monitor,
one poll set over all children, shared deadline, per-monitor geometry/LTR state
and inline batch assembly. The removed emit thread does not return.

**Dev source:** multi-monitor portions of `xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_mm.c`, `xrdp/xrdp.h`,
`tests/xrdp/test_avc444_multimon.c`, `test_avc444_emit_split.c`,
`test_avc444_ffmpeg.c` and `test_xrdp_egfx.c`.

**Acceptance:** independent monitor identity/state, sequence zero, unarmed and
failure states, four-view pump-set and two-monitor live correctness pass.
Normative specification: `PRD/slices/223-multimon-pump-set.md` after #201.

## #224 — paired credit frontier and split acknowledgement

**Status: TODO; blocked on #220/#223.** Port per-monitor capture admission,
slot-only acknowledgements, separate region frontier, bounded wire credit and
trace-only transport queue accounting. Shipped `wire_window` is 1;
`eager_slot_ack` is on.

**Dev source:** `common/xup_client_info.h`, `common/trans.{c,h}`, `xup/xup.c`,
frontier portions of `xrdp/xrdp_encoder.{c,h}`, `xrdp/xrdp_mm.c`,
`xrdp/xrdp_types.h`, `tests/xrdp/test_avc444_credit_frontier.c`,
`test_avc444_multimon.c` and `test_xrdp_egfx.c`; xorgxrdp
`module/rdpClientCon.{c,h}`.

**Acceptance:** every credit term, monotonicity, frozen client, C+2M bound,
slot/region separation, dropped-region return, per-monitor mask and paired
wire serialization pass. Normative specification:
`PRD/slices/224-credit-frontier.md` after #201.

## #225 — sparse auxiliary cadence

**Status: TODO; blocked on #222/#224; live acceptance also depends on #92.**
Port time-based chroma refresh/idle scheduling and skip auxiliary submission
before it can advance the encoder DPB.

**Dev source:** sparse portions of `xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_h264_annexb.{c,h}`,
`xrdp/xrdp_mm.c`, `xrdp/xrdp_tconfig.{c,h}`,
`tests/xrdp/test_avc444_chroma_due.c`, sparse cases in
`test_avc444_ltr.c`, `test_avc444_ffmpeg.c` and `test_tconfig.c`.

**Acceptance:** disabled equivalence, refresh-plus-one-frame bound, settle/rate
clamps, independent intra schedules and DPB continuity pass; #92's real-client
gates are green before the slice is accepted for handoff. Normative
specification: `PRD/slices/225-sparse-chroma.md` after #201.

## #226 — configuration, activation, operating docs and final paired gate

**Status: TODO; blocked on #210–#225.** Make the fully assembled backend
selectable only here. Add the user-facing configuration and documentation for
the mechanisms already green; do not port `tail_flush` or explicit fault
injection.

**Dev source:** `xrdp/xrdp_tconfig.{c,h}`, `xrdp/xrdp_types.h`,
`xrdp/xrdp_mm.c`, `xrdp/gfx.toml`, `docs/man/gfx.toml.5.in`,
`tests/xrdp/test_tconfig.c` and its `tests/xrdp/gfx/*.toml` fixtures.

**Acceptance:** bounds/refusals/defaults, removed-key warning, capability
activation, probe-before-confirm, no fallback, resize lifecycle and all three
real clients plus multi-monitor pass on builds made from the clean-room paired
branches. Default and trace-enabled CI-equivalent matrices are green.
Normative specification: `PRD/slices/226-activation-and-docs.md` after #201.

---

# Evidence and post-port work

## #80 — credit-frontier qualification still owed

**Status: OPEN EVIDENCE; does not block re-authoring #224.** The mechanism,
one-monitor attribution, frozen-client behavior and C+2M bound are recorded in
`docs/experiments/80-the-credit-frontier.md` and
`docs/experiments/91-the-multimon-window-and-the-shared-pump.md`.

**Open:** complete the written six-check visual qualification on identified
macOS and Windows client products and state the upstream performance claim as
one-monitor only unless new two-monitor evidence supports more. The shipped
decision is already settled: `wire_window=1`, `eager_slot_ack=true`; value 2
is maintainer-facing optional guidance, not a second default.

## #88 — attribute the oracle client's 50–150 ms pauses

**Status: TODO; post-port.** Host CPU contention and dump I/O are ruled out.
Use the cheapest existing instrument: client timing logs, then a bounded
per-PID `perf record`, then a one-line `/proc/<pid>/stat` delta. Do not add a
sampler or alter socket buffers. This does not block the server port.

## #92 — sparse-chroma real-client and bandwidth acceptance

**Status: IN PROGRESS; blocks #225 handoff, not its offline implementation.**
The byte mechanism and offline model are recorded in
`docs/experiments/92-sparse-aux-is-a-byte-lever-not-a-time-one.md`.

**Open:** certify an alternating LC=1 / optional LC=2 stream on macOS and
Windows before quoting rate; rerun the decomposed bandwidth pair with valid
instrumentation; perform the still-screen 4:4:4 visual check. The guarantee is
`chroma_refresh_ms` plus one frame interval; no extra heuristic is in scope.

## #93 — T4 re-establishment and conditional hardware claims

**Status: TODO; conditional.** Re-provision the T4 only if the PR retains
NVENC/T4 performance or live-compatibility claims. The run must follow the
committed deploy procedure, use the real hardware path, match bytes per
picture across paired arms, and resolve or bound the recorded bimodality.

## #94 — arm only monitors with changed pixels

**Status: TODO; post-port.** Prevent an idle monitor's repeated full damage
from holding an active monitor. Never drop a real update. Acceptance remains
the recorded one-active-monitor regression returning to at least 1.0x while
the two-active-monitor result stays within noise, with no screen starved over
180 seconds.

## #95 — attribute and improve the capture-side handoff

**Status: TODO; post-port.** Determine the per-monitor floor between one
terminal frame and the next damage and whether both monitors can be handed
over in one producer cycle. The producer side in `/workUpdateXorgXrdp` is
mandatory reading; xrdp-only attribution is not accepted.

## #96 — move capture packing off the X server thread

**Status: TODO; post-port.** Evaluate handing xrdp a raw stable snapshot and
packing on the encoder side. Preserve the current vectorized arithmetic and
wire bytes. This is an ownership/critical-path change, not a reason to retune
code outside the repository.

## #98 — transport policy and adaptive-rate roadmap

**Status: OPEN DECISIONS; post-port.** Tier-0 findings live in
`docs/experiments/98-tier0-bbr-ab.md`; the field survey is
`docs/research/flow-control-survey-2026-08.md`.

**Open:** decide whether p95/p99 delivery delay, stall rate and quality-delay
Pareto become normative figures of merit; whether to prototype userspace
frame pacing/`TCP_NOTSENT_LOWAT`; and whether encoder rate adaptation enters
the roadmap. The owed LAN and C>1 transport measurements are evidence work,
not clean-room implementation prerequisites.

## #99 — evidence gate must prove target identity and record presence

**Status: TODO; blocks new fleet evidence.** `e_gate_run.sh` must compare the
dialled arm with the collected pod identity and reject too few trace records
for the run duration. An empty trace may not be interpreted as an idle
session. No measurement campaign starts until both positive checks pass.

## #103 — host pipe setting, comparable baselines and zero-copy decision

**Status: OPEN ENVIRONMENT/ARCHITECTURE; source guard done.** The implemented
64 KiB pipe requirement and its evidence are in
`docs/experiments/103-pipe-size-and-host-limit.md`.

**Open:** make or deliberately decline host sysctl persistence; re-establish
any baseline used across the sysctl boundary; separately decide whether the
roughly 1.4 ms pipe machinery plus unavoidable copy justifies abandoning the
stock-ffmpeg child architecture. These do not block porting the fail-loud
runner guard.

## #108 — retire timing evidence containing xorgxrdp's per-frame logger

**Status: TODO; blocks quoting affected timings.** Enumerate all captures and
derived conclusions carrying `ACK_TRACE cap`; delete timing captures/records
as required for an instrument on the measured path; write one replacement
record naming what was voided; reopen every dependent conclusion. Preserve
independently valid bytes, identity, ordering and correctness evidence with
explicit provenance. Do not run a logger-vs-no-logger arm. The source removal
is #200.

---

# Closed stubs

The record, not this table, owns conditions, measurements and retractions.

| item | current outcome | record |
|---|---|---|
| #45 | One-thread pump-set and per-monitor budget landed; E5 closed through #52. | `docs/experiments/45-intra-refresh-and-pump-set.md` |
| #52 | Saturated-payload E5-2 reached 2.13x. | `docs/experiments/52-e5-2-saturated-payload.md` |
| #55 | T4 E5-2 recorded AMBER; later claims moved to conditional #93. | `docs/experiments/55-e5-2-on-the-t4.md` |
| #59/#60 | T4 attribution corrected; unresolved bimodality moved to #93. | `docs/experiments/59-61-corrected-attribution.md` |
| #61 | GLAMOR on NVIDIA closed-wontfix after real-path black output. | `docs/experiments/61-glamor-on-nvidia.md` |
| #61h | Per-frame xrdp logging evidence was voided; the ring replacement landed. | `docs/experiments/61h-the-logger-was-in-the-measurement.md` |
| #62 | Original textflood result recorded RED and producer-confounded; #83 supplied the replacement payload. | `docs/experiments/62-textflood-payload.md` |
| #64 | The rect-id ghost hypothesis was refuted. | `docs/experiments/64-rect-id-ack-ghost.md` |
| #70 | Eager slot release established the mechanism later generalized by #80/#224. | `docs/experiments/70-eager-slot-release-ack.md` |
| #75 | LTR rewriting changed from rebuild to payload copy with byte-identical output. | `docs/experiments/75-the-rewrite-was-re-serialising-the-picture.md` |
| #78 | Split-pump attribution moved the remaining stall to withheld capture credit. | `docs/experiments/78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md` |
| #79 | The unbounded horizon proposal was rejected and superseded by #80. | `docs/experiments/79-layer1-the-ack-delay-sweep-confirms-the-withheld-slot-credit.md` |
| #81 | The netem harness was corrected and retained for mechanism tests only. | `docs/experiments/81-the-netem-rtt-harness.md` |
| #87 | Emit-split requirement retired because its premise measured per-frame logging. | `docs/experiments/87-the-emit-split-was-measuring-its-own-logger.md` |
| #90 | PRD-violating stage threads withdrawn after the prize fell to about 1.25 ms. | `docs/experiments/90-the-eight-ms-prize-was-stale.md` |
| #91 | Window arithmetic and encoder overlap resolved; producer cadence moved to post-port work. | `docs/experiments/91-the-multimon-window-and-the-shared-pump.md` |
| #100 | Emit thread removed; inline assembly and old-key warning remain. | `docs/experiments/100-the-emit-thread-bought-nothing.md` |
| #102 | Client 33-pixel offset documented and closed below the port. | `docs/experiments/102-client-display-offset.md` |
| #103(a) | Pipe-size negotiation and the 64 KiB fail-loud guard landed. | `docs/experiments/103-pipe-size-and-host-limit.md` |
| #104 | Five one-image arms built and certified; the stale x035-red paragraph is superseded. | `docs/experiments/104-pr-evidence-matrix.md` |
| #105 preparation | Base, one-PR, tracer and default decisions completed; implementation remains #200+. | `docs/experiments/105-port-preparation.md` |
| #106 | External trace equivalence closed RED: 13/34 semantic records had no exact mapping. | `docs/experiments/106-perf-isolation-and-trace-equivalence.md` |
| #107 | Named text byte ring selected and installed; shipping lifecycle moved to #200. | `docs/experiments/107-private-tracer-is-pr-scope.md` |
