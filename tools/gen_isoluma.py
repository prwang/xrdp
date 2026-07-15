#!/usr/bin/env python3
"""Generate an iso-luminant chroma test pattern for AVC444-vs-AVC420 A/B.

The image isolates detail that lives ONLY in chroma: every colored region is
matched in BT.709 luma (the transform xrdp's converter uses,
Y = (54*R + 183*G + 18*B) >> 8), so the luma plane is flat across each colored
edge. AVC420 keeps full-resolution luma but half-resolution chroma, so it
loses this detail, while AVC444 preserves it. Bright-on-black text, by
contrast, is a luma edge and survives 420 -- which is why it is a poor test.

Blocks:
  A1  1px magenta|green bars (equal luma)  -> 444 keeps stripes; 420 -> flat gray
  A2  2px bars (chroma Nyquist limit)      -> 420 softer but still striped
  B   equal-luma pink text on teal         -> chroma is the only signal
  C   control: white-on-black (luma edge)  -> crisp on both (proves it's chroma)
  D   solarized-light colored code         -> realistic, mild difference

Pure PIL/numpy; no xrdp deps. This is a visual aid, not a test -- the
CI regression that locks down the same mechanism offline is
tests/xrdp/test_avc444_convert.c::test_avc420_isoluminant_chroma_loss.

Usage: gen_isoluma.py [--width W] [--height H] [-o OUT]
Render it full-screen 1:1 in a session and capture through each codec; see
PR-demo/ for the box-specific harness.
"""
import argparse
import numpy as np
from PIL import Image, ImageDraw, ImageFont

FONT_PATH = "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf"

# equal-luma color pairs (BT.709 luma ~ 127)
MAG = (200, 100, 200)   # bars / text foreground family
GRN = (70, 150, 70)     # bars
FG = (190, 110, 130)    # iso-luminant text foreground (pink)
BG = (90, 140, 120)     # iso-luminant text background (teal)


def y709(r, g, b):
    return (54 * r + 183 * g + 18 * b) / 256.0


def font(sz):
    return ImageFont.truetype(FONT_PATH, sz)


def build(W, H):
    img = np.zeros((H, W, 3), np.uint8)
    img[:] = (24, 24, 24)

    pen = Image.fromarray(img)
    d = ImageDraw.Draw(pen)
    d.text((24, 18), "AVC444 vs AVC420 - iso-luminant chroma test "
           "(equal brightness, opposite hue)", font=font(30),
           fill=(235, 235, 235))
    img = np.array(pen)

    # A1: 1px bars (the decisive case)
    a0, a1 = 80, 190
    for x in range(W):
        img[a0:a1, x] = MAG if x % 2 == 0 else GRN
    pen = Image.fromarray(img); d = ImageDraw.Draw(pen)
    d.text((24, a0 + 6), "1px magenta|green bars, equal luma  ->  444 keeps the "
           "stripes; 420 collapses to flat gray", font=font(22),
           fill=(255, 255, 255))
    img = np.array(pen)

    # A2: 2px bars
    b0, b1 = 200, 300
    for x in range(W):
        img[b0:b1, x] = MAG if (x // 2) % 2 == 0 else GRN
    pen = Image.fromarray(img); d = ImageDraw.Draw(pen)
    d.text((24, b0 + 6), "2px bars (at the chroma Nyquist limit): 420 softer "
           "but still striped", font=font(22), fill=(255, 255, 255))
    img = np.array(pen)

    # B: iso-luminant text
    by0, by1 = 320, 470
    img[by0:by1, :] = BG
    pen = Image.fromarray(img); d = ImageDraw.Draw(pen)
    d.text((40, by0 + 24), "4:2:0 loses equal-luma color detail",
           font=font(44), fill=FG)
    d.text((40, by0 + 80), "red on green at the same brightness - fine strokes "
           "blur into the background under 420", font=font(24), fill=FG)
    d.text((24, by1 - 24), "equal-luma pink on teal: chroma is the ONLY signal "
           "here (luma plane is flat)", font=font(20), fill=(20, 20, 20))
    img = np.array(pen)

    # C: control (luma edge survives 420)
    cy0, cy1 = 490, 640
    img[cy0:cy1, :] = (20, 20, 20)
    pen = Image.fromarray(img); d = ImageDraw.Draw(pen)
    d.text((40, cy0 + 30), "4:2:0 loses equal-luma color", font=font(72),
           fill=(230, 230, 230))
    d.text((24, cy1 - 24), "control: white on black is a LUMA edge -> both 444 "
           "and 420 render it crisply", font=font(20), fill=(150, 150, 150))
    img = np.array(pen)

    # D: solarized-light code
    dy0 = 660
    img[dy0:H, :] = (253, 246, 227)
    sol = {"yellow": (181, 137, 0), "orange": (203, 75, 22),
           "red": (220, 50, 47), "magenta": (211, 54, 130),
           "violet": (108, 113, 196), "blue": (38, 139, 210),
           "cyan": (42, 161, 152), "green": (133, 153, 0),
           "base00": (101, 123, 131)}
    pen = Image.fromarray(img); d = ImageDraw.Draw(pen)
    fc = font(26)
    d.text((24, dy0 + 8), "solarized light: colored tokens on a light "
           "background (realistic editor theme)", font=font(20),
           fill=(101, 123, 131))
    lines = [
        [("static ", "cyan"), ("int", "blue"), (" ", "base00"),
         ("xrdp_encode", "green"), ("(", "base00"), ("void", "cyan"),
         (" *", "base00"), ("self", "orange"), (")", "base00")],
        [("    ", "base00"), ("return", "magenta"), (" ", "base00"),
         ("AVC420", "red"), (" ", "base00"), ("?", "violet"), (" ", "base00"),
         ("0x000B", "yellow"), (" : ", "violet"), ("0x000F", "yellow"),
         (";", "base00")],
        [("    ", "base00"),
         ("/* red, magenta, violet, blue keywords all sit ", "green")],
        [("       ", "base00"),
         ("at similar luma - watch them blur under 420 */", "green")],
    ]
    yy = dy0 + 54
    for ln in lines:
        xx = 30
        for txt, colr in ln:
            d.text((xx, yy), txt, font=fc, fill=sol[colr])
            xx += d.textlength(txt, font=fc)
        yy += 40
    return np.array(pen)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--width", type=int, default=1920)
    ap.add_argument("--height", type=int, default=1080)
    ap.add_argument("-o", "--out", default="isoluma.png")
    a = ap.parse_args()
    img = build(a.width, a.height)
    Image.fromarray(img).save(a.out)
    print("wrote %s %dx%d  Ymag=%.1f Ygrn=%.1f Yfg=%.1f Ybg=%.1f" %
          (a.out, a.width, a.height, y709(*MAG), y709(*GRN),
           y709(*FG), y709(*BG)))


if __name__ == "__main__":
    main()
