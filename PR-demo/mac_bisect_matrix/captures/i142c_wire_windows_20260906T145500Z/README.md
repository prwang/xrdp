# Windows comparison: login failure at an odd desktop height

Collected 2026-09-06, without changing either deployment or tester session.
The directory timestamp is a run label, not the RDP connection start time.

## Observation and identity

The owner reports partial desktop on connect and hangup at the first resize
on port 40061; port 40060 connected normally and survived twenty resizes.
The server logs contain exactly twenty completed development resizes.

* Development: port 40060, pod xrdp-x044-77bf89df47-rd44z, xrdp
  `655639d8270c`, xorgxrdp `baf9658c397d`, Windows connection PID 1637.
* Clean-room diagnostic: port 40061, pod xrdp-x045-798bb968d4-wcl2m,
  xrdp `50a974b6e56c`, xorgxrdp `aca3c774cb8b`, connection PID 1488.
* `installed-packages.txt` records the actual `*-dev` package versions.
  The initial `packages.txt` queries for uninstalled unsuffixed names returned
  blank versions; those are not installation identities.
* Both use Ubuntu 24.04 XFCE, CPU libx264, `auto` selecting AVC444v2,
  dense chroma, wire window 1, and semantically identical parsed `gfx.toml`.
  They are not the sparse/window-2 performance profiles.
* Both enable the same logical-wire trace. Clean-room additionally enables
  GFX/ACK perf events and its existing lifecycle diagnostics; development does
  not. This is not a trace-overhead-controlled performance comparison.
* Login geometry differs: clean-room 2360x1033, development 2288x1033.
  The heights match and are odd, but these are not byte-identical workloads.

`pod.json`, `installed-packages.txt`, copied configuration, session/Xorg logs,
and process inventory retain deployment context. Both pods have zero restarts.
`logs.tar` is the original collection transport; its extracted paths are
retained for direct inspection. Other PID trace files predate this Windows
comparison and must not be pooled into its results.

## Collection limitation and recovery

Clean-room exited and flushed `clean/traces/xrdp.1488`. Development stayed
alive: `dev/traces/xrdp.1637` was empty because `common/perf_trace.c` drains
into a fully buffered 1 MiB stdio stream and flushes at close/full buffer,
not on every sink pass. An empty disk file is not evidence of no events.

Recovered `dev/traces/xrdp.1637.buffer-snapshot` by read-only `pread` through
`dd if=/proc/1637/mem`, inside the development pod, from the identified trace
buffer at `0x79b898616010`, bounded to 1048576 bytes. The mapping was the
1 MiB malloc mapping at `0x79b898616000`; its first bytes were the expected
`schema=1 ... event=clock_base` for PID 1637. Only its NUL-terminated text
prefix was retained, after checking the first record and final newline.
No debugger attach, signal, inferior function call, memory write, logoff,
config change or process restart was used. This address is instance-specific,
not a reusable collection command. The snapshot SHA256 is
`431b30e8aa3760fe7ba1f1181f55556d70775dfc7811656cf394ec35838fcc18`.

The retained prefix is 481909 bytes, 2648 parseable records. Every outbound
sequence 1..1422 and inbound sequence 1..346 is present. A later read had a
different hash as the active session continued receiving events; this is a
point-in-time prefix, not a finalized trace or a zero-drop certificate.
Drop diagnostics are emitted at close, so none observed in this live prefix
does not prove that every ancillary worker event was retained. All transaction
sequence continuity checks passed. The clean-room completed file has 917
records, sends 1..708, receives 1..12, and no perf failure/drop diagnostics.

## Transaction evidence

Run from `/work`:

```sh
timeout 20s python3 PR-demo/mac_bisect_matrix/i142_wire_transition.py \
  clean=PR-demo/mac_bisect_matrix/captures/i142c_wire_windows_20260906T145500Z/clean/traces/xrdp.1488 \
  dev=PR-demo/mac_bisect_matrix/captures/i142c_wire_windows_20260906T145500Z/dev/traces/xrdp.1637.buffer-snapshot
```

Clean-room sent 708 logical graphics commands, received four frame ACKs and
two capability advertisements. Development's snapshot has 1422 sends, 172
frame ACKs and exactly one advertisement. Counts describe these different
session durations, not rates or throughput ratios.

1. Clean-room login graphics end at send 683, frame ID 1. The client ACKs
   that transaction before the desktop arrives.
2. First Xorg capture has one damage rectangle `(0,0)-(2360,1033)`.
   Sends 684..687 carry STARTFRAME, AVC444v2 luma, AVC444v2 chroma, ENDFRAME.
   They reuse frame ID 1; both surface writes target `(0,0)-(2360,1033)`.
   Payload lengths are 14208 and 9850 bytes, respectively. All sends succeed.
3. No ACK arrives for this desktop transaction. The client sends another
   capability advertisement 10.913715 ms after send 687, with the same nine
   version/flag sets as its initial advertisement. This is before any resize.
4. The handler confirms, resets and recreates surface 0 (sends 688..691),
   replaces a live encoder without retiring it, and requests no full Xorg
   repaint. The next damage covers only `(1086,0)-(2251,1031)`; its frame 2
   is sent at 692..695 and acknowledged. Unlike the earlier zero-frame wedge,
   this run did get a partial repaint. This accounts for a partially filled
   replacement surface, not a completely absent video stream.
5. The first resize is 2290x1033. The current encoder is deleted; surface 0
   is deleted, reset, recreated and mapped at 696..699. Full capture then
   produces two frames at 700..707, neither acknowledged. The log records a
   graphics channel close response and then unexpected TLS EOF. Send 708 is
   cleanup after the close and fails; it is not the preceding trigger.

Development's first desktop transaction at 667..670 has the same command
ordering, surface 0, codec 15, LC=1 then LC=2, and frame ID 1 reused after
login. It receives the frame ACK after send 670, then continues through all
twenty resizes without another advertisement. Thus frame-ID reuse and the
luma/chroma command ordering alone do not distinguish this failure.

Use monotonic perf times for intervals. The human log's wall timestamps move
backward during the second capability callback, so subtraction of adjacent
wall-clock strings is not a valid duration measurement here.

## Concrete serializer difference and historical cross-check

Clean-room `xrdp_encoder.c:avc_build_region()` rounds rectangle extents upward
to even values and clips them to the padded coded width/height. Its caller
`ffmpeg_emit_frame()` nevertheless creates a visible-size client surface and
uses visible-size destination bounds. Development
`out_RFX_AVC420_METABLOCK()` rounds outward but clips to that visible
destination. It also expands damage by one pixel before rounding, which is
another inventoried difference; it does not change this full-screen example.

`PR-demo/lib/i142_metablock_bounds_probe.sh` compiles both actual serializers
from their existing canonical/clean-room trees. It does not copy their logic.
The driver gives a one-byte dummy encoded payload solely to inspect metadata;
this is not a video decoding test. `metablock-bounds.txt` retains the output:

| Visible desktop / full damage | Development region bottom | Clean-room region bottom |
|---|---:|---:|
| 2360x1033, today's failure | 1033 | 1034 |
| 1820x1171, earlier failure | 1171 | 1172 |
| 1820x1202, preceding successful resize | 1202 | 1202 |

Each value is the serialized rectangle's exclusive bottom coordinate, not a
pixel sample or coded video height. At the two failing heights, clean-room
describes one row beyond the visible client surface. Development does not.
The helper's initial compile failed because its driver omitted config_ac.h;
adding that required include made both real serializer builds and all six
calls succeed. No production source or pre-existing test assertion changed.

The earlier retained Windows capture
`i142d_x045_windows_repro_20260828T002619Z` starts at 1756x1206, then completes
six resizes to 1800x1206, 1792x1206, 1814x1206, 1784x1206, 1820x1206 and
1820x1202 without replacement capabilities. The first odd-height resize,
1820x1171, is immediately followed by replacement. Today's odd-height login
therefore fits the same mechanism without needing a previous resize.

The production difference is proven; Windows rejecting it is the strongest
current causal hypothesis, not yet a controlled client verdict. The bounded
wire tracer records the number of region rectangles but not their coordinates
inside the video metadata. The reported 1034/1172 values are independently
reproduced serializer outputs using captured geometry, not directly sampled
bytes from those old packets. Full H.264 bodies were not captured either.

## Specification/audit defect and next gate

`PRD/slices/135-avc-wire-serialization.md` S135-R4 explicitly says even
extents clipped to coded bounds. Clean-room's edge test asserts that rule;
it has no separate visible-surface input. Development implements visible
clipping instead. This is a concrete missed divergence that invalidates the
earlier equivalence claim, and a normative inconsistency to resolve rather
than silently calling either implementation fully compliant.

Before another production port, extend the same bounded wire inspector to
record region bounds (at least the first rectangle for these one-rectangle
captures), and pin a deterministic odd-visible/even-coded serializer check.
Then a narrowly documented canonical-dev red comparison can reproduce the
coded-bound behavior and the predicted odd-height client transition. The
matching visible-bound correction must be validated in development before
any owning-slice repair. Do not modify the callback to hide the precursor.
If odd-edge overflow does not reproduce the client transition, keep the
client trigger RED and investigate the actual encoded bytes; do not declare
the callback repair or this source discrepancy a complete EOF fix.

The offline analyzer was corrected to use the most recent ACK by time, not
the numerically highest reused frame ID, and to show only the immediately
preceding STARTFRAME..ENDFRAME transaction. Its synthetic reused-ID test passes,
as does its capability-confirmation command-ID test (2/2). No new live test
was run and no production deployment was altered in this turn.
