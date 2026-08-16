# PR evidence status

This file is an evidence index, not a product or implementation
specification. The normative clean-room requirements are in
[`PRD/`](../PRD/README.md); the completed five-arm construction record is
[`docs/experiments/104-pr-evidence-matrix.md`](experiments/104-pr-evidence-matrix.md).

## Current state — 2026-08-16

The five arms were built from one paired image and certified. That completed
the environment-construction item, but it did not validate every PR claim.
Timing evidence containing xorgxrdp's synchronous per-frame `ACK_TRACE cap`
logger is quarantined and may not be quoted. The invalid-capture inventory and
replacement record are open as BACKLOG #108. The gate's target-identity and
minimum-record checks are open as BACKLOG #99.

No new fleet performance campaign may start until #99 is green and #108 has
identified which prior timing records are void. The ordinary clean-room port
does not depend on those numbers: each implementation slice has deterministic
tests in its own PRD file. Live evidence is retained only where the final
handoff makes a client, visual, hardware or transport claim that CI cannot
establish.

## Claims still requiring external evidence

| claim | cheapest valid instrument | open owner |
|---|---|---|
| LC=1 then LC=2 renders correctly | byte audit plus identified Windows/macOS visual check | #80/#92 |
| sparse chroma preserves still-screen full color | identified real-client visual check | #92 |
| sparse chroma changes bytes/rate as claimed | corrected gate after #99/#108, same payload/client/geometry | #92 |
| frozen-client and real-RTT frontier behavior | corrected identity-bearing gate after #99/#108 | #80 |
| NVENC/T4 compatibility or performance | real T4 path only, if the PR retains the claim | #93 |
| multi-monitor correctness | byte/order assertions plus identified two-monitor client check | #80/#226 |

All arm conditions, run duration and client identity must be stated before an
approved run. A rate run also has to prove that its producer is faster than
the measured server pipeline. Correctness bytes and ordering may survive a
slow producer; a throughput conclusion may not.
