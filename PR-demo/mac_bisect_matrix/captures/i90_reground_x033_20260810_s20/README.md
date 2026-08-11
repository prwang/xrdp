# #90 re-grounding — credit frontier at window 2

One 20 s leg on x033: eager credit frontier with `wire_window = 2`, full
AVC444, one monitor at 3840x2400, oracle client, `textflood_strip`, and the
established 592.25 MHz modeline from `i92_sparse_aux_ab.sh`. The encoder input
pipe passed the 64 KiB guard; FR-BENCH-1 margin was 3.86x; both children were
armed on all 944 cycles.

The raw ring for this run is `perf/enc.788`: 944 cycles matching the 944
sends, 33,959 records, zero drops. Steady worker cycle 17.860 ms; collect
1.281 ms mean (p50 1.248, p90 1.544); 10/943 steady waits exceeded 1 ms. The
four-part pump-cycle decomposition closes to 0.0009 ms.

The generic E5 speedup gate is not this characterization's acceptance test;
it encodes a 1.5x threshold unrelated to this stage-attribution run.
