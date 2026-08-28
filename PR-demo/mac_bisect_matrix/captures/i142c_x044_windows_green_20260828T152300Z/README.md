# #142C manually reconciled development Windows control remains green

## Result

The owner reported twenty repeated resize interactions on the manually
reconciled development arm without the clean-room black-surface precursor or
resize-triggered teardown. The server logs make the exercised depth explicit:
the first established Windows connection completed 44 `ResetGraphics`
transitions and the reconnected preserved session completed another 26 before
this capture was taken. Each connection received exactly one GFX capability
advertisement. Neither received the mid-session replacement advertisement
which preceded the clean-room black surface.

The first connection ended with a peer TLS EOF at 15:21:15 UTC and a new
Windows connection was established 1.4 seconds later. The logs alone do not say
why the peer closed, so this record does not label it manual or causal. The
owner did not observe the filed black-then-disconnect behavior, and the second
connection remained live with the same tester Xorg when evidence was copied.

This is a red equivalence result. The clean-room reproduction reached its first
unacknowledged post-resize frame and replacement capability advertisement after
the seventh resize. Exceeding that depth repeatedly without the same precursor
shows that the development reconciliation remains incomplete.

## What the failed reconciliation actually changed

The paired development build now transports AVC444 captures over xup order 64,
matching the clean-room order number and shared-memory fields. In development
xrdp, however, `xrdp_mm_queue_avc444_capture()` immediately reconstructs the
old order-62 STARTFRAME/WIRETOSURFACE_1/ENDFRAME blob and calls
`server_egfx_cmd()`. The queued item and every later encode/emission step are
therefore deliberately the pre-reconciliation development GFX-command path.

Clean-room retains order 64 as a raw surface-capture item through
`server_paint_rects_ex()` and its dedicated ffmpeg queue. The previous change
reconciled the ingress label but neutralized it before the behaviorally distinct
execution seam. Its green Windows outcome is consistent with the code and
does not test the missing dedicated path.

The next development correction must preserve the raw capture contract through
the encoder worker or otherwise prove byte-, ordering-, ownership- and failure-
equivalence at that boundary. Moving directly to the shared capability callback
fix would still be invalid because development has not reproduced the client
precursor which reaches it.

## Exact identity

- Endpoint/arm: `127.0.0.1:40060`, `x044`.
- Image:
  `localhost/xrdp-bisect:reconciled-manual-f7d8c2b-baf9658-u2404-xfce-notrace`.
- Runtime xrdp: `f7d8c2b527c7`;
  `0.10.80+git20260828123212.f7d8c2b527c7`.
- Runtime xorgxrdp: `baf9658c397d`;
  `1:0.10.80+git20260828123216.baf9658c397d`.
- Profile SHA-256:
  `5372197d3246b5da5fd29868c1f3b15b1dbd070a6b4be3b16cd0e56fdd93e22e`.
- Tester Xorg at capture: PID 5643, display `:10`; it was not restarted or
  manipulated while logs were copied.

## Files

- `xrdp.log`: complete server log through the 44-reset and 26-reset Windows
  connections.
- `xorgxrdp.log`: surviving producer/session log, including every resize and
  correctly rebuilt shared mapping.
- `xrdp-sesman.log`: session-manager lifecycle.

SHA-256:

```text
ce924574576bf265a8f2cb6d887e810360d2279638308537b23e699fc06ddcd2  xorgxrdp.log
8b72d35734f33a8a8228e9f0593b8be72566099fa256514d9a4e6e200c859b08  xrdp-sesman.log
ed89d5d0084722a38d16a7f3f794d77f407ed8f733fbc596477c0ce38a9de159  xrdp.log
```
