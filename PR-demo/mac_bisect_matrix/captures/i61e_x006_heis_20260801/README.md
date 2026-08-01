# #61e — the instrument is transparent, and the period closes to 0.007 ms

Two questions, both answered here:

1. **Does observing the pipeline change it?** No — measured, not assumed.
2. **Where does the 36.8 ms period go, under textflood with the emit
   split ON?** Every microsecond of it is now attributed to a bracket.

The answer to (2) also retires a claim I made on 2026-08-01 from the
x005 20 s run — "the encoder essentially never waits for a frame". That
is true of x005 (**split OFF**) and **false of x006 (split ON)**, where
the worker stalls on 35.2 % of cycles. See "The finding" below.

## Arms

| arm | image | `emit_thread` | `XRDP_PERF_TRACE` | port |
|---|---|---|---|---|
| x005 | `2781220ae747` | false | armed | 40027 |
| **x006** | `2781220ae747` | **true** | **armed** | 40028 |
| **x007** | `2781220ae747` | **true** | **DISARMED** | 40029 |

x006 and x007 run the **same image** and a **byte-identical `gfx.toml`
body**; `k8s/x007.yaml` differs from `k8s/x006.yaml` by exactly one env
var. That pair is the control for the instrument itself — x003 could
never have been one, because x003 has the sink armed too and merely
happened to have a single writing thread.

x007's earlier purpose (fresh pod vs old deb) was retired **unrun**: a
microbench inside the two existing pods answered that bit in seconds,
and building a fleet arm to answer a bit is what the 2026-08-01
CLAUDE.md rule forbids.

m=1, 3840x2400 = 9.216 Mpx, oracle client, **60 s**, AMD VAAPI CQP 20.
Geometry verified by active pixel extent, not by mode name
(`E_MODELINE0` passed with `E_MODE0`).

## Precondition — FR-BENCH-1 passes on both arms

The producer's own telemetry (`/tmp/e52_textflood_stamps.tsv`), so the
period being measured is the server's and not the payload's:

| arm | producer | pipeline | margin |
|---|---|---|---|
| x005 | 71.3 fps (14.03 ms, render 11.96) | 24.33 sends/s | **2.93x** |
| x006 | 59.6 fps (16.79 ms, render 14.35) | 27.22 sends/s | **2.19x** |

## Q1 — the tracer is transparent

| arm | trace | mean period | sends/s | n |
|---|---|---|---|---|
| **x006** | **ARMED** | **36.8 ms** | 27.22 | 1544 |
| **x007** | **DISARMED** | **37.1 ms** | 26.96 | 1530 |

**The traced arm is 0.3 ms *faster* than the untraced one** — the
difference is the wrong sign for an overhead and is inside run-to-run
noise. The ring tracer does not perturb the observable.

For scale: 20 events/frame x 27 frames/s = ~540 events/s, each a vDSO
clock read and a store into a ring the calling thread owns. The v1
shared-`FILE*` tracer moved this same measurement from 40.4 ms to
**135.3 ms** at a *lower* event rate, because `fprintf` takes
`flockfile` and a second producing thread serialised the worker against
the main thread (`common/perf_trace.h`, PRD FR-TRACE-1).

Two independent instruments agree on the period, which is quality gate
1: the server log's send-to-send interval says **36.8 ms**, the perf
trace's `wait_beg -> wait_beg` cycle says **36.764 ms**.

**Gate 4, surfaced rather than buried.** Against the recorded
pre-instrumentation pair (x003/x004, `4bbf1181`): x005 41.1 vs x003
**40.1**, x006 36.8 vs x004 **35.9** — both arms ~1 ms (2.5 %) slower on
the instrumented deb. With n=1 per arm this cannot be separated from
day-to-day drift, and it is **common-mode**: the ratio is untouched.
x005/x006 = 41.124/36.764 = **1.119x**, against x003/x004's recorded
**1.117x**.

## Q2 — the frame-interval breakdown, textflood + threaded emit

Cycle = `wait_beg -> wait_beg`, the top of the worker loop. The
partition is over **every consecutive pair of worker events**, so it
closes by construction; the open quantity is not a residue but a named
list of *gaps between brackets*. n=1543 (cycle 0 excluded — it holds the
session's first-frame wait).

```
segment                       n      mean       p50       p90   per-cycle   share
pump_beg -> pump_end       1543   16.690   16.606   17.688     16.690   45.4%
coll_beg -> coll_end       1543    8.486    8.139   10.345      8.486   23.1%
subm_beg -> subm_end       1543    4.134    3.781    5.765      4.134   11.2%
rel_beg  -> rel_end        1543    2.707    2.485    3.594      2.707    7.4%
book_beg -> book_end       1543    2.489    2.041    3.537      2.489    6.8%
wait_beg -> wait_end       1543    2.249    0.002    1.938      2.249    6.1%
join_beg -> join_end       1543    0.001    0.000    0.001      0.001    0.0%
drain_beg -> drain_end     1543    0.000    0.000    0.001      0.000    0.0%
  ... 9 end -> beg gaps                                         0.007    0.0%
------------------------------------------------------------------------------
NAMED STAGES                                                   36.757  100.0%
GAPS (the unknown)                                              0.007    0.0%
period                                                         36.764
```

**Unknown: 0.007 ms** — against the 0.5 ms target, and against the
6.88 ms that x004's stage-sum left unattributed. The largest single gap
is `wait_end -> drain_beg` at 0.003 ms.

Per-cycle closure residual max **0.000000 ms**. This is computed
per cycle and reported as a distribution, never as sum-of-means: the
−6.77 ms "unattributed" seen on 2026-08-01 was one 2665 ms startup wait
divided across 408 cycles, and quality gate 2c is what caught it.

### The split, accounted for exactly

x005 (OFF) → x006 (ON), per cycle:

| | x005 | x006 | Δ |
|---|---|---|---|
| `emit` (on the worker) | 6.005 | — | **−6.005** |
| `wait` | 0.085 | 2.249 | **+2.164** |
| `pump` | 17.234 | 16.690 | −0.544 |
| `coll` | 8.898 | 8.486 | −0.412 |
| `book` | 2.894 | 2.489 | −0.405 |
| `subm` | 3.717 | 4.134 | +0.417 |
| `rel` | 2.285 | 2.707 | +0.422 |
| `join` | — | 0.001 | +0.001 |
| gaps | 0.005 | 0.007 | +0.002 |
| **period** | **41.124** | **36.764** | **−4.360** |

The column sums to −4.360 exactly. **The split removes 6.0 ms of serial
assembly and hands back 2.2 ms of new idle**, netting 4.4 ms — which is
the whole of the 1.12x, and the first mechanical account of why it is
1.12x and not 1.17x.

Assembly itself is now 88.6 % hidden: 1544 emit spans, 9.796 ms/frame,
of which 8.68 ms/frame overlaps worker-busy time and **1.119 ms/frame
is exposed**.

## The finding — `capture || encode` is NOT fully landed with the split ON

This is the part that does not go the way the projection wanted.

| | x005 (split OFF) | x006 (split ON) |
|---|---|---|
| `wait` per cycle | **0.085 ms (0.2 %)** | **2.249 ms (6.1 %)** |
| cycles stalling > 1 ms | 4 of 1381 (**0.3 %**) | 544 of 1543 (**35.3 %**) |
| frame already enqueued at `wait_beg` | **99.7 %** | **64.8 %** |
| fifo residency `enq -> take` | 16.125 ms | 3.899 ms |

With the split OFF the encoder holds a frame essentially always — the
fifo is 16 ms deep and capture is completely hidden. **The split makes
the worker 6 ms faster per cycle, it drains the fifo, and capture
latency becomes exposed.** The two rows are the same mechanism seen from
both ends, and they agree: 100 − 64.8 = 35.2 % ≈ the 35.3 % of cycles
that stall.

### What the stall is, named

Not the client, not a slot, not the producer. On **every** stalled
cycle the wait ends within microseconds of the main thread's `enq`:

| wait class | n | `wait_beg -> next enq` p50 | **`enq -> wait_end` p50** |
|---|---|---|---|
| < 0.01 ms | 998 | 32.606 ms (the *following* frame) | — |
| **1–3 ms** | **466** | **1.855 ms** | **+0.0105 ms** |
| **≥ 3 ms** | **79** | **30.229 ms** | **+0.0308 ms** |

The worker is waiting for the main thread to hand it a frame, and it
resumes the instant one arrives.

The 1–3 ms class is a remarkably tight quantum — min 1.698, p50 1.867,
p90 1.951 — with **nothing at all between 0.01 and 1.00 ms**. A gap that
clean is a mechanism, not jitter.

And the mechanism is visible in the enqueue itself: **`fifo_to_proc_depth`
is 1 at every one of the 1544 enqueues, on both arms** (it is read after
its increment, so 1 means the queue was empty), and the enqueue interval
tracks the send rate (27.13/s enqueued vs 27.22/s sent; x005 24.26 vs
24.33). So `capture || encode` holds only while the encoder is slower
than the capture path — true until the emit split, false for 35 % of
cycles now.

> **CORRECTED same day.** This paragraph originally continued: *"The
> capture path is demand-clocked by the pipeline and only one frame
> deep. It does not run ahead."* **Both of those last two claims are
> wrong**, and the metric behind them was paired by TIME WINDOW — "slot
> release → the next enqueue in time" — which in the 65 % of cycles
> where the frame arrives early picks up the frame AFTER next. The 2c
> gate, cited twice elsewhere in this very file.
>
> Paired by **frame identity**: the median frame is enqueued **1.874 ms
> BEFORE** the worker releases the slot (x005: **10.115 ms** before).
> Capture *does* run ahead, slightly, and is not gated on the release.
> Only "one frame deep" survives, and it is a consequence of the two
> rates having converged rather than of the encoder clocking capture.
>
> The two rows of the table above are also **two different mechanisms**,
> which the 2.249 ms mean concealed:
>
> | population | cycles | each | share of the 2.249 |
> |---|---|---|---|
> | no wait | 1000 (65 %) | ~2 µs | 0 % |
> | arrived slightly early | 466 (30 %) | ~1.87 ms | **25 %** |
> | capture genuinely fell behind | 78 (5 %) | ~33 ms | **75 %** |
>
> The 1–3 ms "tight quantum" is a phase offset between two
> nearly-equal-rate loops, and it is the *minor* term. **The 78-cycle
> tail carries three quarters of the idle time and is not root-caused**
> — not the intra-refresh cut (not periodic mod 240), not the payload
> (textflood held 59.6 fps against a 27 fps consumer). Adding capture
> depth addresses the 25 %, not the 75 %.
>
> And the sentence this file should have opened that section with:
> **the encoder worker waits for the xrdp main thread** — `g_obj_wait`
> on `event_to_proc`, `xrdp_encoder.c:3746`.

### What IS proven: ffmpeg cannot see the frame sooner

For each frame, from `enq(N)` to `subm_beg(N)`, how much of the delay was
the worker *idle*:

| arm | enq → submit | of which worker idle | recoverable |
|---|---|---|---|
| x005 | 16.125 ms | 0.0017 ms | **0.01 %** |
| x006 | 3.899 ms | 0.0088 ms | **0.22 %** |

Once a frame exists, ≥ 99.78 % of the time before ffmpeg receives it is
**serial work already in progress**, not slack. No scheduling change
recovers it; only removing work from the chain does.

Note the direction of this claim precisely: the interval *starts* at the
enqueue, so it cannot and does not speak to whether capture was hidden —
that is the `wait` row above, and it says no for x006. The two are
separate claims and only the second one is green.

## Open, and where it goes next

The stall is upstream of the encoder worker, in the
capture-and-enqueue path, and it is one frame deep by construction. The
existing item for that depth is FR-CAPTURE-8 (two-slot pipelined
capture). Until the capture path can run at least one frame ahead of a
split-emit worker, ~2.2 ms/cycle (6.1 %) of the period is the encoder
waiting on it — and any further reduction of worker-side serial time
converts into more of exactly this wait rather than into rate.

## Reproduce

```sh
cd PR-demo/mac_bisect_matrix
./build_and_deploy.sh x005 x006 x007
ML0="592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync"
for a in x005:40027 x006:40028 x007:40029; do
  ./sessions_off.sh && sleep 25
  E_ARM=${a%%:*} E_PORT=${a##*:} E_MODE=oracle E_MONITORS=1 \
    E_MODE0=3840x2400R E_SIZE=3840x2400 E_MODELINE0="$ML0" \
    E_OUT=$PWD/captures/i61e_${a%%:*}_heis_20260801 ./e_gate_run.sh 60
done
# the perf trace is not collected by the gate; pull it per arm
kubectl -n bisect-matrix cp <pod>:/var/log/xrdp-perf/<enc.PID> perf/
python3 i61e_period_attribute.py <capture>/perf/enc.*
```

Trace integrity on both arms: **0 drops**, 0 `perfnoring`. x006 carries
30 882 records across 3 threads (worker 26 250, assembler 3 088, main
1 544); x005 24 878 across 2.

## ADDENDUM 2026-08-01, same day — the 78-cycle tail ROOT-CAUSED, from these captures alone

The owner asked how the ~33 ms stalls can exist at all when the
producer is always faster and a capture on ack always finds damage.
The answer: **capture is not damage-clocked and never was. It is
ack-clocked, two frames back, through the one xrdp thread that is
also busy writing frames to the client.** Every number below is from
the ACK_TRACE / GFX_TRACE / perf traces already in this capture set
(xrdp `us=` and session-Xorg `begin_us` share CLOCK_MONOTONIC, so the
two processes align without inference).

### The delivery loop, measured (x006, medians, steady state)

| leg | time | spread |
|---|---|---|
| xrdp sends eager slot ack for N−2, at max(absorb N−2, egress N−3) | t=0 | — |
| xorgxrdp **begins capturing frame N** | +8.0 ms | IQR 2.8 |
| capture done, rect sent to xrdp | +11.8 ms | capture 3.8 ms, IQR 0.7 |
| **xrdp main thread reads the rect** (`msgin`) | **+28.0 ms** | transit p50 **16.2 ms**, IQR 4.9 |
| main thread submits to the encoder fifo | +31.9 ms | +3.9 ms |

Two facts pin the clocking. (1) `capbegin(N) − slotack_sent(N−2)` is
8.0 ms with IQR 2.8 and it **stays locked (9.8 ms) during stalls** —
no other candidate event survives that perturbation test. (2) At every
capture the xorgxrdp frontier reads `ack = N−2, shown = N−3`
(1515/1533 captures): the two-slot budget (FR-CAPTURE-8) is exactly at
cap and is re-opened once per encoded frame by the slot ack. Damage
never fires a capture on its own — the deferred pass wakes on damage
every ~15 ms, finds the budget at 2-of-2, and returns.

The 16.2 ms transit is a local unix-socket hop that should cost
microseconds. It costs 16 ms because the main thread is busy: the same
thread writes each frame's ~3.7 MB to the client socket, and those
writes are spread over **17 ms/frame (p50, header→trailer in
GFX_TRACE)** — paced by how fast the client drains the socket. The two
numbers are the same busyness seen from two sides.

### The two wait populations are one loop at two amplitudes

- **Phase offset (466 cycles, ~1.87 ms, 25 % of the wait):** the loop
  above delivers the frame ~1.9 ms after the worker goes idle. That is
  its steady-state phase, dominated by the 16 ms main-thread service
  latency.
- **Tail (78 cycles, ~33 ms, 75 %):** when the client briefly stops
  draining the socket (50–150 ms), egress goes late, and the main
  thread's xup service latency spikes (transit p90 at stalls:
  **56.3 ms**). One late egress delays slotack(N), which delays
  capture(N+2) — so the stall **echoes at exactly two-frame spacing**:
  of 156 consecutive-stall spacings, the modal value is 2 (63×).
  x005 (split OFF) has the same loop and the same hiccups (19 missed
  beats) but a 10.1 ms margin instead of 1.9 ms, so all but the largest
  are absorbed.

### Everything else is exonerated, with the check that rules it out

| suspect | verdict | evidence |
|---|---|---|
| the tracer / new build | **not it** | frame-send holes >50 ms: x006 (traced) 91, x007 (untraced twin) 93, x004 (old untraced build) 82 |
| the fif=2 GFX ack window | **not it** | at stall resume `id_server − id_client` = 0 in 73/91 (x006), 65/82 (x004): the window sat OPEN while the server was quiet |
| the client ack path as *gate* | **not it** (it is the *forcing*) | client acks resume in bursts before the server does; but its ack stream has 100–160 ms holes coinciding with 33/34 stalls — the hiccup is real, it just acts through egress, not the window |
| the producer / session Xorg | **not it** | 16 273 producer frames, zero intervals >30 ms; XSync p99 5.1 ms, max 10.4 ms — damage was pending through every stall |
| intra-refresh cut | **not it** | stalls not periodic mod 240 (established earlier) |

### What this changes

- FR-CAPTURE-8's two slots buy no lookahead at m=1: the budget is
  always exactly full and is re-opened once per encoded frame. Capture
  depth (a third slot, per-monitor — PRD forbids a global pool) is one
  lever, but it addresses the 1.9 ms margin only.
- The larger, newly visible lever is the **main thread's 16 ms service
  latency**: the captured frame exists 16 ms before the thread that
  must enqueue it gets around to reading it. Serving the xup fd
  between frame-part writes (or moving egress off the main thread)
  attacks BOTH populations at once.
- The tail's amplitude is client-dependent. This client is the oracle
  harness; its dump lands in tmpfs (RAM — verified, `/tmp` is tmpfs),
  so "disk writes" do NOT explain its 50–150 ms pauses. The pause
  source inside/around the client (host CPU contention vs something
  internal) is unattributed. The 1.9 ms server-side margin is ours
  regardless of client.

Analysis instrument: `PR-demo/mac_bisect_matrix/i61f_delivery_chain.py
<capture-dir>` reproduces sections 1–4 above from this directory's
logs; the perf-trace side remains `i61e_period_attribute.py`.
