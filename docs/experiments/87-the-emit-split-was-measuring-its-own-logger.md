# #87 -- instrumentation cost and the withdrawn emit split

This record replaces the contaminated fleet narrative on 2026-08-17.
The first experiment correctly found that normal per-frame logging measured
itself. Later fleet reruns used the low-cost xrdp ring, but their paired
xorgxrdp still wrote synchronous per-frame `ACK_TRACE cap` lines. Those
captures and their elapsed-time tables were therefore deleted too.

The emit-thread proposal remains withdrawn: the normative implementation has
one encoder worker and no admissible result justifies another thread. The
shipping trace source cost is independently measured by #120 at
11.825--14.198 us mean and 23.529--24.369 us p99 for twelve named text events
per frame. That local microbenchmark does not depend on xorgxrdp.

See `docs/experiments/120-perf-trace-shipping.md` and
`docs/experiments/121-evidence-admissibility-cleanup.md`.
