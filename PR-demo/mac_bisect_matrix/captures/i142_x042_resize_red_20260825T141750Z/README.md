# #142 x042 real-client growth-resize failure

This capture preserves the first real-client dynamic-resize failure against the
final trace-disabled clean-room pair on `127.0.0.1:40058`. The server image was
`localhost/xrdp-bisect:cleanroom-b38c6347-3dc52da1-u2404-xfce-notrace.p382e50b0`:
xrdp `b38c63473c5297250af325d2770c5219ede69d48`, xorgxrdp
`3dc52da1321644bda7678fb246d815dc27bd9bef`. The live configuration SHA-256 was
`216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971` and
selected dense automatic AVC with libx264, eager slot acknowledgement and a
one-frame wire window.

## Observation and causal sequence

The real client `5Q77` connected at an initial 2196-by-1250 geometry. The
producer allocated 16,760,832 bytes for one monitor's two full-chroma capture
slots. A first resize to 2198-by-1250 completed. A second resize to
2412-by-1344 also reported completion, but xorgxrdp logged that it reused the
same shared-memory descriptor and byte length. Sixteen milliseconds later xrdp
rejected the first capture with `Refusing a full-chroma capture with invalid
slot identity`, closed the xup connection, and the RDP client disconnected.
The Xorg/XFCE session remained alive and the pod did not restart.

The arithmetic independently closes the mismatch. With 32-pixel width and
16-row height alignment, 2196-by-1250 needs 16,760,832 bytes for two slots;
2412-by-1344 needs 19,611,648 bytes. The clean-room producer updated only its
display dimensions during the monitor-update message. Its retained
`avc444_layout.total_bytes` therefore stayed at 16,760,832, making the allocator
reuse the old mapping. xrdp independently rebuilt the expected layout from the
new display description and rejected the old-length snapshot. The rejection
was correct; the producer's stale resize layout was the defect.

## Evidence inventory

* `xrdp.log` contains the RDP connection, both resize completions and the
  terminal slot-identity rejection.
* `xorgxrdp-display-10.log` contains the original allocation and both incorrect
  reuse decisions.
* `xrdp-sesman.log` and `xrdp-chansrv-display-10.log` preserve session
  lifecycle context.
* `pod.yaml`, `deployment.yaml`, `gfx-configmap.yaml` and `runtime-state.txt`
  pin the deployed image, configuration, package versions, process state and
  zero-restart container state.

No performance trace or synchronous per-frame logger was enabled. These are
ordinary lifecycle/error logs, and the causal result depends on identities and
byte lengths rather than timing.

## Required correction order

The development pair is the behavioral source of truth. Reproduce or exclude
the same resize there first; implement and validate any required fix on the
development branches; then re-author the proven invariant into the owning
two-slot clean-room slice and replay its descendants. The earlier clean-room
history is not edited on the strength of this diagnosis alone.
