# #76 -- frame resources are distinct

This record replaces the instrumented timing narrative on 2026-08-17.
The original fleet captures used xorgxrdp's synchronous per-frame
`ACK_TRACE cap` logger and were deleted under the instrument-on-path rule.

The independently retained conclusion is that capture slots, encoder work,
server transactions and client display credits are different resources.
Their bounds and identities are represented separately in the implementation
and deterministic tests. No elapsed-time or speed-ratio claim from the
deleted captures is quotable. BACKLOG #124 owns the live frontier check.

See `docs/experiments/121-evidence-admissibility-cleanup.md`.
