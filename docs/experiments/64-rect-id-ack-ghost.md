<!--
Experiment record moved out of BACKLOG.md on 2026-08-01.

BACKLOG.md is the OPEN work list: hypotheses, justification, and a
pointer. This file is the closed record it points at -- the conditions,
the numbers, the anomalies and the retractions, kept verbatim as they
were written at the time. Nothing here is a live task.
-->

## #64 — rect_id ack ghost (CLOSED 2026-07-31 — root cause refuted; the real serializer measured to 0.0 ms unattributed and tracked forward as #70)

Filed as: "xrdp acks with its own count, the value drifts, and a ghost
frame pins one FR-CAPTURE-8 slot forever — capture‖encode structurally
impossible." Wrong in mechanism; the SYMPTOM (ack-paced capture, one
slot never usable) was real and is now measured, not argued:

* **The ack was always an echo** (`frame_id_server = enc_done->frame_id`
  is its sole assignment; zero drift over 494 live frames), and the
  consume-no-output paths acked anyway. What those paths actually lose
  is the REGION — the one part of the filing worth keeping, revived in
  #70's failure semantics.
* **The 340/1004/0 uprobe histogram fits the ghost, the healthy AND the
  serial timeline equally** — it discriminated nothing and carried
  three successive wrong verdicts (quality gate 2b). The load-bearing
  datum was the 0/205 ordering trace all along.
* **The 2026-07-31 T4 redo falsified both follow-up hypotheses** — H1
  "Xorg applies the ack late" (measured 0.8 ms) and H2 "CPU
  starvation" (8 vCPU moved the period 7 %; no thread starved) — and
  attributed the full 113.6 ms cycle with 0.0 ms unattributed. The
  serializer is the ack's EMISSION POINT: it rides the frame's own
  last enc_done, after the entire encode→rewrite→assembly→egress
  tail, while the fif window is OPEN at every emission. Design
  consequence and next step: **#70**.

Complete history in git — this section was ~260 lines; its full text,
the filing, the invariant proofs, the withdrawal, the reconciliation
and both hypothesis registrations are preserved at commit `0db74f6e`:
`c2cb4008` honesty lesson · `8bd989c0`/`0b295631` + xorgxrdp `59210b2`
FR-ACK-1 implementation checkpoint (`wip/fr_ack_1_checkpoint`, CI
green 380/380 — kept, it is #70's machinery) · `e8d00594` withdrawal ·
`0040e603` reconciliation · `5e979206` H2 registration · `0db74f6e`
T4 redo + resolution · `b245c1a1` client-rig statelessness (the two
harness bugs the redo surfaced). PRD: FR-ACK-1-as-filed moved to
Non-goals (NG-9).
