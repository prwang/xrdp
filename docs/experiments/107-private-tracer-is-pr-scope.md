# #107 — the existing private tracer is required PR scope

2026-08-12. This is a source-and-record audit; it required no new runtime
experiment.

## Decision

Port the full existing server instrument with the AVC444 cleanup PR:

* `common/perf_trace.{c,h}` and its `common/Makefile.am` registration;
* `tests/common/test_perf_trace.c` and its build, declaration and
  suite-selection registration;
* all 34 current `PERF_TRACE*()` call sites and their lifecycle hooks;
* the trace-only `trans::wait_bytes` queue observation.

Keep `tools/perf_trace_bench.c`, `PR-demo/` capture machinery and analysis
scripts on the dev branch. They validate or consume the instrument but are
not part of the shipped server.

## Why a smaller event set is not complete

The decision was made from the open backlog and the analyses that already
read these records, rather than from a new proposed model:

| retained event family | existing obligation that consumes it |
|---|---|
| `auxdue`, `feedend`, `outfirst`, `pump_*`, `wait_*`, `ackslot`, `send`, `egress` | #92 must prove the sparse-chroma mechanism, decompose its throughput pair, pair child encode windows by child and sequence, and show which credit term binds. |
| all worker stage brackets, `take`, `submit`, `absorb`, `batch`, `enc` | #93 requires decomposed one- and two-monitor reruns on the representative GPU, including saturation and mechanism checks across AVC444 and AVC420. |
| `dmg`, `batch`, `submit`, `absorb`, `msgin`, `enq`, `take`, `emit_*`, `egress` | #94 must distinguish an unchanged monitor from one with real pixels; #95 must attribute capture handoff and measure the same monitor from damage arrival through server egress. |
| `ackregion`, `ackslot`, `cliack`, `send`, `egress`, including queued KiB | #80's remaining qualification and #98's LAN/WAN work require the actual credit terms, client frontier, wire distance and transport backlog. |
| `subm_*`, `book_*`, `coll_*`, `rel_*`, `emit_*`, `drain_*` | They close the worker cycle and identify movement between stages. #61e, #87 and #90 show why an unnamed remainder or a bracket with its own logging cost cannot support a performance conclusion. |

The paired endpoints are one unit. Dropping one side of a bracket, or an
identity-bearing event between threads, does not create a smaller valid
instrument; it creates an interval that cannot close or must be joined by
time proximity. The latter is expressly forbidden by the scientific quality
gate.

There is also no meaningful code-size win in selecting individual call sites.
The fixed reviewed mechanism is 614 lines and its schema/ring test is 298
lines. The event calls themselves are small statements inside feature code
that the PR already changes. Removing a few of them retains nearly all of the
review burden while making the one instrument less generally auditable.

## Why external perf does not change the decision

#106 closed RED. Its Phase B audit found no exact external mapping for 13 of
34 records, including `feedend`/`outfirst` sequence identity and explicit
frame chains. Those are not optional payload decoration: they prevent the
bad time-window pairings that previously produced negative and cross-frame
durations. Phase A also failed to record the selected-PID uprobe through the
delegated tracefs boundary, but the semantic failure alone is decisive.

The private tracer has already passed the relevant transparency check: its
armed source cost is about 7 microseconds per frame on the measured workload,
0.02% of the frame period, with a worst observed arm at 0.37%; it reports
drops rather than blocking. The disarmed shipped state costs only the cached
branch. Evidence:
`docs/experiments/61h-what-the-ring-costs.md`.

## Pinned-base integration

Upstream commit `2e8a4a82` added `TEST_NAME` suite selection to
`tests/common/test_common_main.c`. The port must preserve that layout and add
the tracer as another conditional suite:

```
if (run_suite("perf_trace"))
{
    srunner_add_suite(sr, make_suite_test_perf_trace());
}
```

Do not copy the dev branch's older test runner wholesale; that would discard
upstream's new dechunker suite and selection behaviour. The normal source and
test Makefile additions otherwise have no identified pinned-base conflict.

## Acceptance carried into #105

The tracer remains default disarmed and keeps its current source/sink split:
no source-path I/O, allocation or shared lock; one thread-local producer ring
per emitting thread; a separate buffered sink; explicit drop reporting; six
payload fields; monotonic timestamps; and identities preserved at every
retained call site. Its existing test must pass in the full common suite and
when selected alone with `TEST_NAME=perf_trace`.
