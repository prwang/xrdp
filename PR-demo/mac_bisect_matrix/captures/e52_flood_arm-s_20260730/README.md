# E5-2, first attempt — RED at 0.91x, and the payload defect it exposed

Kept as evidence, not as the gate result. The gate result is
`../e52_flood2_arm-s_20260730/` (**2.13x GREEN**).

| | arm-t (steps 0–4) | arm-s (steps 0–7) |
|---|---|---|
| capture dir | `../e52_flood_arm-t_20260730/` | this one |
| mean per send | 62.6 ms | 68.5 ms |
| p50 / p90 / p99 | 61 / 77 / 97 ms | 62 / 118 / 157 ms |
| per-monitor period | 125.4 ms | 137.1 ms |
| `kids_armed=4` | n/a | 35 % of 1 902 cycles |
| **ratio** | | **0.91x — RED** |

`SESSION_KIND=codeflood` had removed the 10 Hz metronome (that part
worked: the interval stopped being a payload echo) but the ink still sat
in the left ~600 px of a 6400 px xterm, because a corpus line is ~27
visible columns. From the oracle dumps of this pair:

    2560x1440 monitor : 167 MB of pictures   (arm-s)   191 MB (arm-t)
    3840x2400 monitor : 0.75 MB              (arm-s)   0.83 MB (arm-t)

The second monitor was **blank**, yet received full-monitor damage every
cycle — the xterm spans both monitors, so a scroll dirties both areas
while only one has changed pixels. A batch cannot overlap two encodes
when one of them is an all-skip frame, and the shared deadline still ties
the active monitor to the idle monitor's full-area capture and upload.
Hence slower than serialized.

Two things follow, and both are recorded rather than swept up:

1. **The payload was fixed, not the verdict.** `banner.sh` now repeats
   each corpus line 32x under `codeflood`, so every row wraps past the
   right edge of the second monitor and both carry real content. That is
   the workload #45 step 7 is judged on, and it produces 2.13x.
2. **0.91x is a real regime.** One active monitor next to an idle one is
   an ordinary desktop, and there the batch is ~9 % slower than
   serialized. It is the one measured case where step 7 does not pay.
   Follow-up (recorded in BACKLOG #52 results): arm a monitor only when
   it has changed pixels, rather than because the window overlaps it.

E2 was clean here too: 7/7 assertions PASS on both arms, zero black
frames in 2 820 (arm-t) / 2 566 (arm-s) pictures, zero rewrite failures.

Dumps are not committed (161 MB / 184 MB). `gfx_trace.txt.gz` is the
source for every number above; re-derive with
`e52_flood_analyze.py "arm-t=...arm-t.../gfx_trace.txt" "arm-s=.../gfx_trace.txt"`.
