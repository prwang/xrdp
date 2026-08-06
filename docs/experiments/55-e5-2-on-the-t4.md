<!--
Experiment record moved out of BACKLOG.md on 2026-08-01.

BACKLOG.md is the OPEN work list: hypotheses, justification, and a
pointer. This file is the closed record it points at -- the conditions,
the numbers, the anomalies and the retractions, kept verbatim as they
were written at the time. Nothing here is a live task.
-->

## #55 — E5-2 on the T4 (DONE 2026-07-30 — **1.5×–2.3×, AMBER**, attributed)

> ### CORRECTION, same day: the T4 number is a band, not 1.67×
> Repeating both arms found the box bimodal. The batched arm splits into
> **47–49 ms** (8 runs) and **67–74 ms** (8 runs); the baseline, re-measured
> later, gave **108.9 / 109.9 ms** rather than 77.3 ms. Three pairings:
> 77.3/46.3 = **1.67×**, 109.4/72.5 = **1.51×**, 108.9/48.2 = **2.26×**.
> The drift is common-mode and every pairing clears 1.5×, so the conclusion
> holds and the single number does not. **Quote 1.5×–2.3×, centred ~1.7×.**
> Not root-caused (**#60**); the 180 s pair below is still the best single
> sample because both arms pushed the same bytes per picture (602.7 vs
> 594.0 KB) and its byte-rate ratio (1.64×) matches its frame-rate ratio.
> Where the saturated core goes: **#59** and
> `PR-demo/mac_bisect_matrix/captures/e52_t4_batched_20260730/CPU_BOTTLENECK.md`.

### RESULTS (2026-07-30, `ubuntu@100.24.126.48`, Tesla T4 / 4 vCPU Xeon 8259CL)

Both arms measured on the T4 itself under `codeflood`, 180 s each,
2560×1440 + 3840×2400, oracle client, identical xorgxrdp and identical
`gfx.toml` — so the A/B isolates xrdp steps 5+7 exactly as on the dev box.

| | baseline (0–4) `5dae11f63adb` | batched (0–7) `52b8798839ad` |
|---|---|---|
| mean per send | 77.3 ms | **46.3 ms** |
| sends/s | 12.94 | 21.61 |
| per-monitor period | 154.6 / 155.1 ms | 91.6 / 93.2 ms |
| pair service dmg→last=1 | 13.9 ms | 23.5 ms |
| worker busy | 18 % | 45 % |
| `kids_armed=4` | n/a (pre-step-5) | **93 %** of 1 965 cycles |
| pictures pushed | 1 330 MiB / 63.6 Mbit/s | 2 183 MiB / 104.5 Mbit/s |
| **E5-2 ratio** | | **1.67× — AMBER** |

Evidence: `PR-demo/mac_bisect_matrix/captures/e52_t4_{baseline,batched}_20260730/`
(READMEs, `VERDICT.txt`, gzipped `gfx_trace.txt`, decomposition, CPU
samples). A 100 s repeat reproduced the batched arm at 47.6 ms.

**Attribution — the remainder is capture-side and it is one saturated
thread.** The session Xorg runs at **92 % of a core**, measured by reading
`/proc/<xorg>/stat` twice 60 s apart during a clean run (a `top` sampler
perturbs a 4-vCPU box enough to move the rate from 47.6 ms to 71.3 ms, so
that run is kept only as CPU evidence, never as a rate). Against that: the
four NVENC children cost ~7 % of a core **each** (0.28 core total), the
worker is idle 55 % of the time, service is 23.5 ms inside a 91.6 ms
period, flow control never binds (un-acked p90 = 1 of a cap of 2,
`queue_depth` 0 throughout), and the box overall sits at ~2.5 of 4 cores.
The T4 is not out of CPU; it is out of *one* CPU. That is **#54**, and it
is why the same code gives 2.13× on the 32-core dev box and 1.67× here.

Pack bench on this CPU (CLAUDE.md obligation), 3840×2400:
**old planar 7.14 · scalar packed 53.09 · shipped vectorized 9.03 ms/frame**
— i.e. ~10 % of a 91.6 ms period spent inside the same saturated thread.

Also clean on the T4: all six E2 counters zero on both arms; wire audit
`--assert --intra-refresh 240` **7/7 PASS** (3 824 pictures batched,
2 274 baseline, 0 frame_num gaps); smoke gate as the last step **PASS** at
1920×1080 and 1024×768, 8/8 keys, edge 1.000, 0 encoder errors.

The black-frame check FAILs on both arms and is the **login paint-in**, not
a dropout: main-view luma is 0 through picture 28, 0.77 at 30, 118.9 by 40,
and there are zero black pictures afterwards in 3 824. The T4 runs XFCE,
which takes seconds to paint; the fleet pods have no desktop and paint
immediately, which is why this never appeared before. `startup_frame40_
painted.jpg` in the batched capture is the painted frame. The FAIL is left
standing in `VERDICT.txt`; see #58 for fixing the checker rather than the
evidence.

**Onscreen (§6 of the protocol) is still open** — the box is left on the
batched build with the payload disarmed, smoke-gated, ready to connect.

### Four things this run found that the protocol did not predict

1. **A freshly booted T4 fails AVC444 negotiation and silently falls back
   to RFX.** First connection after boot: `xrdp_ffmpeg: probe TIMEOUT ...
   elapsed=4008 ms, packets=0` → "ffmpeg verification FAILED (TIMEOUT);
   removing external AVC candidate" → "Matched RFX mode". Cold
   `h264_nvenc` at 3840×2400 measured 3.95 s to first output vs 1.40 s
   warm — the 4 s probe deadline is right on top of the cold path, and
   with NVIDIA persistence mode off the driver unloads whenever no CUDA
   process is running, so *every* first connection is cold. Worked around
   for this campaign with `nvidia-smi -pm 1` (recorded in the capture
   READMEs); the real fix is **#56**.
2. **`apt-get install` of an xrdp-dev deb stops at a conffile prompt**
   (`/etc/xrdp/cert.pem`, `rsakeys.ini` — modified on the box) and leaves
   xrdp-dev half-configured with `dpkg: error processing package`. Every
   T4 deb install needs `-o Dpkg::Options::=--force-confold` alongside
   `--allow-downgrades`. Folded into the protocol.
3. **The T4 runs a window manager and the fleet does not**, so the payload
   inked one monitor. `xterm -maximized` spans the root on a bare X server
   but means *the current monitor* to xfwm4 — the first probe measured 251
   damage events on surface 1 against 44 on surface 0, i.e. #53's regime
   wearing the E5-2 label. Fixed in `e52_payload.sh` by sizing the window
   to the root geometry from `xwininfo -root` (`xdotool getdisplaygeometry`
   returns the *primary monitor*, which is the same bug again) after
   removing the maximized state, and re-asserting it for the life of the
   session — a one-shot resize held on one run and lost on the next, and a
   6400×2400 window at **X=2570** covers exactly one monitor while looking
   correct in every size check.
4. **The harness could not tell.** Both of those produced ordinary-looking
   runs. `e_gate_run.sh` now prints per-surface damage coverage on every
   run and says loudly when one monitor carried it (**gate G5**):
   `COVERAGE WARNING: one monitor carried the run (1376 vs 28) ... an E5
   ratio from it is not an E5-2 result`. Both committed T4 runs pass it
   (1.01× and 1.06× imbalance).

---

## #55 (original scope) — E5-2 on the T4

The dev-box 2.13× is a VAAPI number on a 32-core box. The T4 (Cascade Lake
+ Tesla T4/NVENC) is the representative low-to-average old-CPU target, so
its ratio is the one that belongs in the PR.

**Protocol: `PR-demo/t4_profile/E5-2_T4_PROTOCOL.md`** — written before the
launch, ~40 min of instance time, every step scripted. It covers the
single-instance A/B (both debs named, and the version-sort trap: the
baseline deb sorts NEWER than the batched one, so `--allow-downgrades` and
a hash check before every measurement), the four gates a number must pass
to count (xorgxrdp still installed, deployed hash is the intended arm,
payload declared, session freshly logged off), the artifact inventory with
sizes (the oracle dumps are 5–11 GB per run and are audited on a prefix
then deleted; everything else is committed), the pack-bench and smoke-gate
obligations, and cleanup on both boxes.

Machinery that landed with it: `e_gate_run.sh` grew `E_TARGET=ssh` so the
same harness and the same analysis run against a real box over an ssh
port-forward with the client side still on the dev box;
`PR-demo/t4_profile/e52_payload.sh` + `e52-payload.desktop` +
`e52_t4_payload.sh` are the persistent, checksum-gated, autostart-armed
T4 payload (arming is a marker file plus a session logoff — never an ssh
launch into a live session); `PR-demo/mac_bisect_matrix/sessions_off.sh`
logs every fleet session off afterwards.

**Acceptance.** Both arms measured on the T4 under `codeflood`, ratio and
decomposition committed beside the captures, pack-bench ms/frame recorded
next to the deployed hashes, smoke gate PASS before the owner connects, and
the §6 onscreen checklist walked on both the Windows App (UWP) and macOS —
that list is also what the owner watches, with the mid-stream non-IDR I
refresh cadence (item 1) and the idle-monitor coupling (item 2) as the two
genuinely new risks since the 2026-07-28 T4 test.

---
