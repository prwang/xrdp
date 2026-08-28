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

**Status: IN PROGRESS; corrected automated gates are green, but the owner
visual matrix is blocked by #142D.** The resize repair was re-authored in
owning slice 136 and
all descendants through 142 were replayed. The replacement trace-disabled
x042 arm on `127.0.0.1:40058` stayed connected through the exact growth-resize
sequence, allocated the current 19,611,648-byte mapping and rendered the final
2412-by-1344 XFCE frame. The default and trace-enabled suites, paired producer
tests, automatic-profile certificate and two-size rendered smoke are green.
Windows/macOS acceptance across `auto`, forced AVC444v2, AVC444v1, AVC420 and
sparse profiles remains open after the repeated-resize transport teardown is
understood and corrected. Execution record:
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

**Status: IN PROGRESS; the allocation repair remains proven, but #142D
invalidated the prior complete-audit claim.** Development reconciliation, the
canonical 40059/40060 allocation red-green proof, the owning-slice clean-room
correction, descendant replay and replacement 40058 deployment are complete.
The committed requirement matrix in
[`docs/experiments/142c-dev-cleanroom-equivalence.md`](docs/experiments/142c-dev-cleanroom-equivalence.md)
classified every #126–#142 requirement but did not inventory the clean-room
pinned-base delta or preserve the clean-room AVC444 order-64 ingress in
development. Its conclusion that no normative divergence remained is
superseded: with the same Windows client,
capset, dense AVC444v2 profile and resize interaction, development 40060
survived 64 completed resizes without another GFX capability
advertisement, while clean-room emitted a post-resize frame which the client
did not acknowledge and received a new advertisement 53 ms later. That
clean-room-only precursor must be isolated and reconciled before this item can
close.

The final trace-disabled 40059 packages built from the canonical development
trees reproduce the intended mechanism:
Xorg grows to 2412 by 1344 while retaining the login-sized 16,760,832-byte
mapping, xrdp derives the current 19,611,648-byte requirement and terminates
the connection on the stale snapshot. Evidence:
[`PR-demo/mac_bisect_matrix/captures/i142c_x043_canonical_red_20260825T171141Z/README.md`](PR-demo/mac_bisect_matrix/captures/i142c_x043_canonical_red_20260825T171141Z/README.md).

The same client sequence on repaired port `40060` caused Xorg to allocate the
required 19,611,648-byte mapping before capture resumed. The client remained
connected and a lossless 2412-by-1344 rendered frame was captured after five
seconds. Only the xorgxrdp resize repair differs at runtime. Evidence:
[`PR-demo/mac_bisect_matrix/captures/i142c_x044_canonical_green_20260825T172113Z/README.md`](PR-demo/mac_bisect_matrix/captures/i142c_x044_canonical_green_20260825T172113Z/README.md).

The transactional refresh is in rewritten clean-room slice 136 rather than an
appended fix. Each descendant through 142 passed its independent default,
trace-enabled and static gates before the next was admitted. The replacement
40058 arm then passed the same growth-resize sequence and two-size rendered
smoke. Exact identities and evidence:
[`PR-demo/mac_bisect_matrix/captures/i142c_cleanroom_resize_green_20260826T182220Z/README.md`](PR-demo/mac_bisect_matrix/captures/i142c_cleanroom_resize_green_20260826T182220Z/README.md).

The current manual reconciliation starts from canonical development xrdp
`83bcb274bd29` and xorgxrdp `985bc42d335a`. It incorporates the ten non-merge
channel-reassembly and bounds-hardening commits between development's merge
base and pinned xrdp base `fe850a22c08a`, and changes development AVC444
capture ingress from preassembled order 62 to the clean-room order-64
shared-memory message. A bounded adapter constructs and independently parses
the exact GFX frame envelope before handing it to the existing development
encoder. Default and trace-enabled complete gates are green. The updated 40060
arm remains a RED equivalence gate until the owner reproduces the repeated
resize precursor there; a green interactive result means more clean-room-only
behavior remains to be found, not that #142D is fixed.

The trace-disabled reconciled pair is deployed at `127.0.0.1:40060` with a
configuration-keyed wire/decode certificate. A first certificate with an empty
profile hash was rejected and retained after exposing that the x044 deploy
mounted x043's file while the certifier assumed a same-named profile; the
certifier now fails closed and x044 owns the explicit equivalent profile.
Deployment evidence:
[`PR-demo/mac_bisect_matrix/captures/i142c_x044_manual_reconcile_20260828T123514Z/README.md`](PR-demo/mac_bisect_matrix/captures/i142c_x044_manual_reconcile_20260828T123514Z/README.md).

**RED 2026-08-28:** the owner completed twenty resize interactions without the
clean-room symptom; server evidence contains 44 resets on the first Windows
connection and 26 on the reconnected preserved session, with exactly one
capability advertisement per connection. The order-64 adapter immediately
reconstructed the old order-62 blob and re-entered the old development encoder,
so it did not reconcile clean-room's raw-capture ffmpeg execution path. Resume
that reconciliation before any capability-lifecycle correction. Evidence:
[`PR-demo/mac_bisect_matrix/captures/i142c_x044_windows_green_20260828T152300Z/README.md`](PR-demo/mac_bisect_matrix/captures/i142c_x044_windows_green_20260828T152300Z/README.md).

The next development revision retains the validated order-64 capture as a raw
borrowed item through the encoder FIFO and constructs the legacy-compatible
GFX envelope only on the encoder worker. This removes the proven main-thread
queue divergence without transplanting clean-room source. Its complete
default and trace-enabled suites are green, and tracing is absent from the
restored default binary; deploy it trace-disabled on 40060 and repeat the same
Windows resize gate. Do not repair the replacement-capability callback unless
this arm first reproduces that callback's client-side precursor.

The replacement clean-room acceptance image on port `40058` retains XFCE,
LXTerminal, default xterm and every indexed visual/benchmark helper. It also
provides working Glamor acceleration, Thunar and a Chromium launcher whose
desktop entry states and applies the container sandbox/shared-memory flags.
The profile switcher and the complete checked visual-matrix draft are copied
to the tester account's home as the final handoff step.

The allocation result remains closed evidence; the missing post-resize
wire/lifecycle equivalence is owned jointly by #142C and #142D. Normative plan
and complete acceptance:
[`PRD/gates/142c-dev-cleanroom-equivalence.md`](PRD/gates/142c-dev-cleanroom-equivalence.md).

## #142D — repeated interactive resize tears down the client transport

**Status: IN PROGRESS; blocks the remaining #142 visual matrix.** Repeated
Windows `auto` walks establish a two-stage failure: a resize leaves persistent
black regions, and the following resize completes server-side before the RDP
transport closes without a TLS shutdown. Every observed geometry has a
correctly sized producer mapping, so this is not the stale-layout defect
corrected by #142C. This is not an `auto`-mode difference: the green 40060
FreeRDP arm also used `auto` and selected dense AVC444v2. Its retained log has
one GFX capability advertisement before login and none after Xorg attachment;
the failing Windows connection advertises once for the login screen and again
after attaching the desktop. Source audit found a reachable lifecycle defect
in both source trees if that second callback occurs, while
`xrdp_mm_egfx_caps_advertise()` treats the second advertisement as initial
setup: it resets and recreates a blank client surface, replaces a live
`mm->encoder` without deleting it, and then deliberately skips its repaint
helper because Xorg supplies GFX directly. No complete current Xorg frame is
requested for the new surface, so later damage alone fills parts of it. This
explains the persistent black regions. The following reset reaches the TLS EOF
with orphaned encoder/surface state already present; a controlled correction
must still prove whether that same defect is the complete reason the Windows
client closes.

**Open:** the fault-preserving port-40061 arm reproduced the complete Windows
sequence. It observed a second capability callback with a live direct-Xorg
encoder, no retirement before the owner pointer was overwritten, and no
producer frame after the replacement surface was published. The replacement
still had frame counters 0/0/0 when the next resize began 6.6 seconds later.
That resize requested and received a complete producer frame and xrdp sent its
two logical frames successfully; the peer then closed without acknowledging
either. Ten encoder installations and nine complete deletions prove one
orphan, but the trace contains no stale PDU sent by it, so do not narrow the
EOF cause to the leak alone.

The clean-room-specific precursor is now the first open boundary. The same
Windows client sent no second advertisement to development 40060 through 64
completed resizes, but on clean-room it withheld the acknowledgement for
the first frame after the seventh resize and sent a new advertisement 53 ms
later. Audit the client-visible resize frame, graphics-envelope serialization,
frame/ack ordering and encoder output until that difference is identified.
Reproduce the same trigger on canonical development by reconciling all
normative clean-room-only behavior before correcting it. A handler-only change
on development would exercise no observed failing transition and therefore
would not be a causal fix.

Once the red development transition exists, correct it there: completely
retire the live encoder before state replacement, publish exactly one
successor and request one current direct-producer frame only after it is ready.
Then repeat the same Windows sequence. If the peer EOF remains after both the
precursor and transition are correct, preserve it as a separate red defect
rather than masking it with a retry, fallback, mode change or timeout. Only a
green causal gate may be re-authored into the clean-room history. Raw
reproduced evidence and the bounded verdict are in the linked experiment
record.

**Acceptance:** an exact development/clean-room wire-transition comparison
accounts for the last acknowledged frame, first unacknowledged frame and
subsequent capability advertisement using explicit identities. The same
reconciled development build first reproduces the clean-room red transition.
A deterministic lifecycle test then proves repeated capability advertisement
cannot replace a live encoder without complete teardown and
requests exactly one current full-screen producer capture after the successor
is ready. A rendered-frame test proves the reset/resize surface is fully
repainted rather than merely alive. A fresh login control and a reconnect to an
existing desktop cover both pre-attach and post-attach capability lifecycles;
`auto` and forced-444 profiles prove codec selection does not hide the
lifecycle result. One repeatable client sequence identifies which endpoint
closes first. The corrected development and clean-room remain connected and
repaint after both spaced and rapid grow/shrink changes; current layout
allocation and snapshot validation remain green; no retry, fallback or
suppressed terminal error masks a failure. The acceptance arm's checked visual
instructions and profile reference must also survive a container restart and
be readable by the non-root desktop account. Evidence and current limits:
[`docs/experiments/142d-repeated-resize-transport-teardown.md`](docs/experiments/142d-repeated-resize-transport-teardown.md).

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
