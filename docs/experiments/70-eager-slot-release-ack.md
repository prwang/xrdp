# #70 -- eager slot release and explicit acknowledgement

This record replaces the instrumented timing narrative on 2026-08-17.
The original fleet captures used xorgxrdp's synchronous per-frame
`ACK_TRACE cap` logger and were deleted under the instrument-on-path rule.

The retained result is architectural and deterministic. Slot reuse is gated
by the server's explicit consumed frontier, while displayed-region reuse is
gated by the client's displayed acknowledgement. Message serialization,
identity pairing and frontier arithmetic are covered by the credit-frontier
unit tests. BACKLOG #124 owns the new live frozen-client and RTT
qualification; no elapsed-time value from the deleted captures is quotable.

See `docs/experiments/121-evidence-admissibility-cleanup.md`.
