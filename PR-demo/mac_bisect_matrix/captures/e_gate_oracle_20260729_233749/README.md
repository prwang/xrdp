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

## E5 — RED: 0.97× (REASSESSED 2026-07-30: both sides payload-clocked)

| | baseline (pre-steps 5–7) | this run |
|---|---|---|
| mean per send | 51.1 ms | **52.5 ms** |
| sends/s | 19.57 | 19.04 |

0.97× is under the 1.5× stop rule: RED, nothing re-tuned. The first
version of this section attributed the result to the capture side; the
2026-07-30 reanalysis on repaired timestamps (`reanalyze_repaired.py`,
this directory) corrected it — first version in git history. The
percentile rows originally published here, and `VERDICT.txt`'s, were
computed on corrupted stamps and are superseded.

- **The payload clocks the run.** `SESSION_KIND=code` sleeps 0.1 s per
  scroll line (`banner.sh`): per-monitor period p50 102 ms (mean
  104.7), the two monitors 26 ms apart in phase, and all 121
  steady-state gaps > 150 ms are exactly ONE skipped beat (~204 ms =
  2× the period). The only larger gaps are two session-startup
  transients (4.3 s / 9.0 s). The pipeline is never full.
- **The server is nearly idle.** Service per pair **11.8 ms** (encode
  collect 4.2 + rewrite/emit 7.6; the "~26 ms encode-and-emit" first
  recorded here was a clock artifact), oracle ack 2.1 ms, then ~95 ms
  waiting for the same monitor's next handoff. Worker busy 22 %.
  Nothing waits on encode, rewrite or ack.
- **The mean is not tail-driven.** 52.5 ms is the harmonic of two
  ~105 ms payload periods: the 70–150 ms wait-gaps contribute 35.2 ms
  of it, the entire > 150 ms tail 7.9 ms; tail-free mean 46.3 ms
  (1.10×). p50 ≈ 26 ms is burst spacing between the two monitors'
  frames, not throughput. (Long gaps near a scheduled cut: 9 of 122,
  vs ~5.5 expected by chance — the refresh is not the tail either.)
- **Verdict on the gate itself:** the 51.1 ms baseline ran the SAME
  10 Hz payload, so this experiment could not show an encoder-side
  gain and cannot convict the capture path. Successor benchmark:
  **BACKLOG #52 (E5-2)** — unclocked `codeflood` payload, baseline
  re-measured under the same flood.

**Instrument bug found during the reassessment (upstream xrdp):**
`common/log.c:1159` computes `tv.tv_usec + 500 / 1000` (= µs + 0) and
truncates it to 3 chars — whenever the true sub-second part is
< 100 ms (10.4 % of lines here) the printed fraction is the leading
digits of the µs count, up to ~0.9 s late (acks stamped before the
sends they acknowledge). File order is causal; repair = right-running
minimum. Means are robust, so 0.97× stands. Fix is #52 step 0.

With the rendering client (`../e_gate_render_*`, 60 s, same arm) the
end-to-end rate is **5.92 sends/s = 2.96 pairs/s per monitor**, against
5.94 / 2.97 measured before #45: unchanged, exactly as predicted for a
client-bound session. The batch fired **34 of 305 cycles (11 %)** there
— a slower consumer is what lets two items coexist.

## E4 — mechanism proven; premise was payload-limited

`GFX_TRACE batch ... kids_armed=4` occurred 21 of 3346 cycles (0.6 %),
and the once-per-run INFO line names the four armed children — "ONE
thread drives FOUR views" is demonstrated by an asserted counter. But
with items landing 26 ms apart and 11.8 ms of service, two items almost
never coexist: 0.6 % is the 10 Hz payload's number, not the mechanism's
ceiling. #52 (E5-2) retests the premise under saturation.

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

**Files.** `VERDICT.txt` the generated report (its E5 percentile lines
are superseded by the reassessment above — kept verbatim as the
instrument's raw output) · `reanalyze_repaired.py` the 2026-07-30
timestamp-repair reanalysis (run it on the decompressed
`gfx_trace.txt`) · `gfx_trace.txt` the server's own per-send trace (the
E5/E4 source) · `oracle/` the kept AVC payload dump (the E2 source) ·
`xrdp.log`, `session-xorg.log` server logs, windowed to this run ·
`deployed_*.txt`, `gfx.toml` what was actually deployed · `client.log`,
`client-monitors.txt` client side.

## What was trimmed before committing

The oracle payload dump was **80 MB** (3376 pictures at the E3 geometry)
and is not committed; the audit and black-frame results it produced are
in `VERDICT.txt`, and it is reproducible with
`E_ARM=arm-r E_PORT=40017 E_MODE=oracle e_gate_run.sh 180`. `gfx_trace.txt`
and `xrdp.log` are committed gzipped — they are the E5/E4 source data and
are worth keeping verbatim.
