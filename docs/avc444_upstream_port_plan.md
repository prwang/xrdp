# AVC444 external-ffmpeg backend — upstream port plan

Status: **PLAN. Nothing is cut until step 0's branch point is agreed.**

Owner directives this plan implements (2026-08-10):

1. The work goes upstream as **ONE pull request**, internally sliced into
   reviewable commits. The earlier five-PR proposal is withdrawn: each PR
   would have had to stand on its own and none of the first four does —
   they are prerequisites for each other, and the regressions in the
   early ones are fixed by the later ones. Only the final state is
   complete.
2. **Step 0 is a truly synced `upstream/devel`.** The branch point is the
   real upstream tip, not a stale fork mirror.
3. **`common/perf_trace` is excluded from the PR branch**, and the trace
   code on the dev branch is left untouched. After the port is cooked, a
   private branch off the PR branch carries perf_trace back, for use only
   where an experiment strictly needs it.
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
`BACKLOG.md`, `docs/`, `PR-demo/`, the `tools/` benches and probes,
`common/perf_trace` — simply never gets written, and no decision to
remove it is needed or possible.

## Step 0 — the branch point

**Done 2026-08-10.** `git fetch upstream devel` moved `upstream/devel`
from `3af31df3` (2026-07-02) to **`fe850a22`**, 14 commits. The fork
mirror everything on this branch has been diffed against,
`origin/devel` = `8812646d`, is **11 commits behind that tip** and is a
clean ancestor of it (0 commits ahead), so nothing has to be reconciled
— only rebased forward.

`origin` itself cannot be fetched from this box (no SSH key: *"Permission
denied (publickey)"*), so `origin/*` refs here are frozen at their last
fetch. Only `upstream` is reachable, over https, and it is the one that
matters.

**Of the 15 existing source files this branch modifies, exactly one was
touched by those 11 commits:** `xrdp/xrdp_mm.c`, +13/−11, commit
`de284747` — a reordering in the dynamic-resize state machine so the
screen bitmap is resized *before* the encoder and GFX surfaces are
created. That ordering is favourable to this work (the encoder is
created already knowing the new geometry) but the rebase must be checked
by hand rather than accepted from a clean `git` merge, because this
branch changes `xrdp_encoder_create`'s cost and what it spawns.

Also landed upstream in that range and worth reading before the rebase,
because they are on the EGFX transport path this backend feeds:
`be95ba30`/`3ef2f883` (a separate dechunker module with tests),
`b824c93b`/`a2d130bc`/`5ae11e2a` (DYNVC multi-chunk reassembly and
stream-bounds fixes), and `de284747`/`a5975210` (GFX state machine and
its comments). None of them touch a file this branch creates.

**The branch point to agree before anything is cut:** `upstream/devel`
at `fe850a22`, or whatever it has become on the day the branch is cut —
re-run the fetch and re-run the one-file check above rather than
trusting this paragraph.

## What ships in the PR, and what does not

### Excluded: `common/perf_trace` (owner directive 3)

`common/perf_trace.{c,h}` (614 lines), `tools/perf_trace_bench.c` (416),
`tests/common/test_perf_trace.c` (298) and its registration in
`tests/common/Makefile.am`, `test_common.h` and `test_common_main.c` do
not go upstream, and neither do the **34 `PERF_TRACE*()` call sites** —
`xrdp/xrdp_encoder.c` 24, `xrdp/xrdp_mm.c` 8,
`xrdp/xrdp_encoder_ffmpeg.c` 2 — nor the eleven other references to the
ring (its include, its init and shutdown, and comments) spread over
those files and `xrdp/xrdp_encoder.h`.

Three things make this cheap rather than costly:

* **No test depends on it.** The only mention in
  `tests/xrdp/test_avc444_credit_frontier.c` is a code comment saying
  where the event order in the test came from. Nothing else in `tests/`
  references it. Excluding it costs no CI coverage.
* **It removes a live rebase conflict.** Upstream's `2e8a4a82` changed
  `tests/common/test_common.h` and `tests/common/test_common_main.c` —
  the same two files perf_trace's registration edits. Excluding
  perf_trace deletes that conflict rather than resolving it.
* **The dev branch keeps every line of it.** This is a decision about
  what is *copied out*, not about what exists here. Nothing in
  `/work` is stripped.

**The consequence to state plainly in the PR: the upstream branch has no
per-frame instrumentation, so no timing number can be reproduced on it
as it stands.** The evidence for the performance claims is measured on
the dev branch and on the fleet arms, and the PR says so. A maintainer
who wants to reproduce a number needs the perf_trace branch described in
directive 3, and that branch is the deliverable that makes the claims
auditable — it should exist before the PR is opened, not after.

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

Against `origin/devel`, C and headers only, `tests/` and `PR-demo/`
excluded:

| | files | added | removed |
|---|---|---|---|
| genuinely new files | 19 | 11,008 | 0 |
| modified existing files | 15 | 5,311 | 137 |

Leaving out perf_trace (2 new files, 614 lines) and the seven `tools/`
benches (2,114 lines) takes the new-file column to **10 files, +8,280**.
The modified column barely moves — perf_trace's call sites are
individual lines inside functions that change anyway.

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

