# #61b/#70B — the emit split under textflood at 3840x2400: **1.12x GREEN**

The #70B A/B re-run against a payload that does not saturate the
producer. Under `codeflood` the same knob measured **0.96x**; under
`textflood` it measures **1.12x**. The difference is the payload, and
that is the finding.

## Arms

Same xrdp deb (`4bbf11814323`) and same xorgxrdp (`10fa3aa23033`) on all
four arms. The pairs differ only in `SESSION_KIND`; within a pair, only
in `gfx.toml`'s `emit_thread`.

| arm | payload | `emit_thread` | port |
|---|---|---|---|
| x001 / x002 | codeflood | true / false | 40023 / 40024 |
| **x004 / x003** | **textflood** | **true / false** | 40026 / 40025 |

m=1, **3840x2400 = 9.216 Mpx**, oracle client, 60 s, AMD VAAPI (CQP 20).

Geometry is verified, not named: the first attempt was **REFUSED** by
`setup_monitors.sh` — `FAIL: DUMMY0 active geometry is '2560x1440' but
mode '3840x2400R'` — because `E_MODE0` was overridden without
`E_MODELINE0`, so the mode carried the default 2560x1440 timings. This
is exactly the 2026-07-31 incident the check was added for, and it
caught it. The runs below pass
`E_MODELINE0="592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync"`.

## Precondition 1 — the producer is no longer the ceiling

| payload | session Xorg |
|---|---|
| codeflood (#61c) | **96.4 % of one core** |
| **textflood, x003** | **25.3 %** |
| **textflood, x004** | **25.5 %** |

textflood's own rasterization runs at 83 % of a *different* core, in its
own process. The X thread now does a SHM blit and the capture, nothing
else.

## Precondition 2 — FR-BENCH-1 PASSES

The check #62 lacked, and for want of which its 1.41x was later
annotated producer-confounded. From the payload's own per-frame
telemetry (`/tmp/e52_textflood_stamps.tsv`, 16 289 frames):

```
producer rate            65.07 fps   (interval mean 15.37 ms, p50 15.21, p90 16.69)
  render                 13.08 ms    (cairo, on another core)
  blit                    0.00 ms    (MIT-SHM)
  sync                    2.26 ms
pipeline send rate       24.94 pairs/s
margin                   2.61x
```

The producer delivers 2.6 frames for every one the pipeline ships. The
period being measured is the server's.

## Result

| | x003 (split off) | x004 (split on) |
|---|---|---|
| `subm` | 3.53 ms | 3.87 |
| `pump` | 16.87 | 16.57 |
| `coll` | 8.75 | 8.63 |
| `emit` | 5.89 (on the worker) | 9.32 (**on the assembler, concurrent**) |
| **worker serial** | **35.04 ms** | **29.07 ms** |
| **cycle** | **40.25 ms** | **35.95 ms** |
| send-to-send mean | 40.1 ms | **35.9 ms** |
| pairs/s | 24.94 | **27.90** |

**1.12x** (40.1/35.9 = 1.117; 27.90/24.94 = 1.119). Cycle and
send-to-send agree to 0.15 ms on both arms.

Correctness on both: wire audit `--assert` **7/7 PASS**, **zero black
frames** (2824 and 3158 pictures decoded), zero rewrite failures. Both
audits were run offline against the saved oracle dumps because the 260 s
harness timeout cut the in-run gate; the dumps are 5.0 and 5.5 GB.

## Reading it

The split removed **5.97 ms** of worker serial work and the period fell
**4.30 ms** — a 72 % conversion. The rest is the same effect seen under
codeflood: **`emit` runs 58 % slower on its own thread** (5.89 -> 9.32
ms), consistent with reading bitstream the worker's core just wrote.
That cost is now paid in parallel instead of in series, so it no longer
sets the period — but it is why 1.12x and not 1.17x.

It is **below** FR-ACK-2's 1.22x-1.44x, and that projection is still
withdrawn. It was computed at 2560x1440 where `emit` was 6.39 of a
24.02 ms serial chain (27 %). At 3840x2400 `pump` (16.87) and `coll`
(8.75) grow with pixel count while `emit` does not, so `emit` is 17 % of
a 35.04 ms chain and the most it could ever buy is smaller. The
mechanism behaves as specified; the size of the prize is
resolution-dependent and was never going to be 1.4x here.

## What this does NOT say

It does not restore the eager ack's projection, and it does not say
1.12x is the number at 2560x1440 — that pair (x001/x002) was measured
under codeflood and is void for this purpose. Re-running the codeflood
geometry under textflood is the obvious next comparison and has not been
done.

## Reproduce

```sh
cd PR-demo/mac_bisect_matrix
./build_and_deploy.sh x003 x004
ML0="592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync"
for a in x003:40025 x004:40026; do
  ./sessions_off.sh && sleep 30
  E_ARM=${a%%:*} E_PORT=${a##*:} E_MODE=oracle E_MONITORS=1 \
    E_MODE0=3840x2400R E_SIZE=3840x2400 E_MODELINE0="$ML0" \
    ./e_gate_run.sh 60
done
python3 i70b_stage_split.py <capture>/perf     # concurrency-aware since #61b
```
