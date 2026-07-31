# #65 step 0 — the producer is EXONERATED; the serializer is ack-paced capture

30 s oracle run, T4, m=1 3840×2160 (8.29 Mpx confirmed by the geometry
guard), instrumented textflood (`--stamps`, default-on, epoch-anchored).
Same pipeline regime as the previous run: 122.3 ms/send, 8.21 sends/s.

## The producer was never slow

`textflood_stamps.tsv`, 2622 frames over 94.8 s:

```
producer rate        27.66 fps   (pipeline: 8.21 sends/s -> 3.4x faster)
render      p50 26.2 ms  (ring_recon offline: 24.1 -- agrees)
blit call   p50  0.0 ms
XSync WAIT  p50  6.8 ms  p90 7.6 (tight: the X thread rarely delays it)
full period p50 33.0 ms
```

FR-BENCH-1's floor is 2 × 8.19 = 16.4 fps: **27.66 PASSES.** The
"producer starved the pipeline" resolution of #64b equated the
pipeline's 8.19 sends/s with the producer's rate — an inference, now
falsified by the producer's own telemetry. The pipeline consumes every
~4th producer frame.

## The serializer, named by cross-correlation

Producer stamps are epoch-anchored; the trace has wall clocks; same box,
same clock. For each of 204 capture arms:

```
arm - latest BLIT before it   p10  4.8  p50 25.0  p90 71.2  stdev 30.7 ms
arm - previous LAST=1         p10 26.0  p50 35.0  p90 44.0  stdev 11.8 ms
blits arriving DURING each service window: p50 = 2
```

The arm is **phase-locked to the previous frame's completion** (tight,
stdev 11.8) and **uncorrelated with damage arrival** (scattered across
the 33 ms blit period, stdev 30.7). Two fresh frames of damage exist
during every encode; the per-monitor budget has a free slot
(depth 1 of 2, `budget exceeded` = 0); `msFrameInterval` is 16 ms and
the mode is `CC_GFX_AVC444` (both read from the live session log) — and
the capture still waits for the ack. The ~35 ms arm delay decomposes as
ack transit (~1) + schedule timer (4–16) + X-side capture+pack (~15–20)
+ xup transit.

**Every xorgxrdp code path read (d77d054, the deployed commit, tree
clean) says the second capture should be admitted. The measurement says
it never happens.** The discrepancy lives between "damage arrives" and
"rdpCapRect sends" — the schedule path, the dirty-region lifecycle, or
a gate not visible at INFO. The decisive next probe is the xorgxrdp
uprobe (`PR-demo/t4_profile/xorg_capture_uprobe.sh` extended to
`rdpDeferredUpdateCallback` / `rdpScheduleDeferredUpdate` /
`rdpCapRect`): count callback entries during encode windows and which
gate each entry exits through.

## Consequences

- FR-BENCH-1 flips to **PASSING as measured** (both checks green:
  selftest-equivalent 27.66 ≥ 16.4; in-run damage pending at
  completions). Producer telemetry is now default-on in textflood.
- #65's "producer too slow" premise is closed as falsified; the
  blocking item moves back to the pipeline as **#64c (ack-paced
  capture)**, which now blocks #63.
- #62's m=2 1.41× annotation softens: at m=2 the producer renders
  ~1.85× the pixels (~48 ms ≈ 20 fps) against a 174 ms per-monitor
  period — likely saturating there too, to be confirmed from the m=2
  stamps at the next A/B rather than asserted.

`oracle/` audited (wire audit + black-frame check) and deleted;
everything above re-derives from `gfx_trace.txt.gz` +
`textflood_stamps.tsv`.
