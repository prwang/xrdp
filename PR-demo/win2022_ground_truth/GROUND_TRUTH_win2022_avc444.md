# Ground-truth AVC444 wire capture — real Windows Server 2022 + NVIDIA A10 GRID

Host: 43.98.187.122, Windows Server 2022 (build 20348), NVIDIA A10-4Q vGPU
(hardware NVENC). GPO set: `AVC444ModePreferred=1`, `AVCHardwareEncodePreferred=1`
(HKLM\SOFTWARE\Policies\Microsoft\Windows NT\Terminal Services).

Captured with patched FreeRDP 3.15.0 rdpgfx wire dumper. Two payloads:
- `gfxwin_anim`   — ChromaAnim (isoluminant hue rotation): 356 frames
- `gfxwin_scroll` — ChromaScroll (scrolling saturated bars + colored text): 447 frames
Total 803 AVC444 frames.

## What real Windows actually emits

| property | real Windows (this host) |
|---|---|
| codec | **0x000F (AVC444v2) exclusively** — never v1 (0x000E) |
| first frame | **LC=1 luma-only IDR** — `[AUD,SPS,PPS,IDR,IDR,IDR]`, no aux |
| cadence | **LC=1 ~93%**, LC=2 ~7% (56/803), **LC=0 ~0.25% (2/803)** |
| aux (chroma) NALs | **always `[AUD,P,P,P]`** — 58/58 aux instances; NEVER IDR/SPS |
| IDR/SPS/PPS count | **exactly one**, in seq0 main (luma) view; one shared decode ctx |
| LC=2 region | full-surface OR sub-rect, standalone P-slice chroma catch-up |
| LC=0 regions | **stream1 and stream2 tile DISJOINT, non-overlapping rects** |

LC=0 samples (both disjoint, both P-slice-only):
- anim seq18:   s1=(0,752,1280,800)         s2=(0,0,1280,752)          — strip vs top
- scroll seq25: s1=(1104,752,1152,800)      s2=(0,752,1104,800)+(1152,752,1280,800) — a block vs the rest of its row

### Decoder model implied
ONE shared H.264 context. Bootstraps with a luma-only LC=1 IDR. Main updates,
aux chroma catch-up (LC=2), and combined (LC=0) are ALL P-slices on that single
established reference chain. The "auxiliary chroma view" is temporally
interleaved into the same stream as ordinary P-frames; the RDP layer routes each
decoded AU to main-vs-aux via the LC field. Aux never needs its own IDR because
it is never a separate decoder.

## What xrdp emits (our dumps: gfxdump_444v1, gfxdump_aud)

| property | xrdp |
|---|---|
| codec | 0x000E (v1) in 444v1 mode |
| first AVC frame | **LC=0 dual-stream** — s1=IDR + s2=**bare P-slice**, SAME full surface |
| cadence | **LC=0 every frame**, both streams same full-surface region |
| LC=1 / LC=2 | **never emitted** (PRD NG-6: deferral not implemented) |

xrdp first AVC frame (gfxdump_444v1 seq296, LC=0):
- s1(main) (0,0,1280,800): `[AUD,SPS,PPS,SEI,IDR]`
- s2(aux)  (0,0,1280,800): `[AUD,P]`   ← aux bootstrapped as a P-slice, same region

## The delta (candidate root cause of macOS/VideoToolbox black screen)

xrdp bootstraps AVC444 with a **same-region dual-stream LC=0 whose aux is a
P-slice** and repeats LC=0 every frame — a construction **real Windows never
produces**. Real Windows: (a) bootstraps **luma-only LC=1 IDR**, (b) keeps one
shared decode context, (c) sends aux **only as P-slices on an established
reference chain**, (d) uses **LC=2 deferral** as the normal chroma mechanism,
(e) when it does use LC=0, the two streams cover **disjoint** regions, (f) uses
**v2 (0x000F)**. mstsc/UWP (lenient DXVA) accept xrdp's form; Apple VideoToolbox
(stricter) evidently rejects it → black.

## H1 vs H2 status
- **H1** (Mac categorically can't do inline LC=0): **weakened** — real Windows
  DOES emit LC=0. Refuted outright if the Mac renders this host's session.
- **H2** (our LC=0 is malformed / non-Windows-like): **strongly supported**,
  with the concrete mechanism above.

## Decisive remaining test (onscreen — owner)
Point the **macOS Windows App directly at 43.98.187.122** (normal RDP host).
- renders  ⇒ Mac's AVC444v2/LC=0 path works ⇒ **H2 confirmed** ⇒ fix xrdp to
  emit Windows-like (luma-first LC=1 IDR bootstrap + deferred LC=2, v2, disjoint LC=0).
- blacks   ⇒ problem is broader than stream construction; investigate
  negotiation / caps / VideoToolbox init path.

Artifacts: /work/vm/gfxwin_anim, /work/vm/gfxwin_scroll (raw .bin + manifest.txt),
parsers /work/vm/parse444.py, /work/vm/scan444.py.

## Addendum (2026-07-28): reference machinery — Windows partitions via LTR slots

Full trace_headers sweep of the assembled gfxwin_anim decode-order stream
(357 AUs, 9 aux at ordinals [8,13,19,20,26,97,100,217,311], 3 slices/AU;
/tmp assembly script in session log, counts are `count field = value`):

    1068  ref_pic_list_modification_flag_l0 = 1    (EVERY P slice)
    1068  modification_of_pic_nums_idc = 2         (select by long_term_pic_num)
    1044  long_term_pic_num = 0     24  long_term_pic_num = 1
    1068  adaptive_ref_pic_marking_mode_flag = 1   (EVERY P slice)
    1068  memory_management_control_operation = 6  (mark CURRENT pic long-term)
    1068  memory_management_control_operation = 0  (terminator)
    1041  long_term_frame_idx = 0   27  long_term_frame_idx = 1
       3  long_term_reference_flag = 1             (IDR self-marks LT)
       2  max_num_ref_frames = 3;  gaps_in_frame_num_allowed = 0

Interpretation:

- Windows implements reference partitioning with NAMED LONG-TERM SLOTS:
  every picture marks itself long-term (mmco 6) into a per-view slot —
  LT idx 0 = main, LT idx 1 = aux (27 = 9 aux AUs x 3 slices, exact) —
  and every P slice explicitly selects its own view's slot by
  long_term_pic_num via a per-slice ref_pic_list_modification (idc 2).
  Slot reassignment replaces the previous occupant, so there is NO
  sliding-window dependence, NO eviction pinning and NO PicNum
  arithmetic: the marking and modification syntax are CONSTANTS per
  view. The IDR seeds LT0 via long_term_reference_flag=1.
- First-aux quirk: 1044 = 348x3 and 24 = 8x3 — the FIRST aux after the
  IDR selects long_term_pic_num=0, i.e. references the MAIN IDR. The
  remaining 8 aux AUs reference the previous aux (LT1).
- Invariance consequences: main-chain independence holds (the 348/348
  drop-aux bit-identity above is explained: main only ever selects
  LT0, which only main frames occupy). The STRONG per-view invariant
  does NOT hold: an aux-only feed breaks on the first aux (refs LT0)
  and is frame_num-gapped with gaps_allowed=0 — yet every RDP client
  including macOS VideoToolbox renders this stream. The necessary
  client-compat condition is therefore the WEAK invariant
  (main independence + deterministic interleaved resolution), and the
  Windows LTR shape is its gold-standard implementation.
