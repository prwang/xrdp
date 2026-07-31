# E5-2 on the T4 with the textflood payload — 1.41×, RED, and attributed

**The A/B: baseline 122.9 ms → batched 87.0 ms per send = 1.41×.** That is
below the 1.5× AMBER floor, so under BACKLOG #52's stop rule it is **RED**
and the remainder is attributed below. Nothing was re-tuned to improve it.

| | baseline (#45 steps 0–4) | batched (#45 steps 0–7) |
|---|---|---|
| mean per send | 122.9 ms | **87.0 ms** |
| sends/s | 8.14 | **11.50** |
| per-monitor period | 246.8 / 245.8 ms | 174.0 / 173.0 ms |
| damage coverage | 715 / 714 (**1.00×**) | 1009 / 1009 (**1.00×**) |
| `kids_armed=4` | n/a (cannot batch) | **100 % of 1011 cycles** |
| pictures | 1424 | 2014 |
| black frames | **0** | **0** |
| wire audit | 7/7 PASS | 7/7 PASS |
| **ratio** | | **1.41× RED** |

Both arms ran 180 s in one sitting, same payload, same `gfx.toml`, same
`xorgxrdp-dev 1:0.10.80+git20260729225933.d77d05463e52`. Each deb install
was smoke-gated **before** measuring (the #61 precondition): 8/8 keys,
edge 1.000, at 1920×1080 and 1024×768.

## The payload change worked — that part is unambiguous

`textflood` (PR-demo/textflood/) replaced the xterm codeflood payload so
the benchmark would stop measuring the X server. It did:

| | xterm codeflood (2026-07-30) | textflood (this run) |
|---|---|---|
| session Xorg | **92 % of a core — the ceiling** | **28.9 % of a core** |
| libpixman, self | **71.6 %** of Xorg cycles | **6.3 %** |
| our capture path | 13.8 % of Xorg cycles | **37.4 %** |
| monitor coverage | needed a span-fixer loop, 3 runs lost to it | **1.00× both arms, no fixer** |

`rdpDeferredUpdateCallback → rdpCapRect → rdpCaptureGfxA2 →
a8r8g8b8_to_avc444_box → avc444_decode_row` is now **37.4 %** of Xorg's
cycles (`avc444_decode_row.avx2` alone is 26.5 % self, the largest single
symbol in libxorgxrdp). The payload's own drawing is gone: pixman fell from
71.6 % to 6.3 %. What replaced it at the top is `ProcShmPutImage →
damagePutImage → rdpPutImage` at 43.6 % — textflood's own blit, i.e. the
memcpy floor, which is irreducible for any payload that hands X finished
frames.

## Attribution — and it is NOT a CPU ceiling

Measured live with `../../t4_profile/session_cpu_split.sh`
(`session_cpu_split.txt`), two `/proc/<pid>/stat` reads 45 s apart:

```
  Xorg (capture + X)    28.9 % of a core
  payload               79.7 % of a core
  xrdp encoder side     24.4 % of a core
  measured total       132.9 % of a core = 1.33 of 4 cores
```

**Nothing is pinned and the box has 2.7 idle cores.** So the 87 ms is not
a CPU ceiling — it is a *serialisation*. `e52_period_decompose.py` on the
run's own trace (`E5-2_period_decomposition.txt`) says where:

| segment | ms | % of the 173.7 ms period | whose |
|---|---|---|---|
| batch arm → first enc submit — **capture + AVC444 pack** | **63.7** | 36.7 % | **ours** (xorgxrdp) |
| enc submit → last=1 — **encode + LTR rewrite + EGFX assembly** | **68.4** | 39.4 % | **ours** (xrdp) |
| last=1 → next batch arm — idle, awaiting damage | 41.5 | 23.9 % | payload |
| **our pipeline** | **132.1** | **76.1 %** | |

**76 % of every period is our own pipeline, running serially, on a box
that is 33 % busy.** That is the finding. The benchmark now measures the
component we own — which was the whole point of building textflood — and
what it says is that the remaining cost is latency we have not
parallelised, not arithmetic we cannot afford.

## Why only 1.41×, when codeflood gave 1.5–2.3×

The batch is working *perfectly* — `kids_armed=4` in 100 % of cycles,
against a baseline that cannot batch at all. It gets less leverage here
for a reason that is visible in the bytes: textflood damages the **whole
root every frame**, so a picture pair is ~3.8 MB (main 2.09 MB + aux
1.69 MB) against codeflood's ~0.6 MB. The batch parallelises the *encode*
of the four children; it does nothing for the 63.7 ms of capture+pack in
front of it or for the assembly behind it. Amdahl, measured: the batch
speeds up part of a serial chain that this payload made much longer.

This is not a defect in the batch and not a regression — it is the batch
being measured against a workload heavy enough to expose what is *not*
batched.

## What this motivates: 4:2:0 during motion

The two big serial segments are both paid **twice**, once per view. The
aux view is a second full-frame pack and a second encode: 1.69 MB of the
3.8 MB pair, **44.8 % of the bytes**, and roughly half of
`a8r8g8b8_to_avc444_box`'s work, which is the 26.5 %-self symbol above.

Dropping the aux view *while the screen is in motion* — full 4:4:4 only
when it settles — attacks **both** segments at once, which is exactly what
a 1.41× says is needed: not a faster encoder, but less serial work per
period. Filed as **BACKLOG #63**.

## Reproducing

```sh
sh PR-demo/t4_profile/e52_t4_payload.sh install      # builds textflood on the T4
ssh -f -N -i /root/.ssh/tmp_access_T4 -o ServerAliveInterval=15 \
    -L 33389:127.0.0.1:3389 "$(cat /root/.t4_host)"

for arm in 5dae11f6 52b87988; do
    sh PR-demo/t4_profile/e52_t4_payload.sh disarm            # gate needs it off
    sh PR-demo/t4_profile/t4_install_arm.sh $arm
    sh PR-demo/smoke_gate/smoke.sh                            # MUST pass first
    sh PR-demo/t4_profile/e52_t4_payload.sh arm textflood
    cd PR-demo/mac_bisect_matrix
    E_TARGET=ssh E_PORT=33389 E_USER=ubuntu E_CRED_FILE=/dev/shm/.t4_cred \
      E_MODE=oracle E_REFRESH=240 E_COLD=1 \
      E_OUT=$PWD/captures/e52_t4_textflood_${arm}_$(date +%Y%m%d) \
      ./e_gate_run.sh 180
done
python3 PR-demo/mac_bisect_matrix/e52_period_decompose.py <capture>/gfx_trace.txt.gz
```

**The smoke gate must run with the payload DISARMED.** textflood's window
is override-redirect over the whole root, so nothing — including the smoke
gate's own test window — can stack above it; gating with it armed returns
`ok=0 edge=0.006`, every key `got=black`, which looks exactly like a broken
deb and is not.

`oracle/` (5.0 GB of raw AVC444 dump) was audited by the wire audit and the
black-frame check, then deleted. Everything above is re-derivable from
`gfx_trace.txt.gz` and `VERDICT.txt`.

## Three harness faults this run exposed, all fixed

1. **`E_COLD` raced sesman's teardown.** It waited only for the Xorg
   *process* to vanish, but sesman needs ~600 ms more to reap the WM and
   retire the session; a client connecting inside that window is accepted
   into a dying session and dropped (`freerdp_post_connect failed`). The
   baseline arm won the race and the batched arm lost it — a flake that
   reads as "the batched deb cannot start a session". `e_gate_run.sh` now
   waits for sesexec too.
2. **A deb install invalidates `xrdp.service` and its drop-ins.**
   Restarting without `systemctl daemon-reload` silently dropped
   `XRDP_GFX_TRACE=1`, producing a run with **zero** trace records and no
   computable rate. `t4_install_arm.sh` now reloads and *verifies* the
   variable is in the unit environment.
3. **A dead ssh port-forward is reported as a successful connection.** The
   harness printed `connected; recording for 180s` and produced a VERDICT
   against an empty log. Two runs were lost to this before it was spotted.
