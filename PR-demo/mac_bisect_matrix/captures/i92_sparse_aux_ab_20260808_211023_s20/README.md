# i92_sparse_aux_ab_20260808_211023_s20 — dropping the chroma view in motion

BACKLOG #92 / PRD FR-H264-9, first fleet measurement. Runner:
`PR-demo/mac_bisect_matrix/i92_sparse_aux_ab.sh`; analysis reproduced by
`i92_sparse_aux_analyze.py <this directory>` and archived as
`ANALYSIS.txt`. Full record:
`docs/experiments/92-sparse-aux-is-a-byte-lever-not-a-time-one.md`.

## Verdict, first

**It saves 43.8 % of the bytes and zero milliseconds.** The frame
interval is 24.09 ms with chroma on every frame and 24.49 ms with chroma
only when the screen settles — no improvement, and the small difference
is inside the spread between the two control legs (23.67 and 24.51 ms).

**Why, measured rather than guessed:** the two encodes already run at the
same time. The chroma encode takes 12.0 ms and **98–99 % of it is
concurrent with the 13.9 ms luma encode**. The luma encode continues for
only **0.9–1.1 ms** after the chroma one has finished, and that ~1 ms is
the entire amount that deleting the chroma encode could ever have taken
off the wait. Removing work that was hidden behind other work frees
nothing.

**RED, and it needs an owner decision:** chroma went missing for
**1022 ms** against a **1000 ms** guarantee. See below — the cause is
understood and is 22 ms, one frame interval.

## Conditions

* arm **x030**, `localhost/xrdp-bisect:7b550f6ae87f.xx10fa3aa-tf.p2fde5531`
  — xrdp `7b550f6ae87f` (the #92 implementation), xorgxrdp
  `10fa3aa23033`, the same producer as every textflood arm since x014.
* **one monitor at 3840×2400 = 9.22 Mpx**, `SESSION_KIND=textflood`,
  oracle client, VAAPI 444 CQP 20, `intra_refresh_frames = 240`,
  `eager_slot_ack = true`, `wire_window = 2`.
* arm certificate: 6 checks clean, A3 not applicable (see below), 0 black
  frames, at deploy. Copied here as `arm_certificate.txt`.

**One arm, two configurations, interleaved off/on/off/on.** The control
is this same arm with the feature switched off in its ConfigMap, not an
archived number: an unchanged arm on this host has moved 36.8 → 41.8 ms
across a single day (BACKLOG #88), and this sitting's own archive
confirms it — the four legs of `i87_eager_ab_20260806_180910_s20` read
19.1, 17.6, 27.4 and 27.4 ms on arms that were not changed between them.
Against a band that wide, a single new arm compared with x014's archived
18.5 ms would have measured the host, not the feature.

The two bodies differ by exactly two lines:

```
< chroma_refresh_ms = 0        > chroma_refresh_ms = 1000
< chroma_idle_ms = 0           > chroma_idle_ms = 100
```

Each leg archives the gfx.toml that was live for it plus the sha256 read
back **out of the running pod**, so which body produced which number is
not a matter of trust.

## Did the feature apply? (before any rate)

| leg | config | chroma frames | luma-only frames | share | refresh_ms in the binary |
|---|---|---|---|---|---|
| a1 | OFF | 0 | 0 | — | no records |
| a2 | OFF | 0 | 0 | — | no records |
| b1 | ON | 18 | 665 | 2.6 % | 1000 |
| b2 | ON | 18 | 676 | 2.6 % | 1000 |

"chroma frames" is how many frames carried the AVC444 aux view;
"luma-only" is how many shipped the luma view alone. The last column is
`chroma_refresh_ms` read out of the running binary's own trace records,
not out of the config file. The OFF legs emit no `auxdue` records at all,
which is what the code does when the feature is off — a clean control
signal rather than an absence to interpret.

Seen from the other side of the mechanism, how many encoder children the
pump armed per cycle — two per damaged monitor normally, one when chroma
was skipped and that child was neither fed nor waited for:

| leg | config | children armed |
|---|---|---|
| a1 | OFF | 2 on all 712 cycles |
| a2 | OFF | 2 on all 689 cycles |
| b1 | ON | 1 on 665 cycles, 2 on 18 |
| b2 | ON | 1 on 676 cycles, 2 on 18 |

## The rate

"frame interval" is the wall-clock gap between consecutive frames handed
to the transport, one `egress` record each. It reproduces
`e_gate_run.sh`'s own send-to-send figure (23.7 / 24.4 / 24.6 / 24.3 ms)
to within 0.2 ms from a different instrument — the perf ring against the
GFX_TRACE log — which is how the two are checked against each other.

| leg | config | frames | mean ms | p50 | p90 | p99 | period | pump | writes/frame |
|---|---|---|---|---|---|---|---|---|---|
| a1 | OFF | 711 | 23.667 | 23.470 | 26.588 | 29.763 | 23.820 | 22.425 | 4.00 |
| a2 | OFF | 689 | 24.505 | 24.301 | 27.430 | 30.523 | 24.619 | 23.228 | 4.00 |
| b1 | ON | 682 | 24.627 | 24.607 | 27.081 | 29.089 | 24.790 | 24.029 | 3.03 |
| b2 | ON | 694 | 24.342 | 24.120 | 26.549 | 29.621 | 24.462 | 23.676 | 3.03 |

"period" is `pump_beg` to `pump_beg`, one encoder worker cycle. "pump" is
the part of that cycle spent waiting for the ffmpeg children to finish
encoding. "writes/frame" is how many network writes one frame took: a
luma-only frame carries one PDU where a full frame carries two, and
4.00 → 3.03 is exactly that, with the 0.03 being the 18 frames in 683
that still carried chroma.

Condition means (each the mean of that condition's two interleaved legs):

| | control (chroma every frame) | treatment (chroma when settled) | ratio |
|---|---|---|---|
| frame interval | 24.086 ms (41.5 fps) | 24.485 ms (40.8 fps) | 0.984× |
| wait for the ffmpeg children | 22.826 ms | 23.853 ms | 0.957× |
| **bytes per frame on the wire** | **3.468 MB** | **1.948 MB** | **−43.8 %** |

## Why the bytes moved and the time did not

A child cannot begin encoding until its whole raw picture has arrived, so
its encode window is `[feedend, outfirst]` — the same construction
BACKLOG #91 used, and windows are paired by child identity and sequence
number, never by time window.

| leg | config | luma encode | chroma encode | overlap | luma runs on after chroma ends |
|---|---|---|---|---|---|
| a1 | OFF | 13.90 ms | 11.96 ms | 11.87 ms (99.2 %) | 1.12 ms |
| a2 | OFF | 14.08 ms | 12.08 ms | 11.85 ms (98.1 %) | 0.88 ms |
| b1 | ON | 13.80 ms | 12.13 ms | 11.65 ms (96.1 %) | 0.60 ms |
| b2 | ON | 13.95 ms | 12.51 ms | 11.68 ms (93.3 %) | 0.74 ms |

The chroma encode is almost entirely inside the luma encode. The pump
waits for the *later* of the two to finish, not for their sum, so the
most that deleting the chroma encode could take off the wait is the
0.6–1.1 ms by which the luma encode outlasts it. It took off nothing
measurable, which is consistent.

The last two rows are the check that this is not an artefact of the
treatment: on the ON legs the 18 frames that *did* carry chroma show the
same 93–96 % overlap, so the mechanism is unchanged and it is the
opportunity that was never there.

## Where the whole cycle goes, and whether a deeper pipeline could help

Asked 2026-08-08: with the chroma view gone, is the main encoder able to
accept frames faster and wait less — i.e. would letting xrdp run further
ahead raise the rate? The four segments below are built to **sum** to the
cycle, and the residual is printed, so a decomposition that does not
close cannot be read as one.

| leg | config | feed | encode | drain | between | sum | cycle | residual |
|---|---|---|---|---|---|---|---|---|
| a1 | OFF | 7.497 | 13.898 | 1.030 | 1.398 | 23.823 | 23.820 | −0.003 |
| a2 | OFF | 7.987 | 14.081 | 1.160 | 1.391 | 24.618 | 24.619 | +0.001 |
| b1 | ON | 9.054 | 13.803 | 1.172 | 0.762 | 24.791 | 24.790 | −0.001 |
| b2 | ON | 8.515 | 13.950 | 1.211 | 0.783 | 24.459 | 24.462 | +0.003 |

* **feed** — the raw picture going into the child's 1 MiB pipe. One
  picture at 3840×2400 is **13.82 MB**, or 13.2 pipefuls. xrdp's side is
  `vmsplice`, which moves page references and is near-free, so the
  elapsed time here is the **child's `read()` copying the frame in** at
  1.5–1.8 GB/s — not xrdp waiting.
* **encode** — the child holds a whole picture and produces its first
  encoded byte. On this VAAPI arm that includes uploading the frame to
  the GPU.
* **drain** — xrdp reading the rest of the encoded frame.
* **between** — collect, the reference rewrite, emit, slot release.

**There is no idle to reclaim.** The encoder worker had nothing to encode
on 1–3 occasions per leg out of ~690, and every one of those but the
session-start wait is under a millisecond; the median wait is **1.2
microseconds**. The fifo always has a frame. The child is busy 90–92 % of
the cycle, and the remaining 8–10 % is xrdp's own drain and rewrite.

**Arithmetic, not a measurement:** if the feed of the next picture ran
entirely concurrently with the encode of this one, the cycle floor would
be `max(feed, encode) + drain + between` = **15.7–16.6 ms** against the
measured 23.8–24.8. That is the whole prize available to any amount of
pipelining, and it is bounded below by the encode alone at 13.9 ms.

**But xrdp's frames-in-flight knob cannot collect it.** Submitting frame
N+1 earlier does not make the child read it earlier: the child is one
ffmpeg process running `-async_depth 1 -bf 0`, so it cannot read N+1
while encoding N. A deeper pipeline can only pre-fill the pipe, which
holds 1 MiB of the 13.82 MB picture — 7.6 % of the feed, ≈0.6 ms of a
24.5 ms cycle.

One observation without a story attached: the treatment legs' feed is
~1 ms *longer* than the control's (9.05/8.52 against 7.50/7.99) while
their "between" is ~0.6 ms shorter. A plausible mechanism is that with
two children the poll loop wakes more often and tops the main child's
pipe up more frequently, but that is not measured and is not claimed.

## RED — the guarantee was exceeded by one frame

| leg | chroma gaps | mean ms | p50 | p99 | **max** |
|---|---|---|---|---|---|
| b1 | 17 | 958.4 | 1006.8 | 1022.0 | **1022.0** |
| b2 | 17 | 959.2 | 1008.9 | 1021.2 | **1021.2** |

These are the wall-clock gaps between consecutive frames that carried
chroma.
**Read the max, not the mean.** Sixteen of the seventeen gaps in each leg
are the guarantee firing, spread between 1000.0 and 1022.0 ms. The
seventeenth is 130 ms (leg b1) / 136 ms (leg b2), at the very start of
the leg before the payload had begun drawing continuously — the settle
rule doing its job on a quiet screen. That one value is what pulls the
mean down to 958, so the mean here describes nothing real.

The requirement in PRD FR-H264-9 reads "chroma detail is restored
at least every `chroma_refresh_ms`", and 1022 > 1000.

**The cause is understood and is not a bug in the decision.** The
decision exists only *at* a frame — the server has no way to send chroma
between frames — so the earliest chroma can go once the bound expires is
the first frame at or after it. With a 24.5 ms frame interval the
achievable bound is `chroma_refresh_ms + one frame interval` = 1024.5 ms,
and 1022 is that.

**Why CI did not catch it.** `tests/xrdp/test_avc444_chroma_due.c` drives
the decision with frames exactly 20 ms apart and asserts a worst gap of
exactly 1000 ms. 20 divides 1000, so a frame lands exactly on the bound
and the test can never see the overshoot. That is a real weakness in the
test — it passed for a reason that does not generalise — and it is
recorded here rather than quietly fixed, because the choice between
"correct the stated bound" and "fire the guarantee one frame early" is
the owner's.

## What this run does NOT say

* **Nothing about a bandwidth-limited link, which is what the feature is
  for.** This is loopback: bytes are close to free, so a 43.8 % byte
  saving buys nothing here by construction. BACKLOG #98 measured that on
  a limited link `fps = link_rate / frame_bytes` within 3 %, which
  predicts the saving converts to rate there — predicts, not shows.
* **Nothing about what it looks like.** The oracle client acknowledges
  before decoding and renders nothing. Whether an alternating
  luma-only / full stream renders correctly on the macOS and Windows
  clients is untested and is the acceptance criterion that cannot be
  answered offline.
* **Nothing comparable to x014's archived 18.5 ms.** Different xrdp
  build, different day, and a host whose own drift band that day spanned
  17.6–27.4 ms on unchanged arms. The control leg here is the only
  baseline this run supports.

## A3 on the wire, and why the certificate reads SKIP

The arm's certificate reports `A3 cuts are paired across views` as
**SKIP — NOT APPLICABLE**, with the evidence in the line: 5 aux pictures
against 211 main in the 3 s certification window. A3 asserts that the two
views' intra pictures sit at the same view ordinal, and under a sparse
cadence the two views hold different numbers of pictures, so "the same
ordinal" is not a comparison that can be passed or failed. Owner ruling,
2026-08-08. The audit decides that from the **picture counts in the
capture**, never from a config flag, and A3 keeps full force at 1:1 —
which is what ships. The other six checks pass on real bytes from this
image.
