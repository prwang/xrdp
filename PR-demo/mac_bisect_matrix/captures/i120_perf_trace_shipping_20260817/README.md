# #120 perf-trace source-cost qualification

Recorded 2026-08-17 on the development tree in this container. This is a
local microbenchmark of the source path only; it is not a frame-rate or fleet
result.

The enabled build ran `tools/perf_trace_bench` for 500 iterations of 1000
frames, with 12 named text events per frame. The implementation used one
calling-thread byte ring, the dual-alias mapping, the shipping formatter and
the separate buffered sink. The caller did no file I/O and no normal logging.

| retained run | mean | p50 | p90 | p99 | maximum |
|---|---:|---:|---:|---:|---:|
| run7 | 11.825 us | 13.709 us | 14.229 us | 23.529 us | 55.198 us |
| run8 | 14.198 us | 14.460 us | 14.919 us | 24.369 us | 35.259 us |

Each raw trace is mode 0600, contains one `clock_base` plus exactly 6000
complete event records, and has no `perfdrop`, `perfformat` or `perfnoring`
diagnostic. `run7.txt` and `run8.txt` are the benchmark summaries.

Earlier development variants in this sitting were rejected before this
record was written. Immediate unbuffered sink writes produced roughly
26--27 us means. A variant which substituted a `gettid` syscall for the
thread identifier also failed the selected-path comparison. Their captures
were deleted rather than retained as results; neither is the shipping
implementation.

The two retained distributions are consistent with the selected #107 text
ring prototype: their mean range brackets that prototype's two 12.272--12.653
us means apart from run-to-run scheduler variation, and their 23.529--24.369
us p99 is no worse than its 25.609--42.328 us p99 range. No single mean is
used to hide the distribution.

Quality checks:

1. The event count closes: 500 x 1000 x 12 calls were timed, and each output
   file contains the expected 6000 sampled records plus its clock base.
2. The intervention is the enabled shipping source path; disabled-footprint
   tests separately prove that a default build evaluates no arguments and
   links no tracer implementation.
3. This is the compile-time opt-in design required by PRD slice #127.
4. The selected implementation does not regress the #107 prototype range.
5. Both retained runs use the same executable, event count and local box.
