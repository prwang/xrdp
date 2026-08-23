# Sparse AVC444v2 chroma stall

These two screenshots are the owner-observed stable states from BACKLOG #125.
The Solarized Dark code-scroll printed `#include` lines, was stopped with
Control-C, and was then manually scrolled twice. The same small hash glyph
remained either bright magenta (Color A) or faint purple-magenta (Color B).

The original filenames were `Sparse_inconsistentColor-A 2026-08-23
014049.png` and `Sparse_inconsistentColor-B 2026-08-23 014044.png`. Their
timestamps contain no timezone, so the normalized names retain `-local`
rather than guessing UTC. Moving and renaming did not change the images.

| artifact | role |
|---|---|
| `20260823T014049-local_sparse-codescroll-color-a.png` | full Color-A screenshot |
| `20260823T014044-local_sparse-codescroll-color-b.png` | full Color-B screenshot |
| `20260823T014049-local_sparse-codescroll-color-a_hash-crop-x64-nearest.png` | exact Color-A hash crop, enlarged |
| `20260823T014044-local_sparse-codescroll-color-b_hash-crop-x64-nearest.png` | exact Color-B hash crop, enlarged |

The owner specified inclusive coordinates `(151,125)--(155,131)`, a five by
seven pixel region in ffmpeg's zero-based coordinate system. The derived views
were made without filtering:

```sh
ffmpeg -i source.png -vf 'crop=5:7:151:125,scale=320:448:flags=neighbor' \
    enlarged.png
```

Downscaling either enlarged view to five by seven with nearest-neighbour
selection reproduces its source crop byte-for-byte. No generated or
interpolated pixels enter this evidence.

For the same 20 foreground pixels selected by Color A red greater than 100,
the mean RGB changes from `(203.7, 53.1, 123.5)` in A to
`(149.8, 55.7, 111.5)` in B. The large red loss with nearly unchanged green
is a chroma/hue change, not uniform dimming. It is consistent with the
main-only LC=1 update replacing one-pixel 4:4:4 detail with its 4:2:0
reconstruction and no later chroma restoration reaching the static glyph.
