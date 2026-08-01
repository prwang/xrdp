<!--
Experiment record moved out of BACKLOG.md on 2026-08-01.

BACKLOG.md is the OPEN work list: hypotheses, justification, and a
pointer. This file is the closed record it points at -- the conditions,
the numbers, the anomalies and the retractions, kept verbatim as they
were written at the time. Nothing here is a live task.
-->

## #61 — GLAMOR is not available to us on NVIDIA; the "different benchmark" needs another route (CLOSED-WONTFIX for GLAMOR, the payload half is TODO)

**The ask.** #59 showed the E5-2 benchmark is not touching a ceiling we
own: two thirds of the saturated Xorg thread is the xterm payload's own
software rendering (44.9 %) plus X's Present emulation (18.8 %), against
13.8 % for the whole capture. The proposed fix was **GLAMOR** (move X's
drawing onto the GPU) **+ alacritty** (move glyph rasterisation out of the
X process entirely).

**GLAMOR half: RED, and it is an upstream limitation, not a
misconfiguration.** Enabling it on the T4 (`nvidia` added to
`Option "DRMAllowList"` in `/etc/X11/xrdp/xorg.conf`, `ubuntu` added to
`render`/`video`) *looked* like it worked — the session Xorg logged
`glamor X acceleration enabled on Tesla T4/PCIe/SSE2` and logged in — but
it renders **black**:

```
smoke[1920x1080]: EDGE FAIL (0.000 < 0.50)  ok=0 lag=8   (every key got=black)
Xorg: (EE) XRDPDEV(0): Failed to make 1024x768x32bpp pixmap from GBM bo
```

This is **neutrinolabs/xrdp#1697**, open since 2020-10-06: *"Latest xrdp
can use glamor to accelerate X drawing and make use of hardware 3D
rendering but it only works well with Intel or AMD hardware."* The last
comment on it (2024-11-18, unanswered) reports our exact symptom —
xorgxrdp `--enable-glamor` on NVIDIA, RDP login, black screen.

Both escape routes in that thread are closed for us:

* the only working NVIDIA path is jsorg71's **`nvidia_hack` branch** plus
  `xorg_nvidia.conf` with a hardcoded PCI BusID — a different driver stack
  (the real NVIDIA X driver), not a toggle on the `xorg.conf` we ship;
* and on that branch **dynamic resolution and the virtual monitor do not
  work** ("nvidia proprietary driver does not support virtual monitor"),
  because it drives a real GPU head. E5-2 is a two-monitor
  2560×1440 + 3840×2400 benchmark driven by the RDP monitor layout, so
  `nvidia_hack` cannot host it even if we adopted it.

Related detail worth keeping: with GLAMOR on, alacritty's own stderr
showed `glx: failed to create dri3 screen` / `failed to load driver:
nvidia-drm` and Mesa falling back to **zink** — xorgxrdp calls
`glamor_init(..., GLAMOR_USE_EGL_SCREEN | GLAMOR_NO_DRI3)`, so clients
cannot get DRI3 through this screen regardless.

**Everything measured while GLAMOR was on is VOID.** xterm 46.5 ms;
gpuflood 35.0 / 35.1 / 35.2 / 38.5 ms; gpuflood baseline 60.3 / 61.2 ms;
the re-profile in which `avc444_decode_row.avx2` became the #1 symbol at
14.17 % and libpixman vanished. All of it was a black screen: the capture's
own check reports **1160 of 1160 main-view pictures black**. Archived, with
every rate-bearing file renamed, under
`PR-demo/mac_bisect_matrix/captures/e52_t4_glamor_VOID_20260731/`.

**Process failure, recorded so it is not repeated.** The config under test
was changed and then a full measurement campaign — six rate runs, a perf
profile and a uprobe run — was executed *before* the smoke gate was run
against it. CLAUDE.md already requires the smoke gate against the exact
deployed binary **and config**; the missing rule is the ordering: **prove
basic functionality first, measure second.** A config change that alters
the render path is a deployment, and an unproven deployment produces
numbers that cost more to unwind than the gate costs to run. Added to
`PR-demo/t4_profile/E5-2_T4_PROTOCOL.md` as a hard precondition.

**What is still open (the payload half).** Alacritty does not need GLAMOR
to be useful here: it rasterises glyphs in **its own process** (Mesa
llvmpipe/zink on this box) and hands X finished buffers, which should
remove the 19.3 % glyph-composite and 9.1 % fill paths from the X thread
and replace them with one large blit — and the box has ~1.5 idle cores for
it to run on. That is a real, testable change to what the benchmark
measures, on the render path we already smoke-gate. Measure it as: arm
`gpuflood`, confirm the smoke gate passes first, then a paired ≥180 s
baseline/batched pair, and re-profile to show the pixman paths shrank.
Cross-check the black-frame count on every run — it is what caught this.

**Resolved by #62.** The payload half was built: `PR-demo/textflood/`
renders the same corpus with cairo in its own process and blits it with
`XShmPutImage`, cutting the payload's X-thread cost 7.7x (99.0 % -> 12.8 %
of a core on Xvfb, against a 13.7 % idle floor) with the residual being the
SHM memcpy itself. The `Option "DRI3" "0"` / `-extension Present` idea that
was listed here as a second lever is **withdrawn** — see the box under #59:
it retunes the X server rather than our code, and textflood issues no
Present requests, so the path stops being fed instead.

---
