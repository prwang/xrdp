# #201 — paired bases and normative slice contract

Closed 2026-08-16.

The clean-room bases are pinned to xrdp
`fe850a22c08a624c66bbac07e310251782e6f828` and xorgxrdp
`49bf2dd3546dc48b9d5bae62022762fde11793d0`. Comparing the pinned bases with
the development frontiers found no intervening upstream AVC configuration,
encoder API or xup capture-contract change that breaks the planned port. The
xorgxrdp frontier's 13 commits are the feature-side inventory to re-author,
not upstream base drift.

The 4570-line `PRD.md` mixed current requirements, discarded proposals,
experiment results and an obsolete nine-PR plan. It was replaced by
`PRD/README.md` for the product-wide contract and one normative file for each
clean-room commit #210–#226. The competing
`docs/avc444_upstream_port_plan.md` was removed. `BACKLOG.md` remains the open
work/inventory list and points to the unique slice specifications.
The completed LC-reframing design moved verbatim from `docs/` to
`docs/experiments/`; the live source comment now points to slice #219. The
stale proposed evidence matrix was reduced to a non-normative current-status
index and its completed construction history remains in experiment #104.

The refactor explicitly corrected stale normative language: multi-monitor is
required; AVC444 begins LC=1 then LC=2 and never LC=0; there are two capture
slots per monitor; the shipped credit settings are eager slot acknowledgement
and wire window 1; assembly stays inline; `tail_flush` and explicit fault
injection do not port; and performance trace is a compile-time-erased named
text byte ring rather than JSON or fixed binary records.
