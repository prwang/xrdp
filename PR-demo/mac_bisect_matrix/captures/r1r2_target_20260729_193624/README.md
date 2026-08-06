# R1 + R2 at the E3 target geometry — 2560×1440 + 3840×2400

Taken 2026-07-29 with `PR-demo/mac_bisect_matrix/r1r2_target_geometry.sh 90`.
Both #45 recon gates answered on ONE offscreen session at the size the
acceptance gate is measured at, rather than at the 2×1024×768 development
layout used for the first R1 run (`../r1_slot_recon_20260729_191020/`).

Server: arm-q (arm-n's encoder config on the recon xorgxrdp `957fa79`),
`/dev/shm` = 1 GiB. Client: host dummy X server on `:94` presenting
2560×1440 at +0+0 and 3840×2400 at +2560+0 (virtual 6400×2400), driving
`xfreerdp3 /multimon /gfx:AVC444`. 528 sends over 87 s.

## R1 — capture-slot pinning: CONFIRMED, same as at 2×1024×768

| measurement | value |
|---|---|
| full-pass consecutive sends reusing the same slot | **479 / 479** (zero changes) |
| monitor 1 distinct slots used all run | **1** (based at 22 118 400; second slot never written) |
| monitor 0 slot changes | 46, all on partial passes |
| a monitor holding two outstanding frames in two slots | **2 in 528 sends (0.38 %)** |
| max (`rect_id − rect_id_ack`) | **2** — global budget saturated; 244/528 sends at depth 2 |

Monitor 0's two slot offsets are 0 and 11 059 200 = 2560·1440·1.5·2 views,
so the observed offsets confirm the layout arithmetic exactly.

## R2 — the `/dev/shm` floor: PASS at a flat 1 GiB

| measurement | value |
|---|---|
| capture arena reserved by xorgxrdp at connect | **77 414 400 B (73.8 MiB)** |
| predicted `w·h·1.5 × 2 views × 2 slots`, summed | 22 118 400 + 55 296 000 = **77 414 400 B** |
| peak `/dev/shm` used across the session | **77 414 400 B** — the arena is the only consumer |
| tmpfs configured | **1 GiB → 13.9× headroom**, 950 MiB unused at peak |

No SIGBUS, no "outstanding rect budget exceeded". The same reservation
line read 9 437 184 B at 2×1024×768, also exactly the formula — the model
is confirmed at two geometries, not extrapolated from one.

## Observations that are not gate results

- The session delivered **3.03 pairs/s per monitor** (6.06 sends/s) at
  this geometry. That is the throughput #45 E5 exists to move, measured
  here for the first time at target size.
- FreeRDP logged `YUV decoder: intersecting rectangles, aborting` **48**
  times, all clustered in a ~5 s window shortly after session start, then
  never again (16 times, likewise early, in the 2×1024×768 run). Not
  investigated here and not caused by the recon build, which adds only a
  log statement; it belongs to the region-construction path.

**Files.** `r1slot.txt` raw per-send records · `VERDICT.txt` generated R1
and R2 report · `shm_samples.txt` per-second tmpfs samples ·
`session-xorg.log` server session log · `client.log`,
`client-monitors.txt` client side · `pod.log`. (No `client-xorg.log`
here: the `:94` dummy X server was already up from bringing the layout
online, so the run reused it instead of starting and logging its own.)
