# i55 condition B — pinned to 2 physical cores (the original T4 shape)

**The #55 H1/H2 redo on the re-provisioned T4** (2026-07-31, EC2 at
98.92.29.91, Xeon 8259CL 8 vCPU, Tesla T4, kernel 7.0.0-1009-aws), xrdp
`52b8798839ad` + xorgxrdp `d77d05463e52` — the exact 07-31 pair.
Condition B pins xrdp + xrdp-sesman (and every descendant: session Xorg,
ffmpeg children, textflood) to `CPUAffinity=0 1 4 5` = 2 physical cores +
HT siblings, reproducing the original 4-vCPU instance. Proof of condition
in `i55_condB2/env.txt` (taskset per pid).

m=1, one 3840×2160 monitor, textflood, oracle client over ssh -L.

## Gate run (this directory)

118.3 ms mean per send / p50 117 / 8.8 sends/s, worker busy 63 %, mean
service 71.0 ms, 8.29 Mpx coverage, wire audit + black-frame check PASS.
**1.04× of the 07-31 4-vCPU series (122.6 ms) — the regime reproduces.**

## Probe windows (uprobes on the deployed binaries, separate 30 s windows)

`i55_condB/` (v1 probe set) and `i55_condB2/` (v2, encoder-path symbols).
Analyzer: `PR-demo/t4_profile/i55_analyze.py`. The v2 decomposition
**closes to 0.0 ms unattributed** (legs sum 113.5 vs period 113.6):

| leg | mean ms | what it is |
|---|---|---|
| caprect→pack_ret | 8.7 | Xorg capture + SIMD pack |
| pack_ret→collect | 24.1 | msg62 handoff + NVENC encode of both views |
| collect→fstart | **35.8** | worker CPU: LTR rewrite + NUT demux of ~3.5 MB (zero pump polls inside — working, not waiting) |
| fstart→fend | 9.1 | EGFX assembly |
| fend→aemit | **30.4** | main-thread drain of two ~1.6–2.0 MB EGFX writes; module ack emitted only after the LAST write |
| aemit→xrecv | **0.8** | ack transit + Xorg servicing the xup fd (**H1 falsified**) |
| xrecv→caprect | 4.6 | deferred timer (4 ms) to next capture |
| **period** | **113.6** | |

Timer fired 1074×, captured 264× — 810 refusals, all while the previous
frame's ack was outstanding. **Capture admission is gated on the module
ack, and the module ack is chained to the completion of the entire
encode → rewrite → assemble → drain pipeline. That is the serialization,
exactly.** Nothing overlaps because the one event that would admit the
next capture is emitted last.

Scheduler delay (schedstat two-read): Xorg 1.8 %, xrdp 2–5 %, textflood
0.3 % — **nobody starves even pinned to 2 physical cores.**

## Instrument notes (bearing on earlier numbers)

- `xrdp_ffmpeg_avc444_encode_pair` records ZERO hits on this build — the
  #45 batch path calls `pump_set`. Probes must follow the code that runs.
- The GFX_TRACE records `avc dmg` / `enc submitted` / `send` are all
  written inside the EMIT pass (`gfx_wiretosurface*`), not at damage
  arrival: `e52_flood_analyze.py`'s "dmg → collected → last=1" chain
  measures within-emit stamps, and its "last=1 → next own dmg" wait
  CONTAINS the next frame's rewrite window. The 07-31 segment labels
  ("capture+pack 46.7 / idle 35.6") were mis-attributed for this build;
  this uprobe decomposition supersedes them.

## Voided predecessors

`../i55_t4_condB_pinned_{,v2_,v3_,v4_}20260731` — see their READMEs
(client-geometry mode-name bug; wait-induced desktop black frame).
