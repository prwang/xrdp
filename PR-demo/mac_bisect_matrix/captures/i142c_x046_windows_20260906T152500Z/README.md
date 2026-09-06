# Windows reproduces the odd-edge failure on canonical development

Collected 2026-09-06 after the owner reported: first resize good, second
resize black, third resize disconnected. The capture directory timestamp is
a collection label, not the connection start time. No deployment, session,
codec or graphics configuration was changed during collection.

## Result and scope

The controlled diagnostic development arm reproduces the predicted missing
frame acknowledgement and replacement-capability transition immediately after
its first visible-height overflow. This satisfies the named reproduction gate
in BACKLOG #142C. It does not validate a correction or prove every subsequent
transport/lifecycle defect has the same cause. Windows' internal rejection
reason is not directly available from this server-side capture.

Port 40062 is still the intentionally faulty reference: xrdp c729a50889a2,
xorgxrdp baf9658c397d, CPU libx264, automatic AVC444v2, dense chroma and wire
window 1. The preceding readiness capture proves its restored pipe precondition,
wire/decode certificate and final two-size rendered smoke. pod.json, installed-packages.txt and etc/xrdp/gfx.toml retain the deployment
identity and configuration. Ports 40060 and 40061 were untouched.

## What happened

The Windows connection is xrdp PID 1557. The initial visible desktop is
2056x944. The owner-reported sequence maps to these actual graphics surfaces:

* First resize: 1800x944. Writes 593/594 and 597/598 have inner bottom 944
  and outer bottom 944. Frames 13 and 14 are acknowledged. The owner saw a
  working desktop.
* Second resize: 1800x1085. Writes 605/606 (frame 15, LC=1 then LC=2) have
  inner bottom 1086 and outer bottom 1085. Both contain a single region and
  both send successfully. The first overflow therefore describes one row
  outside the visible surface; this is recorded serialized metadata, not
  inferred from padded encoder geometry. Frame 15 ends at send 607 and is
  never acknowledged. The next STARTFRAME (frame 16) is send 608, then Windows
  repeats all nine of its initial capability sets. The server confirms,
  resets and creates a replacement surface at 609..612. It sends no further
  pixel writes before the third resize. This is consistent with the owner's
  black-screen observation.
* Third resize: 1892x1085. Frames 17 and 18 again have inner bottom 1086
  outside outer bottom 1085 (writes 618/619 and 622/623). All those sends
  succeed, but neither frame is acknowledged. The log records unexpected
  TLS EOF; cleanup surface deletion at 625 fails afterwards. The owner saw
  the connection hang up.

The last frame acknowledgement is frame 14, after send 599. No frame ACK
arrives after the first overflow. The missing ACK plus repeated capabilities,
not simply the EOF, is the predicted client reaction. The only intended
production intervention relative to the working development control was removal
of final visible-bound clipping from external AVC444 metadata; its bounded
trace addition records the changed bytes. That intervention is sufficient to
reproduce the precursor on canonical development in this run.

## Integrity and reproducibility

The connection process had exited before the final hash check. Its closed trace
has the same SHA256 inside the pod and in this archive (trace.sha256 and
closed-trace.txt). The existing reader reports no perfdrop, perfformat or
perfnoring events. Outbound sequences 1..625 and inbound sequences 1..35 are
continuous with no missing/duplicated records. The six overflowing surface
writes all follow the even-size acknowledged control. Unrelated probe and old
pod traces are archived but excluded from this verdict.

Run from /work:

```sh
timeout 15s python3 PR-demo/mac_bisect_matrix/captures/i142c_x046_windows_20260906T152500Z/audit.py
```

The audit uses the existing reader, verifies sequence continuity and prints the
complete transition tail. audit.txt retains its output. transition.txt retains
the ordinary i142_wire_transition.py result. That generic summary only prints
the latest started transaction: here this is the empty frame-16 STARTFRAME,
not the preceding completed overflowing frame 15. It must not be interpreted
as saying that frame 16 carried the malformed region. The complete tail in
audit.txt resolves that limitation without changing the existing analyzer.

The raw log uses human-rate lifecycle timestamps only. No performance claim
or wall-clock interval is derived here. The ordinary analyzer's relative time
is from the next STARTFRAME; it is not reported as latency from the overflow.

## Next gate

Preserve c729a50889a2 and this immutable deployed reference. The previously
conditional forward restoration of visible clipping and normative S135-R4
correction can now proceed on canonical development. A corrected build still
needs its own source gates and the same Windows odd-height sequence before
claiming green or re-authoring the clean-room correction. No implementation,
assertion, specification, deployment or Git commit was changed in this turn.
