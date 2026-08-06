# E5-2 on the T4 — batched arm (#45 steps 0–7), 2026-07-30

**The gate result: 46.3 ms mean per send against the baseline's 77.3 ms —
1.67×, AMBER.** Short of the 2.0× prediction, above the 1.5× floor. Under
the #52 stop rule that is recorded as-is and attributed; nothing was
re-tuned to make it look better, and the baseline is a real T4 measurement
(`../e52_t4_baseline_20260730/`), never the dev box's number.

> ## CORRECTION (same day, later): the number is a BAND, 1.5×–2.3×
>
> Repeating both arms found the T4 is **bimodal**. Sixteen further runs of
> the batched arm split into two tight clusters — **47–49 ms** (8 runs) and
> **67–74 ms** (8 runs) — and the baseline, re-measured hours later, came
> back at **108.9 / 109.9 ms** rather than 77.3 ms.
>
> | pairing | baseline | batched | ratio |
> |---|---|---|---|
> | original 180 s pair (below) | 77.3 ms | 46.3 ms | **1.67× AMBER** |
> | slow-state pair | 109.4 ms | 72.5 ms | **1.51× AMBER** |
> | paired re-measure, same session, deb swapped between | 108.9 ms | 48.2 ms | **2.26× GREEN** |
>
> The drift is largely **common-mode** — both arms slow together, and every
> pairing clears 1.5× — so the *conclusion* (the batch is worth well over
> 1.5× on the T4) survives. The single number does not. **Report the T4
> result as 1.5×–2.3×, centred near 1.7×**, not as 1.67×.
>
> The two clusters differ in bytes, not in cadence alone: the fast cluster
> averages **580 KB per picture at a 92 ms period**, the slow one **875 KB
> at 140 ms** (`E5-2_run_to_run_variance.txt`), with `encode collected →
> last=1` moving 21.3 → 32.6 ms in step. Encode time tracks picture size,
> so the loop has two self-consistent equilibria. What tips it is **not
> root-caused**: it is not the codec (no run fell back to RFX — checked in
> every `xrdp.log`), not the corpus position (3 000 lines, ±17 % density,
> fully traversed every ~12 s), not the profiler (both clusters occur with
> and without one), and not compositing (the xfconf change did not stick and
> did not move the number). Filed as **BACKLOG #60**.
>
> What makes the 180 s pair below still the best single sample: both arms
> pushed the **same bytes per picture** (602.7 KB vs 594.0 KB, 1.5 % apart),
> so the ratio is content-neutral, and the byte-rate ratio (7.6 → 12.5 MiB/s
> = 1.64×) matches the frame-rate ratio (1.67×). A pairing where the arms
> had *different* mean picture sizes would not be trustworthy — check that
> line before quoting any future E5-2 number.
>
> Also: **60–110 s runs are too short.** Use ≥180 s, and pair the arms
> inside one sitting rather than trusting a baseline measured hours earlier.
>
> Where the saturated core actually goes: **`CPU_BOTTLENECK.md`**.

| | T4 baseline (0–4) | T4 batched (0–7) | dev box batched (ref) |
|---|---|---|---|
| mean per send | 77.3 ms | **46.3 ms** | 29.9 ms |
| sends/s | 12.94 | **21.61** | 33.40 |
| per-monitor period | 154.6 / 155.1 ms | 91.6 / 93.2 ms | 59.9 ms |
| pair service (dmg→last=1) | 13.9 ms | 23.5 ms | 14.7 ms |
| `kids_armed=4` | n/a (pre-step-5) | **93 %** of 1 965 cycles | 52 % |
| pictures pushed | 1 330 MiB | **2 183 MiB** | 10 434 MiB |
| wire rate | 63.6 Mbit/s | 104.5 Mbit/s | — |
| **ratio** | | **1.67× AMBER** | 2.13× GREEN |

## What was deployed

* xrdp-dev `0.10.80+git20260730013346.52b8798839ad` — #45 steps 0–7
* xorgxrdp-dev `1:0.10.80+git20260729225933.d77d05463e52` — step 6
* `/etc/xrdp/gfx.toml` = `PR-demo/t4_profile/gfx-t4-nvenc-ltr-g240-gate.toml`
  (h264_nvenc, `aux_ltr_chain=true`, `intra_refresh_frames=240`)
* Tesla T4 / driver 580.173.02, Xeon Platinum 8259CL, **4 vCPUs**
* payload `codeflood`, 2560×1440 + 3840×2400, oracle client, 180 s

`deployed_packages.txt` carries the hashes as `dpkg -l` reported them at
run time — the version-sort trap (the baseline deb sorts NEWER than this
one) makes install order untrustworthy, so the recorded hash is the proof
of which arm this is.

## Why 1.67× and not 2.13× — attributed, not excused

**The T4's session Xorg is a single thread at 92 % of a core.** Measured
during a clean repeat of this arm by reading `/proc/<xorg>/stat` twice, 60 s
apart (`t4_xorg_cpu_frac.txt`) — two file reads, so the measurement does not
move the thing it measures. The `top` samples agree (85–100 % of a core,
`t4_top_sample_*.txt`).

Everything else has slack:

* the four NVENC children cost **~7 % of a core each** — 0.28 core total.
  On this box the encoder is nearly free; the capture is not.
* the worker is busy 45 % of the run; service is 23.5 ms against a 91.6 ms
  per-monitor period, so **74 % of each period is waiting for the next
  damage**, not encoding it.
* flow control never binds: un-acked frames p90 = 1 against a cap of 2, and
  client `queue_depth` is 0 for the whole run.
* the box as a whole is at ~2.5 of 4 cores. It is not out of CPU — it is out
  of *one* CPU, the one Xorg runs on.

So the batch did its job (`kids_armed=4` in 93 % of cycles, up from a
pre-step-5 build that cannot batch at all) and the remaining time is
capture-side, which is **BACKLOG #54**. The dev box reached 2.13× because
its Xorg had headroom; the T4 is the low-to-average old-CPU target and hits
the single-threaded capture wall first. That is a result about the target,
not a defect in the batch.

For scale: `tools/avc444_pack_bench.c` on this CPU costs **9.03 ms/frame**
for the shipped vectorized 4K pack (vs 53.09 ms scalar, 7.14 ms for the old
planar half) — ~10 % of a 91.6 ms period, spent inside that same saturated
thread.

## Reproducibility

A 100 s repeat with the identical config came back at **47.6 ms** (vs 46.3),
`kids_armed=4` in 79 % of cycles, coverage imbalance 1.18× —
`VERDICT_repeat_100s.txt`.

An earlier 75 s repeat came back 71.3 ms and is **not** comparable: a
`top -b -d 2` sampler was running on the 4-vCPU box at the time. That is why
the CPU attribution above uses the two-read `/proc` probe. Perturbed runs
are kept (`t4_top_sample_*.txt`) as the evidence for *where* the CPU goes,
never as rate numbers.

## E2 / wire — clean

* zero on all six server-side counters (rewrite failed, unsupported, did not
  return, budget exceeded, third capture, `fifo_to_proc_depth`)
* `avc444_ltr_wire_audit.py --assert --intra-refresh 240`: **7/7 PASS** over
  3 824 pictures — no mid-stream IDR, intra only on scheduled ordinals
  (0/240/480/…/1680), cuts paired across views, own-slot refs and
  self-marks, one contiguous frame_num chain with 0 gaps, worst chain depth
  239 ≤ 240
* smoke gate against this exact deployed binary+config, as the last step:
  **PASS** at 1920×1080 and 1024×768, 8/8 keys each, edge fidelity 1.000,
  0 encoder errors

### The black-frame check FAILs, and it is the login paint-in

`oracle_black_frame_check.py` reports 16 black main-view pictures at
indices 0–30 of 3 824 and calls the run FAIL. Decoding the head of the dump
settles what they are: main-view luma is exactly 0 through picture 28, 0.77
at picture 30 (first ink, max pixel 255), and 118.9 by picture 40 — a
monotone paint-in ramp. `startup_frame40_painted.jpg` is picture 40 of
**monitor 0**: the corpus, wrapped and repeated, with the XFCE panel on top.
There are **zero** black pictures after index 30.

This is the session painting from nothing after `E_COLD=1` logged the old
one off, and it is new on the T4 only because the T4 runs XFCE (seconds to
paint) where the fleet pods run the payload with no desktop at all and paint
immediately. The checker's "mid-stream" rule is `0 < i < n-2`, which cannot
distinguish a startup ramp from a real dropout; on this evidence the run is
clean, but the FAIL is left standing in `VERDICT.txt` rather than edited
away.

## Reproducing

```sh
sh PR-demo/t4_profile/e52_t4_payload.sh install
sh PR-demo/t4_profile/e52_t4_payload.sh arm codeflood
ssh -f -N -i /root/.ssh/tmp_access_T4 -L 33389:127.0.0.1:3389 "$(cat /root/.t4_host)"
cd PR-demo/mac_bisect_matrix
E_TARGET=ssh E_PORT=33389 E_USER=ubuntu E_CRED_FILE=/dev/shm/.t4_cred \
  E_MODE=oracle E_REFRESH=240 E5_BASE_MS=77.3 \
  E_OUT=$PWD/captures/e52_t4_batched_$(date +%Y%m%d) ./e_gate_run.sh 180
```

`oracle/` (2.29 GB of raw AVC444 dump) was audited by the two checks above
and then **deleted** — it is not committed. Everything needed to re-derive
every number in this README is in `gfx_trace.txt` and `VERDICT.txt`.
