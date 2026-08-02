<!--
Experiment record. BACKLOG.md is the OPEN work list; this file is the
record it points at. Kept verbatim, wrong claims included.
-->

# #76 — fif = 2 was hiding a bug: what fif = 1 exposed

2026-08-02. Arm **x015**, x014's image and `gfx.toml` body exactly, one
environment variable changed. Capture:
`PR-demo/mac_bisect_matrix/captures/i76_x015_fif1_20260802`.

## Lead with what fails

**fif = 2 is hiding a bug, and finding it is now the top open item
(owner directive, 2026-08-02; PRD FR-ACK-3).**

1. fif = 1 cost **34 % of throughput** (54.1 → 35.5 fps) and made
   end-to-end latency **worse**, not better: capture → client ack went
   **49.2 → 61.5 ms**.
2. 100 % of that regression is inside `pump` — feed the NV12 in, wait for
   the children, drain the coded bytes out — which went **16.6 →
   26.7 ms**. A *client* ack window cannot reach it. `frames_in_flight`
   is read in exactly two places (`xrdp_mm.c:1691` gating
   `mod_frame_ack`, and the trace line at `:4234`); neither is on the
   worker's encode path.
3. **It cannot be flow control, and that is what makes it a bug rather
   than a trade-off.** The encoder's depth is provably one frame
   (`pump_pairs` waits for the set it just submitted; `collect_pair`
   verifies `desktop_sequence`), and the worker is never starved — its
   `wait` bracket is **0.002 ms/cycle** at fif = 1. There is no queue for
   the second credit to fill and no idle worker for it to feed. It is
   covering a stall.

**The first framing of this record was wrong and is kept here rather than
tidied away.** It read the 34 % as an unexplained curiosity to be
instrumented later, and filed a faster benchmark producer ahead of it. The
owner's correction is that the causality runs the other way: a pipeline
that needs two frames in flight to reach its rate has a defect, and the
second credit is what has been concealing it. The design requirement that
follows — *all the concurrency we need, at the cost of fif = 1* — is now
PRD **FR-ACK-3**.

## The naming problem this record has to fix first

`pump` is three things and the trace cannot tell them apart. Every
sentence below uses these instead:

| term | what the machine is doing |
|---|---|
| **FEED(N)** | `vmsplice` frame N's two 13.824 MB NV12 views into the two child pipes (1 MiB each, so ≥ 28 poll+splice round trips), from `feed_vmsplice()` inside the poll loop |
| **ENCODE(N)** | the two ffmpeg children actually producing two coded pictures |
| **DRAIN(N)** | reading the coded bytes back, NUT-parsing them into whole packets (`drain_stdout`) |
| **REWRITE(N)** | popping both packets and rewriting both views' LTR references — the `coll` bracket |
| `pump` | **FEED + ENCODE + DRAIN**, indivisibly. This is the defect. |

`submit` does **no I/O**: `xrdp_ffmpeg_avc444_submit_pair`
(`xrdp_encoder_ffmpeg.c:1434`) only calls `in_iov_push`, which stores a
pointer. The one caller of `feed_vmsplice` is `pump_service` ←
`pump_set` ← `xrdp_ffmpeg_avc444_pump_pairs`. **FEED happens inside
`pump` and nowhere else.**

## The dependency chain, by frame number

The encoder worker (`proc_enc_msg`) runs one iteration per frame, and
every stage in it is strictly serial on that one thread:

```
  ... FEED(N) ENCODE(N) DRAIN(N) | REWRITE(N) | FEED(N+1) ENCODE(N+1) ...
      \________ pump(N) ________/  \_coll(N)_/  \______ pump(N+1) _____/
```

so, naming the blocking edges explicitly:

* **REWRITE(N) blocks FEED(N+1).** Same thread, no queue between them.
  While the worker rewrites frame N's two slice headers, not one byte of
  frame N+1 exists in either child's pipe. Measured 1.363 ms (x014) /
  1.498 ms (x015) per frame of guaranteed encoder idle.
* **FEED(N+1) blocks ENCODE(N+1).** ffmpeg's rawvideo reader cannot start
  a picture until the whole picture is readable, and the picture arrives
  in ≤ 1 MiB instalments driven by the same loop that is waiting for the
  output. Measured 2.1–2.5 ms (probe, below).
* **DRAIN(N) blocks REWRITE(N)** and therefore also FEED(N+1) — it is the
  tail of the same `pump` call.
* **ENCODE(N) does not overlap ENCODE(N+1) at all.** The completion
  predicate in `pump_pairs` is "every handle has a picture", and
  `collect_pair` verifies `desktop_sequence`, so the packet popped in
  cycle N is provably frame N's. Depth is exactly one frame.

What **is** parallel, and is worth keeping:

* **main and aux encode concurrently** — one poll set over both children
  (`pump_set`, `#45` step 5). Measured worth: 9.7 ms concurrent vs
  19.0 ms strictly serial, **2.0×**.
* **The assembler builds and queues frame N−1's PDUs during pump(N)**
  (`#70B`). Overlapped **98.1 %** (x014) / **99.9 %** (x015); the
  worker's `join` for it costs 0.000–0.001 ms. It is 0.34 ms of work, so
  the split buys little in absolute terms, but it costs nothing.
* **The xrdp main thread egresses frame N−1** while the worker is in
  pump(N), and **xorgxrdp captures N+1** into the second slot.

So the period is, and can only be:

```
  period = FEED + ENCODE + DRAIN + REWRITE + (worker idle)
```

with everything else hidden. Measured, per cycle, closing to 0.005 ms:

| | x014 fif=2 | x015 fif=1 |
|---|---|---|
| `pump` = FEED + ENCODE + DRAIN | 16.616 | **26.728** |
| `coll` = REWRITE both views | 1.363 | 1.498 |
| `wait` = worker had nothing to encode | 0.515 | **0.002** |
| everything else (submit, book, rel, drain/take) | 0.014 | 0.017 |
| **period** | **18.508** | **28.245** |
| residual | 0.005 | 0.004 |

Under fif = 1 the worker **never** waits (0.002 ms/cycle): the producer
is always ahead of it. Frames instead queue on the fifo for **17.9 ms**
before the worker lifts them (7.3 ms on x014).

## The probe: what FEED and ENCODE cost when nothing else is running

Run **inside the arm's own pod**, with that image's ffmpeg, that host's
VAAPI device and that arm's `encoder_args` verbatim — no session, no
client, no rewrite, no wire. It mirrors `pump_pairs`/`pump_set` path for
path: two children, one poll set, one deadline, non-blocking `vmsplice`
from borrowed pages, stdout drained every round. 3840×2400, 50 frames
after 10 warm-up.

| shape | FEED | ENCODE | total |
|---|---|---|---|
| **A** two children, one poll set (= HEAD) | 2.1–2.5 | 7.2–8.1 | **9.3–10.6** |
| **C** two children, strictly serial (`aux_intra_leaf` shape) | — | — | **19.0** |

Run-to-run spread on A across four runs was 9.3–10.6 ms, so differences
below ~1.3 ms in the probe are not readable.

**A is what makes the deployed number a problem.** The same two children
at the same geometry with the same arguments finish in ~9.7 ms. The
deployed bracket is 16.6 ms on x014 and 26.7 ms on x015, so **6.9 ms and
17.0 ms are unattributed** — and the unattributed part is the part that
moved when the ack window changed.

## Not a result

**One child fed BOTH pictures — the `dev/avc444_metablock_checkpoint`
default shape (`aux_intra_leaf = 0`) — measured 147 ms**, tightly
distributed (142–150), and neither `-async_depth 4` nor
`hwupload=extra_hw_frames=8` moved it. 15× is not a serialisation factor
and there is no mechanism for it, so it is assumed to be a defect in the
probe's driver, not a property of that branch. **It must not be quoted as
a metablock number.**

## What was ruled out for the `pump` regression

Checked before writing any of the above:

* **Content difficulty.** Coded bytes per view: 1 734 414 → 1 747 572,
  **+0.76 %**. A slower pipeline scrolls further between frames, so this
  was the first suspect; it is not it.
* **A throttled producer.** textflood's own interval: 15.94 → 16.26 ms,
  render 13.37 → 13.48 ms. Unchanged. FR-BENCH-1 margin *improved* to
  1.74× (the pipeline got slower, not the payload faster).
* **Host contention from the dump.** x015 wrote **less**: 7.04 GB vs
  10.67 GB of coded bytes over the same 57 s.
* **Mislabelled brackets.** Per-cycle closure residual 0.004–0.005 ms.
* **Encoder errors.** E2's five counters all 0; 0 rewrite failures, 0
  unsupported, 0 pair aborts across 2015 frames × 2 views.
* **The knob not applying.** All 8056 `send` records read `fif=1`;
  `id_server − id_client` was 0 on 8052 of them (4 at 1). On x014 all
  12 300 read `fif=2`, with lag ≤ 1 on 97.0 %.

### Open hypotheses, in the order to try them

All of them concern what changes inside `pump` when the frame the worker
feeds was captured **17.9 ms ago instead of 7.3 ms ago** — the measured
fifo residency, and the one thing that demonstrably differs about the
data the children are handed.

1. **The capture pages are colder.** The children read 27.6 MB/frame of
   borrowed capture shmem *by page reference* (`vmsplice`, FR-PROC-6);
   the copy happens when the child reads. At fif = 1 those pages were
   written 2.5× longer ago and have had 2.5× more traffic over them.
   **Predicts FEED grows and ENCODE does not** — which BACKLOG #78's
   instrument settles in one run.
2. **`pump_set`'s poll loop.** Its timeout becomes a flat 10 ms once no
   child has anything left to write (`timeout = want_write_any ?
   deadline - now_ms() : 10`). Read that against a cadence change before
   blaming hardware.
3. **CPU/GPU clock behaviour under a slower duty cycle** on this
   shared-memory APU (Radeon 8060S encodes out of system RAM). Last: the
   least actionable and the easiest to reach for.

Hypothesis 1 predicts a different split than 2 and 3 do, so **the
instrument comes before the next arm.**

## The latency picture, which is solid

Paired by frame id throughout — never by time window (gate 2c).

| | x014 fif=2 | x015 fif=1 |
|---|---|---|
| capture msg in → on the encoder fifo | 0.001 | 0.001 |
| **fifo residency** (captured, waiting for the worker) | 7.327 | **17.909** |
| worker takes it → input absorbed by the children | 17.989 | 28.239 |
| absorbed → last PDU with the transport | 9.462 | 9.438 |
| **capture → egress** | **34.778** | **55.586** |
| egress → client ack | 14.410 | **5.961** |
| **capture → client ack** | **49.161** | **61.547** |
| absorbed → slot ack to xorgxrdp | 0.022 | 0.037 |

**fif = 1 did remove the queue it was aimed at** — the client's ack comes
8.4 ms sooner, and the server never holds more than one unacked frame.
**It just cost 20.8 ms elsewhere to save 8.4 ms here.** That 20.8 ms is
the bug: it is the pipeline's own cycle getting longer for a reason the
ack window cannot explain, and it is what the second credit was paying
for. Note that the payload is *not* in the way of investigating it — at
fif = 1 the FR-BENCH-1 margin is **1.74×**, not x014's 1.09×, because the
pipeline slowed and the producer did not.

## Correctness

Certificate `certs/x015.cert`, 3 s at 1920×1080 at deploy: **ASSERT
VERDICT PASS, 7/7**, 0 black frames, one contiguous frame_num chain.
Over the 60 s run: 0 rewrite failures, 0 unsupported, 0 pair aborts, 0
budget assertions across 4030 rewritten packets.

## Also not a result

`E5 GATE: baseline 51.1 ms → 1.81x` in VERDICT.txt is void for the fourth
time and for the same reason: 51.1 ms is the harness default measured
under `SESSION_KIND=code` at another geometry. The comparison that means
something is x014's 18.5 ms. `E5_BASE_MS` still prints its default
without refusing — an open harness gap.
