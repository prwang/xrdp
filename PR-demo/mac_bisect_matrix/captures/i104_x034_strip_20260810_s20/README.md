# i104_x034_strip_20260810_s20 — the sparse-chroma arm, against x033

**One 20 s leg, arm x034 (credit frontier at `wire_window = 2` PLUS the
sparse-chroma cadence, `chroma_refresh_ms = 1000`, `chroma_idle_ms =
100`), one monitor at 3840×2400, oracle client,
`SESSION_KIND=textflood_strip`.** Same image as x033
(`3ca17beaa84d.xx10fa3aa-tf.p2fde5531`), same payload, same geometry,
same client rig, same script. **The two arms are one config line apart.**

## The comparison

| | x033 — chroma every frame | x034 — sparse chroma |
|---|---|---|
| FR-BENCH-1 margin | 3.91× | **3.94×** |
| frame period, mean | 17.7 ms | **17.0 ms** |
| p50 / p90 / p99 | 18 / 19 / **26** ms | 17 / 18 / **21** ms |
| cycles in the leg | 952 | 997 |
| encoder children armed | 2 on 952 of 952 (100 %) | **1 on 979 (98 %)**, 2 on 18 |
| network writes per frame | 4.00 | 3.015 |
| bytes per frame | **3.410 MB** | **1.892 MB** |
| | | **−44.5 %** |

**The byte saving is the result, and it reproduces exactly.** 44.5 %
here, against 44.5 % in `i92_sparse_aux_ab_…_233519` and 43.8 % in the
run before it — three measurements, two different arms, two different
builds, two different payloads, and the pipe clamped in two of them and
not in this one. Bytes are a property of what was encoded rather than of
how fast the host was, which is why they travel.

**The mechanism check is explicit and passed.** 979 of 997 cycles armed
ONE encoder child and 18 armed two, so the cadence did what it is
specified to do; and the 18 chroma frames in 20 s are the 1000 ms
guarantee firing, not the 100 ms settle — `textflood_strip` never leaves
the screen still for 100 ms. The write count corroborates the cycle
count independently: a full frame is two PDUs and a luma-only frame is
one, so 4.00 and 3.015 writes per frame are exactly what 100 % and 2 %
full frames predict.

## What the time difference is and is NOT worth

17.7 → 17.0 ms is **0.7 ms, 4.0 %**. Treat it as suggestive, not
established:

* **These are two single legs run about ten minutes apart, not an
  interleave.** An unchanged arm on this host has drifted 36.8 → 41.8 ms
  across one day (BACKLOG #88), and the interleaved design of the i92
  A/B exists precisely because one leg each cannot see that. 4 % is
  inside the range host drift can produce.
* It is, however, the same size and sign as the 5.4 % the interleaved
  i92 A/B measured, and consistent with the mechanism already
  established: the chroma encode runs 97–99 % *inside* the luma encode,
  so skipping it removes bytes rather than milliseconds.
* **The p99 is the more interesting number**: 26 → 21 ms, a 19 % shorter
  tail. One leg each; not established either. Worth an interleaved pair
  if the tail matters to the PR's claim.

**Both legs were taken above the FR-BENCH-1 floor** (3.91× and 3.94×),
so unlike every earlier comparison in this tree these are valid for
stage-overlap claims as well as throughput — the gate says so in its own
words.

## Conditions, stated because a ratio only means something inside them

Image `3ca17beaa84d.xx10fa3aa-tf.p2fde5531`; xorgxrdp `10fa3aa23033`;
one monitor `3840x2400R`; oracle save-only client (acks before decode);
`textflood_strip` producer at 4.3–4.5 ms/frame; encoder input pipe at or
above the 64 KiB minimum on both arms; E2 counters zero on both.
