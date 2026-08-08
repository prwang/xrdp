# i80_onscreen_walk_x027_20260808 — the credit frontier in front of a real, late-acknowledging client

**The first time the frontier's client term has been exercised by
anything other than the oracle client.** Owner-driven onscreen walk on
arm x027 (host port 40043), 2026-08-08. The owner's verdict on what they
saw: *"your payload is displayed without lag."*

Two things make this worth archiving, and neither is visible from the
client:

1. **The client acknowledged genuinely late** — median 68 ms and 53 ms
   in the two sessions, against ~9 ms for the oracle client, which
   acknowledges before it decodes.
2. **The window actually filled, repeatedly, and its bound held
   exactly** — including at TWO monitors, where the bound is 6 and 6 was
   reached 73 times.

## Setup

| | |
|---|---|
| arm | x027, host port 40043, XFCE desktop |
| xrdp | `821218e54c24` — credit frontier on by default, no emit thread in the binary |
| xorgxrdp | `10fa3aa23033` |
| config | `eager_slot_ack = true`, `wire_window = 2`, `aux_ltr_chain = true`, `avc_mode = "444"`, `h264_encoder = "ffmpeg"` (read off the running pod, not the manifest) |
| client | reported computer name `INTERN14DT`, reached over an ssh port-forward from the owner's laptop. **The client PRODUCT is not recorded** — the log does not distinguish the Windows and macOS clients, and the owner ran more than one connection |
| payloads | the onscreen probes: `chroma-probe`, `colorkey_x11`, `chroma_strip_anim`, `codescroll10.sh` |

`XRDP_GFX_FRAMES_IN_FLIGHT=2` is set in the pod environment and is inert
on this build: it feeds the legacy gate, which this ack path never
reaches.

**This is not a benchmark.** A visible client decodes and presents every
frame and is the known bottleneck in this configuration. No rate here
says anything about the server.

## What was measured, and how

`PR-demo/mac_bisect_matrix/i80_ack_latency.py`, over the arm's own perf
rings (archived under `perf/`, gzipped). Regenerate `analysis.txt` with:

```
i80_ack_latency.py perf/enc.3081 --monitors 1 --window 2
i80_ack_latency.py perf/enc.3952 --monitors 2 --window 2
```

Acks are cumulative, so each egressed frame is paired with the FIRST ack
whose id reaches it — by id, never by time window (the 2c gate).

## Session A — one monitor, 3840×2160

2076 frames, all acknowledged.

| ack latency | p10 | p50 | p90 | max |
|---|---|---|---|---|
| this client | 49.9 ms | **68.2 ms** | 93.9 ms | 282.6 ms |
| oracle client, for scale | 5.2 | 9.4 | 20.1 | 36.4 |

Distance at egress — how far ahead of the client's acknowledged position
each frame was when it left:

| distance | frames | share |
|---|---|---|
| 1 | 992 | 47.8 % |
| 2 | 468 | 22.5 % |
| 3 | 443 | 21.3 % |
| **4 — the bound** | **173** | **8.3 %** |

Bound `wire_window + 2 × monitors` = 4. Worst observed 4. **HELD.**

## Session B — TWO monitors, 2 × 3840×2160

The owner reconnected with two monitors (`xrdp_egfx_reset_graphics:
width 7680 height 2166 monitorcount 2`). 3202 frames; one still
outstanding when the ring was snapshotted, i.e. the session was live.

| ack latency | p10 | p50 | p90 | max |
|---|---|---|---|---|
| | 30.8 ms | **53.0 ms** | 89.9 ms | 448.8 ms |

| distance | frames | share |
|---|---|---|
| 1 | 731 | 22.8 % |
| 2 | 882 | 27.5 % |
| 3 | 777 | 24.3 % |
| 4 | 545 | 17.0 % |
| 5 | 194 | 6.1 % |
| **6 — the bound** | **73** | **2.3 %** |

Bound at M = 2 is `2 + 2 × 2` = 6. Worst observed 6. **HELD.**

**Read the monitor count before reading this table.** At first pass this
looked like a bound violation — distance 6 against a remembered bound of
4 — because the bound was being compared against the one-monitor case.
It is not a violation; it is the two-monitor bound met exactly. The
monitor count is not in the ring, so it must be taken from the session
log and passed to the analyser; getting it wrong manufactures a
violation or hides one.

## Why the two-monitor session matters more than the one-monitor one

`BACKLOG` #80 recorded the `C + 2·M` bound as measured at M = 2 — with
the oracle client, on loopback, where the window is barely approached
(1 frame in 953 touched the bound). Here the bound was touched **73
times**, and 25 % of frames left with the client 5 or 6 behind, against a
client whose acknowledgements arrive 30–90 ms late over a forwarded
connection. The arithmetic was already believed; this is the first time
the wire was actually made to carry it.

## Faults

Zero encoder restarts, pair sequence mismatches, parser errors or pair
timeouts in the arm's log for the whole period.

## Limits

* **The visual half is partial.** The owner reported no lag on the
  payload they ran. The full six-check walk across both clients at both
  sizes was not completed, and this record must not be read as one.
* **The client product is not recorded** (above), so "both clients" is
  not evidenced here.
* The ack-latency comparison against the oracle client is not a
  controlled A/B: different payload, different resolution, different
  transport. It is offered for scale. The distance histograms stand on
  their own and need no comparison.
* Which of the frontier's three terms was binding is still not
  attributable — they tie at the emission instant. The distance
  histogram is the evidence instead.
* No legacy control arm was run, by owner decision: build one only if the
  frontier arm visibly faults.
