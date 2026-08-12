# AVC444 external-ffmpeg backend — upstream port plan

Status: **BASE PINNED. Cleanup implementation has not been cut.**

Owner directives this plan implements (2026-08-10, amended through
2026-08-12):

1. The work goes upstream as **ONE pull request**, internally sliced into
   reviewable commits. The earlier five-PR proposal is withdrawn: each PR
   would have had to stand on its own and none of the first four does —
   they are prerequisites for each other, and the regressions in the
   early ones are fixed by the later ones. Only the final state is
   complete.
2. **Step 0 is pinned to
   `fe850a22c08a624c66bbac07e310251782e6f828`.** Newly fetched
   `origin/devel` and `upstream/devel` both resolve to that commit. It is
   the cleanup implementation base even if either ref later advances.
3. **The full existing server-side `common/perf_trace` instrument is in the
   PR.** #106 closed RED because standard per-PID Linux perf probes cannot
   reproduce 13 of 34 semantic records. #107 found that every current event
   family serves an open post-PR obligation, so the ring, test, all 34 call
   sites, lifecycle hooks and queue counter form one slice. Dev-only benches
   and capture/analyzer machinery stay out.
4. **The shipped `wire_window` default is 1**, with 2 documented in
   `gfx.toml` and `gfx.toml(5)` as the value that removes the stall and
   what it costs. The PR text raises the question so maintainer feedback
   is part of deciding it.

5. **This working tree's tip is always the dev branch.** Once the remote
   exists, the port branch lives in its own worktree and is re-authored
   there. Nothing is ever removed from dev to make the port smaller.

This document supersedes the 2026-07-24 plan, which was scoped to
re-authoring one commit on the abandoned `avc444-ffmpeg-upstream`
branch. That text is in git history; it is not reproduced here.

## Where the two branches live

Dev stays here, in `/work`, with linear history, and it keeps
everything: `BACKLOG.md`, `docs/`, `PR-demo/`, `tools/`, `common/perf_trace`.
The PR-procedure documents — this one included — are dev documents and
live here too.

The port branch is **re-authored in a separate worktree** off the
upstream tip. Because it is authored rather than filtered, "what is
excluded" is not a subtraction anyone performs: only what is
deliberately written into a slice appears there. Dev scaffolding —
`BACKLOG.md`, `docs/`, `PR-demo/` and the `tools/` benches and probes —
simply never gets written. `common/perf_trace`, its server call sites and its
unit test are deliberately written as the #107 tracing slice.

## Step 0 — the branch point

**Pinned by the owner 2026-08-11.** Newly fetched `origin/devel` and
`upstream/devel` both resolve to
`fe850a22c08a624c66bbac07e310251782e6f828`. This is the cleanup
implementation base. Do not replace it with "whatever devel is on the
day"; a later upstream move is an explicit rebase decision, not an
implicit change of the experiment and review base.

The pinned base is 14 commits past this dev line's merge-base
`3af31df3` (2026-07-02): 1,229 additions and 90 removals across 14
files. The changes were read semantically and checked with synthetic
merges.

### New-base compatibility audit

**Verdict: no breaking AVC444 API, wire-contract, configuration or build
change.** Preserve one new upstream ordering and inherit the remaining
hardening:

* `de284747` moves `xrdp_bitmap_resize()` before GFX-surface and encoder
  creation in the dynamic-resize state machine. This is favourable to
  AVC444—the external encoder is created against the target geometry—but
  the cleanup must retain that order when adding its more expensive
  `xrdp_encoder_create()` path.
* `b824c93b`, `be95ba30` and `3ef2f883` replace incoming static-channel
  chunk assembly with the tested `common/dechunker` module. The
  `xrdp_drdynvc` callback signatures used by EGFX do not change. This is
  the receive path for client DVC messages (including frame acks), not
  the server's outgoing AVC byte stream; inherit it and keep the normal
  full-frame/ack smoke check.
* `a2d130bc`, `5ae11e2a`, `e4f4364c` and `b36ad7b2` harden stream and
  pointer bounds. They change no AVC-facing API and must not be undone by
  copied old code.
* `2e8a4a82` adds per-suite selection to the common-test runner. It
  conflicts only with this dev branch's perf-trace test registration.
  The cleanup must port its registration as a new conditional
  `run_suite("perf_trace")` entry, preserving upstream's layout and its
  dechunker suite.

`git merge-tree --write-tree fe850a22 avc444-ffmpeg-upstream` completes
without a conflict; its only common modified file is `xrdp/xrdp_mm.c`
and Git merges the resize ordering cleanly. The same synthetic merge of
the whole current dev branch reports one conflict,
`tests/common/test_common_main.c`, entirely from custom perf_trace/log
test registration. The port resolves only the perf-trace part into the new
suite-selection structure; it must not copy the older runner wholesale or
carry unrelated dev-only test registration. `common/trans.{c,h}`'s
`wait_bytes` counter is trace-only and ships with the instrument.

## What ships in the PR, and what does not

### Included: the complete existing server tracing slice

The slice is `common/perf_trace.{c,h}` (614 lines),
`tests/common/test_perf_trace.c` (298), its build and test-runner
registration, the **34 `PERF_TRACE*()` call sites** —
`xrdp/xrdp_encoder.c` 24, `xrdp/xrdp_mm.c` 8 and
`xrdp/xrdp_encoder_ffmpeg.c` 2 — and eleven lifecycle/include/comment
references. `tools/perf_trace_bench.c` remains dev-only unless separately
justified; a benchmark is not part of the server instrument.

The 2026-08-11 exclusion depended on standard external perf replacing this
semantic trace. #106 falsified that premise in two ways:

* Phase A could register a uprobe through the remapped control inode, but
  `perf record` could not read the event metadata required to open it for
  the selected PID.
* Phase B found credible direct mappings for 21 of 34 private records and
  no exact mapping for 13. The missing values include the
  `feedend`/`outfirst` encode identity and explicit frame chains. Omitting
  them would force the time-window joins this project forbids.

Phase C is therefore cancelled. More host permission cannot repair the
semantic coverage failure, and no Build-ID-recorded `perf.data` recipe is a
complete replacement. The exact audit is in
`docs/experiments/106-perf-isolation-and-trace-equivalence.md`.

#107 audited the open post-PR work against the analyses already used in past
experiments. Every event family remains load-bearing: sparse-chroma mechanism
and child windows (#92), full GPU decompositions (#93), changed-monitor and
capture-handoff attribution (#94/#95), or credit/wire/queue behaviour
(#80/#98). The stage endpoints close the cycle and cannot be removed one at a
time. Since the 614-line ring and 298-line test are the fixed review cost,
trimming small call-site statements would save little while making the one
instrument incomplete. Record:
`docs/experiments/107-private-tracer-is-pr-scope.md`.

The tracer remains default disarmed and uses the existing thread-local ring
and separate sink. This decision does not authorize a new tracer, per-frame
`LOG()`, source tracepoints, or inferred time-window pairing.

Resolve the unit test under upstream `2e8a4a82`'s suite-selection layout. The
dev branch keeps every bench and analyzer; only the server instrument and its
test are re-authored into the PR branch.

### Not written into any slice: the `tools/` benches

Seven benches and probes are new on this branch and none is required by
the server: `avc444_pack_bench.c` (485), `vmsplice_pipe_bench.c` (500),
`perf_trace_bench.c` (416), `avc444_ltr_rewrite_bench.c` (345),
`avc444_resize_repro.c` (154), `avc444_pack_selftest.c` (112),
`avc444_convert_bench.c` (102) — 2,114 lines. They carry verbatim
copies of shipped loops that a reader has to keep in sync by hand,
which is a maintenance obligation, and they exist to answer questions
about this dev branch. They stay here and are not written into a slice.

The one case worth revisiting later is `avc444_pack_selftest.c`, which
checks correctness rather than speed. If that coverage should go
upstream it belongs in `tests/`, written as a Check test — not as a
bench copied across.

### The size the reviewer actually sees

Pre-cleanup estimate against the then-current `origin/devel`, C and
headers only, `tests/` and `PR-demo/` excluded. Recompute this table from
the authored branch before using it as the PR diff size:

| | files | added | removed |
|---|---|---|---|
| genuinely new files | 19 | 11,008 | 0 |
| modified existing files | 15 | 5,311 | 137 |

The old exclusion case left out perf_trace and the seven `tools/` benches,
taking the new-file column to 10 files and +8,280. #107 adds the two tracer
source files and 614 lines back: the current source-only estimate is **12 new
files, +8,894**. Recompute from the authored branch before quoting PR size.
The modified column barely moves because the call sites are individual lines
inside functions that change anyway. The 298-line tracer test is already in
the separate test-tree total below.

The modified column is what a reviewer has to hold in their head, and it
concentrates hard: `xrdp/xrdp_encoder.c` +2,526, `common/xup_client_info.h`
+718 (the wire contract shared with xorgxrdp, mostly one new struct and
its comments), `xrdp/xrdp_mm.c` +715, `xrdp/xrdp_encoder.h` +640,
`xrdp/xrdp_tconfig.c` +388, `xrdp/xrdp_tconfig.h` +204. The remaining
nine files add 120 lines between them.

Beyond source: `docs/man/gfx.toml.5.in` +350, and the test tree at
+16,526 across 53 files.

## The `wire_window` default (owner directive 4)

**Applied on the dev branch 2026-08-10**, so the port copies a tree that
already carries the shipped decision:

* `xrdp/xrdp_tconfig.h` — `XRDP_GFX_WIRE_WINDOW_DEFAULT` 2 → **1**, with
  the reason in the comment.
* `xrdp/xrdp_tconfig.c` — the `[avc444_ffmpeg]` defaults comment now
  says 1 is what ships and why.
* `tests/xrdp/test_tconfig.c` — the no-config assertion changed from 2
  to 1. **This is a deliberate, announced test change**: the value in
  that assertion is the owner's specification of what ships, the
  specification changed on 2026-08-10, and the comment above the
  assertion records both the new directive and the one it supersedes.
  It was not read off the loader.
* `xrdp/gfx.toml` — `wire_window` and `eager_slot_ack` are **documented
  for the first time**; neither key appeared in the shipped sample
  configuration before today, which was a real gap for the PR.
* `docs/man/gfx.toml.5.in` — default corrected to 1 in the key's own
  entry, in the multi-monitor arithmetic (two monitors: at most 5, not
  6), and in the round-trip table (80 ms, one monitor: about 37 frames
  per second at the default, about 50 at 2).

`eager_slot_ack` stays defaulted **on**. With it on and the window at 1
the frontier grants exactly what the mechanism it replaces granted —
measured identical on frame period, on the wait for permission to
capture and on the wire bound — so the new code path is exercised by
default while the observable behaviour is unchanged. That pairing is
what makes "upgrading and changing nothing keeps today's wire behaviour"
a claim CI can check, and `test_tconfig.c` checks both halves together
for that reason.

One consequence to carry into the PR text: with the default at 1 this is
**no longer a breaking change**, and the manpage's pointer to
`PR-demo/BREAKING_CHANGE_credit_frontier.md` must not survive the port —
`PR-demo/` is not ported, and a manpage may not reference a path that
does not exist in the tree it ships in.

## Slice order

Each commit **builds and passes `make check`**, so the series is
bisectable. That is a much weaker requirement than "each commit is
shippable on its own", and it is the requirement that matters: a commit
adding a converter nothing calls yet is a good commit and would be a bad
PR. **No commit message claims a rate, a client rendering correctly, or
a bottleneck removed, except where that is true at that commit.**

The existing eleven-slice branch is the starting shape; the frontier,
LTR, sparse-chroma and multi-monitor work extends it. Order:

1. **Foundations, no caller.** RGB→NV12 dual-plane converter, H.264
   Annex-B validator, minimal NUT demuxer, the RFX_AVC420 metablock
   emitter exposed outside the codec guard and its even-alignment fix.
   Each with its unit tests. Nothing in the server calls any of it yet.
2. **Capability negotiation.** `CC_GFX_AVC444` capture capability,
   AVC444/AVC420 GFX capability negotiation, mm selector flags.
3. **The external ffmpeg runner.** Spawn, pipes, the input-pipe size
   negotiation and its non-silent failure, submit/pump/collect.
   `dump_extra` is per-encoder from birth — the blanket form is never
   written (see fold points).
4. **Configuration.** `[avc444_ffmpeg]` parsing, bounds, refusals,
   the shipped defaults including `wire_window = 1`.
5. **Serialization and the wire.** `gfx_wiretosurface1_avc444` and the
   LC serializer, **born emitting luma-then-chroma as two PDUs** (LC=1
   then LC=2). The single-PDU LC=0 form the macOS client rejects never
   exists in the history.
6. **The LTR chain and scheduled intra refresh**, with the golden
   vectors.
7. **Multi-monitor.** Per-monitor encoder, converter, geometry and LTR
   state; the poll set over all children. Landed here rather than
   deferred, per the standing directive that multi-monitor ships with
   this port.
8. **The credit frontier**, default 1, documented at 2.
9. **The sparse chroma cadence.**
10. **Documentation.** `gfx.toml`, `gfx.toml(5)`.

Within each of those, split further wherever a commit exceeds what one
sitting can review. The numbering is review order, not a promise about
commit count.

## Fold points — defects that must never enter the series

A defect a later commit in the same PR fixes is a defect that should not
have been written into the series. Two are known:

* **The LC framing.** Fold into the serializer slice (5); the previous
  plan already decided this and its reasoning stands.
* **The blanket `dump_extra`.** On the abandoned branch, slice
  `04e43ee2` applied the bitstream filter unconditionally, which
  regresses NVENC on Linux and produces duplicate parameter sets that
  black out strict decoders. The runner slice (3) is authored with the
  per-encoder form from the start.

## What the PR claims, and the evidence for each claim

To be filled in from `docs/pr_evidence_matrix.md` once the flow-control
arms have been re-measured on the current build. The claims-to-arms
mapping is already written there; what is missing is that every timing
predating 2026-08-08 was taken with the encoder input pipe clamped to
two pages and must be re-established (BACKLOG #103).

The PR text also has to do one thing this plan cannot: **ask the
maintainers whether `wire_window` should default to 2**, presenting the
measurement and the cost, so their answer is part of the decision rather
than something to reconcile afterwards.

## Validation before the branch is handed over

* Every slice builds; `make check` green at every slice.
* `astyle --options=astyle_config.as` clean (CI pins astyle 3.4.14).
* cppcheck clean, per CI.
* Rendering confirmed on mstsc, UWP and the macOS Windows App, on a
  build made from the PR branch — not from `/work`.
* Multi-monitor confirmed live.
* The paired xorgxrdp branch exists and carries the same wire contract.
* **The agent never pushes.** When the branch is ready, the exact
  command is handed to the owner to run.

## Note on residual macOS 1px-chroma softness (NOT a gate)

Unchanged from the previous plan: on the macOS Windows App's HiDPI
display, isoluminant 1px-stripe bands render softened, while mstsc and
UWP show a crisp grid — i.e. true 4:4:4 on the wire. This is a
client-side artifact of that client's HiDPI pipeline, not reachable from
the server. Accepted; does not block the port.
