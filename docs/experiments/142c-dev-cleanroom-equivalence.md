# #142C development and clean-room equivalence audit

## Scope and identities

This audit compares the development xrdp functional frontier
`6f5b90311b1f831b72369a13951d7a2269d4f27f` (branch-cut tip including the
subsequent gate documentation:
`daa64d8f4d9cbbcdbe2fb68b109f2d96463fb155`) and xorgxrdp
`c190343ff28a61e45b8307993f0fdd54fb596b29` with
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
| #128 | Fixed-width full-chroma capture metadata, exact peer version, format/alignment/geometry bounds and per-frame monitor/slot identity. | **Category 2.** Development had only recomputed offsets plus an untyped `shmem_offset`; it did not carry or validate the complete layout or marked slot identity. Reconciliation adds the typed layout, exact size/version check, marked identity and independent consumer validation. Clean-room sends this capture through its dedicated paint message; development retains its established message-62 GFX envelope, so the validator is attached to `server_egfx_cmd()` after parsing that exact envelope. The differing message shape is category 1; the validation behavior is category 2. | The new development resize-contract test derives both mapping sizes and rejects a previous geometry; the GFX-envelope test independently verifies the capture identity and shared-memory offset parser. The existing clean-room mutation/format/limit cases remain the exhaustive contract gate. |
| #129 | BT.709 full-range conversion, v1/v2/main-only packing, 16/32 width and 16-row height alignment, edge replication and non-aliasing vector/scalar paths. | Category 1 for pixel behavior. The reconciled producer consumes #128's typed offsets and coded geometry, which is the category-2 integration seam rather than a second converter. | Both conversion suites use specification vectors; paired xorg scalar/vector golden test; nonaligned resize-stride cases. |
| #130 | Incremental bounded standard-NUT parser with stable malformed/truncated/ceiling failures and retained fixture provenance. | Category 1. | Six-case `Avc444Nut` suites, including every-byte fragmentation and every truncation point. |
| #131 | Bounded Annex-B parsing, parameter-set policy, HRD sanitation and pic-struct removal. | Category 1. | Base `Avc444H264` golden and rejection cases in both trees. |
| #132 | Pure RDPGFX capability classifier, including the v10.1 distinction, forced modes and no fallback. | Category 1. | Seven table-driven classifier groups in both trees. |
| #133 | Shell-free ffmpeg runner, nonblocking `vmsplice` pump, 64-KiB pipe gate, 16-row geometry, topology-aware probe, stable terminal classes and bounded private forensics. | Category 1. Development decomposes forensic writers differently but has the same first-failure, permission, topology and no-retry behavior. | Live stock-ffmpeg runner suites; fake hang; probe topology; post-confirm terminal-once test; clean-room permission/exclusive forensic cases. |
| #134 | Worker-owned inactive ffmpeg integration, matched pair publication, terminal latch, damage return and no fallback/restart. | Category 1. | EGFX terminal and lifecycle cases plus development post-confirm repeated-damage test. |
| #135 | Exact AVC420/AVC444 LC=1/LC=2 serialization, metablock bounds, no LC=0 and checked capacity. | Category 1. | Specification-derived `Avc444Metablock` and EGFX command parsing/capacity cases. |
| #136 | Exactly two capture slots per monitor, stable borrowed snapshots, fail-early fully backed allocation and cleanup/recreate on geometry change. | **Category 2** at the resize ownership boundary. Clean-room clears producer slot ownership after xrdp deletes the encoder and acknowledges every borrowed capture; development retained its budget through resize. Reconciliation resets the producer budget at that proven empty boundary. Development otherwise already reserves backing and recomputes its old layout. Clean-room's allocation from the typed login layout is deliberately retained for the 40059 red arm and is not accepted as final behavior. | Slot/budget identity and shared-memory reservation tests; ffmpeg resize/reap test; paired xorg tests; real resize arm below. |
| #137 | CABAC auxiliary-IDR-to-leaf transform with typed rejection and production-topology probe. | Category 1. | Auxiliary-leaf golden/rejection cases and live two-child probe in both trees. |
| #138 | Independent LTR chains, re-key, frame-wrap handling and independent intra schedules. | Category 1. | Twenty-eight independent DPB/bitstream/schedule cases and live re-key/cut probes in both trees. |
| #139 | One worker poll set for all monitor children, identity-matched completion, inline deterministic batch assembly and isolated failure. | Category 1. | Pump-set, publication-state, unequal-monitor and EGFX batch suites. |
| #140 | Per-monitor slot plus global wire-credit admission, split slot/region frontiers, damage return, cumulative monotonic acknowledgements and frozen-client bound. | Category 1 for flow behavior. Development uses echoed rectangle identities and held-region maps; clean-room uses typed slot owners. #128's missing marked monitor/slot identity is the sole category-2 validation gap and is reconciled there. | Thirteen credit-frontier cases, development region-loss/eager-ack models, paired producer admission tests and trace-disabled counter-footprint gate. |
| #141 | Dense equivalence at zero, bounded time-only sparse cadence, independent auxiliary state and one-shot full-chroma convergence after quiescence. | Category 1. | Ten cadence/deadline cases, sparse DPB/live runner cases and the retained final-convergence interactive result. |
| #142 | Opt-in activation, exact config-to-probe transfer, documented defaults/ranges/tradeoffs, one terminal hangup, copy-safe software block and operator-only public prose. | **Category 2** for three removed development-only configuration keys: current development still accepted `tail_flush`, `fault_aux_delay` and `fault_strip_mmco`; clean-room correctly makes H.264 unavailable when they appear. Reconciliation rejects them while retaining internal deterministic fault APIs. Other invalid values differ only within the PRD's allowed unavailable-or-documented-safe-default policy. **Category 4** consists of historical/internal labels in development comments and unexposed diagnostic fields; none is copied into clean-room. | `GfxLoad`, operator-surface gate, complete config-to-probe transfer, terminal-once post-confirm test, default/trace builds and live arm. |

## Reconciliation boundary

The development reconciliation therefore changes three product contracts:

1. the xup pair gains the complete fixed-width layout and marked slot identity,
   xorgxrdp validates it before use, and xrdp validates each mapped snapshot
   against current client geometry on development's actual GFX-command
   ingress;
2. producer capture-slot ownership resets at the resize boundary where the
   consumer has already deleted the encoder and retired every borrowed slot;
   and
3. the three removed development-only configuration keys disable H.264 instead
   of activating diagnostic behavior.

For 40059, xorgxrdp intentionally allocates and packs from the typed layout
received at login. Its own display geometry still changes on a RandR update,
but that cached layout does not. This is the actual clean-room behavior, not a
fault-injection switch. The consumer independently rebuilds the expected
layout from current client geometry, so growth outside the original alignment
class must be rejected.

## Exact development delta used to construct 40059

The requirement matrix above guided the reconciliation; 40059 was not built by
copying only the clean-room resize or FFmpeg lifecycle. The functional delta
from development xrdp `daa64d8f` to the reconciled red xrdp `253efd0a`, and
from xorgxrdp `c190343` to `8cf120e`, is limited to the four category-2
contracts below. Deployment, evidence and temporary diagnostics commits in
those ranges are not product behavior; the diagnostics were removed from the
packages used for the final arm.

| new development behavior | owning commits and code seams | principal regression risk | independent defence before clean-room replay |
|---|---|---|---|
| Complete fixed-width capture layout crosses the xup boundary, and the producer requires an exact structure size/version and validates format, alignment, geometry, disjoint regions and total bytes. | xrdp `64e69727`: `common/xup_client_info.h`, `xup/xup.c`; xorgxrdp `f8a0489`: `module/rdpClientCon.c` | **High, paired-ABI and allocation risk.** Mixed packages now fail explicitly. Treating the login layout as permanent caused the growth-resize defect exposed by 40059. | Specification-derived layout sizes, bounds and disjointness in `Avc444Multimon`; paired xorg tests; exact package identities. The resize defect is corrected and separately proven by 40060 below. |
| Every full-chroma capture carries explicit monitor/slot identity; xorgxrdp selects the typed slot planes and xrdp validates identity, mapping size and offset on development's real message-62 GFX ingress before queuing an encode. | xrdp `64e69727`, corrected ingress in `24710989`: `common/xup_client_info.h`, `xrdp/xrdp_mm.c`, `xrdp/xrdp_encoder.{c,h}`; xorgxrdp `f8a0489`: `module/rdpCapture.c`, `module/rdpClientCon.c` | **High, data-plane and fail-closed risk.** A wrong identity could select another monitor's pages; an over-strict or misplaced validator could terminate valid sessions. The first attempt was misplaced on the clean-room-only paint ingress and failed to reproduce the defect. | Pure identity/offset/layout cases, exact-envelope parser truncation tests and complete suites in both repositories. 40059 proves the actual ingress rejects a genuinely stale mapping; 40060 proves a current mapping passes it and renders. |
| Producer capture-slot ownership resets at the resize boundary after xrdp has deleted the encoder and acknowledged all borrowed slots. | xorgxrdp `25a273a`: `module/rdpClientCon.c` | **High if the boundary precondition is false.** Resetting while a consumer still borrows a slot would permit page reuse and content/region desynchronization. | Existing two-slot ownership/frontier tests establish the no-alias rule; the resize state machine supplies the terminal acknowledgement before Xorg changes geometry. The paired real-client arms exercise the boundary without fallback or retry. |
| Removed development-only keys `tail_flush`, `fault_aux_delay` and `fault_strip_mmco` make H.264 unavailable instead of silently activating stale diagnostic behavior. | xrdp `264e010b`: `xrdp/xrdp_tconfig.c`, config fixture and `test_tconfig.c` | **Medium, operator-visible activation risk.** A stale private configuration intentionally loses H.264 rather than running a mode absent from the PRD. Ordinary configurations must remain unchanged. | Dedicated removed-development-key parser test plus the complete `GfxLoad` suite; both live arms use the tracked operator configuration without those keys and negotiate AVC444v2. |

No FFmpeg process runner, encoder arguments, H.264 parser/rewriter, metablock
serializer, sparse cadence, wire-credit policy or multi-monitor pump behavior
was newly ported into development for 40059. Those rows are category 1 in the
matrix: independently shaped implementations already met the same normative
behavior and were left intact. This is also the limit of the audit's current
claim. The live red/green proof covers one-monitor growth resize; the retained
deterministic suites carry the multi-monitor, malformed-input, exact-wire and
configuration cases until the corrected clean-room descendants repeat their
complete gates.

## 40059 result and cause

The final trace-disabled arm used xrdp `b282b0c19ec1` and xorgxrdp
`8cf120e5db7f`. The versioned client script connected at 2196 by 1250,
requested 2198 by 1250 and then 2412 by 1344. It stayed connected after the
first resize and exited with status 12 five seconds after the second. Xorg
reported that it resized the screen to 2412 by 1344 while reusing the
16,760,832-byte mapping; xrdp completed the same resize, derived the required
19,611,648-byte layout and rejected the first stale full-chroma snapshot.
The required mapping is 2,850,816 bytes larger than the retained mapping.

The causal development-to-reconciled diff is the AVC444 allocation source in
`rdpClientConResizeAllMemoryAreas()`. Frontier development calls
`xup_cap_h264_shmem_layout()` with the current `display_sizes` on every resize.
Reconciled development instead reads `client_info.avc444_layout.total_bytes`
and its offsets. That typed layout is built when xrdp sends client information
at login. The RandR monitor update changes `client_info.display_sizes` but
does not rebuild `client_info.avc444_layout`. The producer therefore grows
its screen while retaining the login allocation and pack offsets. The new
consumer validation is not the cause: it derives layout from current geometry
and turns the unsafe size/identity mismatch into the specified terminal
disconnect. The slot-budget reset is also not the cause; it only restores the
clean-room ownership lifecycle at resize.

The first reconciliation attempt stayed connected because the validator was
placed only on `server_paint_rects_ex()`, matching the clean-room message path.
Inspection of today's development producer showed that its AVC444 captures use
the message-62 GFX envelope and enter `server_egfx_cmd()`. Moving the same
contract check to that actual ingress made 40059 reproduce the clean-room red
without changing development's transport architecture.

Raw evidence is indexed at
[`PR-demo/mac_bisect_matrix/captures/i142c_x043_reconciled_red_20260825T160510Z/README.md`](../../PR-demo/mac_bisect_matrix/captures/i142c_x043_reconciled_red_20260825T160510Z/README.md).

The reconciled default xrdp tree passed 216/216 daemon tests, and all other
`make check` suites passed. The paired xorgxrdp build and both ordinary test
programs passed. The last trace-enabled matrix was green before the new
GFX-envelope parser case was added; it has not been rerun at this point. Both
deployed packages were rebuilt with tracing compile-time absent. Temporary
one-shot resize diagnostics were removed before the final package build and
the final red reproduction.

## 2026-08-25 canonical-path provenance correction

The preceding 40059 result was built from `/workDevReconciled`, a linked
worktree sharing `/work`'s repository and linear commit ancestry. The owner
clarified that sharing commits is insufficient: development must be edited,
committed and built only in the literal canonical checkouts `/work` and
`/workUpdateXorgXrdp`. The auxiliary worktrees were retired, both canonical
checkouts were advanced to the same reconciled history, and 40059 was rebuilt
and redeployed from them.

The canonical rerun used xrdp `253efd0a41f9` and xorgxrdp `8cf120e5db7f`.
It reproduced the same mechanism and outcome: the producer reused
16,760,832 bytes after growing to 2412 by 1344, xrdp rejected the stale
snapshot, and FreeRDP exited with status 12. The owner had already confirmed
the equivalent preceding arm interactively. Canonical evidence is indexed at
[`PR-demo/mac_bisect_matrix/captures/i142c_x043_canonical_red_20260825T171141Z/README.md`](../../PR-demo/mac_bisect_matrix/captures/i142c_x043_canonical_red_20260825T171141Z/README.md).

## 40060 repair result

The repaired trace-disabled arm retains the 40059 xrdp package at
`253efd0a41f9` and changes only xorgxrdp from `8cf120e5db7f` to
`985bc42d335a`. The latter transactionally rebuilds and validates the typed
layout from the updated display description before it changes the mapping,
screen geometry or capture ownership. The shared pure refresh helper and its
failure-preserves-old-layout test are xrdp commit `346624e5b632`; that helper
does not change the deployed xrdp runtime in this comparison.

The same versioned client remained connected through 2198 by 1250 and
2412 by 1344. At the second update Xorg allocated 19,611,648 bytes rather than
reusing 16,760,832, then invalidated the 2412-by-1344 screen. xrdp completed
the update in 37 ms with no invalid-snapshot rejection. After five seconds the
client was still connected and a lossless 2412-by-1344 screenshot showed the
rendered XFCE desktop. Thus the intervention applied, the dimensions close and
the repaired behavior did not come from a codec, config, payload, retry,
fallback or tracing change.

Evidence is indexed at
[`PR-demo/mac_bisect_matrix/captures/i142c_x044_canonical_green_20260825T172113Z/README.md`](../../PR-demo/mac_bisect_matrix/captures/i142c_x044_canonical_green_20260825T172113Z/README.md).

## 2026-08-26 corrected clean-room replay

The repair was re-authored in slice 136, the slice which first owns replacement
capture allocation. It transactionally derives and validates a candidate
layout before publishing any mapping, geometry or slot-ownership change. A
failed replacement leaves the previous valid layout byte-identical. The paired
producer rebuilds that layout from the current display description before
allocating its two-slot mapping.

Every descendant through slice 142 was replayed from that corrected owner.
Both default and trace-enabled full suites and the xrdp static gates passed at
each new commit; the final daemon suite is 204/204 in both modes. Paired
xorgxrdp build/tests passed at its changed slices. The exact identities, gate
table, packages and retained runtime proof are in
[`PR-demo/mac_bisect_matrix/captures/i142c_cleanroom_resize_green_20260826T182220Z/README.md`](../../PR-demo/mac_bisect_matrix/captures/i142c_cleanroom_resize_green_20260826T182220Z/README.md).

The replacement 40058 arm then ran the same 2196-by-1250 to 2198-by-1250 to
2412-by-1344 sequence. Xorg allocated the correct 19,611,648-byte mapping,
the client stayed connected and the retained lossless frame is a rendered
2412-by-1344 XFCE desktop. This supersedes the original clean-room red arm;
the original record remains unchanged as the defect evidence.

## 2026-08-28 behavioral-equivalence verdict superseded

The allocation correction and its red/green evidence above remain valid. The
broader conclusion that no category-3 or category-5 behavioral divergence
remained does not.

The same Windows client exercised development 40060 and the instrumented
clean-room candidate with the same nine GFX capsets, v10.7 confirmation,
dense AVC444v2 software profile and interactive resizing. Development survived
64 completed resizes without another capability advertisement, black
regions or disconnect. Clean-room completed six resize cycles, then sent the
first frame after the seventh resize; the client did not acknowledge that
frame and sent a new capability advertisement 53 ms later. The callback then
orphaned the live encoder and published a surface which remained at frame
counters 0/0/0 until the next resize.

The original audit compared normative requirements, source ownership and
deterministic gates. It classified clean-room's dedicated capture/encode path
and development's message-62 GFX path as equivalent implementations without
an exact client-visible post-resize transition gate. The Windows result
falsifies that unqualified classification. Shared unsafe callback code proves
only what either tree would do *if* it received a second advertisement; it
does not reconcile why only clean-room reached that state.

#142C is therefore reopened for the missing wire/lifecycle equivalence. The
current boundary is after clean-room's final ResetGraphics and first
post-resize frame, and before the replacement advertisement. The exact frame
payload, graphics-envelope serialization, frame/ack ordering and capture
ingress remain candidates; none is yet the root cause. The complete trace and
bounded lifecycle finding are recorded in
[`142d-repeated-resize-transport-teardown.md`](142d-repeated-resize-transport-teardown.md).
The same-client development control is retained in
[`i142d_x044_windows_control_20260828T004123Z/`](../../PR-demo/mac_bisect_matrix/captures/i142d_x044_windows_control_20260828T004123Z/README.md).

## 2026-08-28 second audit: pinned base and capture ingress were missed

The superseding result above triggered a fresh file-range inventory. It found
two omissions which make the earlier “complete divergence audit” invalid
independently of the Windows result.

First, clean-room xrdp is based on `fe850a22c08a`, but development xrdp
`83bcb274bd29` diverges from it at `3af31df3fc18`. The pinned side has fourteen
commits, ten excluding merges, which the prior matrix never classified. They
replace dynamic-virtual-channel reassembly with a stateful dechunker, harden
stream and transport bounds, add nine dechunker tests and make individual
common suites selectable. The affected runtime files are `common/parse.h`,
`common/trans.c`, `libxrdp/libxrdp.h`, `libxrdp/xrdp_channel.c`,
`libxrdp/xrdp_mcs.c` and the new `common/dechunker.{c,h}`. The xorgxrdp pinned
base `49bf2dd3546d` is already an ancestor of development xorgxrdp, so there is
no corresponding producer-base gap.

Second, development xorgxrdp preassembled each AVC444 GFX
STARTFRAME/WIRETOSURFACE_1/ENDFRAME envelope and sent it as xup order 62.
Clean-room xorgxrdp sends the capture rectangles, slot identity, frame identity,
geometry and shared mapping as dedicated order 64; xrdp owns envelope
construction after ingress. Both paths had been called equivalent without a
test which crossed the xup message boundary and reconstructed the exact
client-visible envelope. The Windows result proves that classification was not
sufficient.

The manual reconciliation therefore ports the pinned-base safety changes and
preserves order-64 AVC444 ingress on canonical development. The development
consumer validates the current shared-memory snapshot, constructs a bounded
AVC444 or AVC444v2 GFX envelope, parses it back through the existing strict
batch parser and queues it through the established development encoder. Two
new tests prove the dedicated-message round trip and reject an undersized
output, a zero frame identity and missing rectangle storage. The default full
suite passed with 218 daemon checks; the trace-enabled suite passed with 219
daemon checks, and the paired xorgxrdp build and tests passed.

This is not yet an equivalence verdict. Producer-specific pack/credit tests
which exist only in clean-room xorgxrdp remain an explicit test-coverage row,
and the decisive Windows repeated-resize result on the rebuilt development
arm is pending. If the arm does not reproduce the black-surface precursor and
subsequent disconnect, this reconciliation is still incomplete and the next
work is another inventory/classification pass, not a lifecycle fix.

The rebuilt trace-disabled pair is deployed on development port 40060. Its
package, image, profile and byte-certificate identities are retained in
[`i142c_x044_manual_reconcile_20260828T123514Z/`](../../PR-demo/mac_bisect_matrix/captures/i142c_x044_manual_reconcile_20260828T123514Z/README.md).
The record also preserves a false first certificate: the deploy script mounted
the x043 profile while the certifier hashed a nonexistent x044 file and, due to
a missing readability guard, emitted an empty-key `CERTIFIED` result. That
certificate was not used. The certifier now fails closed, x044 owns and mounts
its explicit profile, and the replacement certificate keys the identical pod
and repository profile hash.
