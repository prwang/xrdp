# m=1 at 4K on the T4: capture and encode NEVER overlap — 0 / 205

**RED, and it contradicts the PRD.** PRD "Concurrency state of the encode
pipeline" states `capture ‖ encode` is **"YES for m = 1, shipped"**
(FR-CAPTURE-8), measured at 1600×912 where *"capture is fully hidden"*.
At 3840×2160 on the same box it is not hidden at all: **0 of 205 frames**
overlapped, and the minimum gap between one frame's last send and the
next frame's capture arm is **+7 ms — never once negative.**

## The ladder (CLAUDE.md "Escalation ladder"), all three rungs

| rung | where | m | geometry | payload | overlap |
|---|---|---|---|---|---|
| 1. CI | `make check` | 1 | n/a | n/a | **admitted** — 156/156 PASS |
| 2a. local | arm-s, AMD VAAPI | 1 | 1920×1080 (2.07 Mpx) | codeflood | 7 / 86 = **8.1 %** |
| 2b. local | arm-s, AMD VAAPI | 1 | 3840×2160 (8.29 Mpx) | codeflood | 4 / 53 = **7.5 %** |
| 3. T4 | Tesla T4 / nvenc | 1 | 3840×2160 (8.29 Mpx) | textflood | 0 / 205 = **0.0 %** |

Rung 1 proves the shipped predicates *admit* the overlap at m=1 — the
per-monitor `xup_cap_budget` and `xrdp_gfx_ack_window_open` both grant it
(`test_overlap_m1_capture_and_encode_do_overlap`, with three controls so
it cannot pass vacuously). So this is not a flow-control configuration
that forbids overlap. It is admitted and does not happen.

## The measurement

Pair by frame identity (`id_server`), never by cycle window — a cycle
window mis-attributes sends to the wrong frame and produced a negative
segment on the first attempt (BACKLOG #64, quality gate 2c):

```
overlap = (batch arm of frame N+1) - (last=1 send of frame N)
negative => the next capture began before the previous send finished
```

T4, 30 s, 207 sends over 25.3 s:

```
  batch -> batch (full period)          mean 122.6 ms
  capture + pack                         46.7 ms = 37.9 %
  encode + assembly                      40.7 ms = 33.1 %
  idle (awaiting damage)                 35.6 ms = 29.0 %
  OUR PIPELINE                           87.3 ms = 71.0 %

  OVERLAP: n=205  min +7.0  p50 +36.0  mean +35.7  max +78.0 ms
           overlapping frames: 0 (0.0 %)
```

Numbers close: 46.7 + 40.7 = 87.4 ≈ 87.3 service; 87.3 + 35.6 = 122.9 ≈
122.6 period.

## It is NOT the capture budget

`budget exceeded` and `third capture` are **0** on the T4 run and on both
local runs. The capture side never hit its own capacity limit, so it was
never blocked by the two-slot budget — it simply never had a second frame
to capture while the first was in the encoder. Whatever serialises this
is upstream of the slot accounting.

The `min +7 ms` floor is the strongest single clue: a load effect would
scatter around zero and cross it occasionally, as the local runs do
(min −4 ms at 1080p, −2 ms at 4K). A hard floor that is never crossed in
205 consecutive frames looks structural.

## What this does NOT say

- **Local vs T4 is not a controlled comparison.** Different payload
  (codeflood vs textflood) *and* different hardware. The local 8 % and
  the T4 0 % may not be measuring the same thing, and the difference
  must not be attributed to nvenc-vs-VAAPI on this evidence (CLAUDE.md
  stand-in rule). What stands on its own is the T4 row: on the real
  hardware, at the target resolution, overlap is zero.
- **The two local rungs ARE comparable to each other** (same arm, same
  payload, same client) and they say resolution barely moves it:
  8.1 % → 7.5 % across 4× the pixels.
- **29 % idle** means the payload is not saturating even here, so some of
  the non-overlap is "nothing to capture yet". But idle alone does not
  force zero: local 1080p had 31.9 % idle and still overlapped 8.1 % of
  the time.

## Reproducing

```sh
# rung 1
make -C tests/xrdp check          # 156/156, incl. the 4 overlap assertions

# rung 2 (local fleet, no GPU box needed)
cd PR-demo/mac_bisect_matrix
E_ARM=arm-s E_PORT=40018 E_MONITORS=1 E_MODE0=1920x1080_60 \
  E_MODELINE0="173.00 1920 2048 2248 2576 1080 1083 1088 1120 -hsync +vsync" \
  E_OUT=$PWD/captures/local_arm_s_m1_1080p_5s ./e_gate_run.sh 5
E_ARM=arm-s E_PORT=40018 E_MONITORS=1 E_MODE0=3840x2160R \
  E_MODELINE0="533.00 3840 3888 3920 4000 2160 2163 2168 2222 +hsync -vsync" \
  E_OUT=$PWD/captures/local_arm_s_m1_4k_5s ./e_gate_run.sh 5

# rung 3 (T4) — smoke gate DISARMED first, then arm textflood
python3 e52_period_decompose.py <capture>/gfx_trace.txt.gz
```

`E_MONITORS=1` requires `/size` — without `/multimon` xfreerdp asks for
its own 1024×768 regardless of what the client X server presents. The
harness now passes it and the VERDICT prints the session's real geometry
from the damage extents (here: `3840x2160 = 8.29 Mpx over 207 damage
records`), because the first attempt at this run was served at 1024×768
and would have been read as a 4K result.

`oracle/` (raw AVC444 dump) was audited by the wire audit and the
black-frame check, then deleted; everything above re-derives from
`gfx_trace.txt.gz`.
