# #107 — the existing private tracer is required PR scope

2026-08-12. This is a source-and-record audit; it required no new runtime
experiment.

> **Superseding completeness correction, 2026-08-12:** the shipping
> decision below stands, but the phrase "full existing server instrument"
> was incomplete. A second source audit found that the current code cannot
> be ported unchanged: its first event performs initialization work on the
> measured thread, xrdp never closes it, output failures are silent, its
> shared indices are not atomic, and the paired xorgxrdp capture endpoint is
> still a per-frame `LOG()` call. The exact completion scope is recorded under
> "What must actually ship" below. The
> old wording is retained verbatim as part of the experiment record; it must
> not be read as approval to copy the current files unchanged.

> **Owner amendment, 2026-08-13:** "default disarmed" is not the shipped
> production boundary. The facility is a compile-time opt-in and a disabled
> build contains no tracer code, calls, event strings, environment-variable
> strings, trace-only state or xup diagnostic payload. An enabled build is
> still runtime-disarmed until configured. The fixed positional six-integer
> disk schema and `perf_trace_lines.py` adapter are also superseded: the ring
> keeps a bounded internal envelope, while the sink writes a generic,
> versioned, self-describing text record that repository-wide tools can read
> without an AVC444-specific format adapter. The detailed contract and
> sequencing decision are recorded at the end of this file. Earlier text is
> retained because this is an experiment record.

> **Owner amendment, 2026-08-14:** the output contract above does not decide
> the ring's internal representation. Two implementations remain candidates:
> fixed typed event slots formatted by the sink, or a variable-length text
> byte ring formatted by a bounded fast formatter on the producer. For the
> latter, Linux permits the same backing pages to be mapped into two adjacent
> virtual ranges. A record which crosses the logical end is then contiguous in
> virtual memory: it needs no split copy, padding record or wrap branch. The
> earlier claim below that variable records inherently require such work is
> withdrawn. The remaining choice is measured-thread formatting cost and
> jitter versus fixed-slot space, descriptor lifetime and field-count bounds;
> it is settled by the pre-port source-path microbench, not by assuming a ring
> boundary cost which the proposed implementation does not have. Earlier
> fixed-slot wording is retained as the proposal it recorded, not a decided
> acceptance requirement.

> **Superseding implementation gate, 2026-08-14:** JSON Lines and a custom
> source formatter are not requirements. The leading text-ring candidate is a
> restricted, versioned `key=value` line written directly by an ordinary,
> non-positional `snprintf()` into a Linux double-mapped byte ring. On this
> box's glibc, the exact integer-only record shape was source-audited and
> dynamically checked allocation-free. The next dev step builds that candidate
> beside the old fixed-size object ring and compares the two producer costs at
> mean, p50, p90 and p99 using #61h's existing workload and measurement method.
> This is an experiment plan, not yet a selection result. Full evidence,
> grammar and arm controls are at the end of this record; earlier JSON and
> fixed-slot prescriptions remain as superseded proposals.

> **Result, 2026-08-14:** select the direct non-positional `snprintf()` text
> byte ring. Against the same-sitting fixed-object control, twelve events add
> 9.884–10.750 µs/frame at mean and 21.010–37.509 µs/frame at p99. The text
> arm's absolute p99 is 25.609–42.328 µs, at most 0.169% of a 25 ms frame.
> Every final arm produced 6,240 complete, byte-matched records with zero
> drops/failures and the text arms crossed the 512 KiB alias boundary. The p99
> spread is retained rather than averaged; detail and the remaining tail
> caveat are at the end of this record. This selects the representation, not
> completion of the tracer's other shipping gates.

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

## What must actually ship

The answer is **one tracing facility in the main paired change, not a private
port after the PR**, but it is the existing semantic event set plus the
completion work below. The present dev implementation is evidence and a
starting point, not merge-ready source.

### 1. Preserve the semantic event set

Retain the 34 current xrdp call sites and their identities. The count is an
audit checksum, not the requirement by itself: the requirement is that the
paired stage endpoints and the explicit frame/monitor/child identities listed
above still close the same intervals after cleanup. Retain
`trans::wait_bytes`, the per-monitor trace-only frame-id state, the six-field
payload and the monotonic record timestamp. Do not ship the bench, fleet
capture machinery or analyzers.

The selector contract is three parts:

* `XRDP_PERF_TRACE=<prefix>` arms the sink and the always-present stage
  records;
* `XRDP_GFX_TRACE=1` adds damage, encode, batch, send and client-ack records;
* `XRDP_ACK_TRACE=1` adds producer-credit and handoff records.

The two selectors without an armed sink must warn once and record nothing.
They must never fall back to `LOG()`.

### 2. Initialization and shutdown are outside the measured path

`perf_trace_on()` currently runs `pthread_once(perf_trace_open)`. Therefore
the first `PERF_TRACE*()` call can execute `fopen`, `malloc`, `calloc` and
`pthread_create` on the thread and inside the interval being measured. That
violates FR-TRACE-1 even though later events are cheap.

Split explicit initialization from the cached source-path test. In fork mode,
initialize in the connection child after `fork()` and before its session
loop. In non-fork mode, initialize before any connection thread enters its
session loop. Never start a sink thread in the listening parent and then fork
it. `perf_trace_on()` and both event functions must then be allocation-free,
I/O-free and lock-free from their first possible measured call.

Add the missing normal-shutdown hook after all connection work has stopped
and before `log_end()`. It must disarm producers, join the sink, make the final
drain, check the flush/close result and release the rings. Do not call this
code from a signal handler. The default `fork=true` path thereby gets one
sink lifecycle per connection child; `fork=false` gets one lifecycle for the
whole xrdp process after all connection threads have joined.

### 3. The sink must fail visibly and create a private file safely

Create exactly one `<prefix>.<pid>` file with close-on-exec and mode `0600`,
refuse symlinks, and document that the prefix directory must be owned by the
administrator and writable by xrdp's post-drop runtime user. A requested sink
which cannot create its file, allocate its rings or start its sink thread must
produce one human-rate error. A sink write, flush or close failure must also
produce one error and leave a detectable failed/truncated result. Silent
disarming and silent disk-full traces are not acceptable.

Only explicit xrdp events and producer metadata carried by that connection's
matched xorgxrdp process may enter the file. This facility does not attach to
tracefs, perf, the kernel or an unrelated PID. With the shipped `fork=true`,
the PID suffix also gives one connection process per file. With `fork=false`,
all simultaneous sessions in that one process share the file and cannot be
separated reliably merely by TID; operator documentation must restrict
characterization to one active session in that mode rather than claim
per-session isolation.

### 4. Make the ring a defined lock-free implementation

The current producer and sink share ordinary `head`, `tail`, drop and
lifecycle objects. Compiler barriers and `volatile` do not make those C data
races defined. Use acquire/release atomic loads and stores (or an equivalent
reviewed primitive) for every cross-thread object while preserving the SPSC
rule and the no-lock source path. Closing must occur only after the emitting
threads are quiescent, so freeing a ring can never race an event.

### 5. Remove the paired xorgxrdp per-frame logger

The producer half at `/workUpdateXorgXrdp/module/rdpClientCon.c` still emits
`ACK_TRACE cap ...` with `LOG(LOG_LEVEL_INFO, ...)` once per capture. It calls
Xorg's log path from the captured session's Xorg thread. That is not the xrdp
ring and it fails the same rule: an unmeasured per-frame logger cannot be an
endpoint in a performance characterization.

Do not add a second xorgxrdp tracer. When xrdp has armed capture tracing, have
the matched xup contract carry the producer's frame id, monitor, capture-begin
and capture-packed monotonic timestamps, and the producer ack/shown
frontiers with the existing frame message. On xrdp receipt, emit those
producer-timestamped events into `common/perf_trace`; extend the event API to
accept an already-taken monotonic timestamp. `msgin` remains the receiving
endpoint, so capture, packing and local handoff can be bracketed without an
extra per-frame log or an extra tracing write. When disarmed, the producer
takes no timestamps and sends no diagnostic trailer. The paired xup version
and serialization tests must change together in xrdp and xorgxrdp.

This is a required companion change, not post-PR porting work. Until it lands,
the xorgxrdp `ACK_TRACE cap` line must not be used for timing claims.

The first inventory found 13 affected session-Xorg logs across seven capture
directories: both i92 sparse-chroma matrices, all three #90 re-grounding arms,
and both i104 strip arms. Their timing-record cleanup and conclusion reopening
are tracked as #108. This record does not silently delete those materials; #108
must enumerate every dependent timing claim before applying the repository's
instrument-on-path deletion rule. Bytes, identities, ordering and correctness
are classified separately because they do not become durations merely by
sharing a capture directory.

### 6. Ship the operating contract and lifecycle tests

Update the stale `xrdp/gfx.toml` instruction which currently says to read
`GFX_TRACE` from the ordinary log. Document the three-knob contract, filename,
record schema, permissions, error behavior, process/session isolation and
clean shutdown in the shipped manpages. State that the trace contains timing,
frame, monitor, region and byte-count metadata and one diagnostic centre-luma
sample, but no captured frame buffer or input events.

Keep the existing schema and ring tests, integrated into pinned-base commit
`2e8a4a82` with `TEST_NAME=perf_trace`. Add deterministic tests, using a
separate process where the one-time lifecycle requires it, for:

* armed start, one output file, mode `0600`, base line and final drain;
* create/symlink failure and sink write failure being observable;
* shutdown without arming, repeated close, and no event after close;
* concurrent producer/sink wrap and drop accounting with defined atomics;
* exact producer-trace serialization across the paired xup contract;
* `trans::wait_bytes` under queued, partial and completed sends.

After the cleanup is authored, rerun the dev-only armed/disarmed microbench
and report source overhead and drops in the PR evidence. This checks that the
lifecycle, atomic and producer-timestamp changes did not invalidate the
existing ~7 microsecond/frame result; the bench itself still does not ship.

## 2026-08-13 amendment — compile-time erasure and reusable records

### Compile-time opt-in is the production default

Add an explicit configure option, provisionally `--enable-perf-trace`, default
off. It may follow `--enable-devel-all` when that umbrella option is selected,
but an ordinary build does not enable it. The configure result supplies both
an Automake conditional and one preprocessor definition shared by callers.

When disabled:

* `common/perf_trace.c` is not compiled or linked;
* every `PERF_TRACE*` macro expands to a statement which evaluates none of its
  arguments;
* trace-only helpers, members, queue accounting, producer timestamps and xup
  diagnostic fields/messages are omitted with the same compile guard;
* no event/tag literal, `XRDP_PERF_TRACE`, `XRDP_GFX_TRACE` or
  `XRDP_ACK_TRACE` string, tracer symbol, sink thread, ring storage or trace
  branch remains in the xrdp or xorgxrdp binary; and
* the tracer tests and any tracer-specific library dependency are absent.

This is stronger than a runtime-off branch. Acceptance builds both modes and
checks the disabled executables with symbol and string inspection in addition
to their normal tests. A call-site test pins that disabled macros do not
evaluate side-effecting arguments. The enabled build retains the runtime
environment gate so an instrumented package can remain disarmed until a
characterization explicitly requests it.

### The file is a generic text event stream, not a six-int tuple

The on-disk contract becomes a versioned, one-record-per-line structured text
format. JSON Lines is the initial target because it has standard parsers,
typed numeric values and forward-compatible unknown fields. A representative
record is:

```
{"schema":1,"mono_ns":42,"pid":7,"tid":9,"event":"send","frame_id":288,"bytes":1930000,"last":false}
```

Every line carries the common timestamp/process/thread/event keys and names
its event-specific fields. Values needed for identities and byte counts are
at least 64-bit and retain signed/unsigned or boolean meaning. Readers must
ignore unknown keys and events from a newer producer; an incompatible change
to the common meanings increments `schema`. The clock-base record is an event
in the same format rather than a comment with a separate grammar.

The source path does **not** call the ordinary logger, allocate, perform I/O,
take a shared lock or use a general `printf` formatter. The internal transport
is deliberately not part of the disk contract. The two candidates to measure
before implementation are:

1. Each call site selects an immutable event descriptor and stores it with
   bounded typed values in a fixed-size slot. The sink formats the JSON.
2. Each call site uses fixed schema metadata and a bounded integer/boolean
   formatter to write the complete JSON record directly into its producer's
   text byte ring. The ring backing is mapped twice into adjacent virtual
   ranges on Linux, so a logical-end crossing remains one contiguous output
   span and needs no staging or split copy.

The second candidate checks available space against the event descriptor's
maximum encoded length before formatting, publishes the actual ending cursor
only after the complete newline-terminated record is present, and drops the
whole event if that maximum is unavailable. It never publishes truncated JSON.
Both candidates retain one SPSC ring per producer and the separate sink; both
must keep event/field names static and bound all source work. Adding or
extending an event does not change a central positional schema in either
design.

`perf_trace_lines.py` is retired after its consumers read the structured
stream directly. Domain analyses will still implement domain logic — joining
frames, calculating intervals and applying gates — but no analyzer needs a
one-off adapter which maps anonymous `a` through `f` fields back to names.
The generic reader/fixture used by repository tests must live outside the
AVC444 demo hierarchy so other xrdp subsystems can consume the same stream.

Acceptance adds malformed/truncated-line handling, 64-bit boundaries,
booleans, unknown-key/event tolerance, event field-name uniqueness and JSON
escaping/validity tests. Before the representation is frozen, the armed local
microbench compares both source paths at the worst current event shape and
maximum 64-bit values, including ring-boundary crossings and a delayed sink.
It reports producer p50, p99 and maximum time, whole-event drops, record
integrity and sink catch-up. The text-ring candidate is acceptable only if its
tail cost remains transparent for the measured brackets. A sink stress test
then proves the selected implementation drains the expected event rate without
introducing drops.

### Sequencing decision

Make this facility shippable on the dev branches **before** re-authoring the
AVC444 cleanup. This resolves the lifecycle, atomic, output-security,
compile-time and formatting contracts once, migrates current consumers, and
reruns the transparency bench before the port begins. It also removes the
xorgxrdp per-frame logger before any replacement characterization is trusted.

Then re-author the generic tracer foundation as the first bisectable slice on
the pinned upstream base. That slice contains configure/build integration,
the ring and sink, xrdp process lifecycle, generic structured format,
documentation and tests; it has no AVC444 call sites and is independently
useful to other xrdp subsystems. The main PR remains one PR.

As each later feature slice introduces code, it introduces that code's event
descriptors and call sites in the same commit. The producer timestamp bridge
lands with the paired xup capture/wire slice, because landing an unused
AVC444-specific diagnostic payload in the generic foundation would make the
foundation not actually independent. Instrumented intermediate builds use
`--enable-perf-trace`; ordinary and CI-default builds keep it compiled out.
This order lets performance regressions be caught at the slice which creates
them without claiming that the complete 34-event AVC444 manifest exists before
the feature code it describes.

## 2026-08-14 amendment — measure direct `snprintf` text against fixed objects

### Why JSON Lines is no longer the prescribed format

The current event inventory does not need JSON's general string and nesting
grammar. Event names and field names are static tokens; payloads are bounded
integers and booleans; records are flat; and domain analyses still need to know
what each named event means. Requiring a JSON emitter on the measured C path
for the convenience of Python's standard parser optimizes the consumer while
leaving the producer to pay for syntax it does not use.

The leading text candidate is instead a restricted, versioned key/value line:

```
schema=1 mono_ns=42 pid=7 tid=9 event=send bytes=1930000 last=0 frame_id=288 id_server=19 id_client=17 fif=2
```

The grammar is deliberately smaller than a general logger:

* keys are ASCII `[a-z][a-z0-9_]*` tokens;
* one ASCII space separates `key=value` tokens and one newline terminates a
  record;
* values are signed or unsigned decimal integers, `0`/`1` booleans, or static
  token values;
* dynamic string values are not accepted by the source API; a later need for
  them requires an explicit encoding rule and cannot pass an unescaped `%s`;
* readers ignore unknown keys and events; additive fields keep the same
  `schema`, while incompatible common semantics increment it; and
* clock-base, drop and failure records use this same grammar rather than a
  comment or positional side grammar.

This is self-describing without the anonymous `a` through `f` tuple. A generic
reader splits each token once at `=` and does not need the AVC444-specific
`perf_trace_lines.py` mapping. The durable output requirement is therefore
"versioned, named, self-describing text records"; JSON Lines is one superseded
candidate, not the contract.

### The installed glibc does not allocate for this `snprintf` shape

The allocation concern was checked on the implementation this dev work will
actually run against, not left as an ISO C portability objection:

* installed runtime: Debian glibc `2.41-12+deb13u3`;
* matching source: Debian `glibc-source` `2.41-12+deb13u3`, installed under
  `/usr/src/glibc` for this audit;
* `snprintf()` enters `__vsnprintf_internal()` with
  `struct __printf_buffer_snprintf` on the stack;
* the ordinary non-positional path in `vfprintf-internal.c` uses its
  1000-byte stack `work_buffer` for `%u`, `%d`, `%llu` and `%lld`; and
* the heap-capable `scratch_buffer` code is in the positional/registered
  extension path. The proposed static formats use neither `$` positional
  arguments nor custom printf handlers, and the xrdp tree registers none.

A runtime probe put GDB breakpoints on `__libc_malloc`, `__libc_calloc` and
`__libc_realloc` immediately before the first format call. It formatted one
million copies of the largest proposed `send` record: nine integer
conversions, including 64-bit maxima. Both entry points completed with no
allocation breakpoint:

```
snprintf@GLIBC_2.2.5:       NO_ALLOCATION_HIT_DURING_1000000_SNPRINTF_CALLS
__snprintf_chk@GLIBC_2.3.4: NO_ALLOCATION_HIT_DURING_1000000_SNPRINTF_CALLS
```

This finding is scoped to static, non-positional integer/boolean formats: no
floating point, arbitrary runtime strings or registered extensions. Within
that scope, allocation is not a reason to write a private integer formatter.
Start with libc `snprintf()`; consider a custom formatter only if measured
source cost fails the comparison below.

### Text byte-ring candidate

Each producer gets one SPSC byte ring whose power-of-two, page-rounded backing
is created and pre-touched during trace initialization. On Linux, reserve
twice its capacity in virtual address space and map the same backing pages into
both adjacent halves. A record starting near the logical end is then one
contiguous virtual span. The double mapping consumes one ring of physical
pages, not two, and needs no staging copy, split write, padding record or wrap
branch.

The producer owns a monotonic atomic byte `head`; the sink owns `tail`. Each
static event format supplies a maximum encoded length. The producer checks
that free space can hold that maximum plus `snprintf()`'s NUL, takes the event
timestamp, formats directly at `base + (head & mask)`, and release-publishes
only the returned length after the complete newline exists. The NUL is not
published and is overwritten by the next record. A negative or out-of-bound
return is an internal formatter failure; a full ring drops the whole event and
increments its counter. Partial or truncated lines are never published.

The sink acquire-loads the published `head` and writes the contiguous byte
span without parsing or reformatting it, then release-publishes `tail`.
Mapping failure makes trace initialization fail visibly; it does not silently
select a different hot-path mechanism. This keeps the existing rules: no
source allocation, I/O, syscall, shared lock, blocking, spin or growth.

### Next dev experiment: exactly two mechanisms

Extend `tools/perf_trace_bench.c` on the dev branch to select two internal
mechanisms in separate processes:

1. **Fixed object control.** The old fixed-size source slot stores the
   timestamp, event identity and bounded typed values. The sink formats the
   restricted key/value line with `snprintf()`.
2. **Direct text candidate.** The source formats that identical key/value line
   with a static, non-positional `snprintf()` directly into the double-mapped
   byte ring. The sink only drains bytes.

This holds the disk record and its field names constant. The changed variable
is the internal transport and whether formatting occurs on the producer or
sink. Both implementations use one pre-created, pre-touched SPSC ring per
producer, acquire/release publication, the same sink drain interval, a real
private output file and whole-event drop accounting. Initialization, mapping,
ring claiming, allocation and first-touch are complete before the measured
loop.

Reuse #61h's recorded model rather than inventing a new workload: the actual
twelve-event per-frame tag mix, 20 warm-up frames, 500 measured frames paced at
40 ms, and one `CLOCK_MONOTONIC` pair around all twelve source calls. The batch
bracket avoids making twelve extra clock reads larger than the mechanism being
measured. Run two interleaved repetitions of each mechanism, 20 seconds per
process, for about 80 seconds total. This is still two arms; repetitions are
not additional conditions. No `off`, calibration, ordinary-logger or custom-
formatter arm is added.

For each mechanism report producer microseconds per frame at **mean, p50, p90
and p99**, as requested. Also report process CPU, minor-fault grouping, output
record count, drop count and parse/integrity status as mechanism and anomaly
checks; none substitutes for the four requested producer statistics. The
expected file count per process remains 6,240 complete records: `(500 + 20) ×
12`. A fault mode after the required pre-touch, any malformed line, a count
mismatch or any drop is an anomaly to explain before comparing centres or
tails.

The comparison's goal is not to prove that `snprintf()` is free. It decides
whether its source cost is affordable at the 25–41 ms frame scale in exchange
for a repository-wide trace which is immediately readable, extensible by
named fields and does not require a rigid private object schema plus an
offline positional adapter. Report the direct delta and each distribution as
a percentage of 25 ms and 41 ms. The data and distribution shape decide; do
not declare the text candidate from mean alone, and do not write a private
formatter unless the ordinary `snprintf()` candidate is measured red.

### Result — the direct text cost is affordable

Implemented the two mechanisms in `tools/perf_trace_bench.c`. The fixed arm
uses `common/perf_trace.c`'s actual fixed-slot push/pop and formats the named
record on its sink. The string arm calls the same static, non-positional
format directly into a 512 KiB double-mapped ring and publishes the completed
byte length. Both sinks write the identical restricted key/value record. The
old fixed ring's head/tail publication was changed from compiler barriers over
ordinary shared objects to acquire/release atomic operations before it was
used as the control.

The recorded #61h workload was retained: twelve real event families per
frame, 20 warm-up frames, 500 measured frames at 40 ms, two interleaved
repetitions per mechanism. Producer cost for all twelve events, in
microseconds per frame:

| mechanism/run | mean | p50 | p90 | p99 | max |
|---|---:|---:|---:|---:|---:|
| fixed r1 | 1.903 | 2.160 | 2.479 | 4.599 | 4.859 |
| string r1 | 12.653 | 12.180 | 13.069 | 25.609 | 31.828 |
| fixed r2 | 2.388 | 2.380 | 2.619 | 4.819 | 4.990 |
| string r2 | 12.272 | 11.770 | 12.390 | 42.328 | 91.766 |

String minus its paired fixed control:

| statistic | added µs/frame | string/fixed |
|---|---:|---:|
| mean | 9.884–10.750 | 5.14–6.65× |
| p50 | 9.390–10.020 | 4.95–5.64× |
| p90 | 9.771–10.590 | 4.73–5.27× |
| p99 | 21.010–37.509 | 5.57–8.78× |

The factor makes the string path look expensive because the fixed publication
is only about 2 µs per twelve-event frame. In the domain's scale, the complete
string source costs 0.049–0.051% of a 25 ms frame at mean and 0.102–0.169% at
p99; against 41 ms those are 0.030–0.031% and 0.062–0.103%. Its 91.766 µs
observed maximum is 0.367% of 25 ms. Mean source work is 1.02–1.05 µs/event;
placing the formatter on the source adds about 0.82–0.90 µs/event.

The string p99 is not unimodal enough to collapse to one number. Mean, p50 and
p90 reproduce closely, while p99 moves from 25.609 to 42.328 µs and the second
run contains one 91.766 µs fault-free maximum. Minor faults do not explain it:
the one faulting frame in that run is 28.809 µs, and the fault-free p99 remains
42.328 µs. One alias crossing cannot create the five or more samples which set
p99. Scheduler/cache interaction with the sink or libc formatter is the
remaining hypothesis, not a finding. The range is the result. Even its upper
p99 remains 0.169% of the shorter frame scale, so the unresolved tail does not
make this instrument intrusive.

Every final file has exactly 6,240 lines and 827,120 bytes; all four together
have 24,960 valid key/value records. Drops and formatter/write failures are
zero, files are mode 0600, and fixed/string event payloads are byte-identical
after removing their necessarily different timestamp, PID and TID. Each text
file exceeds the 524,288-byte physical ring and therefore crosses the logical
alias boundary while remaining complete. `make check TEST_NAME=perf_trace`
passed, including common 176/176; the prototype builds with
`-Wall -Wextra -Werror` and `git diff --check` passes.

Two precondition failures were caught and excluded before these final arms.
First, an optimizer-elided `memset()` left the sink buffer lazy and produced
240/500 faulting frames; volatile page pre-touch replaced it. Second, a 1 MiB
text ring produced only 827,120 bytes and never reached its boundary; it was
replaced by the 512 KiB ring. Those captures were deleted and no number from
them enters the tables. Full commands, controls, validation and surviving raw
files:
`PR-demo/mac_bisect_matrix/captures/i107_perf_compare_20260814_174331/README.md`.

**Decision:** use the directly formatted text byte ring for the generic
facility. The roughly 10 µs/frame mean premium is affordable and removes the
fixed anonymous object schema and mandatory offline adapter. The data do not
justify writing a custom formatter. This is not `DONE` for the full item:
production integration, lifecycle, compile-time erasure, output failure paths,
xorgxrdp pairing and the post-integration transparency gate remain.

## 2026-08-15 closure — representation installed; shipping work stays #105

The final sentence above described the broader shipping gate before the owner
split the next commit's completion boundary: install the selected dev
representation, retire the fixed path, migrate its consumers, and close #107.
That work is now complete. It does **not** assert that the main PR's tracer is
already shippable. The lifecycle, default-off compile erasure, visible sink
failures, documentation and paired xorgxrdp timestamp bridge specified earlier
in this record remain acceptance criteria of open item #105.

The dev implementation now has one representation:

* `struct perf_trace_rec`, `PERF_TRACE6`, the six anonymous integers and the
  fixed comparison arm are deleted;
* all 34 xrdp source sites print `event=` plus their semantic field names using
  static, non-positional integer formats;
* each producer writes complete lines into its pre-created 512 KiB SPSC byte
  ring, whose Linux backing is mapped into adjacent aliases; the sink writes
  published byte spans without an event switch or formatter;
* the common prefix, clock-base, drop and formatter-failure records all use the
  same versioned key/value grammar; and
* the generic Python reader validates that grammar and unknown fields without
  knowing AVC444 events. `perf_trace_lines.py` now only adds the clock-derived
  wall-time envelope needed by older gate output; the positional `TAGS` map is
  gone. Every direct raw-trace analyzer in the demo tree reads named fields.

The final implementation exposed a security failure before closure. Its first
500-frame smoke produced 6,000/6,000 valid event records with zero drops and
14.386 µs/frame mean, 25.549 µs p99 source cost, but `fopen()` plus umask 022
created mode 0644. That run is red and is not a surviving capture. Creation was
changed to `open(O_EXCL|O_NOFOLLOW|O_CLOEXEC, 0600)` followed by `fdopen()`.

The identical rerun after the fix produced 6,000/6,000 valid events plus one
clock-base record, 850,954 bytes, zero drop/format/no-ring diagnostics and mode
0600. Its twelve-event source distribution was 14.147 µs mean, 14.049 p50,
14.629 p90, 24.749 p99 and 33.059 maximum. That is 0.057% of 25 ms at mean and
0.099% at p99. The mean is 1.49–1.88 µs slower than the selected prototype's
two-run range because the reusable variadic implementation separately formats
its common prefix and named payload; the regression is explicit. Its p99 is
inside the prototype's 25.609–42.328 µs range, so the transparency decision is
unchanged.

Unit coverage now pins named 64-bit boundaries, whole-record truncation,
FIFO bytes, alias wrap, whole-event drop counting and null handling. The full
tree builds; the common tracer suite and Python syntax/fixture checks are the
closure gates. Commands and the final trace live with the original comparison:
`PR-demo/mac_bisect_matrix/captures/i107_perf_compare_20260814_174331/README.md`.
