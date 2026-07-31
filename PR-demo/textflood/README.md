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

## Build and run

```sh
./build.sh                     # needs libcairo2-dev libx11-dev libxext-dev
./textflood --corpus ../mac_bisect_matrix/code_corpus.ansi
./textflood --help
```

Zero warnings under `-Wall -Wextra`. It lives in `PR-demo/` rather than
`tools/` because it needs a live X session, which is `tools/`'s exclusion
rule.

## Before quoting any number from it

Per the precondition added to `../t4_profile/E5-2_T4_PROTOCOL.md` after
the GLAMOR incident: **smoke-gate the deployed config first, read the
black-frame count before the rate number**, then measure. This payload
changes what is rendered, so it is exactly the kind of change that must
be proven functional before it is measured.
