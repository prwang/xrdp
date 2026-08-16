# #104 — one-image PR evidence fleet

Recorded 2026-08-09 through 2026-08-10. Moved from `BACKLOG.md` on
2026-08-16.

## Decision and resulting fleet

The previous fleet mixed images, configurations and retired campaigns. It was
replaced by five arms, x031 through x035, built from one xrdp image and one
xorgxrdp revision. The arms differ only in `gfx.toml`: legacy acknowledgement,
frontier window 1, frontier window 2, sparse chroma and AVC420. The standard
workload is `SESSION_KIND=textflood_strip`.

Old arm definitions, old certificates and old captures not retained by an
experiment record were garbage-collected. The design and claim mapping are in
`docs/pr_evidence_matrix.md`; the inventory is in
`PR-demo/mac_bisect_matrix/README.md`.

## Red intermediate state, preserved

The first certification attempt left x035 red. `arm_certify.sh` ran the
two-view AVC444 LTR audit against a single-view AVC420 stream, reported no
main pictures and failed its A1–A6 assertions. That was an instrument mismatch,
not evidence that the arm's stream was broken. At that point the arm was
correctly barred from measurement.

## Superseding result, 2026-08-10

Commit `3ca9ae9f` taught certification to select the audit from the arm's
declared AVC mode. AVC420 receives its single-view conformance gate and the
inapplicable LTR assertions are skipped rather than reported as failures.
x035 then certified. The five-arm fleet was therefore complete; the stale
“RED AND OPEN” paragraph which remained in `BACKLOG.md` did not describe the
repository after `3ca9ae9f`.

This closes fleet construction only. BACKLOG #108 separately invalidates
timing evidence collected with xorgxrdp's per-frame `ACK_TRACE cap` logger,
and the evidence matrix document must be updated before it can specify the
final PR campaign.
