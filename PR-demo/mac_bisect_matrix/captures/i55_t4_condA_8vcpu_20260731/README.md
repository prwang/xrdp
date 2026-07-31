# i55 condition A — unrestricted 8 vCPU. H2 (CPU bottleneck) CLOSED.

Same box, same debs, same payload, same geometry and client as condition
B (`../i55_t4_condB_pinned_v5_20260731/`); the ONLY change is removing
the `CPUAffinity=0 1 4 5` drop-ins (proof in `i55_condA2/env.txt`:
affinity 0-7 on every pid, `CPUAffinity=` empty).

## Gate run

109.8 ms mean per send / 9.2 sends/s, worker 65 %, 8.29 Mpx, wire audit +
black-frame PASS.

**Doubling the CPU budget moved the period 118.3 → 109.8 ms (−7 %).
"8 cores stays ~8 fps" — H2 is closed: the serialization is structural,
not a resource bottleneck** (owner criterion, 2026-07-31). The sched-delay
table is conclusive: every thread ≤ 0.2 % runnable-starved.

## Probe window (`i55_condA2/`, closure 0.0 ms)

| leg | pinned B | unrestricted A |
|---|---|---|
| capture+pack | 8.7 | 8.4 |
| handoff+encode | 24.1 | 26.5 |
| rewrite (collect→fstart) | 35.8 | 32.8 |
| EGFX assembly | 9.1 | 9.2 |
| drain→ack (fend→aemit) | 30.4 | 27.4 |
| ack transit (H1) | 0.8 | 0.7 |
| re-arm | 4.6 | 4.6 |
| **period** | **113.6** | **109.5** |

Every leg is per-frame serial work on one thread (worker rewrite, main
drain) or a fixed protocol wait — none scales with core count, which is
why 2× CPU bought 4 ms.
