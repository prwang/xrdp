# E5-2 — saturated-payload frame interval: **2.13x, GREEN**

The measurement of record for BACKLOG **#52 (E5-2)**, and the answer to
#45's open E5 gate. Two arms, same 180 s, same geometry, same encoder
config, same xorgxrdp, same payload — only the xrdp-side steps 5+7
differ.

| | arm-t (baseline) | arm-s (#45 steps 5+7) |
|---|---|---|
| xrdp-dev | `0.10.80+git20260730013437.5dae11f63adb` (steps 0–4 + log fix) | `0.10.80+git20260730013346.52b8798839ad` (steps 0–7 + log fix) |
| xorgxrdp-dev | `1:0.10.80+git20260729225933.d77d05463e52` | **the same** |
| capture dir | `../e52_flood2_arm-t_20260730/` | this one |
| **mean per send** | **63.6 ms** | **29.9 ms** |
| p50 / p90 / p99 | 61 / 80 / 86 ms | 31 / 53 / 64 ms |
| sends/s | 15.72 | **33.40** |
| per-monitor period | 127.4 ms | **59.9 ms** |
| pictures pushed | 4 938 MiB (234 Mbit/s) | **10 434 MiB (495 Mbit/s)** |
| worker busy | 16 % | 32 % |
| `kids_armed=4` | n/a (no batch) | **52 % of 3 889 cycles** |

**Ratio 63.6 / 29.9 = 2.13x — over the 2.0x GREEN bar.** Both arms ran
`SESSION_KIND=codeflood`, both timestamps come from the fixed log clock
(#52 step 0), and the baseline is measured, not inherited: the old
51.1 ms number was taken under the 10 Hz cadence payload and is not
comparable (see `../e_gate_oracle_20260729_233749/README.md`).

Prediction 1 from the #52 spec holds: `kids_armed=4` went from 0.6 % of
cycles at 10 Hz to **52 %** here — step 7's premise is finally exercised.
Prediction 2 holds too: the parallel set is worth ~2x when both monitors
have work.

## E2 under the flood — PASS

`E2_prefix_audit.txt`: all seven assertions PASS (no mid-stream IDR,
intra only on scheduled ordinals 0/240/480, cuts paired across views,
own-slot refs and self-marks, one contiguous frame_num chain, depth
239 ≤ 240), zero black frames in 991 pictures. Server log: 0 rewrite
failures, 0 unsupported, 0 pair aborts, 0 budget assertions, 0 fifo
depth complaints. The wire holds at ~0.93 MB per picture — 20x the
cadence payload's 47 kB pair.

The audit ran on a **1.2 GB prefix**: the full dump was 7.2 GB (the
auditor reads a capture whole) and is not committed.

## The first flood pair was RED at 0.91x — and why

`../e52_flood_arm-{s,t}_20260730/` are the first attempt, kept because
the result is real and the reason it differs matters:

| | arm-t | arm-s | ratio |
|---|---|---|---|
| mean per send | 62.6 ms | 68.5 ms | **0.91x (RED)** |

That payload removed the metronome but left the ink in the left ~600 px:
a corpus line is ~27 visible columns and the xterm is 6400 px wide. The
oracle dumps proved it — **167 MB of pictures on the 2560x1440 monitor
against 0.75 MB on the 3840x2400 one**, which still received full-monitor
damage every cycle (the window spans both, so the scroll dirties both
areas; only one has changed pixels). A batch has nothing to overlap when
one of its two monitors is blank, while the shared deadline still couples
the active monitor to the idle monitor's full-area capture and upload —
hence 9 % slower than serialized.

That is not only a harness artifact: **one active monitor plus one idle
monitor is a common real desktop**, and in that regime the batch is a
small regression. Recorded as such; it is the one case where step 7 does
not pay.

`banner.sh` now repeats each corpus line 32x under `codeflood` so every
row wraps past the right edge of the second monitor. Both monitors then
carry real content, which is the workload #45 step 7 is judged on.

## What still limits the run (attribution, GREEN or not)

`E5-2_decomposition.txt`, both arms:

- **The worker is still mostly idle** — 32 % busy on arm-s. Per-pair
  service is 14.7 ms (encode collected 2.8 + rewrite/emit 12.3) against
  a 59.9 ms per-monitor period.
- **The wait is on the next capture handoff**: after a frame's last=1,
  the same monitor's next damage arrives 41 ms (p50, surface 0) to
  105 ms (surface 1) later. That is prediction 3's branch, now with
  evidence: the remaining headroom is on the capture side (deferred-
  update pacing / ack-budget retirement, and handing both monitors over
  together), which is #45's two deferred capture questions.
- **Flow control never binds**: un-acked frames p50 0 / max 4 against
  fif=2, client queue_depth 0 throughout, ack round trip 11.1 ms.
- **Not bandwidth**: 495 Mbit/s over loopback with the oracle client
  discarding after ack.

So 2.13x is what steps 5+7 are worth on today's capture path, not the
ceiling of the design.

## Instrument note

Both arms carry the #52 step-0 fix to `common/log.c` (microseconds were
being printed as the millisecond field). Verified in situ: arm-t has
**zero** out-of-order stamps and a flat histogram of fractional parts;
arm-s has 490 of 45 200 (1.1 %) with a worst backward jump of **8 ms**,
which is step 5's worker thread interleaving with the main thread in the
same log — not clock corruption (the old bug threw stamps up to ~900 ms
forward and left the `.0xx` decile empty).

**Files.** `VERDICT.txt` the harness report · `E5-2_decomposition.txt`
both arms side by side (`e52_flood_analyze.py`) · `E2_prefix_audit.txt`
the wire audit + black-frame check · `gfx_trace.txt.gz` the server's own
per-send trace (the source for every number here) · `xrdp.log.gz`,
`session-xorg.log` server logs windowed to this run · `deployed_*.txt`,
`gfx.toml` what was actually deployed · `client.log`,
`client-monitors.txt` client side.

Reproduce: `build_and_deploy.sh arm-s arm-t`, then
`E_ARM=arm-t E_PORT=40019 E_MODE=oracle e_gate_run.sh 180` followed by
`E_ARM=arm-s E_PORT=40018 E_MODE=oracle E5_BASE_MS=<arm-t mean> e_gate_run.sh 180`.
