# External-FFmpeg AVC Backend Product Requirements

This directory is the sole normative specification for the paired xrdp and
xorgxrdp clean-room implementation. `BACKLOG.md` tracks open work and may name
the development-tree files used as inventory. Experiment records explain how
decisions were reached. Neither is a substitute for the requirements here.

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
behavior described here and shall not transplant commit history.

The pinned xrdp base already contains dynamic-virtual-channel dechunking,
overflow-safe stream bounds, and corrected dynamic-resize ordering. The port
shall add to those implementations: it shall not restore the old inline
dechunker, weaken `trans_force_read_s()` bounds, remove the base tests, or
create RDPGFX surfaces/encoders before resizing the screen bitmap. Base files
that overlap a slice shall retain these changes and their tests.

## Cross-cutting requirements

### Compatibility and activation

* Existing x264, OpenH264, RFX and uncompressed behavior shall be unchanged
  while the new backend is absent, disabled, unavailable or incomplete.
* New capture structures shall carry an exact version. Mismatched paired
  versions shall fail before shared memory is interpreted.
* No slice before #226 shall advertise, negotiate or select the new backend.
  Incomplete code may be reached only by its deterministic unit tests.
* Capability selection is immutable after RDPGFX confirmation. Probe failure
  shall remove the backend before confirmation; it shall not trigger a codec
  switch or a fallback inside an established connection.
* AVC444v1 uses codec ID `RDPGFX_CODECID_AVC444`; AVC444v2 uses
  `RDPGFX_CODECID_AVC444v2`. AVC420 uses `RDPGFX_CODECID_AVC420`.

### Color, geometry and ownership

* The source is full-chroma XRGB8888. The normative conversion is full-range
  BT.709 and the two Microsoft AVC444 views reconstruct the visible source.
* Visible rectangles may have odd coordinates and sizes. Wire metablock
  rectangles shall have an even origin and even extent and remain clipped to
  the coded surface.
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
  gap is that interval plus one frame period.

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

| item | normative slice | dependency | first activation |
|---|---|---|---|
| #210 | [Shared-memory isolation](slices/210-shmem-isolation.md) | bases | latent AVC420 fix |
| #211 | [Performance trace foundation](slices/211-perf-trace-foundation.md) | bases | trace build only |
| #212 | [Capture wire contract](slices/212-capture-wire-contract.md) | #211 | no backend |
| #213 | [View construction](slices/213-view-construction.md) | #212 | no backend |
| #214 | [NUT demuxer](slices/214-nut-demuxer.md) | bases | parser only |
| #215 | [Annex-B policy](slices/215-annexb-and-parameter-policy.md) | bases | parser only |
| #216 | [Capability classifier](slices/216-capability-classifier.md) | bases | pure policy only |
| #217 | [FFmpeg runner](slices/217-ffmpeg-runner.md) | #213, #214, #215 | test-only runner |
| #218 | [Inactive encoder integration](slices/218-inactive-encoder-integration.md) | #213, #216, #217 | internal only |
| #219 | [Wire serialization](slices/219-avc-wire-serialization.md) | #218 | internal only |
| #220 | [Two-slot capture](slices/220-two-slot-capture.md) | #212, #213 | internal only |
| #221 | [Reference-safe topology](slices/221-reference-safe-topology.md) | #215, #217, #219 | internal only |
| #222 | [LTR, re-key and intra refresh](slices/222-ltr-rekey-intra.md) | #221 | internal only |
| #223 | [Multi-monitor pump set](slices/223-multimon-pump-set.md) | #220, #222 | internal only |
| #224 | [Credit frontier](slices/224-credit-frontier.md) | #220, #223 | internal only |
| #225 | [Sparse chroma](slices/225-sparse-chroma.md) | #222, #224 | internal only |
| #226 | [Activation and documentation](slices/226-activation-and-docs.md) | #210–#225 | selectable backend |

## Gate contract for every slice

The slice document names its exact target files and targeted suite. In
addition, each commit shall pass these gates from a clean tree:

1. `git diff --check` in every changed repository.
2. xrdp: `./bootstrap`, `./configure`, `make -j2`, and `make check`.
3. xorgxrdp when changed: `./bootstrap`, configure with the matching xrdp
   headers, `make -j2`, and `make check`.
4. Paired slices: build both repositories against the same versioned
   `common/xup_client_info.h` contract.
5. `scripts/run_astyle.sh -v 3.4.14`; it shall produce no diff.
6. `scripts/run_cppcheck.sh -v 2.20.0`.
7. From #211 onward, create a second clean xrdp build configured with
   `--enable-perf-trace` and repeat `make -j2` and `make check`. The ordinary
   build shall also pass #211's disabled-footprint test. From #212 onward,
   perform the paired xorgxrdp build in both matching trace modes; mixed modes
   shall fail the version-agreement test.

Tests shall use specification-derived expected values or independent fixtures.
Expected values shall not be copied from the implementation. A behavior test
may be changed only in a separate, acknowledged specification change.

## Legacy requirement names

Older source comments use identifiers such as `FR-CAPTURE`, `FR-PROC`,
`FR-NUT`, `FR-H264`, `FR-WIRE`, `FR-ACK`, `FR-CONFIG` and `FR-TRACE`.
They map respectively to slices #212/#213/#220, #217/#223, #214,
#215/#221/#222, #219, #224, #226 and #211. The slice documents, not the old
identifier wording, are authoritative.
