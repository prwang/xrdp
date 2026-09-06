# External-FFmpeg AVC Backend Product Requirements

This directory is the sole normative specification for the paired xrdp and
xorgxrdp clean-room implementation. `BACKLOG.md` tracks open work and may name
the development-tree files used as inventory. Experiment records explain how
decisions were reached. Neither is a substitute for the requirements here.
`slices/` owns the requirements for each clean-room commit; `gates/` owns
cross-tree qualification which does not itself append a product commit.

The clean-room series is based on xrdp
`fe850a22c08a624c66bbac07e310251782e6f828` and xorgxrdp
`49bf2dd3546dc48b9d5bae62022762fde11793d0`. A slice may not change either
base, its dependencies, or its activation boundary without first changing
this specification.

## Product goal

xrdp shall provide an optional RDPGFX H.264 backend that invokes a stock
`ffmpeg` executable rather than linking libavcodec. It shall support AVC420,
AVC444v1 and AVC444v2, multiple monitors, resize/restart, bounded resource use,
and Windows MSTSC interoperability. The xorgxrdp producer and xrdp consumer
shall share one versioned capture contract.

For AVC444, xorgxrdp packs the final main and auxiliary NV12 wire views into
per-monitor shared-memory slots. One xrdp encoder worker owns one main child
and one auxiliary child per monitor, pumps all children in one poll set,
normalizes their Annex-B output, and emits one luma LC=1 PDU followed by an
optional chroma LC=2 PDU. AVC420 uses the main view and one child. LC=0 is not
part of this design and shall never enter the encoded history.

## Normative language and precedence

`shall` and `must` are requirements; `shall not` and `must not` are
prohibitions. Each slice file owns its functional behavior, file boundary and
tests. This README owns cross-cutting requirements. If two slice documents
conflict, the later slice may extend an earlier one only where it says so
explicitly; otherwise the earlier invariant remains in force.

The development branches are evidence about the intended final behavior, not
an implementation dependency. A clean-room author shall implement the
behavior described here and shall not transplant commit history. Development
and clean-room trees are independent implementations of this same normative
contract: implementation shape may differ, but externally observable behavior,
failure policy, bounds, defaults and operator surface may not. A behavior which
exists in only one tree is a red equivalence result until it is either
implemented in the other tree or shown to be non-normative residue and removed.

## Clean-room authorship and review standard

Before slice #126 starts, every qualification which can change the selected
product behavior shall be closed and this specification shall contain the
result. An open backlog item or experiment record may not be a hidden input to
the clean-room author. The normative README and slice files must be sufficient
to implement and review the complete series from the pinned bases.

Each slice shall be re-derived from its normative behavior and the pinned-base
seams. Development-tree paths listed in `BACKLOG.md` are an inventory used to
check that no responsibility was omitted; they are not text to transplant. A
slice shall not copy development functions, comments, tests, configuration or
manual prose line by line. Every retained branch, field, interface and
assertion shall be explainable from this specification or unchanged base
behavior. Code needed only by a later slice shall wait for that slice.

The clean-room diff shall contain only current, reviewable product prose:

* Comments explain non-obvious protocol meaning, ownership/lifetime,
  concurrency ordering, bounds, security, compatibility or failure behavior.
  They do not restate syntax or narrate project history.
* Production code, tests and public surfaces do not contain backlog numbers,
  slice requirement IDs, experiment or fleet-arm names, dates, commit hashes,
  `FR-*` shorthand, temporary diagnostics, fault injection, dead alternatives
  or plans for later work. Tests and fixtures are named for the behavior they
  prove.
* Installed configuration is a concise valid starting point. It shows defaults,
  accepted values, dependencies and material tradeoffs without advertising
  removed or inert keys. The manual is the complete reference. Runtime and
  help text state the operator impact and action in domain language.
* Historical rationale belongs in the commit message or evidence record, not
  beside shipped code. A measurement claim may enter product documentation
  only when its retained evidence is admissible and the claim is still needed
  by an operator.

A slice is not self-contained if a reviewer needs the development branch to
decode a name, justify a condition or understand the commit message. Its commit
message shall state the outcome, why the change belongs at this boundary and
the independent gates which passed; a work-item label or copied-file list is
not a rationale.

The pinned xrdp base already contains dynamic-virtual-channel dechunking,
overflow-safe stream bounds, and corrected dynamic-resize ordering. The
clean-room series shall add to those implementations: it shall not restore the
old inline dechunker, weaken `trans_force_read_s()` bounds, remove the base
tests, or create RDPGFX surfaces/encoders before resizing the screen bitmap.
Base files that overlap a slice shall retain these changes and their tests.

## Cross-cutting requirements

### Compatibility and activation

* Existing x264, OpenH264, RFX and uncompressed behavior shall be unchanged
  while the new backend is absent, disabled, unavailable or incomplete.
* New capture structures shall carry an exact version. Mismatched paired
  versions shall fail before shared memory is interpreted.
* No slice before #142 shall advertise, negotiate or select the new backend.
  Incomplete code may be reached only by its deterministic unit tests.
* Capability selection is immutable after RDPGFX confirmation. Probe failure
  shall remove the backend before confirmation; it shall not trigger a codec
  switch or a fallback inside an established connection.
* AVC444v1 uses codec ID `RDPGFX_CODECID_AVC444`; AVC444v2 uses
  `RDPGFX_CODECID_AVC444v2`. AVC420 uses `RDPGFX_CODECID_AVC420`.
* `auto` is the normal operator mode and shall prefer AVC444v2 whenever the
  client advertises it. AVC444v1 remains implemented for clients whose
  advertised capability permits v1 but not v2. Forced `444v1` is a legacy
  interoperability and diagnostic control, not a generally recommended
  deployment mode; no post-confirmation fallback may hide a client-specific
  rendering defect.

### Color, geometry and ownership

* The source is full-chroma XRGB8888. The normative conversion is full-range
  BT.709 and the two Microsoft AVC444 views reconstruct the visible source.
* Visible rectangles may have odd coordinates and sizes. Wire metablock
  origins shall round down to even coordinates and right/bottom edges round
  up to even coordinates, then clip to the visible destination bounds.
  An extent may remain odd only where clipping meets an odd visible edge.
  Coded-video padding shall not enlarge the visible destination or authorize
  metadata outside it.
* Coded dimensions shall cover visible dimensions and satisfy the selected
  width alignment of 16 or 32 pixels. Padding shall replicate the nearest
  visible edge; unwritten or uninitialized padding is forbidden.
* Every monitor owns a disjoint shared-memory region and exactly two capture
  slots. A slot is not reusable until its ownership acknowledgement is sent.
* All byte counts, offsets, strides and aggregate allocation sizes shall be
  checked for zero, negative input, overflow and configured limits before use.

### Process and bitstream behavior

* Encoder processes shall be started with an argv vector; no shell command is
  constructed. Descriptors shall be close-on-exec except the intended child
  endpoints, nonblocking where the pump requires it, and closed on every
  failure path.
* Input uses a bounded stock-ffmpeg rawvideo/NUT pipeline. Output is accepted
  only after the bounded NUT parser and Annex-B validator associate exactly
  one access unit with its submitted view and frame identity.
* Startup/reset output shall contain the parameter sets required by the
  selected profile. Malformed, truncated, duplicate or policy-incompatible
  output shall fail loudly. No automatic codec fallback is permitted.
* Main pictures shall never reference auxiliary pictures. The optional LTR
  mode shall preserve independent main and auxiliary reference chains across
  frame-number wrap, re-key and sparse auxiliary cadence.
* Resize or geometry change shall stop submission, close pipes, terminate and
  reap all old children, discard partial state, allocate the new layout, and
  require a new validated reset before output resumes.

### Scheduling and backpressure

* One encoder worker shall pump all monitor children. There is no per-monitor
  pump thread and no assembly or emit thread.
* Slot ownership and visible-region completion are separate frontiers. Slot
  release may be eager; visible damage shall not be retired before the client
  acknowledgement that covers it.
* The shipped settings are `eager_slot_ack=true` and `wire_window=1`.
  Outstanding work shall be bounded by `wire_window + 2 * monitor_count`.
* Sparse auxiliary scheduling shall skip submission before the auxiliary
  encoder can advance. With a nonzero refresh interval, the maximum chroma
  gap during continuing updates is that interval plus one frame period. A
  final main-only update shall arm a one-shot trailing full capture after the
  idle interval so static regions return to current 4:4:4 even when the
  application produces no later damage.

### Security, diagnostics and failure

* Client data, encoder bytes, configuration, monitor geometry and environment
  are untrusted. Validate before allocation, arithmetic, parsing or formatting.
* Configuration errors and required host limitations shall be reported and
  shall disable or refuse the backend; they shall not be silently clamped or
  masked with another codec.
* Human-rate lifecycle and error messages use normal logging. Per-frame or
  per-monitor telemetry shall use the performance tracer only.
* The tracer is a compile-time opt-in. A default build shall contain no trace
  symbol, string, state, branch or argument evaluation. An enabled build uses
  a versioned restricted `key=value` text record in a per-thread byte ring,
  with a separate sink thread. JSON, a fixed binary object format and a
  one-off offline decoder are not required interfaces.
* Trace files shall be newly created mode 0600 without following symlinks.
  Initialization is explicit after fork; shutdown quiesces producers, joins
  the sink and drains complete records. Open, write and drain failure is
  visible once at human rate.
* The tracer observes only explicit events in its xrdp process and writes only
  its configured local file. It shall not attach to host processes, mount or
  read tracefs, use system-wide perf/BPF facilities, open a network sink or
  expose records outside the process/container namespace.
* Evidence shall join events by explicit frame, monitor and surface identity,
  never by nearest timestamp. Trace measurements containing a synchronous
  per-frame logger are invalid.

## Excluded behavior

The clean-room series shall not include a linked libavcodec path, custom
encoder daemon, shell-spawned command, automatic fallback, global capture-slot
pool, LC=0 AVC444 history, per-frame normal logging, an emit thread,
`tail_flush`, `fault_aux_delay`, `fault_strip_mmco`, or benchmark/deployment
scaffolding. `strip_sei`, `sanitize_hrd` and `strip_pic_struct` are bounded
interoperability policies and are not fault injection.

## Slice and commit contract

Each row is exactly one proposed clean-room commit. A slice is complete only
when its own targeted tests and every gate available at that point are green.
A red slice shall not be committed, and work shall not proceed on top of it.
Slices are authored strictly in numeric order from #126 through #142; the
dependency column records the narrower functional inputs, but does not permit
parallel or out-of-order commits.

| item | normative slice | dependency | first activation |
|---|---|---|---|
| #126 | [Shared-memory isolation](slices/126-shmem-isolation.md) | bases | latent AVC420 fix |
| #127 | [Performance trace foundation](slices/127-perf-trace-foundation.md) | bases | trace build only |
| #128 | [Capture wire contract](slices/128-capture-wire-contract.md) | #127 | no backend |
| #129 | [View construction](slices/129-view-construction.md) | #128 | no backend |
| #130 | [NUT demuxer](slices/130-nut-demuxer.md) | bases | parser only |
| #131 | [Annex-B policy](slices/131-annexb-and-parameter-policy.md) | bases | parser only |
| #132 | [Capability classifier](slices/132-capability-classifier.md) | bases | pure policy only |
| #133 | [FFmpeg runner](slices/133-ffmpeg-runner.md) | #129, #130, #131 | test-only runner |
| #134 | [Inactive encoder integration](slices/134-inactive-encoder-integration.md) | #129, #132, #133 | internal only |
| #135 | [Wire serialization](slices/135-avc-wire-serialization.md) | #134 | internal only |
| #136 | [Two-slot capture](slices/136-two-slot-capture.md) | #128, #129 | internal only |
| #137 | [Reference-safe topology](slices/137-reference-safe-topology.md) | #131, #133, #135 | internal only |
| #138 | [LTR, re-key and intra refresh](slices/138-ltr-rekey-intra.md) | #137 | internal only |
| #139 | [Multi-monitor pump set](slices/139-multimon-pump-set.md) | #136, #138 | internal only |
| #140 | [Credit frontier](slices/140-credit-frontier.md) | #136, #139 | internal only |
| #141 | [Sparse chroma](slices/141-sparse-chroma.md) | #138, #140 | internal only |
| #142 | [Activation and documentation](slices/142-activation-and-docs.md) | #126–#141 | selectable backend |

After the assembled series, the
[development/clean-room equivalence gate](gates/142c-dev-cleanroom-equivalence.md)
must pass before #142 can close. It is a qualification gate over the complete
series, not another product commit and not permission to append a repair after
the final slice.

## Gate contract for every slice

The slice document names its exact target files and targeted suite. In
addition, each commit shall pass these gates from a clean tree:

1. `git diff --check` in every changed repository.
2. xrdp: `./bootstrap`, `./configure`, `make -j2`, and `make check`.
3. xorgxrdp when changed: `./bootstrap`, configure with the matching xrdp
   headers, `make -j2`, and `make check`.
4. Paired slices: build both repositories against the same versioned
   `common/xup_client_info.h` contract.
5. Each xrdp commit: `scripts/run_astyle.sh -v 3.4.14`; it shall produce no
   diff. xorgxrdp does not carry this script or use xrdp's whole-tree formatter;
   applying it there would rewrite unrelated pinned-base code. Review every
   changed xorgxrdp hunk against its surrounding repository style instead.
6. Each xrdp commit: `scripts/run_cppcheck.sh -v 2.20.0`. xorgxrdp has no
   corresponding repository cppcheck gate; its changed code remains covered
   by its clean compiler build, tests, `git diff --check` and the diff review
   in item 8.
7. From #127 onward, create a second clean xrdp build configured with
   `--enable-perf-trace` and repeat `make -j2` and `make check`. The ordinary
   build shall also pass #127's disabled-footprint test. Performance tracing
   is private to the xrdp process and shall not change
   `common/xup_client_info.h`, its version, or the xorgxrdp build. From #128
   onward, paired slices shall verify that xorgxrdp builds against the exact
   checked-out header independently of whether the paired xrdp binary enables
   tracing. A genuine xup header-version mismatch remains a hard runtime
   rejection; a trace-enabled/disabled pairing is not a wire mismatch.
8. Audit the complete slice diff against the authorship and prose standard
   above. Every new production comment and user-visible string shall have a
   current purpose; internal work labels, development archaeology, dead keys
   and later-slice scaffolding make the gate red.

Tests shall use specification-derived expected values or independent fixtures.
Expected values shall not be copied from the implementation. A behavior test
may be changed only in a separate, acknowledged specification change.
