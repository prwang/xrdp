# VOID — every number in this directory was measured on a BLACK SCREEN

**Do not quote any rate, ratio or profile percentage from this capture.**
It is kept only as the evidence that GLAMOR does not work on this box
(BACKLOG #61), not as a measurement of anything.

## What happened

To make the E5-2 benchmark measure a ceiling we own (BACKLOG #59: only
13.8 % of the saturated Xorg thread is our capture, against 44.9 % for the
xterm payload's own software rendering), GLAMOR was enabled on the T4 —
`nvidia` added to `Option "DRMAllowList"` in `/etc/X11/xrdp/xorg.conf`,
`ubuntu` added to the `render`/`video` groups — and the payload switched to
alacritty (the new `gpuflood` kind).

The session Xorg logged `glamor X acceleration enabled on Tesla T4/PCIe/SSE2`
and logins succeeded. Six rate runs, a perf profile and a uprobe run were
taken. The numbers looked like a clean win: tight ~35 ms with no bimodality,
and the profile flipped exactly as intended — `avc444_decode_row.avx2` rose
to the #1 symbol at 14.17 %, libxorgxrdp to 19.73 %, and libpixman
disappeared.

Then the smoke gate was run:

```
smoke[1920x1080]: EDGE FAIL (0.000 < 0.50)   ok=0 lag=8   (every key: got=black)
Xorg: (EE) XRDPDEV(0): Failed to make 1024x768x32bpp pixmap from GBM bo
```

The capture's own black-frame check confirms it end to end:
**1160 of 1160 main-view pictures black**. libpixman "disappeared" from the
profile because nothing was being drawn; the capture kernels dominated
because they were converting a black framebuffer. Every number here is an
artefact of that.

## Why the fix is not available

Upstream **neutrinolabs/xrdp#1697** (open since 2020-10-06): *"Latest xrdp
can use glamor to accelerate X drawing and make use of hardware 3D rendering
but it only works well with Intel or AMD hardware."* The newest comment on
it (2024-11-18, unanswered) is our exact symptom. The only working NVIDIA
route in that thread is the `nvidia_hack` branch with a hardcoded PCI BusID,
and on it dynamic resolution and the virtual monitor do not work — which
rules it out for a two-monitor E5-2 run regardless. See BACKLOG #61.

## The process error this cost

The config under test was changed and the whole campaign was run *before*
the smoke gate was run against it. CLAUDE.md requires the gate against the
exact deployed binary and config; what was missing was the ordering. **Prove
basic functionality first, measure second** — a config change that alters
the render path is a deployment. Now a hard precondition in
`PR-demo/t4_profile/E5-2_T4_PROTOCOL.md`.

## Files

Renamed so nothing can be grepped out of them by accident:

| file | was |
|---|---|
| `VERDICT_VOID_DO_NOT_QUOTE.txt` | `VERDICT.txt` |
| `E5-2_decomposition_VOID_DO_NOT_QUOTE.txt` | `E5-2_decomposition.txt` |
| `xorg_perf_profile_VOID_DO_NOT_QUOTE.txt` | `xorg_perf_profile_gpuflood.txt` |
| `xorg_capture_uprobe_VOID_DO_NOT_QUOTE.txt` | `xorg_capture_uprobe_gpuflood.txt` |
| `VOID_gfx_trace*.txt.gz` | `gfx_trace*.txt.gz` |

`session-xorg.log` is 0 bytes — the collector ran after the session had been
logged off, so the GBM error line is not in this directory; it is quoted
above and in BACKLOG #61 from the live smoke-gate run.

## State of the box afterwards

`/etc/X11/xrdp/xorg.conf` was restored from `xorg.conf.pre-glamor`, xrdp
restarted, the session logged off, and the smoke gate re-run against the
restored config:

```
smoke[1920x1080]: target=t4 ok=8 lag=0 edge=1.000 encoder_errors=0
smoke[1024x768]:  target=t4 ok=8 lag=0 edge=1.000 encoder_errors=0
SMOKE PASS — safe to hand over
```

The valid T4 E5-2 result remains the one in
`../e52_t4_batched_20260730/` (1.5×–2.3×, centred near 1.7×).
