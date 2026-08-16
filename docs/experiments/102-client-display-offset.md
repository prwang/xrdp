# #102 — client display offset after initial connection

Recorded 2026-08-08; closed as document-only by owner decision. Moved from
`BACKLOG.md` on 2026-08-16.

## Observation

With two 3840x2160 monitors side by side, one client's left display was drawn
about 33 pixels to the right. A black strip appeared at the far left and the
same width spilled onto the other display. The fault survived a full-screen
repaint and cleared after the client was minimised and restored.

An early explanation combined a 27-pixel panel with a six-pixel monitor
offset. That explanation was wrong. The owner aligned the monitors and
reconnected: the six-pixel layout gap disappeared while the 33-pixel defect
remained. The arithmetic was coincidental.

## Evidence

* A flat-white framebuffer scan found no corresponding hole in the X
  framebuffer.
* The affected monitor received repeated full-monitor damage.
* Both encoder children used the expected 3840x2160 coded geometry.
* No value sent by xrdp was 33 pixels on either axis.
* Initial connection creates EGFX surfaces during capability negotiation,
  roughly 2.1–2.8 seconds before the framebuffer resize. A dynamic resize
  performs those operations in the opposite order with about a 0.01-second
  gap. Whether the client fixes its canvas layout during that initial window
  is not observable from the server and remains only a hypothesis.
* The relevant resize and monitor plumbing was byte-identical to upstream
  `devel` at the time of the investigation.

The archived evidence is under
`PR-demo/mac_bisect_matrix/captures/i102_wedge_live_20260808/` in git
history.

## Decision

The owner classified this as document-only and below the AVC port. It is not
an open server-port task. A future investigation would compare the same
layout against the Windows Server 2022 ground-truth rig and inspect the
ordering of `RESET_GRAPHICS`, surface creation, mapping and framebuffer
resize.
