# BACKLOG

This is the open work list. Each open item states the unresolved question or
implementation, why it matters, its acceptance condition, and a durable
pointer. Completed experiments live in `docs/experiments/`; normative product
requirements live in `PRD/`; per-run evidence stays with its capture under
`PR-demo/mac_bisect_matrix/captures/`.

Do not add result tables or completed narratives here. A closed item keeps one
stub at the end of this file and points to its record.

## Current repository state — 2026-08-25

* `/work` and `/workUpdateXorgXrdp` are both on
  `dev/avc444_metablock_checkpoint`; the qualified runtime frontiers are xrdp
  `518e9575` and xorgxrdp `c190343`. The xrdp branch head additionally carries
  the copy-safe operator and clean-room documentation. The runtime frontier
  includes #125C's exact-topology probe and terminal no-respawn handling;
  xorgxrdp remains at the qualified producer frontier which includes #120's
  per-frame logger removal.
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

The porting list is one chain. #125 is closed; #126 is the first open item and
an item does not start until its predecessor is closed. #126–#142 re-author
the qualified development tree as the clean-room commit series. #143 is
explicitly later architecture work and cannot change or qualify that series.
#300 is the public-PR documentation deliverable after #142; it does not block
#143 and is not permission to publish or push anything.

For every dev qualification, commit the exact procedure, paired source and
package identities, configuration, workload, instrument and expected checks.
The clean-room run changes no arm, knob or interpretation: it replays that
anchor on an immutable candidate tree. A red clean-room slice never becomes a
commit, and the next slice does not start. The final paired tree is not frozen
history until all retained replays are green.

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

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #127 — generic compile-time performance tracer foundation

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #128 — paired AVC capture and diagnostic wire contract

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #129 — full-chroma view construction and producer packing

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #130 — bounded standard-NUT demuxer

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #131 — Annex-B validation and parameter-set policy

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #132 — pure AVC capability classification

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #133 — secure ffmpeg runner and behavioral probe

**DONE.** Reconstructed, corrected to consume the capture contract's 16-row
coded height, and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #134 — inactive server encoder integration

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #135 — LC=1/LC=2 wire serialization

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #136 — two-slot capture and fail-early shared memory

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #137 — reference-safe AVC444 topology

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #138 — long-term-reference chain, re-key and scheduled intra refresh

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #139 — one-thread multi-monitor pump set and batch emission

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #140 — paired credit frontier and split acknowledgement

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #141 — sparse auxiliary cadence

**DONE.** Reconstructed and independently gated; see
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

## #142 — configuration, activation, operating docs and final paired gate

**Status: IN PROGRESS; real-client dynamic resize is red.** The trace-disabled
clean-room x042 arm on `127.0.0.1:40058` disconnected after a growth resize:
xorgxrdp retained the old AVC444 capture layout while xrdp advanced to the new
geometry, and xrdp correctly rejected the first mismatched slot snapshot. The
development pair must first reproduce or exclude the defect under the same
geometry transition; any fix is implemented and validated there before the
owning clean-room slice is corrected and its descendants replayed. Automated
gates, the numerical replay and all five local profile certificates preceding
this live failure remain recorded evidence, not closure. Execution record:
[`docs/experiments/126-142-cleanroom-reconstruction.md`](docs/experiments/126-142-cleanroom-reconstruction.md).

The fully assembled backend becomes selectable only here. The user-facing
configuration and documentation bind the exact loaded configuration to the
existing behavioral probe and map a terminal result to one connection hangup.
`tail_flush`, explicit fault injection, a retry, a fallback or a second
forensic mechanism are out of scope.

**Development inventory (not source text):** `xrdp/xrdp_tconfig.{c,h}`,
`xrdp/xrdp_types.h`, `xrdp/xrdp_mm.c`, `xrdp/xrdp_encoder.{c,h}`,
`xrdp/xrdp.h`, `xrdp/xrdp_encoder_ffmpeg.{c,h}`, `xrdp/gfx.toml`,
`docs/man/gfx.toml.5.in`, `tests/xrdp/Makefile.am`,
`tests/xrdp/test_avc444_ffmpeg.c`,
`tests/xrdp/check_operator_surface.sh`, `tests/xrdp/test_tconfig.c`,
`tests/xrdp/test_xrdp_egfx.c` and the `tests/xrdp/gfx/*.toml` fixtures.

**Acceptance:** bounds/refusals/defaults, removed-key warning, capability
activation from the earlier exact-probe result, configuration-to-probe
identity, terminal hangup/no-respawn, no fallback, resize lifecycle, operator
surface entropy scan and the complete client, multi-monitor, sparse and
numerical gates defined by the normative specification pass on builds made
from the clean-room paired branches. A growth resize shall rebuild the producer
layout and backing store atomically before capture resumes, and a deterministic
test plus one short dynamic-resolution client run shall prove that no old-size
snapshot is emitted. Default and trace-enabled CI-equivalent matrices are
green. Normative specification:
[`PRD/slices/142-activation-and-docs.md`](PRD/slices/142-activation-and-docs.md).

## #142C — reconcile dev/clean-room behavior and prove resize correction

**Status: IN PROGRESS; plan committed before implementation.** #142 is
blocked on a complete semantic audit of every #126–#142 requirement across the
current paired development and clean-room trees. Source-shape differences are
allowed; any normative behavior, bound, default, failure policy or operator
surface present in only one implementation is not. The audit must classify and
resolve every divergence before either live comparison arm is built.

After reconciliation, port `40059` runs the full development frontier plus
every normative behavior found only in clean-room, retaining the observed stale
growth-resize behavior. Port `40060` is the same paired build and configuration
plus only the proposed resize repair. One versioned dynamic-resolution client
script must make `40059` reproduce the stale 16,760,832-byte mapping and
disconnect, while `40060` allocates 19,611,648 bytes, stays connected and
renders after the same 2412-by-1344 update. No trace, retry, fallback or codec
change is part of the comparison.

Only a green development audit, paired CI and red/green arm proof authorizes
re-authoring the repair into its owning clean-room slice and replaying all
descendants through #142. Normative plan and complete acceptance:
[`PRD/gates/142c-dev-cleanroom-equivalence.md`](PRD/gates/142c-dev-cleanroom-equivalence.md).

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
| #125 | Sparse/T4 qualification retained; the CABAC template is copy-safe and explained, the exact leaf topology is probed, and post-confirm failure is terminal without damage-driven respawn. | `docs/experiments/125-sparse-chroma-qualification.md` |
| #201 | Paired bases pinned and the monolithic PRD replaced by one normative file per clean-room slice. | `docs/experiments/201-prd-refactor.md` |
