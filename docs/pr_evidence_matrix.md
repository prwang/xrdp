# PR evidence status

This file is an evidence index, not a product or implementation
specification. The normative clean-room requirements are in
[`PRD/`](../PRD/README.md); the completed five-arm construction record is
[`docs/experiments/104-pr-evidence-matrix.md`](experiments/104-pr-evidence-matrix.md).

## Current state — 2026-08-17

The five arms were built from one paired image and certified. That completed
the environment-construction item, but it did not validate every PR claim.
Timing evidence containing xorgxrdp's synchronous per-frame `ACK_TRACE cap`
logger was deleted by #121 and may not be quoted. The gate now rejects a
requested arm, selected pod and dialled port which do not resolve to one
identity, and rejects an empty or too-short trace. The replacement inventory
is `docs/experiments/121-evidence-admissibility-cleanup.md`.

Each clean-room implementation slice has deterministic tests in its own PRD
file. Live evidence is retained only where the final handoff makes a client,
visual, hardware or transport claim that CI cannot establish.

## Claims still requiring external evidence

| claim | cheapest valid instrument | open owner |
|---|---|---|
| LC=1 then LC=2 renders correctly | byte audit plus identified Windows/macOS visual check | #125 |
| sparse chroma preserves still-screen full color | identified real-client visual check | #125 |
| sparse chroma changes bytes/rate as claimed | identity-bearing valid trace, same payload/client/geometry | #125 |
| frozen-client and real-RTT frontier behavior | identity-bearing valid trace | #124 |
| NVENC/T4 compatibility | real T4 path only | #123 |
| multi-monitor correctness | byte/order assertions plus identified two-monitor client check | #124/#125 |

All arm conditions, run duration and client identity must be stated before an
approved run. A rate run also has to prove that its producer is faster than
the measured server pipeline. Correctness bytes and ordering may survive a
slow producer; a throughput conclusion may not.
