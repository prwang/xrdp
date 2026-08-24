# BACKLOG

This is the open work list. Each open item states the unresolved question or
implementation, why it matters, its acceptance condition, and a durable
pointer. Completed experiments live in `docs/experiments/`; normative product
requirements live in `PRD/`; per-run evidence stays with its capture under
`PR-demo/mac_bisect_matrix/captures/`.

Do not add result tables or completed narratives here. A closed item keeps one
stub at the end of this file and points to its record.

## Current repository state — 2026-08-23

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
  and evidence are recorded under
  `PR-demo/mac_bisect_matrix/captures/i123_t4_frontier_preinteractive_20260822T183935Z/`.
  The same instance has migrated to `100.55.149.97`. #123 is closed with the
  forced-AVC444v1 macOS fidelity limitation preserved; `auto` still prefers
  v2 and v1 remains only the v10.0 compatibility tier. That instance was
  imaged and decommissioned on 2026-08-23; its addresses are historical and
  no remote work is possible until a replacement is provisioned.

## Execution order

The porting list is one chain. Complete #125 before #126; an item does not
start until its predecessor is closed. #120–#124B are closed below.
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

## #125 — IN PROGRESS — client, numerical and runtime qualification

Scopes A/B remain green: retain sparse chroma as an opt-in bandwidth/quality
tradeoff; dense and a one-frame wire window remain shipped defaults. Scope C
is now the first open action and blocks #126. The record is
[`docs/experiments/125-sparse-chroma-qualification.md`](docs/experiments/125-sparse-chroma-qualification.md).

**Scope C — false-green CPU/leaf certification and restart storm.** An owner
deployment on Ubuntu 24.04.4 LTS with its FFmpeg 6/libx264 path passed
the pre-confirm behavioral probe at 2560x1440, then every post-login
`aux_intra_leaf` rewrite failed. No pair was published, the client stayed
black, and persistent damage caused about ten teardown/recreate cycles per
second. The probe left `aux_intra_leaf` disabled while live AVC444 forces it
on, and the real-ffmpeg pair test repeats that single-child mismatch. The
original log is retained under
`PR-demo/mac_bisect_matrix/captures/i125c_ubuntu2404_ffmpeg6_cpu_blackscreen_20260824T142954Z/`.

Build exactly one local Ubuntu 24.04.4 arm with the affected FFmpeg 6 CPU
profile and connect through the repository FreeRDP client at the recorded
2560x1440 single-monitor geometry. Preserve the first rejected main and
auxiliary encoded access units, exact argv/version/configuration and a typed
rewrite reason before teardown; encoded forensics are bounded and mode 0600,
and raw desktop pixels or credentials are never dumped.

**Acceptance:** the unmodified frontier first reproduces probe-OK followed by
the leaf rejection and black presentation. The repair probes the exact
selected topology: an AVC444 leaf candidate spawns the real main and
forced-IDR auxiliary children, encodes a pair and runs the production leaf
transform before capability confirmation. An unsupported stream is rejected
there with a typed reason. Any child-creation, stream-contract or rewrite
failure which nevertheless occurs after confirmation terminates the affected
connection/session once, after retaining the bounded forensics; later damage
cannot spawn another child and no codec fallback masks it. A FreeRDP replay
then proves either the affected FFmpeg 6 stream is correctly transformed and
presented or is refused before AVC selection. Real-ffmpeg tests cover the
actual leaf topology, not the old single-child stand-in, and the corrected
requirements in PRD slices #137/#142 are green before #125 closes again.

---

# Clean-room commit series

Every slice below is one commit and is blocked on the immediately preceding
item. Its `PRD/slices/` file is the unique normative implementation and test
specification. Every commit must be green in both repositories in every build
mode available at that point; the ordinary xrdp build contains no trace
footprint; no intermediate commit advertises an incomplete backend; and
nothing is pushed by the agent.

The development paths below are an inventory proving that the plan covers the
frontier; they are not text to transplant. Each slice is re-authored from its
pinned base and normative specification. The common authorship/prose gate in
`PRD/README.md` applies to every commit: no development-history comments,
internal work labels, dead knobs, later-slice scaffolding or copied test
expectations enter the clean-room tree. A reviewer must be able to understand
the slice without opening the development branch.

## #126 — paired latent GFX H.264 shared-memory isolation fix

**Status: TODO; blocked on #125.** Re-author the pre-existing AVC420/H.264
multi-monitor plane-overwrite fix before any AVC444 feature.

**Development inventory (not source text):** xrdp
`common/xup_client_info.h`, `xrdp/xrdp_encoder.c`,
`tests/xrdp/test_avc444_multimon.c`; xorgxrdp `module/rdpCapture.c`,
`module/rdpClientCon.c`, `module/rdpClientCon.h`.

**Acceptance:** standalone behavior for upstream-reachable `CC_GFX_A2`; each
monitor owns a disjoint region; protocol version and both repositories agree;
single- and multi-monitor layout tests plus paired builds are green. Normative
specification: [`PRD/slices/126-shmem-isolation.md`](PRD/slices/126-shmem-isolation.md).

## #127 — generic compile-time performance tracer foundation

**Status: TODO; blocked on #126.** Re-author the completed generic
facility without AVC events or diagnostic xup payloads.

**Development inventory (not source text):** `common/perf_trace.{c,h}` and
`tests/common/test_perf_trace.c`. The transport queue counter belongs to #140,
not this generic slice.

**Acceptance:** the slice is independently usable; default builds contain no
trace symbols, strings, state or argument evaluation; enabled builds pass all
generic lifecycle/security/format/concurrency tests. Normative specification:
[`PRD/slices/127-perf-trace-foundation.md`](PRD/slices/127-perf-trace-foundation.md).

## #128 — paired AVC capture and diagnostic wire contract

**Status: TODO; blocked on #127.** Add the capture capability, versioned xup
layout/geometry primitives and optional producer-timestamp fields. No AVC
backend is selectable.

**Development inventory (not source text):** `common/xrdp_client_info.h`,
`common/xup_client_info.h`,
`xup/xup.c`, `tests/xrdp/test_avc444_multimon.c`; xorgxrdp
`module/rdpClientCon.{c,h}`.

**Acceptance:** bounds/overflow and serialization tests cover both trace build
modes; older peers reject incompatible structure versions cleanly; paired
headers and builds agree. Normative specification:
[`PRD/slices/128-capture-wire-contract.md`](PRD/slices/128-capture-wire-contract.md).

## #129 — full-chroma view construction and producer packing

**Status: TODO; blocked on #128.** Re-author v1, v2 and main-only AVC420 view
construction, coded alignment, padding and the vectorized xorgxrdp producer.
The backend remains unadvertised.

**Development inventory (not source text):**
`xrdp/xrdp_avc444_convert.{c,h}`,
`tests/xrdp/test_avc444_convert.c`; xorgxrdp `module/rdpCapture.c`,
`module/rdpClientCon.c`, `module/rdpYuvVectorize.h`.

**Acceptance:** specification-derived color, layout, odd-size, padding,
alignment and AVC420-loss vectors pass; xorgxrdp packed bytes are checked
against the independent xrdp oracle; scalar and vector paths are identical.
Normative specification:
[`PRD/slices/129-view-construction.md`](PRD/slices/129-view-construction.md).

## #130 — bounded standard-NUT demuxer

**Status: TODO; blocked on #129.** Re-author the independent standard-NUT
subset as a pure leaf.

**Development inventory (not source text):** `xrdp/xrdp_nut.{c,h}`,
`tests/xrdp/test_avc444_nut.c`,
`tests/xrdp/avc444/fixture_4frame.nut` and `PROVENANCE.md`.

**Acceptance:** valid fixture, byte fragmentation, truncation, CRC, file-id
and total-ceiling cases pass; no server caller exists. Normative specification:
[`PRD/slices/130-nut-demuxer.md`](PRD/slices/130-nut-demuxer.md).

## #131 — Annex-B validation and parameter-set policy

**Status: TODO; blocked on #130.** Re-author bounded Annex-B inspection plus the
recorded SPS/SEI interoperability transforms used by current hardware
profiles. LTR rewriting is not in this slice.

**Development inventory (not source text):** base functions in
`xrdp/xrdp_h264_annexb.{c,h}` and base cases in
`tests/xrdp/test_avc444_h264.c`.

**Acceptance:** reset packet, missing/duplicate parameter sets, malformed NAL,
HRD sanitization and `pic_struct` vectors pass. Explicit `fault_*` injections
remain dev-only. Normative specification:
[`PRD/slices/131-annexb-and-parameter-policy.md`](PRD/slices/131-annexb-and-parameter-policy.md).

## #132 — pure AVC capability classification

**Status: TODO; blocked on #131.** Re-author client-capability classification
and server mode choice as pure logic. Do not advertise a live backend.

**Development inventory (not source text):**
`xrdp/xrdp_avc444_caps.{c,h}` and
`tests/xrdp/test_avc444_caps.c`.

**Acceptance:** table-driven v8/v8.1/v10.0/v10.1/v10.2–10.7, AVC-disabled,
unknown-version and v2-support cases pass. Normative specification:
[`PRD/slices/132-capability-classifier.md`](PRD/slices/132-capability-classifier.md).

## #133 — secure ffmpeg runner and behavioral probe

**Status: TODO; blocked on #132.** Re-author spawn, descriptor layout,
nonblocking pipe pump, bounded collection, termination/reaping, static
`dump_extra` verification, one-frame `probesize`, pipe-size negotiation and
failure classification. LTR and multi-monitor pump-set behavior are later.

**Development inventory (not source text):** base portions of
`xrdp/xrdp_encoder_ffmpeg.{c,h}` and
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

**Development inventory (not source text):** relevant portions of
`xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_mm.c`, `xrdp/xrdp_types.h` and `tests/xrdp/test_xrdp_egfx.c`.

**Acceptance:** existing x264/OpenH264 paths are unchanged; internal
AVC420/444 transactions are unit-testable; no configuration or capability can
reach the new path. Normative specification:
[`PRD/slices/134-inactive-encoder-integration.md`](PRD/slices/134-inactive-encoder-integration.md).

## #135 — LC=1/LC=2 wire serialization

**Status: TODO; blocked on #134.** Add AVC420 and AVC444 serializers. AVC444
is born as luma LC=1 followed by chroma LC=2; LC=0 never enters history.

**Development inventory (not source text):** serializer portions of
`xrdp/xrdp_encoder.{c,h}` and
`tests/xrdp/test_avc444_metablock.c`.

**Acceptance:** exact metablock, even-origin/even-extent, region, codec-id and
two-PDU byte vectors pass; the backend remains unadvertised. Normative
specification:
[`PRD/slices/135-avc-wire-serialization.md`](PRD/slices/135-avc-wire-serialization.md).

## #136 — two-slot capture and fail-early shared memory

**Status: TODO; blocked on #135.** Re-author two slots per monitor, per-monitor
slot rotation, snapshot ownership and up-front backing-store reservation.

**Development inventory (not source text):** `common/os_calls.c`,
`common/xup_client_info.h`, capture portions of
`xrdp/xrdp_encoder.{c,h}` and `xrdp/xrdp_mm.c`,
`tests/xrdp/test_avc444_multimon.c`; xorgxrdp `module/rdpClientCon.{c,h}` and
`module/rdpMisc.c`.

**Acceptance:** layout, alternation, capacity, failure-before-session and
paired ownership tests pass; allocation failure cannot become a later SIGBUS.
Normative specification:
[`PRD/slices/136-two-slot-capture.md`](PRD/slices/136-two-slot-capture.md).

## #137 — reference-safe AVC444 topology

**Status: TODO; blocked on #136.** Re-author the mandatory decode-topology
invariant: main pictures never reference auxiliary pictures; auxiliary IDRs
become non-IDR intra leaves on the shared chain.

**Development inventory (not source text):** relevant functions in
`xrdp/xrdp_h264_annexb.{c,h}` and
`xrdp/xrdp_encoder_ffmpeg.{c,h}`; leaf cases in
`tests/xrdp/test_avc444_h264.c`.

**Acceptance:** golden leaf vectors, malformed/truncated rejection and both
decoder-topology simulations pass. Normative specification:
[`PRD/slices/137-reference-safe-topology.md`](PRD/slices/137-reference-safe-topology.md).

## #138 — long-term-reference chain, re-key and scheduled intra refresh

**Status: TODO; blocked on #137.** Re-author per-view LT0/LT1 rewriting, bounded
frame-number re-key without surface churn, and independent scheduled main/aux
intra refresh.

**Development inventory (not source text):** LTR portions of
`xrdp/xrdp_h264_annexb.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_encoder.{c,h}` and
`xrdp/xrdp_mm.c`; `tests/xrdp/test_avc444_ltr.c`, its two golden headers, and
LTR/re-key/intra cases in `test_avc444_ffmpeg.c` and `test_tconfig.c`.

**Acceptance:** golden bytes, Windows-field cross-check, both decode modes,
sparse cadence, wrap/restart, observed-vs-requested cuts and live real-ffmpeg
cut cases pass. Normative specification:
[`PRD/slices/138-ltr-rekey-intra.md`](PRD/slices/138-ltr-rekey-intra.md).

## #139 — one-thread multi-monitor pump set and batch emission

**Status: TODO; blocked on #138.** Re-author one encoder pair per monitor,
one poll set over all children, shared deadline, per-monitor geometry/LTR state
and inline batch assembly. The removed emit thread does not return.

**Development inventory (not source text):** multi-monitor portions of
`xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_mm.c`, `xrdp/xrdp.h`,
`tests/xrdp/test_avc444_multimon.c`, `test_avc444_emit_split.c`,
`test_avc444_ffmpeg.c` and `test_xrdp_egfx.c`.

**Acceptance:** independent monitor identity/state, sequence zero, unarmed and
failure states, four-view pump-set and two-monitor live correctness pass.
Normative specification:
[`PRD/slices/139-multimon-pump-set.md`](PRD/slices/139-multimon-pump-set.md).

## #140 — paired credit frontier and split acknowledgement

**Status: TODO; blocked on #139.** Re-author per-monitor capture admission,
slot-only acknowledgements, separate region frontier, bounded wire credit and
trace-only transport queue accounting. Shipped `wire_window` is 1;
`eager_slot_ack` is on.

**Development inventory (not source text):** `common/xup_client_info.h`,
`common/trans.{c,h}`, `xup/xup.c`,
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
Re-author time-based chroma refresh/idle scheduling and skip auxiliary
submission before it can advance the encoder DPB.

**Development inventory (not source text):** sparse portions of
`xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/xrdp_h264_annexb.{c,h}`,
`xrdp/xrdp_mm.c`, `xrdp/xrdp_tconfig.{c,h}`,
`tests/xrdp/test_avc444_chroma_due.c`, `test_avc444_convert.c`, sparse cases in
`test_avc444_ltr.c`, `test_avc444_ffmpeg.c` and `test_tconfig.c`.

**Acceptance:** disabled equivalence, refresh-plus-one-frame bound, settle/rate
clamps, one-shot trailing restoration without future damage, independent
intra schedules and DPB continuity pass. This internal
slice has no real-client replay; the activated candidate runs the live client
gate specified by #142 only after activation. Normative specification:
[`PRD/slices/141-sparse-chroma.md`](PRD/slices/141-sparse-chroma.md).

## #142 — configuration, activation, operating docs and final paired gate

**Status: TODO; blocked on #141.** Make the fully assembled backend
selectable only here. Add the user-facing configuration and documentation for
the mechanisms already green; do not carry `tail_flush` or explicit fault
injection.

**Development inventory (not source text):** `xrdp/xrdp_tconfig.{c,h}`,
`xrdp/xrdp_types.h`,
`xrdp/xrdp_mm.c`, `xrdp/gfx.toml`, `docs/man/gfx.toml.5.in`,
`tests/xrdp/check_operator_surface.sh`, `tests/xrdp/test_tconfig.c` and its
`tests/xrdp/gfx/*.toml` fixtures.

**Acceptance:** bounds/refusals/defaults, removed-key warning, capability
activation, probe-before-confirm, no fallback, resize lifecycle, operator
surface entropy scan and the complete client, multi-monitor, sparse and
numerical gates defined by the normative specification pass on builds made
from the clean-room paired branches. Default and trace-enabled CI-equivalent
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
| #123 | T4/client compatibility closed with forced-v1 macOS per-pixel fidelity RED; auto prefers v2 and v1 remains a legacy capability tier. | `docs/experiments/123-t4-nvenc-compatibility.md` |
| #124/#124B | Windows/macOS AVC444v2 visual qualification and both corrected flood payloads passed. | `docs/experiments/124-credit-frontier-client-qualification.md` |
| #201 | Paired bases pinned and the monolithic PRD replaced by one normative file per clean-room slice. | `docs/experiments/201-prd-refactor.md` |
