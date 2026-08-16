# Experiment records

Closed investigations, moved out of `BACKLOG.md` on 2026-08-01.

`BACKLOG.md` is the **open work list**: a hypothesis, its justification,
and a pointer. It had grown to 1923 lines for the second time (3268 the
first, rewritten 2026-07-28) because finished experiments kept being
written into it. They live here instead.

**These files are closed records, not tasks.** They are kept verbatim as
they were written — including the anomalies, the retractions and the
claims that turned out to be wrong, because the retractions are usually
the most useful part. Do not "tidy" a result to match what is now
believed; supersede it with a dated note and say what changed it.

Where things go:

| content | home |
|---|---|
| an open question + why it matters + a pointer | `BACKLOG.md` |
| how a closed investigation was run and what it measured | here |
| a contract, invariant, or baseline that is still true | `PRD/` |
| how to install, deploy, or triage | `DEPLOY_RUNBOOK.md` |
| working rules for agents and contributors | `CLAUDE.md` |
| the evidence for one run (logs, images, VERDICT) | `PR-demo/mac_bisect_matrix/captures/<run>/` |
| narrative of who did what when | git history |

| file | item | outcome |
|---|---|---|
| `45-intra-refresh-and-pump-set.md` | #45 | DONE — E5 resolved by #52 at 2.13× |
| `52-e5-2-saturated-payload.md` | #52 | DONE — 2.13× GREEN; retired the 51.1 ms cadence baseline |
| `55-e5-2-on-the-t4.md` | #55 | DONE — 1.5×–2.3× AMBER; T4 since decommissioned |
| `59-61-corrected-attribution.md` | #59/#60 | capture is 13.8 % of the bottleneck thread |
| `61-glamor-on-nvidia.md` | #61 | CLOSED-WONTFIX — GLAMOR renders black on NVIDIA |
| `62-textflood-payload.md` | #62 | DONE — 1.41× RED, later annotated producer-confounded |
| `64-rect-id-ack-ghost.md` | #64 | CLOSED — root cause REFUTED |
| `70-eager-slot-release-ack.md` | #70 | DONE — 1.11×, encode‖tail 4.8 → 8.5 ms |
| `88-client-pauses-are-not-a-port-gate.md` | #88 | WITHDRAWN — external-client diagnosis does not qualify the server port |
| `90-the-eight-ms-prize-was-stale.md` | #90 | WITHDRAWN — PRD-violating stage threads have insufficient ROI at 1.24–1.28 ms |
| `61h-the-logger-was-in-the-measurement.md` | #61h | the per-frame trace was `log.c` on the measured path; 25 captures and 3 records voided, #61c/#61e/#70B reopened |
| `91-the-multimon-window-and-the-shared-pump.md` | #91 | window arithmetic and encoder overlap resolved |
| `92-sparse-aux-is-a-byte-lever-not-a-time-one.md` | #92 | mechanism implemented; remaining qualification renumbered #125 |
| `95-capture-handoff-target-withdrawn.md` | #95 | WITHDRAWN — optional extra-speed target does not qualify the port |
| `96-pack-off-x-thread-withdrawn.md` | #96 | WITHDRAWN — cross-repository architecture expansion is outside the port |
| `98-tier0-bbr-ab.md` | #98 | open transport/adaptation roadmap withdrawn; #121 audits old timings |
| `100-the-emit-thread-bought-nothing.md` | #100 | emit thread removed; inline assembly retained |
| `102-client-display-offset.md` | #102 | documented client-visible 33-pixel offset; closed below the port |
| `103-pipe-size-and-host-limit.md` | #103(a) | 64 KiB requirement and fail-loud guard established |
| `104-pr-evidence-matrix.md` | #104 | five one-image arms built and certified; stale x035-red state superseded |
| `105-port-preparation.md` | #105 preparation | pinned base, one-change, tracer and default decisions recorded |
| `106-perf-isolation-and-trace-equivalence.md` | #106 | CLOSED RED — no exact external mapping for 13/34 records |
| `107-private-tracer-is-pr-scope.md` | #107 | named text byte ring selected; shipping lifecycle moved to #120 |
| `201-prd-refactor.md` | #201 | paired bases pinned; one normative file per clean-room slice |
| `avc444-lc-reframe-design.md` | pre-backlog | historical LC=1/LC=2 ground truth and original fix design; superseded normatively by PRD slice #135 |
