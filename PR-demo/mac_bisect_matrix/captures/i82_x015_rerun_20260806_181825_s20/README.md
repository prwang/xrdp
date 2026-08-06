# i82_x015_rerun_20260806_181825_s20 — the 26.7 ms encoder wait does not reproduce; RETIRE #82

BACKLOG #82, owner-approved 2026-08-06: reproduce or retire the single
unexplained 26.7 ms encoder wait measured once on arm x015 on 2026-08-02
(`docs/experiments/78-pump-split-fif1-tail-is-the-ack-gated-slot-release.md`,
which named it "x015's 26.73 ms pump" and left it as the one number that
run could not explain).

Every figure below was re-derived from the archived rings for this
README. Two of the figures first reported to the owner did not survive
that re-derivation; they are called out where they occur and again at the
end.

## Verdict, first

**The encoder wait does not depend on frames-in-flight, and the 26.7 ms
does not reproduce.** Interleaved in one sitting, on the two arms that
differ only in that variable, the wait for the ffmpeg children to finish
encoding a frame is **16.160 and 16.199 ms at frames-in-flight 2**
against **16.212 and 16.305 ms at frames-in-flight 1** — a spread of
0.15 ms across four legs, with the two arms interleaved inside it. This
is the third independent null, after the controlled re-run in the #78
record measured 16.40 against 16.44 ms.

**Recommend RETIRE.** The condition that produced 26.7 ms is real and was
seen again today — but on *both* arms of an unrelated A/B at once, which
makes it a state of the host, not a property of frames-in-flight. See
"the cross-reference that settles it" below.

The frame period *does* differ between the arms, and every millisecond of
that difference is the encoder worker sitting idle waiting for permission
to capture — a defect already root-caused as BACKLOG #79 and fixed by the
credit frontier in BACKLOG #80. Both arms here predate that fix.

## The arms, stated before any number

Both arms run the **same image and the same `gfx.toml` body byte for
byte**; the only difference is one environment variable in the k8s
manifest.

| | x014 | x015 |
|---|---|---|
| host port | 40031 | 40032 |
| pod | `xrdp-x014-84dbb77fc6-wggdt` | `xrdp-x015-64679c686-2qdtw` |
| image | `localhost/xrdp-bisect:73e4cb76d483.xx10fa3aa-tf` | same |
| server build | xrdp-dev `0.10.80+git20260801232618.73e4cb76d483` | same |
| producer build | xorgxrdp-dev `1:0.10.80+git20260731212221.10fa3aa23033` | same |
| **the one difference** | frames-in-flight 2 — the binary's compiled-in default (`DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT`, `xrdp/xrdp_encoder.c:51`); `k8s/x014.yaml` sets no such variable | frames-in-flight 1 — `XRDP_GFX_FRAMES_IN_FLIGHT = "1"` in `k8s/x015.yaml:62` |
| certificate | 7/7 wire checks clean, 0 black frames, 2026-08-01T23:29:31Z | same checks, 2026-08-02T02:06:19Z |

Both: one monitor at 3840x2400 (9.22 Mpx), `textflood` payload, the
oracle client on this host (it saves the received bytes and acknowledges
each frame *before* decoding it, so it measures the server's ceiling).
Four legs of 20 s, interleaved c1 (x014), f1 (x015), c2 (x014), f2 (x015).

**These are pre-credit-frontier builds, but they are not the legacy
path.** Their `gfx.toml` carries `eager_slot_ack = true`, and in build
`73e4cb76` that line means BACKLOG #70's earlier mechanism — release the
capture slot when the children have absorbed frame N *and* frame N−1 has
reached the transport — **still wrapped in the client's acknowledgement
window** `frame_id_client + frames_in_flight > frame_id_server`
(`git show 73e4cb76:xrdp/xrdp_mm.c`, `xrdp_mm_update_module_frame_ack`).
That outer gate is precisely the cross-layer stall #79 measured, and it
is why `XRDP_GFX_FRAMES_IN_FLIGHT` still controls how often the credit is
withheld on these arms. The same config line in the newer arms
(x018/x020/x021) selects a different function entirely; do not read the
two as the same condition.

## Mechanism check, before any rate

Every network-write record carries the `frames_in_flight` the encoder
actually holds. It reads **2 on all 3792 and 3736 writes of the x014
legs** and **1 on all 3192 and 3020 writes of the x015 legs**. The
variable reached the encoder; the arms are what they claim to be.

## The result: the encoder wait is equal

The `pump` bracket is one call that hands both views to the two ffmpeg
children and blocks until both return (`xrdp/xrdp_encoder.c:2693-2696`).
It is the "encoder wait" #82 is about.

| leg (arm) | frames | mean | p50 | p90 | p99 | max |
|---|---|---|---|---|---|---|
| c1 (x014, fif 2) | 948 | **16.160 ms** | 15.992 | 16.997 | 18.436 | 119.561 |
| f1 (x015, fif 1) | 799 | **16.212 ms** | 16.018 | 17.043 | 18.986 | 121.132 |
| c2 (x014, fif 2) | 934 | **16.199 ms** | 15.978 | 17.019 | 18.891 | 120.915 |
| f2 (x015, fif 1) | 755 | **16.305 ms** | 16.092 | 17.163 | 18.912 | 121.870 |

All four legs sit in a 0.145 ms band. Both frames-in-flight-2 legs are at
its bottom and both frames-in-flight-1 legs at its top, so there is an
ordering — of 0.05 to 0.11 ms. That is two orders of magnitude below the
~10.5 ms gap the filed observation implied (26.73 against ~16.2), and its
sign is not stable between sittings: the controlled re-run recorded in
the #78 document measured frames-in-flight 1 at **16.40 ms** against
frames-in-flight 2's **16.44 ms**, i.e. the other way round. There is no
26.7 ms here and no effect worth a name.

**About the ~120 ms maximum, which was first reported as "a rare,
consistent outlier".** It is consistent, but it is not rare and it is not
mysterious: it is **exactly one bracket per leg, and it is always the
first encode of the session**, occurring 11–12 ms after that leg's first
capture arrives. Dropping the first second of each leg removes it
entirely and takes the maxima to 22.2 / 20.5 / 24.1 / 19.6 ms. It is the
cold start of the ffmpeg children, paid once per session because every
leg logs in fresh. Worth recording; not worth investigating as an
anomaly.

## The frame period does differ, and where the difference lives

| leg (arm) | frames | mean period | p50 | p90 | p99 |
|---|---|---|---|---|---|
| c1 (x014, fif 2) | 948 | 17.909 ms | 17.253 | 19.277 | 45.297 |
| f1 (x015, fif 1) | 798 | 21.157 ms | 17.660 | 39.805 | 52.867 |
| c2 (x014, fif 2) | 934 | 18.153 ms | 17.337 | 19.470 | 47.482 |
| f2 (x015, fif 1) | 755 | 22.446 ms | 17.794 | 43.938 | 52.905 |

The difference is 3.248 ms (f1 − c1) and 4.293 ms (f2 − c2). Note the
p50s are nearly identical across all four legs: the arms differ in the
*tail*, not in the typical frame.

That difference is the encoder worker's **idle bracket** — the interval
in which the worker has finished frame N and frame N+1 does not exist
yet, described in the source as "the exact per-frame amount by which
capture is NOT hidden behind encode" (`xrdp/xrdp_encoder.c:3765-3782`):

| leg (arm) | brackets | mean | p50 | p90 | p99 | max |
|---|---|---|---|---|---|---|
| c1 (x014, fif 2) | 948 | 0.582 ms | 0.001 | 0.002 | 28.794 | 50.710 |
| f1 (x015, fif 1) | 799 | 3.758 ms | 0.002 | 22.054 | 35.403 | 40.364 |
| c2 (x014, fif 2) | 934 | 0.790 ms | 0.002 | 0.002 | 28.750 | 56.471 |
| f2 (x015, fif 1) | 755 | 4.981 ms | 0.001 | 26.722 | 34.537 | 60.101 |

Idle difference: 3.176 ms (f1 − c1) and 4.191 ms (f2 − c2), against
period differences of 3.248 and 4.293 ms — **97.8 % and 97.6 % of the
period gap**. The claim "every millisecond of the difference is in the
idle bracket" holds.

**Two shape warnings, because these means describe two populations, not
one.** On x014 the worker is idle for a microsecond on a typical frame
(p50 0.001–0.002 ms) and for tens of milliseconds on a few per cent of
them; the mean is a mixture and should never be quoted alone. On x015 the
same is true but the minority is much larger — its p90 of 22–27 ms says
roughly a quarter to a third of frames wait tens of milliseconds.

**Why the worker is idle: it is waiting for permission to capture.** The
metric is the interval between the ffmpeg children absorbing a frame's
pixels — at which point the capture pages the producer lent are free —
and xrdp actually telling the producer it may capture again. Paired by
frame identity, never by time window: for each capture k, the first
credit naming an id at least k−2 minus the moment the children absorbed
frame k−2's input, kept only when that credit landed before capture k
arrived. The last column counts frames whose wait exceeded 10 ms, the
threshold #78 and #79 both quoted.

| leg (arm) | frames | p50 | p90 | p99 | frames waiting > 10 ms |
|---|---|---|---|---|---|
| c1 (x014, fif 2) | 946 | 0.015 ms | 0.042 | 37.971 | 28 (3.0 %) |
| f1 (x015, fif 1) | 797 | 0.020 ms | **32.914** | 43.282 | 203 (25.5 %) |
| c2 (x014, fif 2) | 932 | 0.016 ms | 0.042 | 36.803 | 37 (4.0 %) |
| f2 (x015, fif 1) | 753 | 0.031 ms | **36.296** | 44.313 | 248 (32.9 %) |

Same shape warning: p50 near zero, p90 either near zero (x014) or in the
thirties (x015). The mechanism is not "x015 is uniformly slower"; it is
"on x015 the client's acknowledgement window closes on a quarter to a
third of frames, and on those frames the producer is told to wait". That
is BACKLOG #79's defect, and `XRDP_GFX_FRAMES_IN_FLIGHT = 1` simply makes
the window that gates it half as wide. It is fixed on the current build
by the credit frontier (#80), which these two arms predate.

## The cross-reference that settles the retirement

The strongest evidence is not in this capture. It is in
`i87_eager_ab_20260806_180910_s20`, run eight minutes earlier on this
same host with different arms and a newer build: there, the encoder wait
was 16.258 and 16.457 ms in the first pair of legs and **26.223 and
26.188 ms in the second pair — on both arms of that A/B simultaneously**,
as a step change in an 11 s gap between legs, with every other stage
unchanged.

So across the eight legs of the two captures, on four different pods:

| capture, leg | arm | wall clock | encoder wait |
|---|---|---|---|
| i87, a1 | x020 | 18:09:18 | 16.258 ms |
| i87, b1 | x021 | 18:09:49 | 16.457 ms |
| i87, a2 | x020 | 18:10:20 | **26.223 ms** |
| i87, b2 | x021 | 18:10:51 | **26.188 ms** |
| i82, c1 | x014 | 18:18:34 | 16.160 ms |
| i82, f1 | x015 | 18:19:05 | 16.212 ms |
| i82, c2 | x014 | 18:19:36 | 16.199 ms |
| i82, f2 | x015 | 18:20:07 | 16.305 ms |

A bracket that is otherwise steady at 16.16–16.46 ms across six legs on
four pods went to 26.2 ms on two arms at once for about a minute and then
went back. The condition that produced x015's 26.7 ms on 2026-08-02
therefore **exists on this host and is not a frames-in-flight property**.
The #78 record's own note anticipated this — it named the likely
transient as the APU's sustained-power budget and asked for
`amdgpu_pm_info` sampled at 1 Hz on metal during a run.

## What would reopen #82

* The 26 ms wait appearing on a frames-in-flight-1 arm while a
  frames-in-flight-2 arm measured in the same sitting stays at 16 ms.
  Interleaved legs, as here — a single arm measured alone cannot
  distinguish it from the host state.
* Or the wait rising with frames-in-flight held constant while GPU clocks
  and package power are being sampled during the legs, so the host state
  is observed rather than inferred. Neither this capture nor the A/B
  cross-referenced above sampled the GPU during a leg.

Absent either, the number belongs in the host-state bucket, which is
where BACKLOG #88 already holds this host's other unattributed pauses.

## Producer margin, and what it voids

The gate's per-leg ratio of the pipeline's mean frame interval to the
payload's own mean frame interval: **1.08x (c1), 1.32x (f1), 1.08x (c2),
1.39x (f2)** — all below the 2.0x floor. Under the owner's 2026-08-06
ruling, **claims from this run about two pipeline stages overlapping are
void**; throughput and regression comparisons between these two arms,
which share payload, geometry and client, remain valid and must be quoted
with the margin beside them.

Note the x015 legs' margins are higher only because the x015 pipeline is
slower, not because its producer is faster: the payload's own interval
was 16.6/16.7 ms on the x014 legs and 16.1/16.1 ms on the x015 legs.

## Process note: the first attempt at this run was invalid and was deleted

The first attempt, started 18:13:30, was run with **the two arms' host
ports swapped**: each leg dialled one arm while the gate collected the
other arm's logs, ring and certificate. It was discarded rather than
superseded, per the standing rule that a result whose instrument was
pointed at the wrong thing teaches nothing.

The swap is still provable from this capture, because each leg directory
carries the target pod's whole ring collection, including rings left by
earlier sessions. The `leg_c*` directories hold one family of process ids
(19009, 19550, 19853, 20094, 20396) and the `leg_f*` directories another
(261, 658, 956, 1369, 1671), so which pod a ring came from is
unambiguous. Session start times, taken from each ring's
`# perfbase real_ns` header:

| session start (UTC) | pod | which attempt |
|---|---|---|
| 18:13:39 | x015 | invalid attempt, leg 1 — but leg 1 is `c1`, which is supposed to be **x014** |
| 18:14:10 | x014 | invalid attempt, leg 2 — labelled `f1`, supposed to be x015 |
| 18:14:41 | x015 | invalid attempt, leg 3 — labelled `c2` |
| 18:15:12 | x014 | invalid attempt, leg 4 — labelled `f2` |
| 18:18:34 | x014 | this run, `c1` — correct |
| 18:19:05 | x015 | this run, `f1` — correct |
| 18:19:36 | x014 | this run, `c2` — correct |
| 18:20:07 | x015 | this run, `f2` — correct |

The invalid attempt's leg order is x015, x014, x015, x014 where the
runner's order is x014, x015, x014, x015 — the arms are exactly
transposed. The valid re-run is in the intended order.

**What caught it is not recoverable, and the guard credited with it could
not have.** The deleted capture's own output is gone, so this README
cannot say which check fired. Reading `e_gate_run.sh:610-632`, the span
guard compares the first and last rendered trace record and fails when
their separation exceeds `2 × seconds + 30`; when the rendered trace has
fewer than two records the shell variable it computes is empty and the
guard is skipped entirely, printing `trace span: ?s` and failing nothing.
A run that collected the wrong pod's ring would render **no** records
inside its window, not too many — so on the code as written that specific
guard would have passed it. Something caught the run; the most likely
candidate in the same file is `e_gate_run.sh:569`, which fails when there
is no session Xorg log for the run on the pod it is collecting from.
**Either way there is a real hole here worth filing: the span guard does
not fire on an empty trace, which is the failure mode a swapped target
produces.**

## Reproducing the numbers

Per leg, use the ring in `leg_*/perf/` whose `# perfbase real_ns` header
is **latest** — the leg directories deliberately contain the residue
rings listed above, and pooling them would double-count. Window each leg
to the `t0`/`t1` in its `window.txt`. No warm-up is dropped in the tables
above, matching how the run was first read; dropping the first second
after each leg's first capture lowers the encoder-wait means by
0.13–0.16 ms and removes the one cold-start outlier per leg.

Two fields cannot be read on this build and should not be attempted: the
`egress` record's third to sixth fields are hard zero here
(`git show 73e4cb76:xrdp/xrdp_mm.c` at the `egress` call site), so
neither the transport's queued-byte count nor the client's acknowledged
frame id — both of which the newer arms report — exists in these rings.
Likewise the acknowledgement records' fifth field is a literal 0 on this
build and is *not* a credit window of zero; the credit window did not
exist yet.

## Where this README differs from the first reading of the run

* **The idle-bracket means were inflated by one session-startup
  bracket.** The figures first reported — 3.36 / 3.62 ms on x014 against
  7.06 / 8.47 ms on x015 — each include a single bracket of ~2.64 s that
  *begins before the leg's first capture arrives*: the encoder worker
  sitting idle between session start and the first frame of the payload.
  It is present identically on all four legs (2640.2, 2643.3, 2642.6,
  2640.6 ms) and it is roughly four fifths of each x014 mean (78 % and
  83 %). Excluding it gives 0.582 /
  0.790 against 3.758 / 4.981 ms, the figures used above. This matters
  beyond tidiness: the corrected differences account for 97.8 % and
  97.6 % of the frame-period gap, where the inflated ones overshot it
  (114 % and 113 %) — so the corrected numbers support the conclusion
  better than the originals did.
* **The ~120 ms encoder-wait maximum is not a rare recurring outlier.**
  It is exactly one bracket per leg and it is always the session's first
  encode, 11–12 ms after the first capture. Described above.

Everything else re-derived to the values first reported: encoder wait
16.160 / 16.199 (x014) against 16.212 / 16.305 (x015); frame period
17.909 / 18.153 against 21.157 / 22.446; encoder-wait p99 18.4–19.0 ms on
every leg.
