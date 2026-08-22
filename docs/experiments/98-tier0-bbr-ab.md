# #98 -- transport adaptation roadmap withdrawn

This item is withdrawn as scope beyond the AVC444 port. Its old fleet timing
used xorgxrdp's synchronous per-frame `ACK_TRACE cap` logger and was deleted
on 2026-08-17 under the instrument-on-path rule. No transport speed ratio from
that work is quotable and no clean-room slice implements it.

The shipped static credit frontier remains covered by deterministic tests and
the narrowed live qualification in BACKLOG #124. See
`docs/experiments/121-evidence-admissibility-cleanup.md`.
