# #121 -- evidence admissibility cleanup

Recorded 2026-08-17.

## What was invalid

The xorgxrdp `ACK_TRACE cap` logger was introduced by this development branch
in commit `10fa3aa2`; it is absent from pinned upstream xorgxrdp `49bf2dd`.
It read the clock and called the ordinary synchronous logger once per capture
on the producer path. A low-cost ring on xrdp's side cannot make the paired
producer instrument admissible.

Thirteen affected session-Xorg logs were found across seven capture
directories:

* `i92_sparse_aux_ab_20260808_211023_s20`;
* `i92_sparse_aux_ab_20260808_233519_s20`;
* `i90_reground_x031_20260810_s20_a2`;
* `i90_reground_x032_20260810_s20`;
* `i90_reground_x033_20260810_s20`;
* `i104_x033_strip_20260810_s20`; and
* `i104_x034_strip_20260810_s20`.

Those directories and the numerical narratives which depended on them were
deleted, not superseded with new numbers. Git history retains the invalid
artifacts. The earlier x031 pre-login modeline failure contains no timing and
is retained as a procedure failure.

## What remains valid

Code identities, exact modelines, configuration text and deterministic unit
tests do not become timing evidence merely by sharing a record with a bad
instrument. The standalone #103 pipe microbenchmark and the local #120 trace
source-cost benchmark do not use the xorgxrdp logger and remain valid.

The conclusions now have explicit owners:

* #90 remains withdrawn: it violates the single-worker requirement and has no
  admissible evidence sufficient to amend it.
* #122 rechecks one-active versus two-active monitor behavior.
* #123 must use the real T4 before making NVENC compatibility or throughput
  claims.
* #124 re-establishes frozen-client and real-RTT frontier behavior.
* #125 re-establishes sparse-chroma client rendering and any retained byte or
  rate claim.
* #98 remains withdrawn as transport/adaptation scope.

No open obligation needs producer packing duration, so #120 removes the
xorgxrdp endpoint instead of transporting its timestamps into xrdp.

## Harness closure

`gate_evidence_check.py` makes target identity and record sufficiency explicit.
Before login the gate requires the requested arm, selected running pod and
dialled port to resolve to one identity. After the run it requires at least
`max(30, 5 * declared_seconds)` classed trace records. Wrong-target, empty and
too-short cases are deterministic tests. A failed evidence check is reported
only after the server session is logged off cleanly, and it makes the run red;
an empty trace can no longer be called an idle session.
