# BACKLOG

This is the open work list. Each open item states the unresolved question or
implementation, why it matters, its acceptance condition, and a durable
pointer. Completed experiments live in `docs/experiments/`; normative product
requirements live in `PRD/`; per-run evidence stays with its capture under
`PR-demo/mac_bisect_matrix/captures/`.

Do not add result tables or completed narratives here. A closed item keeps one
stub at the end of this file and points to its record.

## Current repository state — 2026-08-22

* `/work` and `/workUpdateXorgXrdp` are both on
  `dev/avc444_metablock_checkpoint`; the committed dev-qualification
  frontiers are xrdp `00bce44e` and xorgxrdp `c190343`. They include the
  completed #120 trace lifecycle and producer-logger removal.
* The xrdp clean-room base is pinned to
  `fe850a22c08a624c66bbac07e310251782e6f828`. Its compatibility audit is
  complete and found no breaking AVC API, configuration or wire change.
* The xorgxrdp clean-room base is pinned to
  `49bf2dd3546dc48b9d5bae62022762fde11793d0`. The paired compatibility audit
  found no upstream capture/xup contract change to reconcile; the development
  branch's 14 commits are the feature inventory, not base drift.
* The old `/work-PR` branch at `c74a09e7` is an abandoned reference. It
  diverges from the pinned xrdp base and predates the current frontier.
* The five-arm x031–x035 baseline fleet and the paired x036/x037 #122
  characterization arms certify. #121 deleted timing captures carrying
  xorgxrdp's per-frame `ACK_TRACE cap` logger; none of their numerical claims
  is quotable.
* A real Tesla T4 host was re-provisioned on 2026-08-22 with the exact
  xrdp `00bce44e` / xorgxrdp `c190343` pair. The server-side #123 preflight
  and dense AVC444v2 real-client result are recorded under
  `PR-demo/mac_bisect_matrix/captures/i123_t4_frontier_preinteractive_20260822T183935Z/`;
  forced AVC444v1 and AVC420 real-client qualification remains open.

## Execution order

The porting list is one chain. Complete #123 through #143 in numeric order; an
item does not start until its predecessor is closed. #123–#125 finish and
anchor the development implementation; #120–#122 are closed below.
#126–#142 re-author it as the clean-room commit series. #143 is explicitly
later architecture work and cannot change or qualify that series. #300 is the
public-PR documentation deliverable after #142; it does not block #143 and is
not permission to publish or push anything.

For every dev qualification, commit the exact procedure, paired source and
package identities, configuration, workload, instrument and expected checks.
The clean-room run changes no arm, knob or interpretation: it replays that
anchor on an immutable candidate tree. A red clean-room slice never becomes a
commit, and the next slice does not start. The final paired tree is not frozen
history until all retained replays are green.

---

# Active dependency chain

## #123 — T4/NVENC compatibility qualification

**Status: IN PROGRESS; first open item.** The real T4 is provisioned with the
pinned development pair and the server-side one-monitor, two-monitor, AVC420,
AVC444 and NVENC/LTR preflight is green. Windows and macOS both negotiated
dense AVC444v2 and preserved visibly distinct one-pixel red/blue stripes;
Windows dynamic resize and its real two-monitor layout are also recorded.
Forced AVC444v1 and AVC420 remain open on both clients. This item does not
retain or recreate a numerical T4 throughput claim.

**Scope:** follow the committed deploy procedure with pinned paired package
hashes and the real NVENC path. Qualify cold and warm behavioral probing,
required parameter-set transforms, probe-before-confirm selection, no
post-confirmation codec fallback, supported AVC modes, one and two monitors,
resize/restart and Windows/macOS rendering. Record exact client, driver,
ffmpeg, geometry, configuration and trace-build identities.

**Acceptance:** every supported advertised mode negotiates and renders on the
real hardware without fallback; cold start and resize either pass or produce
the specified loud pre-confirmation refusal; byte audits and client checks are
green. The historical 47–74 ms bimodality and E5-2 ratio are out of scope.
Current procedure and partial evidence:
`PR-demo/mac_bisect_matrix/captures/i123_t4_frontier_preinteractive_20260822T183935Z/README.md`.

## #124 — credit-frontier client qualification

**Status: TODO; blocked on #123.** The mechanism, frozen-client behavior and
`C + 2M` bound are anchored by dev tests and recorded in
`docs/experiments/80-the-credit-frontier.md` and
`docs/experiments/91-the-multimon-window-and-the-shared-pump.md`.

**Scope:** complete the written six-check visual qualification on identified
macOS and Windows client products on the dev pair; pin the artifacts and
expected checks for clean-room replay; and state any upstream performance
claim as one-monitor only. Do not reopen window tuning: the shipped decision
is `wire_window=1`, `eager_slot_ack=true`; value 2 is maintainer guidance, not
a second default.

**Acceptance:** all six visual checks pass on both named clients with one
payload active at a time, xrdp's human-rate log confirms the intended codec
and contains no codec fallback or encoder fault, and the complete replay
procedure is committed before #125 starts. The compile-time trace is retained
passively for agent-owned diagnosis, but exercising a particular credit
distance is not a condition on the owner's visual verdict and does not cause a
visual rerun.

## #125 — sparse-chroma client qualification

**Status: TODO; blocked on #124.** The byte mechanism and offline model are
anchored by dev tests and recorded in
`docs/experiments/92-sparse-aux-is-a-byte-lever-not-a-time-one.md`.

**Scope A — real-client correctness:** on the dev pair, certify on identified
macOS and Windows clients a stream which contains both main-only cycles and
main-plus-auxiliary cycles. Run only `chroma-probe`; no benchmark payload,
sampler or unrelated GUI sidecar runs during the observation. A configuration
change requires a whole-session logoff, but changing clients without changing
configuration does not. Perform the still-screen 4:4:4 visual check and record
the exact artifacts and procedure for later clean-room replay. The guarantee
is `chroma_refresh_ms` plus one actual frame interval; no extra heuristic is in
scope.

**Scope B — T4 oracle numerical qualification:** after Scope A, the agent runs
the existing oracle-client and `textflood` harness on the same pinned T4 pair
at the recorded 3840x2400 modeline. Run exactly four 20-second legs in
dense/sparse/dense/sparse order; the only configuration difference is
`chroma_refresh_ms=0, chroma_idle_ms=0` against
`chroma_refresh_ms=1000, chroma_idle_ms=100`. Disable visual autostart payloads
for these legs. Use only the compile-time `common/perf_trace` instrument: no
sampler, logger or sidecar tracer. Report mean, p50, p90 and p99 frame interval
and throughput, plus the latency spent (1) feeding raw pixels to the ffmpeg
children, (2) waiting for encoded output, (3) draining encoded bytes, and
(4) collecting, rewriting, handing the frame to the transport and releasing
credit. Pair child windows by child and sequence identity, make the four
segments close to the full encoder cycle, and report the producer-idle count
before making a server-throughput claim. Also report main-only and
main-plus-auxiliary command counts and transmitted bytes and close their sum
against the audited video-command total.

**Acceptance:** in Scope A both clients render the alternating stream
correctly and the still image restores distinct one-pixel red/blue chroma
detail. In Scope B both dense repetitions and both sparse repetitions are
internally consistent; the sparse decision is observed on the mechanism's own
records; the chroma-gap bound holds; both command classes occur; byte
accounting closes exactly; no latency segment is negative and their sum
closes to the full cycle. The exact run identity, raw distributions, readable
breakdown and replay procedure are recorded in-tree. A red result is fixed and
independently tested on dev before #126 starts.

---

# Clean-room commit series

Every slice below is one commit and is blocked on the immediately preceding
item. Its `PRD/slices/` file is the unique normative implementation and test
specification. Every commit must be green in both repositories in every build
mode available at that point; the ordinary xrdp build contains no trace
footprint; no intermediate commit advertises an incomplete backend; and
nothing is pushed by the agent.

## #126 — paired latent GFX H.264 shared-memory isolation fix

**Status: TODO; blocked on #125.** Port the pre-existing AVC420/H.264
multi-monitor plane-overwrite fix before any AVC444 feature.

**Dev source:** xrdp `common/xup_client_info.h`, `xrdp/xrdp_encoder.c`,
`tests/xrdp/test_avc444_multimon.c`; xorgxrdp `module/rdpCapture.c`,
`module/rdpClientCon.c`, `module/rdpClientCon.h`.

**Acceptance:** standalone behavior for upstream-reachable `CC_GFX_A2`; each
monitor owns a disjoint region; protocol version and both repositories agree;
single- and multi-monitor layout tests plus paired builds are green. Normative
specification: [`PRD/slices/126-shmem-isolation.md`](PRD/slices/126-shmem-isolation.md).

## #127 — generic compile-time performance tracer foundation

**Status: TODO; blocked on #126.** Re-author the completed generic
facility without AVC events or diagnostic xup payloads.

**Dev source:** `common/perf_trace.{c,h}` and
`tests/common/test_perf_trace.c`; lifecycle shape comes from #120. The
transport queue counter belongs to #140, not this generic slice.

**Acceptance:** the slice is independently usable; default builds contain no
trace symbols, strings, state or argument evaluation; enabled builds pass all
generic lifecycle/security/format/concurrency tests. Normative specification:
[`PRD/slices/127-perf-trace-foundation.md`](PRD/slices/127-perf-trace-foundation.md).

## #128 — paired AVC capture and diagnostic wire contract

**Status: TODO; blocked on #127.** Add the capture capability, versioned xup
layout/geometry primitives and optional producer-timestamp fields. No AVC
backend is selectable.

**Dev source:** `common/xrdp_client_info.h`, `common/xup_client_info.h`,
`xup/xup.c`, `tests/xrdp/test_avc444_multimon.c`; xorgxrdp
`module/rdpClientCon.{c,h}`.

**Acceptance:** bounds/overflow and serialization tests cover both trace build
modes; older peers reject incompatible structure versions cleanly; paired
headers and builds agree. Normative specification:
[`PRD/slices/128-capture-wire-contract.md`](PRD/slices/128-capture-wire-contract.md).

## #129 — full-chroma view construction and producer packing

**Status: TODO; blocked on #128.** Port v1, v2 and main-only AVC420 view
construction, coded alignment, padding and the vectorized xorgxrdp producer.
The backend remains unadvertised.

**Dev source:** `xrdp/xrdp_avc444_convert.{c,h}`,
`tests/xrdp/test_avc444_convert.c`; xorgxrdp `module/rdpCapture.c`,
`module/rdpClientCon.c`, `module/rdpYuvVectorize.h`.

**Acceptance:** specification-derived color, layout, odd-size, padding,
alignment and AVC420-loss vectors pass; xorgxrdp packed bytes are checked
against the independent xrdp oracle; scalar and vector paths are identical.
Normative specification:
[`PRD/slices/129-view-construction.md`](PRD/slices/129-view-construction.md).

## #130 — bounded standard-NUT demuxer

**Status: TODO; blocked on #129.** Port the independent standard-NUT subset as
a pure leaf.

**Dev source:** `xrdp/xrdp_nut.{c,h}`, `tests/xrdp/test_avc444_nut.c`,
`tests/xrdp/avc444/fixture_4frame.nut` and `PROVENANCE.md`.

**Acceptance:** valid fixture, byte fragmentation, truncation, CRC, file-id
and total-ceiling cases pass; no server caller exists. Normative specification:
[`PRD/slices/130-nut-demuxer.md`](PRD/slices/130-nut-demuxer.md).

## #131 — Annex-B validation and parameter-set policy

**Status: TODO; blocked on #130.** Port bounded Annex-B inspection plus the
recorded SPS/SEI interoperability transforms used by current hardware
profiles. LTR rewriting is not in this slice.

**Dev source:** base functions in `xrdp/xrdp_h264_annexb.{c,h}` and base cases
in `tests/xrdp/test_avc444_h264.c`.

**Acceptance:** reset packet, missing/duplicate parameter sets, malformed NAL,
HRD sanitization and `pic_struct` vectors pass. Explicit `fault_*` injections
remain dev-only. Normative specification:
[`PRD/slices/131-annexb-and-parameter-policy.md`](PRD/slices/131-annexb-and-parameter-policy.md).

## #132 — pure AVC capability classification

**Status: TODO; blocked on #131.** Port client-capability classification and
server mode choice as pure logic. Do not advertise a live backend.

**Dev source:** `xrdp/xrdp_avc444_caps.{c,h}` and
`tests/xrdp/test_avc444_caps.c`.

**Acceptance:** table-driven v8/v8.1/v10.0/v10.1/v10.2–10.7, AVC-disabled,
unknown-version and v2-support cases pass. Normative specification:
[`PRD/slices/132-capability-classifier.md`](PRD/slices/132-capability-classifier.md).

## #133 — secure ffmpeg runner and behavioral probe

**Status: TODO; blocked on #132.** Port spawn, descriptor layout,
nonblocking pipe pump, bounded collection, termination/reaping, static
`dump_extra` verification, one-frame `probesize`, pipe-size negotiation and
failure classification. LTR and multi-monitor pump-set behavior are later.

**Dev source:** base portions of `xrdp/xrdp_encoder_ffmpeg.{c,h}` and
`tests/xrdp/test_avc444_ffmpeg.c`, plus
`tests/xrdp/gfx/fake_encoder_hang.sh`.

**Acceptance:** pure and real-ffmpeg gates cover probe success, header-policy
mismatch, duplicate headers, timeout, synchronous pair/single identity,
resize/reap and small-geometry startup. Normative specification:
[`PRD/slices/133-ffmpeg-runner.md`](PRD/slices/133-ffmpeg-runner.md).

## #134 — inactive server encoder integration

**Status: TODO; blocked on #133.** Add internal encoder ownership,
mode state and dispatch without making the backend selectable or advertising
an AVC capability.

**Dev source:** relevant portions of `xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_mm.c`, `xrdp/xrdp_types.h` and `tests/xrdp/test_xrdp_egfx.c`.

**Acceptance:** existing x264/OpenH264 paths are unchanged; internal
AVC420/444 transactions are unit-testable; no configuration or capability can
reach the new path. Normative specification:
[`PRD/slices/134-inactive-encoder-integration.md`](PRD/slices/134-inactive-encoder-integration.md).

## #135 — LC=1/LC=2 wire serialization

**Status: TODO; blocked on #134.** Add AVC420 and AVC444 serializers. AVC444
is born as luma LC=1 followed by chroma LC=2; LC=0 never enters history.

**Dev source:** serializer portions of `xrdp/xrdp_encoder.{c,h}` and
`tests/xrdp/test_avc444_metablock.c`.

**Acceptance:** exact metablock, even-origin/even-extent, region, codec-id and
two-PDU byte vectors pass; the backend remains unadvertised. Normative
specification:
[`PRD/slices/135-avc-wire-serialization.md`](PRD/slices/135-avc-wire-serialization.md).

## #136 — two-slot capture and fail-early shared memory

**Status: TODO; blocked on #135.** Port two slots per monitor, per-monitor
slot rotation, snapshot ownership and up-front backing-store reservation.

**Dev source:** `common/os_calls.c`, `common/xup_client_info.h`, capture portions
of `xrdp/xrdp_encoder.{c,h}` and `xrdp/xrdp_mm.c`,
`tests/xrdp/test_avc444_multimon.c`; xorgxrdp `module/rdpClientCon.{c,h}` and
`module/rdpMisc.c`.

**Acceptance:** layout, alternation, capacity, failure-before-session and
paired ownership tests pass; allocation failure cannot become a later SIGBUS.
Normative specification:
[`PRD/slices/136-two-slot-capture.md`](PRD/slices/136-two-slot-capture.md).

## #137 — reference-safe AVC444 topology

**Status: TODO; blocked on #136.** Port the mandatory decode-topology
invariant: main pictures never reference auxiliary pictures; auxiliary IDRs
become non-IDR intra leaves on the shared chain.

**Dev source:** relevant functions in `xrdp/xrdp_h264_annexb.{c,h}` and
`xrdp/xrdp_encoder_ffmpeg.{c,h}`; leaf cases in
`tests/xrdp/test_avc444_h264.c`.

**Acceptance:** golden leaf vectors, malformed/truncated rejection and both
decoder-topology simulations pass. Normative specification:
[`PRD/slices/137-reference-safe-topology.md`](PRD/slices/137-reference-safe-topology.md).

## #138 — long-term-reference chain, re-key and scheduled intra refresh

**Status: TODO; blocked on #137.** Port per-view LT0/LT1 rewriting, bounded
frame-number re-key without surface churn, and independent scheduled main/aux
intra refresh.

**Dev source:** LTR portions of `xrdp/xrdp_h264_annexb.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_encoder.{c,h}` and
`xrdp/xrdp_mm.c`; `tests/xrdp/test_avc444_ltr.c`, its two golden headers, and
LTR/re-key/intra cases in `test_avc444_ffmpeg.c` and `test_tconfig.c`.

**Acceptance:** golden bytes, Windows-field cross-check, both decode modes,
sparse cadence, wrap/restart, observed-vs-requested cuts and live real-ffmpeg
cut cases pass. Normative specification:
[`PRD/slices/138-ltr-rekey-intra.md`](PRD/slices/138-ltr-rekey-intra.md).

## #139 — one-thread multi-monitor pump set and batch emission

**Status: TODO; blocked on #138.** Port one encoder pair per monitor,
one poll set over all children, shared deadline, per-monitor geometry/LTR state
and inline batch assembly. The removed emit thread does not return.

**Dev source:** multi-monitor portions of `xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_mm.c`, `xrdp/xrdp.h`,
`tests/xrdp/test_avc444_multimon.c`, `test_avc444_emit_split.c`,
`test_avc444_ffmpeg.c` and `test_xrdp_egfx.c`.

**Acceptance:** independent monitor identity/state, sequence zero, unarmed and
failure states, four-view pump-set and two-monitor live correctness pass.
Normative specification:
[`PRD/slices/139-multimon-pump-set.md`](PRD/slices/139-multimon-pump-set.md).

## #140 — paired credit frontier and split acknowledgement

**Status: TODO; blocked on #139.** Port per-monitor capture admission,
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
[`PRD/slices/140-credit-frontier.md`](PRD/slices/140-credit-frontier.md).

## #141 — sparse auxiliary cadence

**Status: TODO; blocked on #140.**
Port time-based chroma refresh/idle scheduling and skip auxiliary submission
before it can advance the encoder DPB.

**Dev source:** sparse portions of `xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_h264_annexb.{c,h}`,
`xrdp/xrdp_mm.c`, `xrdp/xrdp_tconfig.{c,h}`,
`tests/xrdp/test_avc444_chroma_due.c`, sparse cases in
`test_avc444_ltr.c`, `test_avc444_ffmpeg.c` and `test_tconfig.c`.

**Acceptance:** disabled equivalence, refresh-plus-one-frame bound, settle/rate
clamps, independent intra schedules and DPB continuity pass. This internal
slice has no real-client replay; the activated candidate replays #125 only in
#142. Normative specification:
[`PRD/slices/141-sparse-chroma.md`](PRD/slices/141-sparse-chroma.md).

## #142 — configuration, activation, operating docs and final paired gate

**Status: TODO; blocked on #141.** Make the fully assembled backend
selectable only here. Add the user-facing configuration and documentation for
the mechanisms already green; do not port `tail_flush` or explicit fault
injection.

**Dev source:** `xrdp/xrdp_tconfig.{c,h}`, `xrdp/xrdp_types.h`,
`xrdp/xrdp_mm.c`, `xrdp/gfx.toml`, `docs/man/gfx.toml.5.in`,
`tests/xrdp/test_tconfig.c` and its `tests/xrdp/gfx/*.toml` fixtures.

**Acceptance:** bounds/refusals/defaults, removed-key warning, capability
activation, probe-before-confirm, no fallback, resize lifecycle and all three
real clients plus multi-monitor pass on builds made from the clean-room paired
branches. The frozen #124 and #125 client procedures pass without changing
their arms, checks or interpretation. Default and trace-enabled CI-equivalent
matrices are green. Normative specification:
[`PRD/slices/142-activation-and-docs.md`](PRD/slices/142-activation-and-docs.md).

## #143 — host pipe setting, comparable baselines and zero-copy decision

**Status: TODO; blocked on #142; outside the port.** The implemented 64 KiB
pipe requirement and its evidence are in
`docs/experiments/103-pipe-size-and-host-limit.md`.

**Open:** make or deliberately decline host sysctl persistence; re-establish
any baseline used across the sysctl boundary; separately decide whether the
roughly 1.4 ms pipe machinery plus unavoidable copy justifies abandoning the
stock-ffmpeg child architecture. This cannot change or qualify #126–#142;
abandoning that architecture starts a later normative series.

---

# Public PR deliverable

## #300 — write the public-facing PR documentation and evidence package

**Status: TODO; blocked on #142; independent of #143.** This deliverable was
previously left implicit inside #142's operating documentation. That was a
planning omission: operator documentation in the implementation slice and the
reviewer-facing PR narrative are different artifacts with different readers
and completion gates.

**Scope:** write the reviewer-facing problem statement, design and commit
walkthrough, compatibility and security boundaries, configuration/migration
notes, and claims-to-evidence index for the final clean-room pair. Reconcile
every implementation claim against the final source and normative `PRD/`
slices; do not copy stale dev-branch commit identities, test counts or proposed
runtime A/B results into the final narrative. Inputs and drafts live under
`PR-demo/public_pr/`; curated visual evidence and bug/fix timelines are in
`PR-demo/media_evidences/README.md`. Historical research may support the
narrative only when its provenance and current applicability are stated.

**Acceptance:** `PR-demo/public_pr/README.md` contains a self-contained draft
PR description and maps every material claim to a final clean-room commit,
test, experiment record or inventoried media artifact. All linked files exist;
historical-only material is labelled; no instrument-invalid number or
unreplayed dev result is presented as final evidence; and the documented
commands and configuration match the paired tree produced by #142. Publication
and `git push` remain owner actions.

---

# Closed stubs

The record, not this table, owns conditions, measurements and retractions.

| item | current outcome | record |
|---|---|---|
| #45 | One-thread pump-set and per-monitor budget landed; E5 closed through #52. | `docs/experiments/45-intra-refresh-and-pump-set.md` |
| #52 | Saturated-payload E5-2 reached 2.13x. | `docs/experiments/52-e5-2-saturated-payload.md` |
| #55 | T4 E5-2 recorded AMBER; #123 retains compatibility, not its numerical rerun. | `docs/experiments/55-e5-2-on-the-t4.md` |
| #59/#60 | T4 attribution corrected; the unresolved timing bimodality is outside #123. | `docs/experiments/59-61-corrected-attribution.md` |
| #61 | GLAMOR on NVIDIA closed-wontfix after real-path black output. | `docs/experiments/61-glamor-on-nvidia.md` |
| #61h | Per-frame xrdp logging evidence was voided; the ring replacement landed. | `docs/experiments/61h-the-logger-was-in-the-measurement.md` |
| #62 | Original textflood result recorded RED and producer-confounded; #83 supplied the replacement payload. | `docs/experiments/62-textflood-payload.md` |
| #64 | The rect-id ghost hypothesis was refuted. | `docs/experiments/64-rect-id-ack-ghost.md` |
| #70 | Eager slot release established the mechanism later generalized by #124/#140. | `docs/experiments/70-eager-slot-release-ack.md` |
| #75 | LTR rewriting changed from rebuild to payload copy with byte-identical output. | `docs/experiments/75-the-rewrite-was-re-serialising-the-picture.md` |
| #78 | Split-pump attribution moved the remaining stall to withheld capture credit. | `docs/experiments/78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md` |
| #79 | The unbounded horizon proposal was rejected and superseded by #124. | `docs/experiments/79-layer1-the-ack-delay-sweep-confirms-the-withheld-slot-credit.md` |
| #81 | The netem harness was corrected and retained for mechanism tests only. | `docs/experiments/81-the-netem-rtt-harness.md` |
| #87 | Emit-split requirement retired because its premise measured per-frame logging. | `docs/experiments/87-the-emit-split-was-measuring-its-own-logger.md` |
| #88 | Oracle-client pause attribution withdrawn; same-sitting controls own port evidence. | `docs/experiments/88-client-pauses-are-not-a-port-gate.md` |
| #90 | PRD-violating stage threads withdrawn without admissible evidence of sufficient ROI. | `docs/experiments/90-the-eight-ms-prize-was-stale.md` |
| #91 | Window arithmetic and encoder overlap resolved; its optional producer-cadence follow-up was withdrawn with #95. | `docs/experiments/91-the-multimon-window-and-the-shared-pump.md` |
| #95 | Capture-handoff 1.5x target withdrawn as optional optimization. | `docs/experiments/95-capture-handoff-target-withdrawn.md` |
| #96 | Moving packing across xup withdrawn as architectural scope expansion. | `docs/experiments/96-pack-off-x-thread-withdrawn.md` |
| #98 | Open transport/adaptation roadmap withdrawn; contaminated timing deleted. | `docs/experiments/98-tier0-bbr-ab.md` |
| #100 | Emit thread removed; inline assembly and old-key warning remain. | `docs/experiments/100-the-emit-thread-bought-nothing.md` |
| #102 | Client 33-pixel offset documented and closed below the port. | `docs/experiments/102-client-display-offset.md` |
| #103(a) | Pipe-size negotiation and the 64 KiB fail-loud guard landed. | `docs/experiments/103-pipe-size-and-host-limit.md` |
| #104 | Five one-image arms built and certified; the stale x035-red paragraph is superseded. | `docs/experiments/104-pr-evidence-matrix.md` |
| #105 | Umbrella closed after preparation; its remaining work is the linear #120–#142 chain. | `docs/experiments/105-port-preparation.md` |
| #106 | External trace equivalence closed RED: 13/34 semantic records had no exact mapping. | `docs/experiments/106-perf-isolation-and-trace-equivalence.md` |
| #107 | Named text byte ring selected and installed; shipping lifecycle moved to #120. | `docs/experiments/107-private-tracer-is-pr-scope.md` |
| #120 | Compile-time-erased trace shipped on dev; unnecessary producer logger removed. | `docs/experiments/120-perf-trace-shipping.md` |
| #121 | Contaminated captures deleted; identity and minimum-record gates made fail-loud. | `docs/experiments/121-evidence-admissibility-cleanup.md` |
| #122 | Valid selected-monitor evidence refuted the one-active slowdown hypothesis. | `docs/experiments/122-one-active-monitor.md` |
| #201 | Paired bases pinned and the monolithic PRD replaced by one normative file per clean-room slice. | `docs/experiments/201-prd-refactor.md` |
