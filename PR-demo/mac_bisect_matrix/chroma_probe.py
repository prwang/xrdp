#!/usr/bin/env python3
"""chroma-probe: on-screen instrument for AVC444 main/aux pairing faults.

Design principle: LUMA and CHROMA carry independent, readable clocks.
Every element that changes over time changes in exactly ONE of the two
planes, so any de-synchronization between the main (luma) and aux
(chroma) streams is directly readable off the screen:

  - Numerals / rulers / labels are white-on-black  -> pure LUMA signal.
  - Timed color patches use an EQUILUMINANT palette (constant Y after
    the server's BT.709 full-range RGB->YUV conversion, hue rotating in
    the U/V plane only) -> pure CHROMA signal.

Elements (top to bottom):
  1. FAST CLOCK: big white numeral N = tick mod 8 (1 Hz) next to a patch
     whose hue = palette[N]; the SAME repaint paints both. A small white
     copy of N sits inside the patch. Reading: in-patch numeral says N,
     patch hue reads palette index M via the printed legend =>
     chroma lags by (N - M) mod 8 frames-of-damage.
  2. SLOW CLOCK: same, tick/8 (one step per 8 s) -> disambiguates lags
     >= 8 and shows drift vs constant offset.
  3. LEGEND: the 8 palette swatches with their indices. A global channel
     transform (U/V swap etc.) shifts patch AND legend identically, so
     the lag reading stays valid even under a color cast.
  4. NAMED BARS: RED GREEN BLUE CYAN MAGENTA YELLOW WHITE at full
     saturation with luma labels + 1px red/blue stripe pairs ->
     channel swaps, casts, and 4:4:4 edge fidelity (static content).
  5. STATIC ZONE (bottom left): painted ONCE at startup, never damaged
     again -> distinguishes "wrong since connect / full-frame" from
     "wrong only where damage flows" (caseH).
  6. MOTION ZONE (bottom right): a bouncing equiluminant block over a
     luma ruler at 8 Hz -> continuous incremental damage; trailing or
     displaced hue vs the block's luma outline is visible directly.

Usage: run inside the RDP session under test:  chroma-probe [&]
Dependencies: python3-tk only.
"""
import sys
import time
import tkinter as tk

# --- equiluminant palette: Y fixed, hue angle rotates in U/V ----------
# BT.709 full range (the server-side conversion in use, verified
# 2026-07-27: colour VUI 709 full both encoders).
import math


def yuv2rgb(y, u, v):
    r = y + 1.5748 * (v - 128.0)
    g = y - 0.1873 * (u - 128.0) - 0.4681 * (v - 128.0)
    b = y + 1.8556 * (u - 128.0)
    return tuple(max(0, min(255, int(round(c)))) for c in (r, g, b))


Y0 = 140.0
RAD = 45.0
PALETTE = []
for k in range(8):
    ang = 2.0 * math.pi * k / 8.0
    PALETTE.append(yuv2rgb(Y0, 128.0 + RAD * math.cos(ang),
                           128.0 + RAD * math.sin(ang)))


def hx(rgb):
    return '#%02x%02x%02x' % rgb


BARS = [('RED', '#ff0000'), ('GREEN', '#00ff00'), ('BLUE', '#0000ff'),
        ('CYAN', '#00ffff'), ('MAGENTA', '#ff00ff'),
        ('YELLOW', '#ffff00'), ('WHITE', '#ffffff')]


class Probe:
    def __init__(self):
        self.root = tk.Tk()
        self.root.attributes('-fullscreen', True)
        self.root.configure(bg='black')
        self.w = self.root.winfo_screenwidth()
        self.h = self.root.winfo_screenheight()
        self.c = tk.Canvas(self.root, width=self.w, height=self.h,
                           bg='black', highlightthickness=0)
        self.c.pack()
        self.t0 = time.monotonic()
        self.tick = -1
        self.bx = 0
        self.bdir = 1
        self.static_zone()
        self.legend_bars()
        self.root.after(50, self.motion_loop)
        self.root.after(50, self.clock_loop)
        self.root.bind('<Escape>', lambda e: self.root.destroy())

    # -- rows layout ----------------------------------------------------
    def clock_loop(self):
        t = int(time.monotonic() - self.t0)
        if t != self.tick:
            self.tick = t
            # every 32 s: repaint EVERYTHING (full-screen damage). If an
            # accumulated color error wipes clean here and re-grows, the
            # fault lives in incremental-damage decode, not in a broken
            # base image — and the decoder is still alive.
            if t > 0 and t % 32 == 0:
                self.c.delete('all')
                self.static_zone()
                self.legend_bars()
                self.c.create_text(
                    self.w - 20, self.h - 30, anchor='se', fill='white',
                    font=('DejaVu Sans Mono', 18, 'bold'),
                    text='FULL REPAINT EPOCH %d' % (t // 32))
            self.draw_clock('fast', 20, 20, t % 8)
            self.draw_clock('slow', 20, 190, (t // 8) % 8)
        self.root.after(100, self.clock_loop)

    def draw_clock(self, tag, x, y, n):
        c = self.c
        c.delete(tag)
        label = 'FAST 1Hz' if tag == 'fast' else 'SLOW 1/8Hz'
        c.create_text(x, y + 70, text=str(n), fill='white', anchor='w',
                      font=('DejaVu Sans Mono', 90, 'bold'), tags=tag)
        c.create_text(x + 8, y + 145, text=label, fill='white', anchor='w',
                      font=('DejaVu Sans Mono', 13), tags=tag)
        px = x + 140
        c.create_rectangle(px, y, px + 260, y + 150,
                           fill=hx(PALETTE[n]), width=0, tags=tag)
        c.create_text(px + 12, y + 24, text=str(n), fill='white',
                      anchor='w', font=('DejaVu Sans Mono', 26, 'bold'),
                      tags=tag)

    def legend_bars(self):
        c = self.c
        x0, y0, sw = 440, 20, 90
        c.create_text(x0, y0 - 4, text='LEGEND: patch hue -> index',
                      fill='white', anchor='nw',
                      font=('DejaVu Sans Mono', 13))
        for k, rgb in enumerate(PALETTE):
            x = x0 + k * (sw + 6)
            c.create_rectangle(x, y0 + 18, x + sw, y0 + 98,
                               fill=hx(rgb), width=0)
            c.create_text(x + sw / 2, y0 + 120, text=str(k), fill='white',
                          font=('DejaVu Sans Mono', 20, 'bold'))
        # named bars + fine stripes
        y1 = y0 + 150
        bw = (self.w - x0 - 20) // len(BARS)
        for k, (name, col) in enumerate(BARS):
            x = x0 + k * bw
            c.create_rectangle(x, y1, x + bw - 4, y1 + 90,
                               fill=col, width=0)
            c.create_text(x + bw / 2, y1 + 110, text=name, fill='white',
                          font=('DejaVu Sans Mono', 14, 'bold'))
        y2 = y1 + 130
        for i in range(0, self.w - x0 - 20, 2):
            c.create_line(x0 + i, y2, x0 + i, y2 + 30, fill='#ff0000')
            c.create_line(x0 + i + 1, y2, x0 + i + 1, y2 + 30,
                          fill='#0000ff')
        c.create_text(x0, y2 + 44, anchor='nw', fill='white',
                      font=('DejaVu Sans Mono', 12),
                      text='1px red/blue stripes: solid violet = 4:2:0, '
                           'distinct columns = 4:4:4')

    def static_zone(self):
        c = self.c
        y0 = self.h // 2 + 60
        c.create_text(20, y0, anchor='nw', fill='white',
                      font=('DejaVu Sans Mono', 14, 'bold'),
                      text='STATIC ZONE - painted once at startup, '
                           'never repainted')
        for k, rgb in enumerate(PALETTE):
            x = 20 + k * 100
            c.create_rectangle(x, y0 + 30, x + 92, y0 + 130,
                               fill=hx(rgb), width=0)
            c.create_text(x + 46, y0 + 150, text=str(k), fill='white',
                          font=('DejaVu Sans Mono', 16, 'bold'))
        c.create_text(20, y0 + 175, anchor='nw', fill='white',
                      font=('DejaVu Sans Mono', 12),
                      text='wrong HERE = fault present on unchanged '
                           'content (connect-time / full-frame)\n'
                           'clean HERE while MOTION ZONE is wrong = '
                           'fault rides the damage path only')

    def motion_loop(self):
        c = self.c
        y0 = self.h // 2 + 60
        x0 = self.w // 2 + 40
        span = self.w - x0 - 240
        c.delete('mz')
        c.create_text(x0, y0, anchor='nw', fill='white', tags='mz',
                      font=('DejaVu Sans Mono', 14, 'bold'),
                      text='MOTION ZONE - 8 Hz bouncing patch')
        # luma ruler
        for i in range(0, span + 200, 50):
            c.create_line(x0 + i, y0 + 170, x0 + i, y0 + 185,
                          fill='white', tags='mz')
        self.bx += self.bdir * 25
        if self.bx <= 0 or self.bx >= span:
            self.bdir = -self.bdir
            self.bx = max(0, min(span, self.bx))
        n = self.tick % 8 if self.tick >= 0 else 0
        c.create_rectangle(x0 + self.bx, y0 + 30, x0 + self.bx + 200,
                           y0 + 165, fill=hx(PALETTE[n]), width=2,
                           outline='white', tags='mz')
        self.root.after(125, self.motion_loop)

    def run(self):
        self.root.mainloop()


if __name__ == '__main__':
    sys.exit(Probe().run())
