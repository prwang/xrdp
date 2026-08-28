# mac_bisect_matrix — containerized per-variant xrdp fleet

Owner directive (2026-07-26, after two host-breakage incidents): bisect arms
are **never** deployed by mutating the host's single xrdp instance. Every
variant runs as its own k3s pod with its own pinned xrdp-dev deb and its own
`gfx.toml`, all up simultaneously, so the tester (owner's Mac) validates the
entire matrix in one sitting. **Server side only** — the client harness on
the host is never touched by this rig.

## Current matrix

**See "The arm set" below — the five-arm `x031`-`x035` baseline plus the
paired `x036`/`x037` #122 characterization.** Everything before x031 was
garbage-collected on 2026-08-10.

*History, in two lines, because the rig exists because of it:* this
folder was built on 2026-07-26 for the macOS blackout bisect, arms A-E
on ports 40000-40004. It convicted `nal_hrd_parameters` in the SPS VUI
(arm-c rendered black with zero SEI NALs) and exonerated `timing_info`;
arm-e carried the `xrdp_h264_sanitize_hrd()` fix, which is pinned by a
golden unit test and has shipped since. Those arms are gone; the current
normative policy is in `PRD/slices/215-annexb-and-parameter-policy.md`.

Every comparison names its deterministic payload and changed condition.
Most arms flood both monitors; `x036` and `x037` deliberately compare
both-monitor flood against monitor-0-only flood for #122. A frozen or black
target surface is a pipeline failure by construction. In the selected-monitor
arm the unselected surface is intentionally static and the banner names it.

## Files

- `Containerfile` — Debian 13 (= dev box) + distro ffmpeg/Mesa VAAPI +
  pinned debs; parametrized by `XRDP_DEB` build-arg.
- `Containerfile.ubuntu2404` — FFmpeg 6 compatibility and clean-room
  acceptance image. Its acceptance option adds XFCE, Thunar and a pinned,
  hash-verified official Chromium snapshot.
- `chromium-container` / `chromium-container.desktop` — command-line and XFCE
  launchers which state and apply the sandbox/shared-memory exceptions required
  inside the isolated privileged acceptance pod.
- `entrypoint.sh` / `startwm.sh` / `banner.sh` — pod boot + deterministic
  session content.
- `gfx/arm-*.toml` — full per-arm gfx.toml (single source of truth; becomes
  ConfigMap `xrdp-gfx-<arm>`).
- `k8s/*.yaml` — namespace + one Deployment per arm (privileged, `/dev/dri`
  hostPath, `hostPort` pinned to `hostIP 127.0.0.1`).
- `build_and_deploy.sh` — build → import into k3s → apply → roll → wait.
- `deploy_x042_cleanroom.sh` / `x042_profile.sh` — replace the final
  clean-room acceptance arm and select one certified compatibility profile.
- `arm_certify.sh` — run once per deploy, from `build_and_deploy.sh`:
  3 s of real payload through the arm, then the wire audit and the
  black-frame decode on those bytes, plus the **encoder input pipe
  guard** (BACKLOG #103 / PRD FR-BENCH-2). It reads the arm's own
  `/var/log/xrdp.log` for the `PIPE_TOO_SMALL` line xrdp emits when the
  kernel gives it less than the **64 KiB minimum** it requires — a
  measured knee, not the 1 MiB it asks for, so a host with a lowered
  `fs/pipe-max-size` raises no false alarm. A `PIPE VERDICT: TOO SMALL`
  is **NOT CERTIFIED**: an arm whose children could not get their pipes
  cannot produce a timing number that means anything, and it needs the
  owner to raise `fs/pipe-user-pages-soft` on the host. The certificate
  is reprinted by every gate run, so both verdicts travel with every
  number the arm produces.
- `e_gate_run.sh` — the acceptance-gate runner (E2/E3/E4/E5 in one
  offscreen dual-monitor session). `E_TARGET=pod` (default) measures a
  fleet arm; `E_TARGET=ssh` measures a real box over an ssh port-forward
  with the client side still here — see
  `../t4_profile/E5-2_T4_PROTOCOL.md`.
  It runs the same **encoder input pipe guard** twice: before the run, so
  a warm pod carrying the warning from an earlier session is refused
  before the wall time is spent, and after it, because on a cold pod this
  run's session is the first to spawn any child. A clamped run gets a
  banner at the top of `VERDICT.txt` above every number it contaminates,
  its lines archived as `pipe_too_small.txt`, and a non-zero exit so a
  chained leg cannot read it as good. `E_ALLOW_TINY_PIPE=1` runs anyway
  and stamps the result invalid — it suppresses nothing.
- `e52_flood_analyze.py` — where the frame interval goes, per arm: service
  split, per-monitor period, the `last=1 → next own dmg` wait that says
  whether the pipeline was full, `kids_armed` histogram, ack path.
- `netem_rtt.sh` — BACKLOG #81: simulate a WAN by putting a `tc netem`
  delay on BOTH ends of one arm's veth pair (half the RTT each way), so
  a round trip picks up the whole thing. Verifies the applied RTT by
  measurement through that arm's own RDP hostPort, refuses an interface
  carrying a qdisc it did not create, and restores everything on exit.
  `netem_rtt.sh selftest <arm>` proves all four in 22 s.
  **It replaces `ack_delay_proxy` / `ack_delay_sweep.sh`, deleted
  2026-08-03** (owner: 100 % CPU when idle, and it overlaps with netem).
  `i79_ack_delay_analyze.py` stays — it is the measurement layer the
  #80 head-to-head imports, not part of the proxy.
- `i80_wan_pair.sh` + `i80_wan_pair_analyze.py` — BACKLOG #80 step 4:
  the credit frontier measured at two round-trip times (arms x018/x019),
  with the predictions written into the runner's header before the run.
- **`sessions_off.sh` — run this after a campaign.** A fleet session keeps
  running its payload after the client disconnects; accumulated sessions
  were found burning ~4 cores (2026-07-30). One command logs every session
  off and kills the dev-box client rig.

## Host assumptions (this dev box)

- k3s single node in LXC: `/etc/rancher/k3s/config.yaml` carries
  `KubeletInUserNamespace` + `conntrack-max-per-core=0`; `/dev/kmsg` is a
  symlink to `/dev/console` (recreate after reboot if missing).
- Pods reuse the host `tester` password via its shadow hash in root-only
  `/etc/xrdp-matrix/tester.hash` (written by `build_and_deploy.sh`).
- **`fs/pipe-user-pages-soft` must be large enough on the HOST**, and it
  is not a fleet setting — this LXC's root maps to host uid 1000, so
  every pod's xrdp counts against that one host account's 64 MiB default.
  Over it, the kernel refuses `F_SETPIPE_SZ` and each encoder child gets
  a two-page input pipe; the standalone #103 reproducer measured 5.84 ms
  to hand over one 13.8 MB picture. The owner raised it to
  262144 pages the same day, with `sysctl -w` — **which is lost on
  reboot**. Nothing here re-applies it and xrdp will not touch a system
  setting; what happens instead is that `arm_certify.sh` fails the next
  certification and prints the owner action.
- GPU: AMD render node `/dev/dri/renderD128`, shared by all arms and the
  host instance (VAAPI contexts are independent; fine at banner frame
  rates).

## Host operating point for performance runs (owner-measured, 2026-08-02)

The container cannot administer host power management (user-mode incus
with `/dev/dri` mapped in): DVFS pinning is done on the METAL host by
the owner, per the procedure in
`docs/experiments/78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md`.
Measured outcomes on this box, binding for future runs:

- **CPU**: the amd_pstate recipe (`scaling_governor` +
  `energy_performance_preference` = `performance` on all cores) works
  as written.
- **GPU**: use **`high`**, NOT `profile_peak`.
  `power_dpm_force_performance_level=high` already pins
  **MCLK 1000 MHz / SCLK 2900 MHz**, which is sufficient.
  `profile_peak` drives the package to an uncomfortable thermal/power
  state — **~85 °C with NO load** — which is itself a confound (skin-
  temp/STAPM behaviour changes) and a hardware-stress risk. Do not use
  it on this box.
- Every capture taken with pins active must say so (the `level=` column
  of `clock_log.sh` records the GPU side; note the CPU side in the
  capture README). Pinned and auto runs are different conditions —
  never compared as one arm.

## Adding/changing an arm

Edit/add `gfx/arm-X.toml` + `k8s/arm-X.yaml` (next port), map the arm in
`build_and_deploy.sh` if it needs a different deb, rerun the script. Commit
the yaml/toml with the bisect log entry in `BACKLOG.md` — the matrix in git
must always describe what is actually listening.

The one temporary exception is x045 on port 40061. It is the exact x042
clean-room candidate plus fault-preserving graphics lifecycle instrumentation,
with the unchanged x042 `auto` profile. Human-rate transitions go to
`xrdp.log`; the compile-time perf-trace ring records per-frame damage, ack and
send ordering under `/var/lib/xrdp-matrix/x045-lifecycle-trace`. It exists only
to capture the repeated-capability/resize teardown before any correction. The
owner reproduced that sequence; the raw logs, complete perf trace and ownership
accounting are retained in
`captures/i142d_x045_windows_repro_20260828T002619Z/`. They prove the
reset-without-producer-repaint and one unmatched encoder, while deliberately
leaving the peer EOF separate from the narrower unproven claim that the orphan
alone caused it. The same-client development control is retained in
`captures/i142d_x044_windows_control_20260828T004123Z/`: 64 completed resizes,
one pre-login capability advertisement and no observed black/disconnect
transition. Together the captures reopen the development/clean-room
equivalence verdict at the preceding client-visible wire transition.

## The arm set (#104 baseline, #122 selected-monitor pair)

The baseline is **five arms on one image**, `x031`–`x035`, differing only in
`gfx/<arm>.toml`. The #122 `x036`/`x037` pair shares a second image and differs
only in `TEXTFLOOD_MONITOR`. Everything before x031 was garbage-collected: 30 arm
configs, 89 capture directories (32 GB down to 32 MB) and every
certificate. The reason is in `docs/pr_evidence_matrix.md` — the old
fleet was 17 pods across five images, so no two of them were comparable,
and every timing in them predates the encoder-input-pipe fix (#103).

| arm | port | flow control | chroma | mode | it exists to be |
|---|---|---|---|---|---|
| x031 | 40047 | legacy (`eager_slot_ack` off) | every frame | 444 | the reference: what upstream does today |
| x032 | 40048 | frontier, `wire_window` 1 | every frame | 444 | the legacy-equivalent window |
| x033 | 40049 | frontier, `wire_window` 2 | every frame | 444 | the frontier as proposed to ship |
| x034 | 40050 | frontier, `wire_window` 2 | sparse 1000/100 ms | 444 | the byte lever, against x033 |
| x035 | 40051 | frontier, `wire_window` 2 | n/a | 420 | what 4:4:4 costs, against x033 |
| x036 | 40052 | frontier, `wire_window` 2 | every frame | 444 | #122 both-monitors-active control |
| x037 | 40053 | frontier, `wire_window` 2 | every frame | 444 | #122 monitor-0-active / monitor-1-idle arm |
| x038 | 40054 | frontier, `wire_window` 2 | sparse 1000/100 ms | 444 | #125 A1 trailing-restoration treatment against x034 |

The current clean-room acceptance arm is x042 on port 40058. It is not a
performance-comparison arm: it carries the final paired clean-room packages,
an ordinary XFCE desktop, LXTerminal, package-default xterm, Thunar, working
Glamor, Chromium and every indexed visual/benchmark helper. Its `auto`, `444`,
`444v1`, `420` and `sparse` profiles are selected only between whole sessions
with `x042_profile.sh`.

For #125's interactive A/B, x034 and x038 both use a fresh regular XFCE
desktop with LXTerminal installed, plus the current `codescroll10.sh` and
`code_corpus.ansi` from the shared ConfigMap. Run the visual payload in
LXTerminal. No xterm resources or appearance options are installed: an xterm
opened from XFCE retains the package-default white background, small bitmap
font and non-antialiased rendering and is only a control, not the A/B payload.
Their `gfx.toml` bodies and xorgxrdp package are byte-identical. x034 retains
the pre-fix xrdp; x038 uses committed xrdp `386ca6951a3d` (the source is
unchanged from repair commit `23b6235d`).

The throughput matrix normally runs `SESSION_KIND=textflood_strip`; x034 and
x038 are temporarily assigned `SESSION_KIND=xfce` for #125's interactive
visual A/B. The slow `textflood` payload is retired as an instrument: at
16.2 ms/frame it is the same
speed as the pipeline now the pipe is unclamped, an FR-BENCH-1 margin of
1.04× against the 2.0× floor.

**Round-trip time, payload, geometry and bandwidth are run-time
conditions, never arms.** A WAN leg is `netem_rtt.sh` on x032 and x033,
not two more pods.

**All eight arms are certified.** x035 needed a fix first: the certifier
ran the AVC444 two-view wire audit against a single-view AVC420 stream,
read the whole thing as "aux", reported `main pictures=0` and failed
A1–A6 — on bytes that were correct for the configuration. Owner ruling,
2026-08-10: *"let the certify accept the 420 mode if config says so ...
the validation machine should not bite on normal functionality/legit
config an user or admin would set."* `arm_certify.sh` now reads
`avc_mode` from the arm's own gfx.toml and passes `--single-view`, and
the audit runs a different, smaller gate (S1–S4) whose checks are all
POSITIVE, so a capture it failed to parse cannot pass quietly: S1
asserts pictures were parsed at all, S2 that there is no aux sub-stream,
S3 that a reset is self-describing with an in-band SPS, and S4 that
`frame_num` is contiguous within each IDR period. x035 passes 4/4 with 220
pictures parsed.
