# #123 T4 frontier pre-interactive qualification

Captured 2026-08-22 on `98.93.137.204`. This is the server-side and local
FreeRDP preflight for BACKLOG #123--#125. It does **not** close #123: the
identified Windows/macOS mode, resize and visual matrix in
`PR-demo/INTERACTIVE_ARM.md` remains open.

## Identity

| component | exact identity |
|---|---|
| host | Ubuntu 26.04, kernel `7.0.0-1009-aws` |
| GPU | NVIDIA Tesla T4, driver 580.173.02, persistence enabled |
| ffmpeg | 8.0.1, stock `h264_nvenc` available |
| xrdp source | `00bce44e8fea` |
| xrdp package | `0.10.80+git20260822114412.00bce44e8fea` |
| xorgxrdp source | `c190343ff28a` |
| xorgxrdp package | `1:0.10.80+git20260822114312.c190343ff28a` |
| trace build | compile-time enabled; `XRDP_PERF_TRACE`, `XRDP_GFX_TRACE` and `XRDP_ACK_TRACE` enabled for this qualification |
| live handoff profile | `gfx-deployed-final.toml`, SHA-256 `d750fbaf8fa01146cf67136fe7d7254823d2b313d515a3ffd4650c3d4af01ef6` |

The package hashes are in `deployed-deb-sha256.txt`. The xrdp suite passed all
208 tests. Both xorgxrdp test suites passed before packaging. The
packages were produced from clean exact-commit worktrees; an earlier local
package whose generated version said `+dirty` was rejected and deleted before
anything was deployed.

## Safe replacement of the old installation

The host initially carried stale development packages:

* xrdp `0.10.80+git20260727225731.9539565594e3`;
* xorgxrdp `1:0.10.80+git251bc4d3db8d`;
* removed-package remnants for the stock `xrdp` and `xorgxrdp` packages;
* `.dpkg-*`, `pre_*` and old-configuration copies under `/etc/xrdp`.

There was no live RDP Xorg session. Before changing anything, the complete
old `/etc/xrdp`, `/etc/X11/xrdp`, package inventory and hashes were copied to
`/root/xrdp-pre-frontier-20260822T183446Z`. That directory is owned by root
and mode 0700. The services were then stopped and exactly the stale xrdp and
xorgxrdp packages/remnants were purged. `apt autoremove` was deliberately not
run, so no unrelated NVIDIA or host package was removed.

The exact pair above was installed in one transaction. New package defaults
were used, with only these intentional host changes:

* xrdp listens on `tcp://127.0.0.1:3389`;
* sesman uses the proven XFCE `wm1.sh` and ignores per-user window-manager
  overrides;
* the existing TLS certificate, key and RSA key were restored, preserving
  host identity;
* the committed T4 NVENC/LTR profile was installed;
* the perf trace sinks write mode-0600 files under mode-0700
  `/var/log/xrdp-perf`.

No stale backup/config file remains in the live `/etc/xrdp` or
`/etc/X11/xrdp` directory. `final-host-state.txt` records the final package,
service, listener, profile, device, account, payload and session state.

## Deployment defects found and fixed

Three preflight failures were kept red until their causes were fixed.

1. The `ubuntu` account was locked. The committed `t4_restore_cred.sh`
   restored its existing credential (`L` to `P`) without printing or copying
   the password into this capture.
2. `e52_t4_payload.sh install` could not build `textflood`: its prerequisite
   list omitted `pkg-config` and `libxrandr-dev`, and its predicate did not
   test `xrandr`. The versioned helper now tests `cairo x11 xext xrandr` and
   installs both missing packages. `textflood --help` then passed. The
   benchmark payload remains disarmed.
3. The first session could not open `/dev/dri/renderD128`; the device is
   `root:render` mode 0660 and `ubuntu` lacked that group. Adding only the
   `render` group fixed it. Every later session log, including
   `session-xorg-2mon-log.txt`, says `renderD128 open ok`. The adjacent logind
   errors for primary `/dev/dri/card0` and `card1` remain, but the xorgxrdp
   GLAMOR path uses the successfully opened render node.

Two attempted local logout/screenshot wrappers also failed because of shell
quoting or a missing local image utility. They did not change the server or
the measured path. Every resulting GUI session was terminated as a whole via
logind, never by killing/relaunching a process inside the session. The final
test used an XDG-autostart payload and a whole-session logout.

## NVENC and parameter-set preflight

The exact profile's 1920x1088 NVENC command was run twice for two seconds.
The first process took 2.09 s wall time and the second 0.91 s. Both produced
the same 59,495-byte Annex-B stream with SHA-256
`47138697b70db64aff870125526ae0687066b4f85c20b2a66e818e1e7b721019`.
These wall times characterize process startup; they are not frame latency or
throughput.

`nvenc-ltr-wire-audit.txt` records all nine LTR-splice guards green: High
profile, level 4.0, one reference, POC type 2, frame-only coding, CABAC, no
slice groups, no weighted prediction, one default L0 reference and no
redundant-picture count.

## Live preflight

All sessions used the installed XDG-autostart `chroma-probe`; no GUI process
was launched or supervised over SSH. The dense profile was AVC444v2, aux LTR
chain, `wire_window=1`, eager slot acknowledgement and sparse chroma off.

| condition | negotiation and hardware | local visual preflight |
|---|---|---|
| 1920x1080, one monitor | AVC444v2, probe 952 ms, two NVENC children | pairing/static/motion/stripes coherent in `client-1920x1080.png` |
| 1024x768, one monitor | AVC444v2, probe 756 ms, two NVENC children, render node open | coherent but labels crowd at the small geometry; `client-1024x768.png` |
| 1024x768, forced AVC420 | AVC420, probe 759 ms, one NVENC child | coherent 4:2:0 image with expected fine-stripe degradation; `client-avc420-1024x768.png` |
| 2560x1440 + 3840x2400 | AVC444v2, one probe at largest single monitor (3840x2400) in 775 ms, two surfaces and four NVENC children | both surfaces correct; probe on the second surface in `client-2mon-2560x1440-plus-3840x2400.png` |

The two-monitor client used the repository's recorded modelines verbatim:

```
312.25 2560 2752 3024 3488 1440 1443 1448 1493 -hsync +vsync
592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync
```

`client-2mon-layout.txt` independently records two RandR monitors and their
6400x2400 union. The server log records `monitorCount 2`, surface 0 at
2560x1440 and surface 1 at 3840x2400. No probe failure, post-confirmation
fallback, encoder restart, sequence mismatch, parser failure or pair timeout
appears in the retained logs.

FreeRDP 3.15 emitted `YUV decoder: intersecting rectangles` warnings during
the one-monitor sessions. This is not treated as green client evidence. The
same warning was already recorded on 2026-07-29 at 2x1024x768 and at this
exact large layout; the final large two-monitor run did not emit it. The
Windows/macOS matrix, not this local client, decides #123--#125.

## Identified real-client addendum — 2026-08-22

The owner connected from Windows host `5Q77` and macOS host `Signals-iMac`
using the dense AVC444v2 profile. On both clients, the alternating one-pixel
red/blue pattern remained visibly distinct rather than becoming a flat
colour. This is the owner-observed evidence that 4:4:4 chroma was working;
the server log cannot establish a visual result. The application version was
not exposed in the server log and remains to be added to the final client
inventory.

The retained server log independently establishes the negotiated path and
geometry:

* Windows negotiated AVC444v2 (`0x000F`). Its single-monitor connection was
  resized from 2196x1267 to 2294x1267 and then 1636x1267; the two resizes
  completed in 470 ms and 495 ms and recreated the surfaces and encoder.
* The Windows two-monitor connection negotiated AVC444v2 and supplied a
  3840x2400 monitor at client origin (-594,1440) plus a 2560x1440 primary at
  (0,0). xrdp normalized them into two surfaces and armed four NVENC children
  in one encoder-thread pump set.
* macOS negotiated AVC444v2 at 2560x1440 and reattached to the same Xorg
  desktop. It replaced the Windows xrdp connection without requiring an XFCE
  logoff.

The closed Windows two-monitor trace is valid as one stable encoder epoch:
all 602 frames handed to the transport were acknowledged, acknowledgement
latency was 29.79 ms at p50 and 46.06 ms at p90, and the greatest observed
distance ahead of the client was five frames, equal to the configured
`wire_window + 2 x monitors` bound.

`ack-windows-single.txt` is retained as a rejected analysis, not a numerical
result. It reports four impossible distances of 148, 149, 176 and 177 around
the two dynamic resizes. The trace is sound; the analyzer's assumption is
not. Dynamic resize deletes the encoder and creates a zero-initialized one,
so its client-acknowledgement frontier begins at zero while session-global
frame identifiers continue. `i80_ack_latency.py` treats the whole file as one
epoch and subtracts those unlike frontiers. Outside the four transitional
records its observed distance is at most three. The full-session latency
distribution and bound verdict in that derived file are therefore not
quotable. This is also why the perf trace remains agent-owned diagnostic
evidence rather than a condition on the owner's visual acceptance.

## Quantitative credit check

The arithmetic closes for all retained dense AVC444 traces:

| geometry | egress | ack | tail | ACK p50 | ACK p90 | worst distance | required bound |
|---|---:|---:|---:|---:|---:|---:|---:|
| 1920x1080 | 514 | 513 | 1 | 21.34 ms | 56.27 ms | 3 | `1 + 2x1 = 3` |
| 1024x768 | 173 | 173 | 0 | 19.62 ms | 29.75 ms | 3 | `1 + 2x1 = 3` |
| 2560x1440 + 3840x2400 | 382 | 381 | 1 | 32.12 ms | 208.94 ms | 5 | `1 + 2x2 = 5` |

Thus `egress = ack + tail` in every row and the configured `C + 2M` limit
held exactly. The intervention was exercised rather than merely configured:
15.6% of the 1920x1080 egresses reached distance 2 or 3, and 39.0% of the
two-monitor egresses reached distance 2 through 5. The full histograms are in
`ack-*.txt`.

The large-layout p90 and maximum acknowledgement latency are a property of
this local FreeRDP presentation arm. They are not a server throughput result
and are not compared to a different client or payload.

## Final handoff state

The host is intentionally left with:

* xrdp and xrdp-sesman active;
* RDP listening only at `127.0.0.1:3389`;
* one `ubuntu` X11 desktop retained for the continuing interactive checks;
* dense AVC444v2 profile live (sparse chroma off);
* forced AVC420, forced AVC444v1 and sparse-AVC444 profiles staged mode 0600
  under `/root/xrdp-frontier-profiles/`;
* `chroma-probe` globally autostarted and `textflood` disarmed.

Follow `PR-demo/INTERACTIVE_ARM.md`. Dense AVC444v2, Windows resize and the
Windows two-monitor check are complete. #123 remains open for forced AVC444v1
and forced AVC420 on both clients. #124 then requires the six-check walk on
both clients. Only after that should the staged sparse profile be activated
for #125.

## File map

* `final-host-state.txt`, `deployed-packages.txt`,
  `deployed-deb-sha256.txt`, `gpu.txt`, `ffmpeg-version.txt`, `uname.txt`:
  deployment identity;
* `gfx-deployed-final.toml`, `gfx-444v1.toml`, `gfx-420-final.toml`,
  `gfx-sparse.toml`: exact replay profiles;
* `frontier-nvenc-probe*.h264`, `nvenc-ltr-wire-audit.txt`: independent
  encoder/stream preflight;
* `client-*.png`, `xrdp*-log.txt`, `session-xorg*-log.txt`,
  `xfreerdp-2mon-log.txt`: live evidence;
* `xrdp-real-clients-log.txt`, `session-xorg-real-clients-log.txt`: Windows
  and macOS server-side negotiation, resize, geometry and session evidence;
* `perf-trace-windows-2mon.txt`, `ack-windows-2mon.txt`: stable Windows
  two-monitor credit evidence;
* `perf-trace-windows-single.txt`, `ack-windows-single.txt`: raw Windows
  dynamic-resize trace and the explicitly rejected cross-epoch analysis;
* `perf-trace*.txt`, `perf-trace-lines-*-summary.txt`, `ack-*.txt`: raw
  compile-time opt-in trace, formatter summaries and derived credit reports.
