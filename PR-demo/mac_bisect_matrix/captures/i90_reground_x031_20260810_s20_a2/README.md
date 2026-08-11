# #90 re-grounding — legacy acknowledgement

One 20 s leg on x031: legacy acknowledgement, full AVC444, one monitor at
3840x2400, oracle client, `textflood_strip`, and the established 592.25 MHz
modeline from `i92_sparse_aux_ab.sh`. The encoder input pipe passed the 64 KiB
guard; FR-BENCH-1 margin was 4.42x; both children were armed on all 882
cycles.

The raw ring for this run is `perf/enc.306`: 882 cycles matching the 882
sends, 30,868 records, zero drops. Steady worker cycle 19.138 ms; collect
1.241 ms mean (p50 1.207, p90 1.499); 148/881 steady waits exceeded 1 ms.
The four-part pump-cycle decomposition closes to 0.0008 ms.

The harness's 51.1 ms E5 baseline line is stale for this geometry and payload
and is not a result of this characterization.
