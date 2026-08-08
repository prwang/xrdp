# i92_sparse_aux_ab_20260808_233519_s20 — the same four legs, with the pipes unclamped

The **after** half of a before/after pair. Identical arm, identical
image, identical payload, identical script, run two and a half hours
after `i92_sparse_aux_ab_20260808_211023_s20`. **The only thing that
changed on the box is a host sysctl:** the owner raised
`fs/pipe-user-pages-soft` from 16384 pages (64 MiB) to 262144 (1 GiB),
so container root — host uid 1000, what xrdp runs as — is no longer over
the per-user pipe budget. Diagnosis and mechanism:
`captures/i103_pipe_handover_20260808/` and BACKLOG #103.

## Verdict, first

**The frame period fell from 24.5 ms to 17.0 ms — 41 fps to 59 — with no
code change at all.** The handover of the raw picture to the encoder
child fell from 8.5–9.1 ms to 1.86–2.05 ms, and the read-back of the
encoded frame from 1.03–1.21 ms to 0.375–0.383 ms.

**And the encode is unchanged**, 13.80–14.36 ms on all eight legs across
both runs. That invariance is the control: the sysctl moved the two
segments that cross a pipe and nothing else.

| segment | before (8 KiB pipes) | after (1 MiB pipes) |
|---|---|---|
| feed — raw picture into the child | 7.50 / 7.99 / 9.05 / 8.52 | 1.95 / 2.05 / 1.86 / 1.96 |
| encode — the child's own work | 13.90 / 14.08 / 13.80 / 13.95 | 14.05 / 14.36 / 13.84 / 13.83 |
| drain — encoded frame back out | 1.03 / 1.16 / 1.17 / 1.21 | 0.38 / **9.89** / 0.38 / 0.38 |
| between — collect, rewrite, emit | 1.40 / 1.39 / 0.76 / 0.78 | 1.61 / 1.61 / 0.95 / 0.95 |
| **cycle** | 23.82 / 24.62 / 24.79 / 24.46 | 17.99 / **27.91** / 17.02 / 17.11 |

Legs in order a1, a2, b1, b2. The two bold figures are the anomaly
below.

I projected ~19 ms from the standalone bench before this ran. Measured
17.0. The projection was conservative.

## RED — leg a2 is not usable, and it nearly produced a false headline

Leg a2 ran at 27.7 ms against leg a1's 17.9 in the **same condition**.
Averaging the two would have given the control condition 22.8 ms against
the treatment's 17.0 and produced a **"1.343×"** that is entirely an
artefact of one leg.

Its own decomposition says what went wrong, and it is not the treatment:

* **drain 9.893 ms** against 0.375–0.383 ms on every other leg of this
  run — 26× — and drain is xrdp reading bytes back out, downstream of
  everything the treatment touches;
* the **chroma encode took 22.69 ms** against ~12.2 ms elsewhere, so the
  two encodes overlapped only 57.9 % instead of 97.5–99.9 %.

Both children slow and the read-back slow together points at the host,
not at this build. No cause is claimed — nothing in this capture can see
outside the container.

`i92_sparse_aux_analyze.py` now **refuses to print a ratio** when a
condition's own two legs disagree by more than 15 %, and names the legs
instead. The interleave exists to bracket host drift; a bracket this wide
has caught something, and averaging across it hides exactly what it
caught.

## What still reads cleanly

**The treatment legs agree to 0.13 ms** (16.915 and 17.044), as they did
before the sysctl (24.627 and 24.342). So the before/after on the
sysctl — 24.5 → 17.0 ms — rests on four internally consistent legs and
does not involve a2 at all.

**The byte saving reproduces independently:** 44.5 % here against 43.8 %
in the earlier run, on a different day's host state, with per-leg spreads
of 3.452/3.485 MB (control) and 1.926/1.924 MB (treatment).

**The chroma cadence still buys no time**, which is the conclusion the
first run reached. Against the one healthy control leg, 17.887 ms
control versus 16.980 ms treatment — about 5 %, on a single control leg,
and not a number to lean on. The mechanism is unchanged: the chroma
encode still runs 97.5–97.9 % inside the luma encode.

**The corrected guarantee bound is confirmed on new data.** Chroma went
missing for at most 1015.8 and 1017.9 ms, against frame intervals of
16.9 and 17.0 ms — i.e. `chroma_refresh_ms` + one frame interval,
exactly as the wording corrected earlier today now says. Before the
sysctl, when frames were 24.5 ms apart, the same overshoot was 22 ms. The
model tracks the frame rate, which is what it claims to do.

## The consequence for everything else in `captures/`

**Every archived fleet number in this tree was taken with clamped pipes.**
This run is the first that was not. Any comparison of a future run
against an archived one is now invalid unless the archive is re-measured
— the box got roughly 7 ms per frame faster at 3840×2400 for reasons
that have nothing to do with any xrdp change. Ratios inside a single
archived capture are still fine; absolute numbers across the sysctl
boundary are not.
