# #106 — per-PID perf isolation and private-trace equivalence

2026-08-12. Phase A and Phase B were authorised; Phase C was not.

## Load-bearing result first

Phase A is **RED before target capture**. The remapped
`/run/uprobe_events` is sufficient to register a userspace probe, but this
installed `perf` must also read that probe's `events/<group>/<event>/id` and
`format` files. AppArmor denies their canonical tracefs paths. Consequently
`perf record` cannot name or open the event, no selected-PID sample was
captured, and neither the cross-pod negative control nor trace-loss check has
been reached.

Phase B is complete as an inventory, not as an equivalence proof. All 34
private call sites are accounted for below. DWARF exposes a credible direct
mapping for 21. Thirteen contain a computed helper result, dynamically indexed
field, or return value which is not exposed as a scalar at the candidate
source line. Those thirteen remain unresolved until actual probes can be
recorded. No event is silently dropped and no Phase-C comparison is justified
by this audit.

> **Superseding owner correction, 2026-08-12:** Phase B is **RED**, not
> merely an inventory awaiting runtime work. #106 required an exact mapping
> for every retained event. Thirteen unresolved mappings fail that acceptance
> criterion, including load-bearing frame and encode identities. Phase C is
> cancelled. The private tracer is not retired; #107 reopens its PR scope.
> This correction supersedes the "Next gate" below while preserving the
> original record.

## Phase A — observed boundary

Environment:

* kernel `7.0.0-27-generic`, perf `6.12.96`;
* outer Incus PID namespace `pid:[4026532774]`;
* UID map maps container UID 0 to physical-host UID 1000;
* `kernel.perf_event_paranoid = 2` and
  `kernel.unprivileged_bpf_disabled = 2`;
* tracefs mounted `rw,nosuid,nodev,noexec`;
* `dynamic_events`, `trace`, and `trace_pipe` remain owned by unmapped host
  root and are unreadable;
* `uprobe_events` is the only delegated inode, host UID 1000 / container UID
  0, mode `0600`.

The canonical and remapped control paths are the same object:

```
/run/uprobe_events                 tracefs device 14, inode 68
/sys/kernel/tracing/uprobe_events  tracefs device 14, inode 68
```

The canonical path is denied by the Incus AppArmor policy. The remapped path
is readable and writable. `perf probe --target-ns <pod-xrdp-pid>` resolved
`xrdp_listen_conn_in` at file offset `0x13cd0` in deployed Build ID
`96aa2eeedf29b89225ce694629d1c5a4ee99e4f2`. Writing perf's generated
definition through `/run/uprobe_events` created `x106:xrdp_conn_in`.

The next required operation failed:

```
perf record -p <pod-xrdp-pid> --no-inherit --all-user \
    --timestamp --clockid mono -e x106:xrdp_conn_in ...

event syntax error: 'x106:xrdp_conn_in'
can't access trace events
No permissions to read
/sys/kernel/tracing/events/x106/xrdp_conn_in
```

The event's `id`, `format`, `enable`, and `filter` files were all owned by
unmapped host root and unreadable at the canonical path. `perf` has no
alternate-tracefs-root option in this build; its `--debugfs-dir` option did
not redirect tracefs discovery.

Negative controls reached before the stop:

* `perf record -a --all-user -e cycles` was denied at paranoid level 2;
* `trace`, `trace_pipe`, and `dynamic_events` were unreadable;
* the physical-host process namespace was not exposed by `/proc` (an
  owner-named live PID was not supplied, so the explicit PID test remains
  outstanding).

The first temporary definition was removed. A fresh disabled
`x106:xrdp_conn_in` definition against outer PID 2473 was then left registered
so the physical host can read its numeric event ID. No recorder or tracefs
sink is enabled. The ID permits one last no-new-grant check using perf's raw
`tracepoint/config=<id>/` selector; it will distinguish an event-metadata path
problem from paranoid level 2 denying the per-PID event itself. Delete the
definition through `/run/uprobe_events` after that check. These controls do
not turn Phase A green: the positive selected-PID capture is a prerequisite
for interpreting them.

**Cleanup note, 2026-08-12:** a later check found no `x106` definition or
event directory. The disabled probe described above is no longer registered.

## Phase B — exact 34-callsite manifest

The audit used the deployed symbolized binary above and the local dev binary
Build ID `3e06a41b0d211b9e8549990b36930ee2d64935fe`. Representative
`perf probe -V` checks on both builds exposed the same variables and the same
optimized/inlined shapes. `candidate` below means the required boundary and
scalars are visible to DWARF; it does **not** mean runtime-equivalent.
`unresolved` means the exact private payload is not available at that source
line without another independently verified probe and identity join.

| # | private event and source | machine action / private payload | external mapping audit | status |
|---:|---|---|---|---|
| 1 | `outfirst`, `xrdp_encoder_ffmpeg.c:891` | first encoded byte read; sequence, child view, byte count, monitor | internal line in `drain_stdout`; `self`, byte count and monitor exist, but `trace_seq_front(self)` is an inlined dynamic queue lookup | unresolved |
| 2 | `feedend`, `xrdp_encoder_ffmpeg.c:993` | last raw byte entered pipe; sequence, child view, monitor | internal line in `feed_vmsplice`; identity is the same unavailable dynamic queue-front lookup | unresolved |
| 3 | `dmg`, `xrdp_encoder.c:933` | damage surface/count/bounding box | line 932 exposes all six scalars (`surface_id`, `num_rects`, `bx1..by2`) | candidate |
| 4 | `enc` AVC420, `xrdp_encoder.c:1686` | submit/return sequence, ready state, inflight, centre luma | sequence/pair/centre are visible; ready and inflight are computed results not live at every emitted address | unresolved |
| 5 | `enc` AVC444, `xrdp_encoder.c:2222` | same schema for pair path | sequence/pair/centre are visible; exact ready/inflight payload is not | unresolved |
| 6 | `auxdue`, `xrdp_encoder.c:2466` | monitor, decision, two elapsed clocks, refresh bound | `mon`, `now`, and `self` are visible; two values use arrays dynamically indexed by `mon` | unresolved |
| 7 | `absorb`, `xrdp_encoder.c:2550` | captured frame id and monitor whose input was consumed | frame id is visible; monitor is `set_mon[index]`, a dynamic array lookup | unresolved |
| 8 | `subm_beg`, `xrdp_encoder.c:2789` | begin parsing and handing a set to children; set count | source-line boundary and `set_n` are visible | candidate |
| 9 | `submit`, `xrdp_encoder.c:2834` | one accepted frame id and monitor | monitor/sequence are visible; frame id is computed by parsing `set[index]` | unresolved |
| 10 | `subm_end`, `xrdp_encoder.c:2845` | end submit pass; armed-handle count | source-line boundary and `n_handles` are visible | candidate |
| 11 | `pump_beg`, `xrdp_encoder.c:2870` | begin waiting for all ffmpeg children; count/masks/credit | all five nonzero fields are visible; `perf probe -D` produced concrete register/stack fetches | candidate |
| 12 | `pump_end`, `xrdp_encoder.c:2874` | child wait complete; count/armed/masks/credit | all fields including return-dependent `kids_armed` are visible | candidate |
| 13 | `book_beg`, `xrdp_encoder.c:2884` | begin counters and human-rate E4 logging | boundary and `n_handles` visible | candidate |
| 14 | `batch`, `xrdp_encoder.c:2905` | cycle/count/armed/max/status summary | all six fields visible | candidate |
| 15 | `book_end`, `xrdp_encoder.c:2909` | counters/logging complete | adjacent optimized address with `n_handles`; exact address must be selected by runtime count equality | candidate |
| 16 | `coll_beg`, `xrdp_encoder.c:2942` | begin one monitor's NUT pop and LTR rewrite | entry of `gfx_batch_collect_one(self, ff, mon)` supplies the same boundary and monitor | candidate |
| 17 | `coll_end`, `xrdp_encoder.c:2944` | that monitor's pop/rewrite complete | return of `gfx_batch_collect_one`, paired by thread and monitor | candidate |
| 18 | `rel_beg`, `xrdp_encoder.c:2947` | begin releasing consumed capture slots | entry of `gfx_batch_release_slots(..., set_n)` | candidate |
| 19 | `rel_end`, `xrdp_encoder.c:2949` | slot-release pass complete | return of the same function | candidate |
| 20 | `emit_beg`, `xrdp_encoder.c:3769` | begin building/queuing one frame's EGFX PDUs; frame id, monitor | `pf_id` is visible, but monitor is `set_mon[index]`; a `process_enc` entry mapping still needs an identity proof | unresolved |
| 21 | `emit_end`, `xrdp_encoder.c:3771` | that frame's EGFX assembly/queueing complete | same unresolved monitor join at the return boundary | unresolved |
| 22 | `wait_beg`, `xrdp_encoder.c:3854` | worker has no frame and begins blocking for work | source-line boundary and wait state visible | candidate |
| 23 | `wait_end`, `xrdp_encoder.c:3861` | producer or termination event woke worker | source-line boundary; pair on worker TID, not wall-time proximity across threads | candidate |
| 24 | `drain_beg`, `xrdp_encoder.c:3898` | begin removing queued frames under encoder mutex | nearby line 3896 exposes queue arrays/state and boundary | candidate |
| 25 | `drain_end`, `xrdp_encoder.c:3912` | queue removal complete; item count/full flag | both fields visible | candidate |
| 26 | `take`, `xrdp_encoder.c:3926` | one explicit frame id removed from FIFO; remaining depth | depth visible; frame id is parsed from `items[index]` by a helper inside the trace argument | unresolved |
| 27 | frontier `ackregion`, `xrdp_mm.c:1750` | producer region frontier plus server/consumed/client/window | `plan` and `encoder` visible; fields are direct DWARF member fetches | candidate |
| 28 | `ackslot`, `xrdp_mm.c:1766` | producer slot frontier plus same state | `encoder` visible and plan-slot value has a candidate address | candidate |
| 29 | legacy `ackregion`, `xrdp_mm.c:1807` | legacy producer ack plus server/consumed/client | `encoder` visible; direct member fetches | candidate |
| 30 | `cliack`, `xrdp_mm.c:1950` | client frame ack/queue/decode plus server/off state | arguments and `encoder` visible | candidate |
| 31 | `send`, `xrdp_mm.c:4329` | EGFX chunk bytes/last/frame plus server/client/in-flight | `enc_done` and `self` visible; direct member fetches | candidate |
| 32 | `egress`, `xrdp_mm.c:4400` | last transport byte queued; frame/display/pending KiB/client | frame/display/client visible; pending KiB is a helper return and the exact line is address-collapsed | unresolved |
| 33 | `msgin`, `xrdp_mm.c:5161` | producer frame arrived; parsed frame id and data bytes | command/data arguments visible; frame id is a validated helper parse rather than a scalar | unresolved |
| 34 | `enq`, `xrdp_mm.c:5214` | frame entered encoder FIFO; parsed frame id and depth | command/depth visible; same validated helper result is not directly exposed | unresolved |

## What Phase B decides

The broad timing decomposition is externally plausible: submit, child wait,
bookkeeping, collect/rewrite, slot release, worker wait, and FIFO drain all
have usable candidate boundaries. That does **not** yet replace the private
trace. The encode-window identity (`feedend`/`outfirst`) and three explicit
frame chains (`msgin`/`enq`/`take`, submit/absorb, and emit) include unresolved
identities. Those are precisely the fields which prevent time-window pairing.

Possible helper entry/return probes are hypotheses for those rows, not an
accepted manifest. They must demonstrate an explicit identity join and exact
event counts at runtime; nearest-event or caller-time joins are forbidden.

## Next gate

Do not run Phase C. First make the selected event's metadata available to the
same standard `perf record` process without exposing `trace`, `trace_pipe`,
other event groups, host PIDs, or system-wide recording. Then rerun the Phase-A
positive and negative controls. If that cannot be done at paranoid level 2,
#106 remains RED and the safe standard alternative is a physical-host-side
per-PID recorder, as the backlog already states.

**Superseded 2026-08-12:** no further permission or host-recorder work is
needed to decide equivalence. Phase B already failed the exact-mapping gate,
so the physical-host recorder cannot make the proposed replacement complete.
