# #90 -- submit/collect overlap withdrawn

The submit/collect split is withdrawn because it violates the specified
single-worker architecture and has insufficient demonstrated return to amend
that requirement. The three re-characterization captures used xorgxrdp's
synchronous per-frame `ACK_TRACE cap` logger, so their timing tables were
deleted on 2026-08-17 and are not part of this decision.

The failed pre-login setup which supplied a mode name without its modeline
performed no measurement and remains only as a procedure failure. It does not
support a performance claim.

No clean-room slice implements #90. Reopening it would require a new,
specification-compatible proposal with admissible evidence of material ROI.
See `docs/experiments/121-evidence-admissibility-cleanup.md`.
