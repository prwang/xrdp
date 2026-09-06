# Rendering client capability reset

This apparatus exercises repeated graphics capability negotiation on a real
RDP connection. It uses the installed FreeRDP 3.15.0 X11 renderer at
`/opt/freerdp-vaapi/bin/xfreerdp`, with the save-only oracle mode disabled.
Only the graphics channel plugin is replaced through `LD_PRELOAD`. The server
uses its existing package and configuration.

Build in the canonical checkout:

```sh
timeout 90s python3 /work/PR-demo/gfx_reset_client/build.py
```

The build downloads six upstream files from the pinned 3.15.0 tag and verifies
`upstream.sha256`. Generated source and binaries stay under ignored `build/`.
The SDK version must also match. Two warning classes from the pinned upstream
headers are disabled (deprecated declarations and unsigned lower-bound
assertions); other warnings remain errors. The build also runs a deterministic
parser test: ignore graphics before confirmation, resume after confirmation,
and reject invalid framing. It does not test server retirement.

For the currently authorized one-connection reproduction:

```sh
timeout 110s python3 /work/PR-demo/gfx_reset_client/run.py
```

It requires the corrected development image on port 40062, no existing tester
desktop and the installed smoke colour-key autostart. It starts its own Xvfb,
arms the existing login payload, establishes a rendered red screen at
1024×768, sends one reset, saves before/after screenshots and thread/child
ownership, logs off the whole desktop, tears down its client/Xvfb and archives
the server logs and existing perf trace. No remote GUI process is relaunched.
The loop is bounded to a 60-second connection observation, with separate
bounded setup and cleanup. Evidence goes under `mac_bisect_matrix/captures/`.

The control socket resides in a fresh owner-only directory. A nonblocking
datagram event is registered with FreeRDP's protocol loop; the event callback
and DVC receive callback share a mutex. Reset is refused before a decoded
frame, while a prior reset awaits confirmation, or outside negotiated versions
10.3–10.7. The harness additionally verifies the attached desktop payload.
After sending the client's actual capabilities, the plugin deletes surfaces,
evicts caches, resets decoders/frame counters and ignores graphics until
confirmation. The ordered ZGFX transport decompressor continues consuming the
channel stream. No packet is fabricated at the server callback.

For a separately launched cooperating client, set `XRDP_GFX_CONTROL_DIR` to
an existing private directory and load `build/libgfx-reset.so`. Its local
controller is:

```sh
timeout 10s python3 control.py /path/to/private/directory status
timeout 10s python3 control.py /path/to/private/directory reset
```

This is a dev-box control, not a command available in a stock MSTSC or remote
Linux desktop. No session-terminal tunnel has been added.

The first run reproduced an extra surviving encoder worker and no repaint
after reset. The old red pixels remained in the client's output window;
unchanged screenshots alone must never pass repaint acceptance. A successful
script exit means the capture completed, not that the server lifecycle passed.
The parser test covers the ignore path because the live run had no intervening
graphics to ignore. It does not cover in-flight ownership or decoder-failure
injection. Full server retirement tests and the corrected rendered run remain
required. See the
[capture](../mac_bisect_matrix/captures/i142d_freerdp_reset_20260906T174945Z/README.md).
