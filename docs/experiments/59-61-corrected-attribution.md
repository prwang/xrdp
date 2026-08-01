<!--
Experiment record moved out of BACKLOG.md on 2026-08-01.

BACKLOG.md is the OPEN work list: hypotheses, justification, and a
pointer. This file is the closed record it points at -- the conditions,
the numbers, the anomalies and the retractions, kept verbatim as they
were written at the time. Nothing here is a live task.
-->

## Corrected-attribution record (2026-07-31, compressed)

Four superseded analyses of "why is the E5-2 pipeline serial", kept as
one paragraph each; full text in git history (46207bd7, f5e01aec,
6cb07227, 94d5c1ec) and in the capture READMEs.

1. **"`inflight=0` proves no overlap" — invalid metric.** `inflight` is
   one ffmpeg child's internal queue depth, zero by design under
   `-tune zerolatency`. The fif=4 probe against it was aimed at a value
   that cannot move (`e52_t4_fif4_PROBE_REVERTED_20260731`).
2. **"0/205 frames overlap" — mis-paired events.** Cycle-window pairing
   attributed sends to the wrong frame (negative −3.3 ms segment was
   the tell); frame-identity pairing plus the later probes show the
   capture side pipelining is *admitted* but *starved by the ack*.
3. **"The producer starves the pipeline" — conflated counters.** The
   "8.19 fps producer" was the pipeline's send rate wearing the
   producer's name; instrumented textflood measures 27.66 fps, 3.4×
   the pipeline (`e52_t4_textflood_m1_4k_step0_20260731`). FR-BENCH-1
   PASSES as measured.
4. **"~100 ms cairo render" — unmeasured inference.** ring_recon
   measured 24.1 ms on the T4; compute was never the constraint.

What survived every correction: the uprobe result below (#64), which
closes the chain with counters read from the live gates themselves.
*(2026-07-31, later the same day: it did NOT survive — the uprobe
counters are real but their reading was wrong; see the #64
withdrawal. The instrument cannot distinguish a pinned slot from
ordinary saturation, and static analysis shows the ack was an echo
all along.)*

The blocking chain is LINEAR (renumbered 2026-07-31 after the m=1
serializer was measured): **#70 → #71 → #72 → #73**. #70 is the eager
slot-release ack (the head, THE NEXT STEP); #71 (was #65) the
per-monitor window; #72 (was #66) motion-gated 4:2:0; #73 (was #67)
the T4 headline re-runs. Nothing else advances until its predecessor
closes. *(Earlier forms of this chain — "#64 → #65 → #66" and its
amendments — are preserved at commit `0db74f6e`.)*
