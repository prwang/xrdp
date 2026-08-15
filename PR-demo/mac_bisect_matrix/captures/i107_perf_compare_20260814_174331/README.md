# #107 fixed-object versus direct-string trace source

2026-08-14, local dev-box microbenchmark. No remote session, encoder, client or
network participates.

## Question

For the actual twelve-event per-frame trace mix, what producer latency is added
by formatting the final named record with a static, non-positional
`snprintf()` directly into a Linux double-mapped byte ring, compared with
publishing the old fixed-size object and formatting the same record on the
sink?

## Instrument and controls

`tools/perf_trace_bench.c` was extended with exactly two comparison mechanisms
in one binary:

* `fixed`: the producer takes the timestamp and publishes the event identity
  and six values through `common/perf_trace.c`'s real fixed-slot push; the sink
  calls `snprintf()`.
* `string`: the producer takes the same timestamp and calls the same formatter
  directly into a 512 KiB byte ring. The same memfd backing is mapped into two
  adjacent virtual ranges; the sink only writes published bytes.

Both use the same static event descriptors, value sequence, acquire/release
publication, 10 ms sink cadence, pre-touched ring and 1 MiB stdio buffer, and a
real mode-0600 output file. Each process emits the real twelve-tag mix for 20
warm-up plus 500 measured frames, paced at 40 ms. One clock pair brackets all
twelve source calls. Order was fixed/string/fixed/string, 20 seconds each.

Build:

```
make -C common -j2
gcc -O2 -Wall -Wextra -Werror -I. -Icommon \
    tools/perf_trace_bench.c common/.libs/libcommon.a -lpthread \
    -o /tmp/perf_trace_bench_compare
```

Runs used `XRDP_PERF_TRACE=<this-directory>/r1` and `.../r2` with
`fixed` or `string`. This is a mechanism comparison, not the complete future
runtime-gated API: the cached armed branch and one-time ring claim are common
integration work and are outside both comparison paths.

## Precondition failures removed before the final run

The first attempted fixed run reported minor faults in 240/500 measured
frames. The intended pre-touch had been a semantically redundant `memset()`
and the optimizer removed it. That capture was deleted and no timing from it is
used. Pre-touch now uses volatile page writes; the final runs have one process-
wide minor-fault sample each in fixed and string, with no sustained fault mode.

A following preliminary pair used a 1 MiB text ring. Its 827,120-byte file did
not reach the logical boundary, so those captures were also deleted rather than
presented as proof of the alias. The final ring is 512 KiB, close to the old
fixed ring's 393 KiB physical footprint. Every final string run therefore
crosses the alias once while producing the exact expected output.

## Result

Producer cost for twelve events, microseconds per frame:

| mechanism/run | mean | p50 | p90 | p99 | max |
|---|---:|---:|---:|---:|---:|
| fixed r1 | 1.903 | 2.160 | 2.479 | 4.599 | 4.859 |
| string r1 | 12.653 | 12.180 | 13.069 | 25.609 | 31.828 |
| fixed r2 | 2.388 | 2.380 | 2.619 | 4.819 | 4.990 |
| string r2 | 12.272 | 11.770 | 12.390 | 42.328 | 91.766 |

Direct string minus its paired fixed control:

| statistic | added µs/frame | string/fixed |
|---|---:|---:|
| mean | 9.884–10.750 | 5.14–6.65× |
| p50 | 9.390–10.020 | 4.95–5.64× |
| p90 | 9.771–10.590 | 4.73–5.27× |
| p99 | 21.010–37.509 | 5.57–8.78× |

The factor is large because the fixed operation is only about 2 µs for all
twelve events. The absolute direct-string cost is:

| direct-string statistic | share of 25 ms frame | share of 41 ms frame |
|---|---:|---:|
| mean, 12.272–12.653 µs | 0.049–0.051% | 0.030–0.031% |
| p99, 25.609–42.328 µs | 0.102–0.169% | 0.062–0.103% |
| observed max, 91.766 µs | 0.367% | 0.224% |

Mean source work is about 1.02–1.05 µs per event; moving formatting onto the
source adds about 0.82–0.90 µs per event against fixed publication.

### The p99 is not stable enough to collapse to one number

String mean, p50 and p90 reproduce closely. String p99 does not: it is 25.609
µs in r1 and 42.328 µs in r2; r2 also contains one 91.766 µs fault-free
maximum. Minor faults do not explain the upper tail: the one r2 faulting frame
is 28.809 µs, and the fault-free p99 remains 42.328 µs. One alias boundary
crossing also cannot account for the five or more samples which determine
p99. The remaining hypothesis is scheduler/cache interaction with the sink or
libc formatting, but this run does not attribute it. The result is therefore
the p99 range, never its average. Even the upper observed p99 is 0.169% of the
shorter 25 ms frame scale; the unroot-caused tail is visible but not large
enough to make the formatter intrusive at this project's scale.

Total process CPU was 86.2/87.3 ms for fixed and 85.4/77.6 ms for string over
20 seconds. The formatter exists in both processes and only moves between
threads, so the lack of an increase is plausible; the spread is too large to
claim a total-CPU improvement.

## Integrity and quality gates

1. Every file contains exactly 6,240 lines and 827,120 bytes. The count closes
   as `(20 + 500) × 12`; all four files together contain 24,960 records.
2. Both string files exceed the 524,288-byte physical ring capacity, so the
   monotonically increasing head crosses the logical boundary. The complete
   output proves the adjacent alias, publication and sink drain survived it.
3. Drops are zero and formatter/write failures are zero in all four runs.
4. Every token in all 24,960 records matches the restricted `key=value`
   grammar. Each fixed/string pair is byte-identical after removing its
   monotonic timestamp, PID and TID.
5. All four files are mode 0600. The same binary, host, event sequence, pacing,
   sink cadence and output schema were used; only internal transport and
   formatter placement changed.
6. The old #61h ~7 µs/frame number is not used as this fixed control. It
   measured the complete older API and included a sustained lazy-first-touch
   mode. This experiment pre-touches both candidates and isolates the source
   mechanism; comparing its 1.9–2.4 µs fixed result directly to #61h would not
   be apples-to-apples.
7. `make check TEST_NAME=perf_trace` passed: common 176/176 and the complete
   repository test run remained green. The benchmark also built with
   `-Wall -Wextra -Werror`; `git diff --check` passed.

## Verdict

**Select the direct `snprintf()` text byte ring for the reusable tracer
foundation.** It costs about 10 µs/frame more at mean and 21–38 µs/frame more
at p99 for twelve events, but the upper measured p99 is only 0.169% of a 25 ms
frame. That is affordable for the benefit being purchased: the ring already
contains named, immediately readable records; the sink is a byte drain; and
the repository does not ship a rigid anonymous object format plus an offline
adapter. No custom formatter is justified by this result.

This verdict selected the internal representation. The remaining shipping
gates are carried by #105; the dev representation migration which closes #107
is recorded below.

## 2026-08-15 — final dev implementation smoke

The fixed record struct, six-int calls and comparison arm were then removed.
All 34 xrdp call sites now format their final named fields directly into
`common/perf_trace`'s double-mapped byte rings. The sink drains bytes without
parsing or reformatting them. The generic Python reader rejects malformed or
truncated lines, ignores unknown named fields, and the wall-clock adapter no
longer contains an event-to-positional-field table.

The first 500-frame final-implementation smoke was red on file privacy:
6,000/6,000 event records were complete with zero drops, and source cost was
14.386 µs/frame mean and 25.549 µs p99, but the inherited process umask made
the `fopen()` output mode 0644. That output was in `/tmp` and is not a surviving
capture. File creation was changed to `open()` with `O_EXCL`, `O_NOFOLLOW`,
`O_CLOEXEC` and an explicit mode 0600; the failure is retained here because it
is what found the unsafe creation path.

The identical 500-frame rerun after that fix is the surviving
`final.named.trace.237366`:

| actual selected implementation | mean | p50 | p90 | p99 | max |
|---|---:|---:|---:|---:|---:|
| twelve events/frame, µs | 14.147 | 14.049 | 14.629 | 24.749 | 33.059 |

It contains one clock-base record plus exactly 6,000 event records, 850,954
bytes, no drop/format/no-ring diagnostic, and is mode 0600. The generic
wall-clock adapter rendered all 6,000 events with none skipped or outside its
window. At the 25 ms frame scale, the actual implementation is 0.057% at mean,
0.099% at p99 and 0.132% at its observed maximum.

The actual mean is 1.49–1.88 µs above the selected prototype's two-run range;
this is surfaced as a regression, not folded into the old range. The production
API formats the common prefix and the event payload separately so it can offer
one variadic named-field interface, whereas the closed candidate used one
event-specific complete format. Its p99 is inside the prototype's measured
25.609–42.328 µs range and the absolute source disturbance remains below
0.1% of 25 ms at p99. The representation decision therefore still holds.
