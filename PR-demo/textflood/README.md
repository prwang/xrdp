# textflood — a payload that measures *our* ceiling

The E5-2 benchmark scrolled an ANSI-highlighted code corpus in an
**xterm**. Profiling the T4 (BACKLOG #59) showed that measures the X
server, not us: of the saturated session Xorg's single core, **44.9 %**
went to the payload drawing *itself* — glyphs through
`pixman_image_composite32` (19.3 %), scroll through `pixman_blt`
(16.5 %), fills through `fbFill` (9.1 %) — against **13.8 %** for the
entire xrdp capture. A pipeline change cannot show up against that.

`textflood` renders the **same corpus** with cairo **in its own process**
and hands X one finished image per frame over MIT-SHM. Same pixels, same
solarized-dark colours, same scroll cadence — only the rasterization
moves off the X thread.

## Measured (dev box, Xvfb at 6400×2400, 30 s per arm)

| arm | X-server CPU | payload's X-side cost |
|---|---|---|
| idle Xvfb, no payload | 13.7 % of a core | — |
| **xterm codeflood** (server-side XRender glyphs) | **99.0 %** | **85.3 points** |
| **textflood** (client-side cairo + MIT-SHM) | **12.8 %** | **~0, at the idle floor** |

**7.7× less X-thread cost for the same content.** textflood's own
rasterization — 31.4 s of CPU over the 36 s window, ~87 % of a core —
runs in a separate process on a different core. The 3.85 s of X CPU that
remains is the SHM copy itself: ~1050 frames × 61 MB in 3.85 s ≈
16.6 GB/s, i.e. the memcpy floor, and irreducible.

Total system work goes *up*; the bottleneck thread is freed. That is the
right trade on a box whose Xorg is single-threaded with idle cores beside
it, and it is the whole point.

> Scope of that table: it measures the **payload's** X-side cost, which is
> a property of the payload, on Xvfb. It is not an xrdp measurement and
> not a substitute for one — the E5-2 number still has to come from the
> T4 with xorgxrdp in the loop.

## Rendering, verified rather than assumed

A 6400×2400 frame captured with `xwd` and decoded:

* **8 of 9** solarized palette entries present (the 9th, magenta, occurs
  on only 43 of 3000 corpus lines, so a sample missing it is expected);
* **3650 distinct colours** in the sampled set;
* **21.5 % of pixels are subpixel-AA fringes** — non-palette colours with
  R≠G or G≠B, e.g. `#002b37`, `#012b36`, `#165d83`.

That last number is the reason subpixel AA is requested explicitly rather
than inherited from the session's fontconfig. It is what a real desktop
renders, and it is the maximal stressor for AVC444: a different value in
each of R, G and B at every glyph edge is exactly the chroma detail 4:2:0
discards and the 4:4:4 aux view preserves. A run that quietly fell back to
greyscale AA would carry far less chroma and flatter the 4:2:0 arm.

## Why not a GPU terminal

alacritty and kitty render through OpenGL, and there is no usable GL in
these sessions: xorgxrdp calls `glamor_init()` with `GLAMOR_NO_DRI3`, so
clients fall back to Mesa zink/llvmpipe, which **emulates** the GL state
machine on the CPU and would simply become the new bottleneck. Enabling
GLAMOR to fix that renders black on NVIDIA — upstream
neutrinolabs/xrdp#1697, BACKLOG #61.

cairo's image backend is a *native* CPU rasterizer using the same
FreeType glyph rasterizer GTK applications use. Fast on the CPU by
design, not by emulation. That distinction is what makes this payload
viable where alacritty is not.

## Content stays representative

`code_corpus.ansi` is real xrdp source (`xrdp_mm.c`) highlighted by
pygments + clangd semantic tokens in solarized-dark
(`../mac_bisect_matrix/gen_code_corpus.py`). textflood renders that
corpus unchanged, so numbers stay comparable with the codeflood history.

The corpus dialect is narrow: exactly two escape sequences,
`ESC[38;2;R;G;Bm` (24-bit truecolor foreground) and `ESC[0m`, over 8
colours. This is not a terminal emulator and does not try to be — other
escapes are skipped. **`gen_code_corpus.py` and this parser are a pair**:
regenerating the corpus with a style that emits bold, backgrounds or
256-colour indices would render differently without failing.

## Override-redirect, on purpose

The window is `override_redirect` and covers the whole root, so no window
manager touches it. That deletes the entire class of bug that invalidated
three T4 runs with the xterm payload — to xfwm4 "maximized" means the
*current monitor*, and it re-snaps on its own schedule, producing the
one-active-one-idle regime (BACKLOG #53) that an E5-2 ratio must not be
computed from. There is nothing to re-snap and no span-fixer loop to keep
running. `--managed` restores WM placement for debugging; Escape or `q`
quits.

## The fast producer: `--scroll strip` (BACKLOG #83)

`--scroll full` is the default and is the loop every archived capture
was measured with: every visible row re-rendered every frame, and the
corpus advanced `--step` lines **per rendered frame**. It is unchanged,
and it still draws the same bytes — a 6-frame byte-for-byte comparison
against the committed pre-#83 `draw_frame` at 3840×2400 is identical.

`--scroll strip` is PRD FR-BENCH-1's **design B**: move the picture up
by the scroll distance with one `memmove` and render only the newly
exposed strip at the bottom. Nothing in it runs unless the flag is
given.

### What changes with the flag, and why

**1. The advance is time-based, not per-frame.** In strip mode the
corpus moves at `--lines-per-sec` (default **1479.2**), not at N lines
per frame. The default reproduces today's content speed exactly: the
full-redraw payload advanced 25 lines per frame at a measured 16.901 ms
frame interval on arm x014, and 25 / 0.016901 s = 1479.2 lines/s. This
matters because a producer that draws 2.4× faster must not also put
2.4× more motion into every encoded frame — otherwise "the producer got
faster" silently also means "the encoder's job got harder", which is
the exact confound #83 exists to remove.

**2. The line height is pinned to a whole number of pixels**, checked
at startup, and the scroll distance is `advance × line_height`. A
fractional line height would put band boundaries between pixels, which
is the one way the strip render can be subtly wrong instead of loudly
wrong. On this box DejaVu Sans Mono 14px reports a natural height of
exactly **16.000 px**, so the rounding is currently a no-op — but that
is a property of this freetype/fontconfig, not a guarantee. Where the
natural extent *is* fractional, strip mode's rows sit slightly closer
together or further apart than the archived full-redraw captures, and
the row count changes with them. Not hidden: this is why the flag
exists rather than the behaviour being switched.

**3. Rows are clipped to their own band.** Band r is
`[r·lh, (r+1)·lh)` and row r's glyphs are masked to it. That is what
makes "scroll by k bands and redraw the exposed ones" *provably* the
same picture as "redraw every band" — without it, a descender crossing
a band boundary survives the scroll at the top of the frame and is
clipped at the bottom. The cost is a real, small fidelity change
against the legacy render: measured at 3840×2400, same offset, same
16 px line height, **18 580 of 9 216 000 pixels differ (0.2016 %)**,
all of them antialiasing fringe from ink that crossed a row boundary.

Frame zero always takes the full-redraw path, as does any frame after
an `Expose`, and any advance large enough that nothing would survive
the scroll.

### `--verify N` — the same picture, computed two ways

```sh
./textflood --corpus ../mac_bisect_matrix/code_corpus.ansi \
            --geometry 3840x2400 --verify 32
```

Renders N frames down **both** paths into two image surfaces and
compares them byte for byte, exiting non-zero on the first differing
pixel. No X server, no session, ~0.5 s. The advance sequence is
deliberately irregular (1, 2, 7, 13, 25, 40, 3, and one advance larger
than the screen) because an off-by-one in the redrawn band range only
shows at some advances. Green at 1920×1080, 2560×1440 and 3840×2400.

The expectation comes from the *other* code path, not from the path
under test, which is what makes it an assertion rather than a
restatement of the implementation. It has been shown to fail, by
breaking the strip path four ways and watching each go red:

| deliberate break | what `--verify` said |
|---|---|
| strip drawn one corpus line off | mismatch, frame 0, pixel x=35 y=2386 |
| redrawn band range one band too narrow | strip path refused a legal advance |
| scroll distance one pixel short | strip path refused a legal advance |
| `memmove` one band too far | mismatch, frame 0, pixel x=45 y=2 |

The last two are caught by the invariant check inside the scroll, which
returns *fatal* rather than falling back to a full redraw. That
distinction is load-bearing: an earlier revision fell back, both paths
then ran the same code, and `--verify` went **green while testing
nothing**. `--verify` now fails if the strip path declines an advance
it should have handled.

### `--selftest` — the producer's own rate (PRD FR-BENCH-1)

FR-BENCH-1 requires the producer's "standalone rate (`--selftest`, no
RDP session)". It did not exist until #83; it does now. It renders into
memory with no X server, discards two frames to warm the glyph cache,
and prints mean/p50/p90/p99/max ms per frame, the content speed it
actually achieved, and the FR-BENCH-1 margin against `--pipeline-ms`
(default 18.476 ms, arm x014). It does **not** include `XShmPutImage`
or `XSync`; the deployed period is this plus the X server's own copy.

## Measured offline, 3840×2400, this dev box (2026-08-06)

Dev box = AMD Ryzen AI MAX+ 395, 32 threads. **Not the T4**, which is
decommissioned; the 7.1 ms/frame for design B in `PRD.md` was measured
there at 3840×2160 and is not comparable with anything below.

`ring_recon` prices both designs at the **same** 25 lines of scroll per
frame, which is the only way they are comparable — A's cost does not
depend on the advance and B's is nearly proportional to it:

| design | ms/frame | vs the 18.476 ms pipeline period |
|---|---|---|
| A full redraw (`--scroll full`) | **11.6 ± 0.1** | 1.6× |
| B scroll + strip (`--scroll strip`) | **2.68 ± 0.03** | 6.9× |
| C pre-rendered ring (not implemented) | 1.45 | 12.7× |

**B is 4.3× cheaper per frame than A at the same scroll distance**
(three back-to-back runs: 4.40×, 4.31×, 4.40×).

`textflood --selftest` at the same geometry, letting the producer
free-run so the time-based advance settles where it will:

| mode | ms/frame mean (p90) | lines/frame | standalone rate |
|---|---|---|---|
| `--scroll full` | 11.71 (12.57) | 25.0 | 85.4 fps |
| `--scroll strip` | 1.10 (1.20) | 1.63 | 910.9 fps |

Strip mode's 1.10 ms is *not* design B's cost at the deployed cadence:
free-running at 911 fps it only advances 1.63 lines per frame, so it
renders a 2-band strip. Both numbers are true and they answer different
questions — 2.68 ms is what a strip frame costs when the pipeline paces
the producer at ~25 lines per frame, 1.10 ms is the standalone rate
FR-BENCH-1 asks for.

Cross-checks on those numbers, before they are used for anything:

* `ring_recon`'s design A (11.6 ms) and `textflood --selftest --scroll
  full` (11.71 ms) are separate code paths timing the same work and
  agree to 1 %.
* The deployed producer on arm x014 (same geometry, same host, in a
  container beside the pipeline) spent **14.100 ms** of its 16.898 ms
  frame in exactly that redraw — 0.002 ms issuing the blit and 2.796 ms
  waiting in `XSync` — recomputed here from the run's own
  `textflood_stamps.tsv.gz` over its first 3010 frames, whose mean
  interval is 16.913 ms and reproduces the 16.91 ms in that capture's
  README. The 2.4 ms above the offline 11.7 ms is what contention with
  the pipeline costs.
* So **83 % of the deployed producer's frame period is the full
  redraw**, and that is the term design B replaces.

What is NOT measured here: the deployed strip-mode frame period. That
needs the #83 producer arm, in a session, with the X blit and `XSync`
in the loop. Projecting 2.68 + 2.80 ms from the numbers above gives
~5.5 ms, which would be a margin of 3.4× against an 18.476 ms
pipeline — a projection, not a result, and it is stated as one.

## Build and run

```sh
./build.sh                     # needs libcairo2-dev libx11-dev libxext-dev
./textflood --corpus ../mac_bisect_matrix/code_corpus.ansi
./textflood --help

# offline, no X server, seconds of CPU:
./textflood --geometry 3840x2400 --verify 32
./textflood --geometry 3840x2400 --selftest --scroll strip
./ring_recon ../mac_bisect_matrix/code_corpus.ansi 3840x2400
```

`build.sh` builds both binaries. Zero warnings under `-Wall -Wextra`.
It lives in `PR-demo/` rather than `tools/` because the payload needs a
live X session, which is `tools/`'s exclusion rule — the two offline
modes do not.

## `ring_recon` drew the strip from the wrong place (fixed 2026-08-06)

`ring_recon.c`'s design-B bench asked for screen rows
`[rows - strip_rows, rows)` while passing the row renderer a corpus
offset of `offset + rows - strip_rows`. The renderer adds the row index
to the offset, so the strip showed corpus line
`offset + (rows - strip_rows) + row` where it should have shown
`offset + step + row` — **85 lines off** at 3840×2160, where the bench
computed 135 rows of 16 px.

Its **timing** was not affected: the same number of screen rows of the
same corpus were rendered either way, and every row is filled edge to
edge by repetition regardless of which corpus line it is. So the 7.1
ms/frame in `PRD.md` is not withdrawn on this account. What it means is
that the bench had never drawn the picture design B produces, and could
therefore never have caught a strip-geometry error — which is why
`textflood --verify` exists rather than a reading of the bench output.

Two other things in that bench were corrected at the same time, both of
which move its numbers: it computed `rows = height / line_height` where
the shipped payload uses `height / line_height + 1`, so it rendered one
row fewer than the payload it stood in for; and its FR-BENCH-1 floor
was still the T4-era 8.19 sends/s, against which everything trivially
passed. The floor is now the 18.476 ms period of arm x014.

## Before quoting any number from it

Per the precondition added to `../t4_profile/E5-2_T4_PROTOCOL.md` after
the GLAMOR incident: **smoke-gate the deployed config first, read the
black-frame count before the rate number**, then measure. This payload
changes what is rendered, so it is exactly the kind of change that must
be proven functional before it is measured.
