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
| a contract, invariant, or baseline that is still true | `PRD.md` |
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
| `90-the-eight-ms-prize-was-stale.md` | #90 | WITHDRAWN — PRD-violating stage threads have insufficient ROI at 1.24–1.28 ms |
| `61h-the-logger-was-in-the-measurement.md` | #61h | the per-frame trace was `log.c` on the measured path; 25 captures and 3 records voided, #61c/#61e/#70B reopened |
