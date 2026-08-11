# #90 — the eight-millisecond overlap prize was stale

2026-08-10. The owner asked whether #90 was still necessary after #103
revealed that every old fleet run used a two-page encoder-input pipe. Three
arms were approved for re-characterization with the existing perf ring:
legacy acknowledgement, the credit frontier at window 1, and the frontier at
window 2.

## What was filed

#90 proposed overlapping the ffmpeg pump for one frame with collection of the
previous frame. Its size came from #61e's 8.802 ms `collect` measurement. The
open candidates were:

* reorder the depth on the existing worker, submitting frame N+1 before
  collecting N;
* split submit and collect into stage threads with a bounded one-frame SPSC
  queue, the owner's proposed shape, which required a dated amendment to the
  PRD's "exactly one worker thread" paragraph; or
* use resumable collect coroutines on one thread, already rejected as heavier
  than a thread for the same work.

The item explicitly prohibited implementation until the architecture was
decided. The re-characterization below retires the premise before any of those
shapes is built.

## Answer

No: withdraw #90 in its filed form. The 8.802 ms number was not caused by the
two-page pipe, but it was no longer a current prize either. It measured
`collect` before #75 made the LTR rewrite 10.7 times faster. The current
full-AVC444 arms measure `collect` at 1.24–1.28 ms, 6.5–7.2% of the worker
cycle. Fully hiding that stage is the hard upper bound for #90's proposed
submit/collect split. Stage threads violate the PRD's specified single-worker
architecture, and a 1.24–1.28 ms ceiling is insufficient ROI to justify a
dated amendment to that requirement.

The pipe finding remains real and separate. A clamped pipe made raw-frame
feed cost 7.5–9.1 ms; after the host pipe budget was raised, feed costs about
2.0 ms. Feed is inside the ffmpeg pump. `collect` begins after `pump_end`, so
its bracket cannot contain the raw-frame pipe handoff.

There is a causal cross-check stronger than the bracket names: #75 reduced
deployed `collect` from 8.802 to 1.362 ms while every fleet pipe was still
clamped. #103 later changed feed, not encode, by raising the host pipe budget.

## Conditions

All three successful legs used image
`3ca17beaa84d.xx10fa3aa-tf.p2fde5531`, xorgxrdp `10fa3aa23033`, the oracle
save-only client, one monitor at 3840x2400, full AVC444, chroma every frame,
and `SESSION_KIND=textflood_strip`. The exact monitor invocation was copied
from the established `i92_sparse_aux_ab.sh` and `i87_eager_ab.sh` wrappers:
mode `3840x2400R` with modeline
`592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync`.

The three arms differed only as intended: legacy acknowledgement; eager
frontier with `wire_window = 1`; eager frontier with `wire_window = 2`.
Every arm passed the 64 KiB pipe guard, armed both encoder children on every
cycle, and had zero perf-ring drops. Producer margins were 4.42x, 4.26x and
3.86x, all above FR-BENCH-1's 2.0x floor.

CPU/GPU pin state was not recorded during these three legs. Consequently the
small period differences between arms are not used as a precise throughput
ratio. The decision rests on the stage identity, the 1.24–1.28 ms collect
range reproduced three times, and the earlier causal #75/#103 interventions.

Evidence:

* `PR-demo/mac_bisect_matrix/captures/i90_reground_x031_20260810_s20_a2/`
* `PR-demo/mac_bisect_matrix/captures/i90_reground_x032_20260810_s20/`
* `PR-demo/mac_bisect_matrix/captures/i90_reground_x033_20260810_s20/`

## Current decomposition

The four broad segments below are constructed to sum to the pump-to-pump
cycle. `between` contains collect, reference rewrite, EGFX assembly, slot
release and any wait for another captured frame.

| flow control | feed raw pictures to children | children encode | drain encoded output | between frames | cycle | closure residual |
|---|---:|---:|---:|---:|---:|---:|
| legacy acknowledgement | 1.960 ms | 14.000 ms | 0.369 ms | 2.958 ms | 19.289 ms | 0.0008 ms |
| frontier, window 1 | 2.038 ms | 13.989 ms | 0.380 ms | 2.417 ms | 18.825 ms | 0.0007 ms |
| frontier, window 2 | 1.998 ms | 13.936 ms | 0.404 ms | 1.657 ms | 17.995 ms | 0.0009 ms |

The load-bearing stage inside `between` is invariant:

| flow control | collect mean | p50 | p90 | share of steady worker cycle |
|---|---:|---:|---:|---:|
| legacy acknowledgement | 1.241 ms | 1.207 ms | 1.499 ms | 6.5% |
| frontier, window 1 | 1.249 ms | 1.226 ms | 1.490 ms | 6.7% |
| frontier, window 2 | 1.281 ms | 1.248 ms | 1.544 ms | 7.2% |

The flow-control intervention changed the mechanism it targets, not
`collect`. The steady worker's waits longer than 1 ms were 148/881 under
legacy acknowledgement, 91/902 at window 1, and 10/943 at window 2. The wait
distribution is bimodal, so its mean is not a sufficient summary. Feed,
encode, drain and collect stayed stable while those stalls disappeared.

## Quality gates

1. The four-segment residual is below 0.001 ms on every arm. Independently,
   the worker cycles of 19.138, 18.694 and 17.860 ms reproduce the server's
   send intervals of 19.1, 18.7 and 17.9 ms.
2. The configurations read back from each pod. Both children were armed on
   100% of cycles, the pipe guard passed, and the intended flow-control
   change appears in the stall counts while unrelated stages stay flat.
3. The PRD says the encoder has exactly one worker thread and requires a
   future threading proposal to demonstrate work a set-pump cannot reach.
   A 1.28 ms ceiling does not supply that evidence.
4. This is not a regression against #75 or #103. It reproduces #75's 1.362
   ms deployed collect and #103's roughly 2 ms unclamped feed.
5. Image, payload, client, geometry and codec are identical across the three
   legs. Only the declared flow-control configuration differs.

The generic `E5 GATE` speedup line in the window-1 and window-2 VERDICT files
is not this experiment's acceptance test. It compares each equal-build arm
to the legacy period and asks for at least a 1.5x speedup; no such claim was
made. Saturation, stage closure and the mechanism fields are the applicable
checks.

## Failed setup attempt

The first x031 invocation supplied the 3840x2400 mode name without its
matching modeline. The geometry guard stopped before login, so it produced no
measurement. It is retained at
`captures/i90_reground_x031_20260810_s20/README.md` rather than being allowed
to look like a missing arm.
