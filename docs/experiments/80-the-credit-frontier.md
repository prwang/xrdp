# #80 -- credit-frontier implementation anchor

This record replaces the instrumented fleet timing narrative on 2026-08-17.
The affected captures used xorgxrdp's synchronous per-frame `ACK_TRACE cap`
logger and were deleted under the instrument-on-path rule.

The retained result is the code and deterministic proof of the frontier:
credit is bounded by consumed capture ownership, one-frame server inventory
and the configured client-display window. The tests cover message bytes,
wrap-safe identities, per-monitor ownership and boundary arithmetic. Live
frozen-client, RTT and real-client rendering claims remain BACKLOG #124; no
timing number from the deleted captures is quotable.

See `docs/experiments/121-evidence-admissibility-cleanup.md`.
