# BACKLOG #70B — the emit split, measured: RED

The EGFX assembly split (PRD FR-ACK-2) works as specified and buys
**nothing** at this geometry. Period 33.3 ms -> 34.7 ms, i.e. **0.96x**,
against a predicted 1.22x-1.44x. The hypothesis is falsified.

## Arms

Three arms, `SESSION_KIND=codeflood`, m=1, **2560x1440 = 3.69 Mpx**,
oracle client, 45 s, AMD VAAPI (`h264_vaapi`, CQP 20), all with the
perf-trace sink armed.

| arm | xrdp deb | `emit_thread` | port |
|---|---|---|---|
| arm-w | `e6e1f6f5641e` | (not in this build) | 40022 |
| x002 | `4bbf11814323` | false — CONTROL | 40024 |
| x001 | `4bbf11814323` | **true** | 40023 |

x002 and x001 are the SAME deb and the SAME xorgxrdp
(`10fa3aa23033`), differing in one `gfx.toml` line. arm-w is the older
build, present only to check that the new binary did not regress.

Letters ran out at arm-w; arms are numbered from here (owner directive).

## Gate 4 first: the new binary is not a regression

| stage | arm-w | x002 (new, split off) |
|---|---|---|
| `subm` | 3.88 ms | 3.89 ms |
| `pump` | 10.71 | 10.61 |
| `coll` | 3.34 | 3.12 |
| `emit` | 6.35 | 6.39 |
| `gap` | 4.15 | 4.26 |
| **cycle** | **33.50 ms** | **33.42 ms** |
| send-to-send | 33.4 ms, 29.97 pairs/s | 33.3 ms, 30.04 pairs/s |

Every stage matches within 0.25 ms. The prerequisite refactor (emit no
longer touching the ffmpeg handle array; arm state published after the
join; the re-key deferred one frame) costs nothing measurable.

## Gate 2: the mechanism DID move

From the sink, cross-tabulating stage against thread id:

| arm | distinct tids | `emit_*` tid | `join_*` |
|---|---|---|---|
| x002 | 1 | same as `subm`/`pump`/`coll` | absent |
| x001 | **2** | **different** | 559 beg / 559 end |

So this is not a knob that failed to apply. The assembly genuinely left
the encoder worker.

## The result

| | x002 (split off) | x001 (split on) |
|---|---|---|
| `subm` | 3.89 ms | 3.55 |
| `pump` | 10.61 | 10.50 |
| `coll` | 3.12 | 3.13 |
| `emit` | 6.39 (on the worker) | 8.59 (on the assembler) |
| **worker serial** | **24.02 ms** | **17.18 ms** |
| **cycle** | **33.42 ms** | **34.79 ms** |
| send-to-send mean | 33.3 ms | 34.7 ms |
| pairs/s | 30.04 | 28.86 |
| pictures / 45 s | 2512 | 2416 |

Correctness held on both: wire audit `--assert` **7/7 PASS**, zero
rewrite failures, zero pair aborts, **zero black frames** (2416 and 2512
pictures decoded).

## Why it bought nothing

The split removed 6.8 ms from the worker's serial chain, exactly as
designed — and the period did not move. **The worker was never the
binding constraint.** It was 72 % occupied before the split (24.02 of
33.42 ms) and 49 % after (17.18 of 34.79 ms); a stage with 28 % slack
does not set the period, so relieving it cannot raise the rate.

FR-ACK-2's projection assumed the worker's serial chain paced the frame
period. That assumption is what this measurement falsifies. The
"children are idle 67 % of wall time" observation was correct and is
still correct — it simply does not follow that filling that idle raises
throughput, because the thing that would have to deliver more frames is
not the worker.

**Leading candidate for what does bind: the session Xorg.** Sampled at
6 s intervals through a run, the session `Xorg` sits at **93-95 % of one
core** for the whole measurement (xterm ~35 %, the codeflood shell
~31 %). `codeflood` has no metronome — it is explicitly
consumer-limited — so a producer pinned at ~1 core is the visible cap,
and 30.0 pairs/s with a p50 send gap of exactly 30 ms is what that looks
like downstream. This is the same shape as BACKLOG #59/#60 (the T4's
Xorg bottleneck) on different hardware.

Stated as the leading candidate, not as proven: what IS proven here is
the negative — the encoder worker does not bind. The decisive test for
the positive is to give the producer headroom (or lower the damage rate)
and see whether the period follows.

## Cost of the split when it is not needed

`emit` itself got **34 % slower** on its own thread: 6.39 -> 8.59 ms,
p50 5.85 -> 7.84. The assembler reads bitstream bytes the worker's core
just wrote, so the likeliest cause is cache locality across cores. It is
recorded rather than explained; it is also the reason the split is a net
0.96x rather than a neutral 1.00x.

## A void run, and why it is recorded here

The first A/B sweep measured 117-135 ms periods on all three arms —
~4x every recorded number, with `pump` and `coll` unchanged and `subm`
and `emit` 5x inflated. That split (child-bound stages normal,
CPU-bound stages starved) is a contention signature, and it was
contention: `sessions_off.sh` had been given 8 s to settle, and a
previous arm's session `Xorg` was still flooding at 95 % of a core
94 s later. Re-running arm-w alone on a verifiably quiet box reproduced
its own record (33.4 ms) immediately.

Recorded because the failure was silent: every arm was slow, so the
comparison between them still "looked" internally consistent. **A run
whose control cannot reproduce its own previous number is void
regardless of how self-consistent the arms are** — which is why arm-w
was re-measured rather than assumed.

The void numbers, kept: arm-w 134.6 ms, x002 125.8 ms, x001 121.6 ms.
None of them mean anything.

## Reproduce

```sh
cd PR-demo/mac_bisect_matrix
./build_and_deploy.sh x001 x002
./sessions_off.sh && sleep 45          # settle; verify no `Xorg :1*`
E_ARM=x002 E_PORT=40024 E_MODE=oracle E_MONITORS=1 \
  E_MODE0=2560x1440_60 E_SIZE=2560x1440 ./e_gate_run.sh 45
./sessions_off.sh && sleep 45
E_ARM=x001 E_PORT=40023 E_MODE=oracle E_MONITORS=1 \
  E_MODE0=2560x1440_60 E_SIZE=2560x1440 ./e_gate_run.sh 45
# then, per arm:
kubectl -n bisect-matrix exec <pod> -- \
  cat /var/log/xrdp-perf/$(kubectl ... ls -t | head -1) > perf/perf_enc.trace
python3 i70b_stage_split.py <capture>/perf
```
