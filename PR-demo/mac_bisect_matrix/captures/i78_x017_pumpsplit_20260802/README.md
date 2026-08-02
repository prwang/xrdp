# i78_x017_pumpsplit_20260802 — #78 Run A: the pump split, and a control that falsified its baseline

Owner-approved design (2026-08-02, observe-only; clock pinning declined):
Run A = instrumented arm x017 at fif = 1 with a 1 Hz host clock log.
Run B = `i78_x014_fif2_clocks_20260802` (untouched x014 pod, fif = 2).

## Conditions

* arm **x017**, `localhost/xrdp-bisect:661ff5fc64fa.xx10fa3aa-tf` —
  x015's `gfx.toml` body byte-for-byte from `[codec]` onward,
  `XRDP_GFX_FRAMES_IN_FLIGHT=1`, xorgxrdp `10fa3aa23033`. The xrdp deb
  adds ONLY the #78 `feedend`/`outfirst` ring records.
* one monitor 3840×2400, `SESSION_KIND=textflood`, oracle client, 60 s,
  cold session, fleet idle (no sessions in any other pod —
  `fleet_sessions_after_runA.txt`).
* `clocks.tsv`: 1 Hz sclk/socclk/gpu-busy/GPU-power/CPU-freq sidecar.
  VCN VCLK is NOT exposed on this host; GPU average power is the duty
  proxy. mclk/fclk carry no active marker on this APU and read `-`.

## Mechanism checks (gate 2), run before the rate

* All 10 060 `send` records read `fif=1`. The knob applied.
* Ring carries exactly 2 `feedend` + 2 `outfirst` per cycle (5030/5030
  over 2515 cycles), seq-paired. The instrument fired as designed.
* **The internal control FAILED, in the informative direction: pump did
  NOT reproduce x015's 26.7 ms.** See the result — this is the finding,
  not a defect of the run. (The VERDICT's "E5 GATE RED 1.25x" is against
  `E5_BASE_MS=28.2` = x015's mean; the run was FASTER than that base.)

## Result

| per cycle (worker ring, n = 2493–2506) | this run | x015 (02:07 same day) | x014 hist. |
|---|---|---|---|
| **pump** | **16.40 ms** | 26.73 | 16.62 |
| — FEED (pump_beg → last `feedend`) | 2.61 | — | — |
| — ENCODE (last `feedend` → last `outfirst`) | 13.38 | — | — |
| — DRAIN tail (last `outfirst` → pump_end) | 0.41 | — | — |
| coll | 1.36 | 1.50 | 1.36 |
| **wait (worker starved)** | **4.79 (p50 0, p90 26.7)** | 0.002 | 0.52 |
| send interval mean / p50 / p90 | 22.6 / 18.0 / 45.4 | 28.2 / 28 / 30 | 18.5 / 18 / 19 |

Closure: 16.40 + 1.36 + 4.79 + 0.05 = 22.60 vs 22.60 measured.

1. **The x015 pump inflation did not reproduce.** Same config, same
   payload, same geometry, same host, clean fleet: fif = 1 pump equals
   fif = 2 pump (16.40 vs 16.44 in Run B, same hour). x015's 26.7 ms is
   now a single unreproduced observation (its record is superseded, not
   deleted — the instrument was sound; the condition that produced it is
   unknown).
2. **The fif = 1 cost that DOES reproduce is worker starvation, in a
   tail.** 367 of 2514 egress gaps exceed 30 ms (sum 17.2 s of 56.8 s).
   Anatomy of a traced stall (t=0 = egress 223): absorb(223) at −10.5 ms
   queued the consumed report, but `xrdp_mm.c` withholds BOTH the region
   ack and the eager slot ack while `xrdp_gfx_ack_window_open` is closed
   — and at fif = 1 the window only opens on the CLIENT's ack. Credit
   reached xorgxrdp at cliack(223)+ε = +10.1 ms; capture(224) landed
   +18.6 ms; the worker sat starved 29 ms. **The slot ack's safety
   condition is "children absorbed the input" (the absorb frontier), not
   "client displayed the frame" — at fif = 1 the gate chains slot
   recycling to the client round trip, which FR-ACK-3 says the server
   must not lean on.**
3. **The deployed-vs-probe gap lives in ENCODE.** FEED 2.61 ms matches
   the probe (2.1–2.5); DRAIN is 0.41; ENCODE is 13.4 deployed vs
   7.2–8.1 standalone — the ~5 ms of deployed overhead is inside the
   children's encode service time under live-session load, in BOTH fif
   modes equally.
4. **Clocks (observe-only):** sclk sat at its 600 MHz floor in both runs
   (GFX engine idle — software rendering; `gpu_busy_percent` counts only
   GFX). GPU average power 52.9 W here vs 58.8 W in Run B — duty differs
   with cadence, pump does not. No observable clock state distinguishes
   the runs.
