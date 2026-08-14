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
