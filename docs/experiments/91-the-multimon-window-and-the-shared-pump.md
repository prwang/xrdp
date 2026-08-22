# #91 -- one worker pumps all monitor encoders

This record replaces the instrumented live timing narrative on 2026-08-17.
The affected captures used xorgxrdp's synchronous per-frame `ACK_TRACE cap`
logger and were deleted under the instrument-on-path rule.

The retained implementation uses one encoder worker and one poll set for all
active monitor/view child pipes; per-monitor slots and transaction identities
remain separate. Deterministic tests cover poll-set membership, ordering,
identity and batch emission. The old overlap and interval numbers are not
quotable. BACKLOG #122 rechecks one-active versus two-active monitor behavior
with valid xrdp-only tracing.

See `docs/experiments/121-evidence-admissibility-cleanup.md`.
