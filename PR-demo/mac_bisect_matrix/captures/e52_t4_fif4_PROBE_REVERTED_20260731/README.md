# VOID AS A RESULT — a diagnostic probe of a spec-forbidden shape, reverted

This is **not** a measurement of a candidate configuration. It is the
falsification of BACKLOG #64's hypothesis 1, kept as evidence.

`XRDP_GFX_FRAMES_IN_FLIGHT=4` was set to test whether xrdp's **global**
ack window was what cancels xorgxrdp's **per-monitor** two-slot capture
budget on a two-monitor session.

**A global pool of 2m is explicitly forbidden by the PRD** (flagged
2026-07-29): the spec is "≤ 2 outstanding PER MONITOR — never a global
pool of 2m", because a pool "lets one damaged monitor run 4-deep on 2
slots: bufferbloat, +2 frames latency, slot aliasing". It was run ONLY as
a diagnostic and reverted in the same session; the drop-in is gone and the
box is back to the default window.

## What it showed — hypothesis falsified

| | fif=2 | fif=4 (this probe) |
|---|---|---|
| `inflight` on enc lines | 0 / 2018 | **0 / 2084 — unchanged** |
| mean per send | 87.0 ms | **98.1 ms (worse)** |
| idle window | 41.5 ms | **63.2 ms (worse)** |
| final ack -> next batch | 23.5 ms | 19.2 ms |

The window took effect (`fif=4` on all 8328 send lines) and acks did come
back sooner, but **no capture/encode concurrency appeared** and latency
rose — the PRD's predicted bufferbloat, reproduced exactly.

So the global ack window is not the binding constraint. Making it
per-monitor is still required for spec compliance, but it will not by
itself deliver overlap. Next hypotheses (H2/H3/H4) are in BACKLOG #64;
H4 — that step 7's pump set drains synchronously — is the one most
consistent with `inflight=0` holding under *any* window size.

Smoke gate passed before this run, black frames 0 of 2078, coverage 1.00x:
the run is technically clean. It is void as a *result* because the
intervention did not change the mechanism it targeted.
