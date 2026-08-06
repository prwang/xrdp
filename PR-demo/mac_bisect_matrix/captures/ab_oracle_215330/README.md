# Server-ceiling run: ORACLE client at 2560×1440 + 3840×2400

Same pod, same geometry, same 60 s as `../ab_render_215222/`, run
immediately after it. `R1_CLIENT_MODE=oracle` drives the save-only oracle
build, which dumps each encoded AVC payload and acks BEFORE decode and
present, so the rate observed at the server is the server's own ceiling.
TIMING INSTRUMENT ONLY — it renders nothing and proves no fidelity.

**19.57 sends/s = 9.79 frame-pairs/s per monitor**, i.e. **3.29×** the
rendering client's 2.97. Send-gap p10/p50/p90 = 3 / 50 / 103 ms, mean
51 ms (against the rendering client's 169 ms mean). 590 of 1167 sends at
outstanding depth 2, so the capture budget is contended in both runs —
what differs is the pace, not the contention.

Conclusion: at this geometry the pipeline is **client-bound**. The server
sustains ≥ 9.79 pairs/s per monitor; the rendering client caps the session
at 3.0. This is the same effect recorded on 2026-07-26 (xfreerdp's
software 4:4:4 reconstruction ~65 ms/frame at the owner layout), now
measured at the #45 E3 target geometry, where the implied client cost is
~117 ms per surface frame beyond the server's 51 ms.

Incidental, and a stronger R1 result than the rendering runs: at this rate
essentially every pass was a full pass, and **both** monitors stayed in a
single slot for the entire run — 1165/1165 full-pass consecutive sends
same-slot, **zero** per-monitor two-slot pipelining events in 1167 sends.
