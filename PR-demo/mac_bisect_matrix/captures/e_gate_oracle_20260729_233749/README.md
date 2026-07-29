# #45 gate run — arm-r, oracle client, 180 s, E3 target geometry

The measurement of record for BACKLOG #45 gates **E2, E3, E4 and E5**.

Deployed pair (recorded by the harness before it measured anything):

| component | version |
|---|---|
| image | `localhost/xrdp-bisect:f7acb5979788.xxd77d054` |
| xrdp-dev | `0.10.80+git20260729233553.f7acb5979788` (steps 0–7) |
| xorgxrdp-dev | `1:0.10.80+git20260729225933.d77d05463e52` (step 6, recon-free) |
| gfx.toml | `aux_ltr_chain = true`, `intra_refresh_frames = 240`, VAAPI CQP 444 |
| client | oracle (acks before decode/present) on the host dummy X rig |
| monitors | 2560×1440 at +0+0, 3840×2400 at +2560+0 |

## E2 — PASS

1688 pictures per view (**1688 pairs**, gate ≥ 1000), **8 scheduled cuts
per view** (gate ≥ 4) at view ordinals 0, 240, 480, 720, 960, 1200, 1440,
1680 — the schedule, exactly.

| assertion | result |
|---|---|
| A1 no mid-stream IDR | **PASS** (0) |
| A2 intra only on a scheduled ordinal | **PASS** |
| A3 cuts paired across views | **PASS** (identical ordinal lists) |
| A4 no scheduled cut skipped | **PASS** |
| A5 own-slot refs and self-marks | **PASS** (1680/1680 P per view) |
| A6 one contiguous frame_num chain | **PASS** (0 gaps in 3376 pictures) |
| A7 chain depth ≤ 240 | **PASS** (worst 239, both views) |
| black frames | **PASS** — 3376/3376 decoded, **zero** black anywhere |
| server log: rewrite failures / unsupported / pair aborts / budget assertions / fifo depth | **0 / 0 / 0 / 0 / 0** |

## E5 — RED: 0.97×

| | baseline (pre-steps 5–7) | this run |
|---|---|---|
| mean per send | 51.1 ms | **52.5 ms** |
| p50 | 50 ms | **28 ms** |
| p90 | 103 ms | **88 ms** |
| p99 | not recorded | 190 ms |
| sends/s | 19.57 | 19.04 |

The **median halved** (50 → 28 ms) and p90 improved, but the mean did
not move, so by the item's own metric this is 0.97× — under the 1.5×
stop rule. **The stop rule applies and nothing is being re-tuned to make
the number look better.** The attribution asked for by the stop rule,
from this capture:

- **The concurrency premise almost never held.** The worker armed 4
  children in **21 of 3346 cycles (0.6 %)**; the other 3325 cycles had
  one monitor's item in hand and armed 2. E4's mechanism works — the
  counter proves it fired — but there was nothing to batch.
- **Why:** the capture side serialises the two monitors. Consecutive
  sends of *different* monitors are **26 ms apart (p50)** while a pair's
  encode-and-emit finishes in ~26 ms, so the second monitor's item
  arrives after the first is already done. Batching cannot overlap work
  the producer hands over sequentially.
- **What actually binds:** each monitor sends every **~105 ms**
  (p50 102 ms, both monitors) while its own encode costs ~26 ms — the
  worker is idle about half the time. The limit is the per-monitor
  capture/ack period, not the encoder.
- **The tail is not the refresh.** 130 gaps > 150 ms account for 28.2 s
  of the 176.9 s window (16 %), and only **2 of them** sit at or next to
  a scheduled cut ordinal. Excluding that tail the mean is 46.0 ms
  (1.11× the baseline) — so even a tail-free run would be far short of
  2.0×.

With the rendering client (`../e_gate_render_*`, 60 s, same arm) the
end-to-end rate is **5.92 sends/s = 2.96 pairs/s per monitor**, against
5.94 / 2.97 measured before #45: unchanged, exactly as predicted for a
client-bound session. Notably the batch fired **34 of 305 cycles (11 %)**
there — a slower consumer is what lets two items coexist, which is the
same mechanism seen from the other side.

## E4 — mechanism proven, premise rare

`GFX_TRACE batch ... kids_armed=4` occurred 21 times, and the
once-per-run INFO line names the four armed children. So "ONE thread
drives FOUR views concurrently" is demonstrated on the deployed build,
by an asserted counter rather than a wall-clock inference — but at 0.6 %
of cycles it cannot carry E5, which is the finding above.

## Refresh cost (E6's bandwidth half), measured here

From this capture's own picture sizes: a P pair is 47 614 B
(main 18 593 + aux 29 021) and a paired cut adds
(140 206 − 18 593) + (117 716 − 29 021) = 210 308 B. Amortised over the
240-pair period that is **876 B/pair = +1.84 %** — better than the PRD's
predicted ≈ +4 % at N = 240. The arm-n/arm-m `bandwidth_bench.sh` A/B was
NOT re-run (arm-n is `SESSION_KIND=xfce`, a different payload, so it is
not comparable to this run); this figure is the refresh cost on the
gate's own corpus, which is what E6 asks to have recorded rather than
assumed.

**Files.** `VERDICT.txt` the generated report · `gfx_trace.txt` the
server's own per-send trace (the E5/E4 source) · `oracle/` the kept AVC
payload dump (the E2 source) · `xrdp.log`, `session-xorg.log` server
logs, windowed to this run · `deployed_*.txt`, `gfx.toml` what was
actually deployed · `client.log`, `client-monitors.txt` client side.

## What was trimmed before committing

The oracle payload dump was **80 MB** (3376 pictures at the E3 geometry)
and is not committed; the audit and black-frame results it produced are
in `VERDICT.txt`, and it is reproducible with
`E_ARM=arm-r E_PORT=40017 E_MODE=oracle e_gate_run.sh 180`. `gfx_trace.txt`
and `xrdp.log` are committed gzipped — they are the E5/E4 source data and
are worth keeping verbatim.
