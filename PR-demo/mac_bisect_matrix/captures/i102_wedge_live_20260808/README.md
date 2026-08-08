# i102_wedge_live_20260808 — the wedge, collected while it was on screen

Owner-driven. Collected 2026-08-08 17:30 UTC on arm x027 (host port
40043) **while the fault was visible on the owner's client**, at their
request. This is the evidence half; the diagnosis is not complete and
this README says so rather than reaching.

## The one thing that changed the diagnosis

**The wedge SURVIVES a full-screen repaint.** `colorkey_x11` repaints
every pixel of the desktop in one write; the owner ran it and the wedge
persisted. It is cleared only by minimising and restoring the client
window.

That combination rules out stale pixels. If the wedge were old content
in a region the server had failed to damage, one full repaint would have
overwritten it. It did not. **So the pixels we send are landing
somewhere other than where they belong, and it is the client's placement
of them — not their content — that is wrong.**

What clears it is understood exactly: on resume the client sends
`ALLOW_DISPLAY_UPDATES` with a rectangle and the producer damages it
(`/workUpdateXorgXrdp/module/rdpClientCon.c:2039`). But that only
explains the clearing if the client ALSO recomputes its layout at the
same moment, which is a client-side act we cannot observe.

## Server-side state at the moment of collection

Everything below was correct. That is the point of collecting it.

| | |
|---|---|
| X screen | 7680 x 2166, stride 30720 bytes = exactly 4 bytes x 7680 px |
| `rdp0` (primary) | 3840x2160 **+3840+0** |
| `rdp1` | 3840x2160 **+0+6** |
| client's raw layout | monitor 0 `left 0 top 0 right 3839 bottom 2159`; monitor 1 `left -3840 top 6 right -1 bottom 2165` |
| surfaces mapped | id 0 -> `left 3840 top 0 3840x2160`; id 1 -> `left 0 top 6 3840x2160` |
| ffmpeg children | four, all `coded 3840x2160 generation 1` |
| `_NET_DESKTOP_GEOMETRY` | 7680, 2166 |
| `_NET_WORKAREA` | 0, 0, 7680, 2166 — the full desktop, no strut applied |
| panel | 3840x27 at `+0+6`, `_NET_WM_STRUT_PARTIAL` top = **33**, x-range 0..3839 |

The normalisation is arithmetically right: the client's bounding box is
`left -3840, top 0`, so subtracting it maps monitor 0 to (3840, 0) and
monitor 1 to (0, 6), which is exactly what was sent.

## Files

* `server_framebuffer_1280x361.png` — the whole server desktop,
  downsampled 6x, as X had it at collection time. **Compare against what
  the client was showing**: any difference is introduced after us.
* `server_seam_x3600-4080.png` — 480 px full-resolution crop centred on
  the monitor boundary at x = 3840, vertically downsampled 4x.
* `perf/ring.txt.gz` — the last 60 000 records of the live trace ring.
* `xrdp.log.gz`, `gfx.toml`, `packages.txt`, `xrandr.txt`,
  `xdpyinfo.txt`, `root_props.txt`, `windows.txt`.

## What is MISSING, and it is the decisive artifact

**A screenshot from the client showing the wedge.** Everything here is
the server's side, and every server-side quantity checked out. The fault
is in what the client renders, and nothing in this capture can show
that. Without it the wedge's size, shape and position are known only
from the owner's description (a width offset, measured at 33 px).

The most useful version: run `colorkey_x11`, press `w` for a flat white
desktop, and screenshot the client. Against a uniform background the
wedge's geometry is measurable in pixels, and the server's framebuffer
for the same instant is a flat colour, so any structure at all in the
client image is introduced downstream of us.

## Related, and not yet connected

* **#102** — at connect the surfaces are mapped 2.1-2.8 s BEFORE the X
  framebuffer is resized. If the client fixes its canvas layout during
  that window it fixes it against a framebuffer that does not exist yet.
  This is the leading hypothesis for a persistent misplacement, and it
  is unproven.
* The client declares its second monitor **6 px lower**, which is where
  the panel's 33 px strut comes from (27 px panel + 6 px offset). That
  is a separate, real, client-supplied misalignment. Whether it also
  causes the wedge is unknown.
* A client can suppress all output indefinitely and a release build logs
  nothing — `xrdp_rdp_process_suppress` logs only at `LOG_DEVEL`
  (`libxrdp/xrdp_rdp.c:1474`). Measured here: 258.7 s of total silence
  with the pipeline idle and credit unused (distance 1 before and
  after, so flow control was not involved). That silence was invisible
  in the log and is why an earlier reading of this fault was wrong.

---

## ADDENDUM — flat-white test, 17:36 UTC: the server's desktop is UNIFORM

The owner set the desktop to flat white with `colorkey_x11` and reported
**a 33 x 6 black rectangle on the RIGHT screen**.

The server's framebuffer at that moment was scanned pixel by pixel — all
16.6 million of them. **Non-white pixels: 1045, all of them inside
x 20..142, y 19..43** — that is `colorkey_x11`'s own text label, which it
draws at (20, ascent+16). Nothing else on the desktop is anything but
white:

* both monitors, uniformly white;
* the 6-row dead band above the left monitor, white;
* the 6-row dead band below the right monitor, white;
* no 33 x 6 black rectangle anywhere, at any position.

Recorded as `server_framebuffer_white_1280x361.png` (whole desktop) and
`server_right_monitor_topleft_400x60.png` (the top-left 400 x 60 of the
right monitor at full resolution, which is where such a hole would sit
if it were ours).

**So the black rectangle is introduced downstream of the X framebuffer.**
That is not yet the same as "the client's fault": downstream includes our
own capture, damage translation and encode. What it does exclude is the
desktop contents themselves.

**The dimensions are the two known anomalies multiplied together.** 33 is
the panel's strut (27 px panel + the 6 px monitor offset); 6 is the
monitor offset itself, and also the height of each dead band. A 33 x 6
region is exactly the size of the intersection of those two, which is
unlikely to be coincidence and is the thread to pull.

**The next test, and it is two seconds:** with the rectangle visible,
press `b` then `r` in `colorkey_x11`.

* the rectangle stays BLACK through every colour -> it is a region that
  receives no updates at all. Something is failing to damage, clip or
  send it, and that is ours.
* the rectangle changes colour with the rest -> it is being painted and
  merely displaced, which puts it in the client's composition.
