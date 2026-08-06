# i78_x014_fif2_clocks_20260802 — #78 Run B: fif = 2 control with clock log

Run B of the owner-approved #78 pair (2026-08-02, observe-only). The
UNTOUCHED x014 pod — same image, gfx.toml and cert as its 2026-08-01
capture (`i75_x014_rewrite_20260801`) — rerun for 60 s with the 1 Hz
host clock sidecar (`clocks.tsv`), one monitor 3840×2400,
`SESSION_KIND=textflood`, oracle client, cold session, fleet idle.

## Why it exists

With clock pinning declined, the observational comparison needs the
fif = 2 side of the clock log, same hour, same host state as Run A
(`i78_x017_pumpsplit_20260802`).

## Mechanism check

All 12 572 `send` records read `fif=2`.

## Result

* send interval **18.1 ms** mean / 18 p50 / 19.7 p90 — reproduces the
  2026-08-01 capture (18.5) apples-to-apples; ring `enc.19009`.
* worker pump **16.44** / coll 1.34 / wait 0.28; closure
  16.44+1.34+0.28 = 18.06 vs 18.08 measured.
* clocks: sclk at the 600 MHz floor throughout (GFX idle), GPU average
  power **58.8 W** (vs Run A's 52.9 W at fif = 1) — duty tracks cadence;
  pump does not (16.44 vs Run A's 16.40).

Serves as the same-day uninstrumented control proving the #78 trace
points did not perturb the measurement (Run A pump 16.40 on the
instrumented build).
