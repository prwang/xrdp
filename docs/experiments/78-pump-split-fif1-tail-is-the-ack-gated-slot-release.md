<!--
Experiment record. BACKLOG.md is the OPEN work list; this file is the
record it points at. Kept verbatim, wrong claims included.
-->

# #78 — the pump split: fif = 1's cost is the ack-gated slot release, and the 26.7 ms pump was never reproduced

2026-08-02. Instrument: two `PERF_TRACE6` records per child per cycle in
`xrdp/xrdp_encoder_ffmpeg.c` (commit `661ff5fc64fa`) — `feedend` (the
picture is fully in the pipe; input no longer paces the worker) and
`outfirst` (first output byte since submit; the child finished
encoding). Runs (owner-approved, observe-only; clock pinning declined):

* **Run A** — arm x017: x015's config byte-for-byte + the instrumented
  deb, `XRDP_GFX_FRAMES_IN_FLIGHT=1`, 1 Hz host clock log.
  `captures/i78_x017_pumpsplit_20260802`.
* **Run B** — the untouched x014 pod (fif = 2), same hour, same clock
  log. `captures/i78_x014_fif2_clocks_20260802`.

## Lead with what fails

1. **The #78 internal control failed in the informative direction: the
   x015 baseline did not reproduce.** fif = 1 pump = **16.40 ms** =
   fif = 2's 16.44 (same hour, clean fleet, closures 22.60/18.06 ms vs
   22.60/18.08 measured). x015's 26.73 ms pump — the number #76 was
   filed on — is a single unreproduced observation; its record carries
   a dated supersede note; the condition that produced it is UNKNOWN
   and is #76's remaining open sub-question.
2. **The fif = 1 cost that does reproduce is a starvation tail caused
   by a mis-gated producer ack** — specified as BACKLOG **#79**, now
   the top-priority item (owner, 2026-08-02).

## Trace archaeology (existing x014/x015 rings, before any new run)

* **Cold capture pages: refuted.** Within-arm residency→pump slope
  0.05–0.28 ms/ms (the between-arm delta needs ~1.0); 60 x014 cycles
  after 10–37 ms producer stalls pumped 0.04 ms-residency frames at
  baseline speed (16.64 vs 16.58 ms).
* **The 10 ms poll timeout: refuted.** The children's stdout is always
  in the poll set (`pump_arm`), so the timeout is a wakeup floor, not a
  delay; both pump histograms are smooth flat shifts, no 10 ms mode.
* **Per-unit slowdown was real in the x015 data**: pump vs own coded
  bytes regressed 9.85 + 1.94·MB (x014, r 0.74) vs 14.30 + 3.54·MB
  (x015, r 0.87). This pointed at an operating-point change and
  motivated the clock question — see below for how the runs answered
  it.
* Wait-chain map (held up in the runs): capture(N+2) is released by the
  producer ack riding behind egress(N) (+0.77 ms p50 in both arms);
  every stage phase was identical between arms; fifo residency is a
  consequence of pump length, not a cause.

## The split (Run A, 2493 cycles)

| stage | mean | p10 | p50 | p90 | p99 |
|---|---|---|---|---|---|
| pump total | 16.40 | 15.44 | 16.33 | 17.40 | 18.83 |
| FEED (pump_beg → last feedend) | 2.61 | 2.11 | 2.61 | 3.05 | 3.58 |
| ENCODE (last feedend → last outfirst) | 13.38 | 12.46 | 13.34 | 14.30 | 15.41 |
| DRAIN tail (last outfirst → pump_end) | 0.41 | 0.27 | 0.41 | 0.54 | 0.72 |

FEED matches the in-pod probe (2.1–2.5 ms), so the ~5 ms
deployed-vs-probe gap (probe pair total 9.3–10.6 ms) sits inside
**ENCODE** under live-session load — equally in both fif modes.
Caveat carried on the record: `feedend` means "picture fully in the
pipe"; the child may still hold ≤ 1 pipe window unread.

## The reproducing defect: the ack-gated slot release

`xrdp_mm_update_module_frame_ack` (`xrdp_mm.c:1697`) emits BOTH
producer acks — ordinary/region and the #70 eager SLOT_ONLY — only
inside `xrdp_gfx_ack_window_open(client, server, fif)`. PRD #70
specifies the eager ack's emission point as **max(absorb N,
egress N−1)**; the client's ack appears in neither the spec nor the
in-tree safety condition (the absorb frontier).

Measured (withheld = credit_emit − absorb(k−2); zero negative values):

| | fif = 1 (Run A) | fif = 2 (Run B) |
|---|---|---|
| withheld p50 / p90 | 0.04 / **36.4 ms** | 0.02 / 0.05 ms |
| withheld > 10 ms | **31.1 %** (782/2513) | 1.5 % (46/3141) |
| worker waits > 20 ms | 338 (336 id-match withheld frames) | 23 |
| send interval mean / p90 | 22.6 / 45.4 ms | 18.1 / 19.7 ms |

p90-wait timeline (frame 13; the mid-run stalls are identical in
shape): slot-safety met at −16.72 ms (absorb 11); window closed at
every re-check (cliack(10) −8.85: 10+1>11 false; cliack(11) +11.77:
11+1>12 false); credit finally emitted +28.36 (cliack(12) opened the
window); capture executed in 5.56 ms; worker starved 33.95 ms.
**Withheld 45.08 ms past the specified emission instant.**

1526 of 2513 captures were released by a **solo** SLOT_ONLY emission —
slot credit alone demonstrably suffices for xorgxrdp to capture. Within
one run: credit at absorb+ε → never a stall; withheld → every stall.

**Why fif = 2 masks it.** Window-open at the absorb instant needs
`cliack(server)`. At fif = 2 that ack belongs to a frame one full
period older — the ack race (client egress→ack p50 8.5 / p90 18.9 ms)
gets a period of headroom and loses only on p99 hiccups. At fif = 1 the
needed ack's frame egressed ~8–10 ms earlier — a near-fair race, lost
31 % of the time. **The second in-flight credit was buying slack in an
ack race the mis-gated slot ack created — not concurrency.** The
residual 1.5 % at fif = 2 is x014's long-known p99 send tail (47 ms in
`i75_x014_rewrite_20260801`; the "66 of 3074 cycles stall on the
producer" PRD note): one defect, both arms' tails, scaled by window
depth.

Also measured while verifying the regression surface: **egress is not
gated by the window at all** (send-time `id_server − id_client` reaches
1 on 1566/5030 payload sends and 2 twice at fif = 1; up to 3 at
fif = 2). fif's client-facing bound is emergent, not enforced.

Fix, blast radius, test method and regression surface: **BACKLOG #79**.

## Clocks: what was observed, and the limit of this container

Observe-only (pinning declined this round): GFX sclk sat at its 600 MHz
floor in both runs (GFX engine idle; `gpu_busy_percent` counts only
GFX); GPU average power 52.9 W (fif = 1) vs 58.8 W (fif = 2) — duty
tracks cadence, pump does not. Duty-driven DVFS is refuted as a
steady-state explanation of the (unreproduced) x015 number.

**VCN VCLK/DCLK are not observable from this environment**: the dev
box runs xrdp inside a user-mode incus container with `/dev/dri` mapped
in; `pp_dpm_vclk`/`pp_dpm_dclk` are not exposed, debugfs is empty, and
the container's root cannot administer host power management. mclk/fclk
carry no active-level marker on this APU from here either
(`clock_log.sh` states all of this per line).

## Follow-up procedure: pin the operating point on the METAL host (owner-run)

Purpose: make the hardware operating point deterministic so the
unreproduced x015 condition either cannot recur (if it was a
power/thermal transient) or recurs under pinned clocks (which would
exonerate DVFS entirely). To be run on the metal host, not in the
container; every measurement taken with any of this applied must record
the pin state (the `level=` column of `clock_log.sh` picks up the GPU
side automatically; note the CPU side in the capture README).

```sh
# --- observe first (metal exposes what the container cannot) ---------
cat /sys/devices/system/cpu/amd_pstate/status        # active|passive|guided
grep . /sys/class/drm/card0/device/pp_dpm_*          # incl. vclk/dclk on metal
sudo cat /sys/kernel/debug/dri/0/amdgpu_pm_info      # live VCLK/DCLK/power
cat /sys/firmware/acpi/platform_profile 2>/dev/null  # STAPM/skin-temp budget

# --- CPU: pin all cores to the performance operating point ------------
for c in /sys/devices/system/cpu/cpu*/cpufreq; do
    echo performance | sudo tee $c/scaling_governor >/dev/null
    echo performance | sudo tee $c/energy_performance_preference >/dev/null 2>&1
done
# verify: grep . /sys/devices/system/cpu/cpu*/cpufreq/scaling_cur_freq

# --- GPU: force peak DPM levels (covers VCN via the SMU profile) ------
echo profile_peak | sudo tee \
    /sys/class/drm/card0/device/power_dpm_force_performance_level
# verify the pin took (mechanism check before any measurement):
grep . /sys/class/drm/card0/device/pp_dpm_sclk   # '*' on the top level
sudo cat /sys/kernel/debug/dri/0/amdgpu_pm_info  # VCLK/DCLK at max

# --- platform power budget (STAPM) — often the real transient ---------
echo performance | sudo tee /sys/firmware/acpi/platform_profile

# --- revert everything after the measurement window -------------------
echo auto | sudo tee /sys/class/drm/card0/device/power_dpm_force_performance_level
for c in /sys/devices/system/cpu/cpu*/cpufreq; do
    echo powersave | sudo tee $c/scaling_governor >/dev/null   # or the prior value
done
echo balanced | sudo tee /sys/firmware/acpi/platform_profile
```

> **Outcome, owner-run on the metal host, 2026-08-02.** Both pins
> applied. The CPU recipe works as written. On the GPU, **`high` is the
> level to use on this box**: it already pins MCLK 1000 / SCLK
> 2900 MHz. `profile_peak` drove the package to ~**85 °C with no
> load** — an uncomfortable thermal/power state that is itself a
> confound and a stress risk; do not use it here. Recorded as binding
> in `PR-demo/mac_bisect_matrix/README.md` ("Host operating point").

Notes for reading the results honestly:

* `profile_peak` pins every engine's clock for profiling (higher idle
  power/heat than `high`, which pins only sclk/mclk); either is
  acceptable if recorded. `manual` + writing index masks into
  `pp_dpm_vclk` is the surgical option if metal exposes it.
  *(Superseded by the outcome note above for THIS box: `high` only.)*
* On this APU class the likelier transient is the **STAPM/skin-temp
  power budget**, which DPM forcing does not override: a heat-soaked
  package silently lowers the sustained budget and per-IP clocks. If
  chasing the x015 condition, capture `amdgpu_pm_info` (power, VCLK)
  at 1 Hz on metal during runs, plus `sensors`/thermal_zone temps, and
  run once cold vs once heat-soaked.
* Pins do not survive reboot/driver reload — that is a feature; do not
  persist them. A pinned run and an auto run are different conditions
  (quality gate 5): never compare them as if they were the same arm.
* If x015's 26.7 ms pump recurs UNDER pinned clocks and performance
  platform profile, clocks are exonerated and the condition hunt moves
  to software (scheduling, memory pressure, a concurrent consumer).

## Correctness

x017 certified at deploy (`certs/x017.cert`: ASSERT VERDICT PASS 7/7,
0 black frames, contiguous frame_num chain). Both runs: E2 counters all
zero. Both sessions logged off by the gate. Fleet session state
recorded post-Run A (`fleet_sessions_after_runA.txt`: clean).
