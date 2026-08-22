# #78 -- the second slot and acknowledgement-gated release

This record replaces the instrumented timing narrative on 2026-08-17.
The original fleet captures used xorgxrdp's synchronous per-frame
`ACK_TRACE cap` logger and were deleted under the instrument-on-path rule.

The retained code result is the explicit two-slot ownership model and
identity-based acknowledgement release. Layout, alternation, capacity and
frontier transitions have deterministic tests. The old pump-tail timings are
not evidence and may not be quoted. BACKLOG #124 owns live qualification.

See `docs/experiments/121-evidence-admissibility-cleanup.md`.
