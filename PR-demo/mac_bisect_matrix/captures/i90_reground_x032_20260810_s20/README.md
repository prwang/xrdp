# #90 re-grounding — credit frontier at window 1

One 20 s leg on x032: eager credit frontier with `wire_window = 1`, full
AVC444, one monitor at 3840x2400, oracle client, `textflood_strip`, and the
established 592.25 MHz modeline from `i92_sparse_aux_ab.sh`. The encoder input
pipe passed the 64 KiB guard; FR-BENCH-1 margin was 4.26x; both children were
armed on all 903 cycles.

The raw ring for this run is `perf/enc.273`: 903 cycles matching the 903
sends, 32,235 records, zero drops. Steady worker cycle 18.694 ms; collect
1.249 ms mean (p50 1.226, p90 1.490); 91/902 steady waits exceeded 1 ms. The
four-part pump-cycle decomposition closes to 0.0007 ms.

The generic E5 speedup gate is not this characterization's acceptance test;
the arm is expected to reproduce legacy behavior, not beat it by 1.5x.
