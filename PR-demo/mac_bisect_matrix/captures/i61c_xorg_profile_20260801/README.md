# #61c — what saturates the session Xorg on the dev box

Opened by #70B's red. Answers: the encoder worker provably does not pace
the frame period — what does?

Conditions: arm **x002** (the #70B control, split off), m=1 at
2560x1440, `SESSION_KIND=codeflood`, oracle client, AMD dev box.
All three runs delivered 29.9-30.1 pairs/s, so the pipeline was in the
same steady state throughout.

## The measurement

`perf` sampling is UNAVAILABLE on this box: `kernel.perf_event_paranoid
= 4` and it is host-owned (`sysctl -w` silently fails inside the LXC
container). `perf record` produces a zero-byte file. This is the same
class of blocker as the seccomp `bpf()` denial found for #70B, and it is
why the breakdown below is built from `/proc/<pid>/stat` deltas and an
offline bench rather than a symbol profile.

| phase | client | Xorg CPU | pairs/s |
|---|---|---|---|
| **A** | **disconnected** — payload alone | **98.9 % of one core** | — |
| **B** | connected — payload + xorgxrdp capture | **96.4 % of one core** | 30.05 |

20-25 s `utime+stime` deltas, pid verified stable across each window.

**The payload alone saturates the X server.** Adding the entire capture
path moved Xorg's CPU by **-2.5 points**, not up. It is already pinned;
the capture does not add load, it *displaces* payload drawing inside the
same single thread. `codeflood` has no metronome — it is explicitly
consumer-limited — so it simply draws less when something else takes
cycles.

This reproduces the T4's confirmation in #59 (**99.9 %** with no client
connected) on entirely different hardware.

## Decomposition

The capture's own share, derived from `tools/avc444_pack_bench.c` (the
shipped pack loops verbatim, built `-O2`, run on this CPU):

```
packed views VECTORIZED (shipped)  3840x2400: 3.27 ms/frame -> 0.355 ms/Mpx
packed views VECTORIZED (shipped)  2000x1000: 0.80 ms/frame -> 0.400 ms/Mpx
```

At 2560x1440 = 3.686 Mpx that is **1.3-1.5 ms/frame**, and at 30.05
frames/s **3.9-4.4 % of one core**.

| | share of the Xorg thread |
|---|---|
| xorgxrdp capture pack | **~4 %** |
| everything else — xterm glyph compositing, scroll blits, Present emulation, fills | **~92 %** |

Closure check: capture adds ~4.1 points while total Xorg fell 2.5
points, so payload drawing gave up ~6.6 points. Consistent with an
elastic consumer-limited payload; the numbers agree.

Caveat on the split: the bench measures the PACK, which is most but not
all of xorgxrdp's capture path (damage tracking, region ops and shmem
are not in it). On the T4 the AVX2 kernels were 13.4 of the 13.8 points
attributed to the whole capture chain, so pack ~= capture there; that is
assumed, not measured, here.

## Why the "optimized cairo feeder" did not help: it was never in this run

`textflood` (#62) — the payload that rasterizes with cairo in its own
process and hands X one finished image over MIT-SHM, measured at
**7.7x less X-thread cost** for the same corpus — **is not wired into
the container fleet.** `PR-demo/mac_bisect_matrix/banner.sh` has no
`textflood` kind at all; the binary lives in `PR-demo/textflood/` and
was only ever deployed to the (now decommissioned) T4. x001/x002
inherited arm-w's `SESSION_KIND=codeflood`, which is precisely the
xterm-glyph payload #59 identified as making the benchmark measure the
X server.

So this is not "the feeder is not enough". The feeder was not present.

## What this means for #70B

**#70B's red is a property of the payload, not a verdict on the split.**
Under a producer-bound workload the encoder worker cannot be the
constraint no matter how it is arranged, so no worker-side change is
measurable here — including the two that have already been spent:
#70 (1.11x) and #70B (0.96x).

Projected headroom under textflood, using #62's ratio: X-side drawing
~92 % -> ~13 %, leaving ~85 % of the Xorg thread. At 1.3-1.5 ms of pack
per frame the producer would stop being the ceiling by a wide margin,
and the encoder's 33 ms cycle (24 ms of worker serial work) would become
the visible constraint for the first time.

**Not a promise.** #62's own T4 result came back 1.41x RED and was later
annotated **producer-confounded** — both arms may have been paced by the
same producer. Swapping the payload is necessary, not sufficient:
FR-BENCH-1's check (the payload's measured frame rate must exceed the
pipeline's by a clear margin) has to pass on the new arm BEFORE any
ratio taken on it is quoted.

## Reproduce

```sh
# Phase A: with a session up and the client gone
XPID=$(pgrep -f "Xorg :1[0-9] .*xrdp" | head -1)
# sample utime+stime from /proc/$XPID/stat 20 s apart

# Phase B: same, 40 s into an e_gate_run so the cold-session relogin is past
E_ARM=x002 E_PORT=40024 E_MODE=oracle E_MONITORS=1 \
  E_MODE0=2560x1440_60 E_SIZE=2560x1440 ./e_gate_run.sh 60

gcc -O2 -o /tmp/packbench tools/avc444_pack_bench.c -lm && /tmp/packbench
```
