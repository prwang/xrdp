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

---

## ADDENDUM 2 — the black region survives every colour, and the damage trace says why

Owner: the 33 x 6 rectangle **stays black** through white, blue and red.

The first reading of that ("a region we never paint, therefore ours to
fix") is WRONG, and the damage trace is what corrects it. Every `dmg`
record carries its surface and bounding box; over 4000 recent events:

| surface | events | full-monitor damage `(0,0,3840,2160)` |
|---|---|---|
| 0 — right monitor | 1153 | **61 times** |
| 1 — left monitor | 2847 | present |

The right monitor's entire area is damaged and re-sent repeatedly. The
boxes are surface-local (surface 1 sits at desktop y = 6 yet reports
y1 = 0), so both surfaces are covered corner to corner.

**So the only regions that CAN stay black are those no surface covers.**
The desktop is 7680 x 2166; surface 0 covers desktop y 0..2159 and
surface 1 covers y 6..2165. Two strips belong to neither:

* y 0..5, x 0..3839 — above the left monitor
* y 2160..2165, x 3840..7679 — below the right monitor

Both exactly **6 px tall**, matching the measured height, and both exist
solely because the client declares its second monitor 6 px lower. The X
desktop IS painted there (verified white in addendum 1); it is simply
never transmitted, so the client shows its initialised canvas.

**Unresolved: the bands are 3840 px wide, not 33.** Either the visible
rectangle is a fragment of one, or the 6 is coincidence. The
distinguishing question, asked and not yet answered, is whether it
touches the bottom edge of the right screen (the band below the right
monitor), the top edge (the other band, which should not be visible
there and would be a client placement error), or floats free (neither
band, model wrong).

**Certain regardless:** the 6-px bands are real, are never transmitted,
and are caused entirely by the client-declared misalignment. Aligning
the two displays removes them.

---

## ADDENDUM 3 — displays aligned: the 6 px is gone, the 33 px STAYS

The owner aligned the two displays in the client and reconnected.
Verified on the session at 17:50:

```
Screen 0: current 7680 x 2160          (was 7680 x 2166)
rdp0 connected primary 3840x2160+3840+0
rdp1 connected         3840x2160+0+0    (was +0+6)

client raw layout   monitor 0  left 0     top 0  right 3839  bottom 2159
                    monitor 1  left -3840 top 0  right -1    bottom 2159
surfaces mapped     id 0 -> left 3840 top 0 ;  id 1 -> left 0 top 0
```

A textbook layout: no offset, no gaps, no strip belonging to no monitor.

**The 33 px displacement is unchanged.** It is still there from connect
and still cleared only by minimising and restoring the client.

**So the "33 = 27 px panel + 6 px monitor offset" derivation in addendum
1 is WITHDRAWN.** It fitted the number exactly and it was a coincidence.
Two things falsify it: the 6 is now zero and the fault is identical, and
the panel is now **43 px** tall rather than 27, so neither term of that
sum survives contact with the aligned layout.

Everything ruled out earlier still stands — our pixels, our damage, our
encoder geometry, our arithmetic, flow control. What is left is the
connect-ordering window (surfaces mapped 2.1-2.8 s before the
framebuffer is resized) and the client's own layout computation, and
distinguishing those requires knowing when the client decides, which is
not observable from this side.

**Owner ruling: option 1 — document and move on.** The item is below #92
(4:2:0 in motion) and no further work is authorised on it.
