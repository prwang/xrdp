# Curated media evidence

This is a minimal visual history of five distinct client bugs. Media is kept
only when it shows a different evidentiary stage: the reported symptom, a
material failed direction, a decisive diagnostic behavior, or the fixed
result. The source and test records, not screenshots alone, establish cause.

The initial 2026-08-22 import contained 19 artifacts. Nine were excluded before
the package's first commit: one color-key screenshot with no recoverable defect
identity, six duplicate full/cropped views of the dual-monitor ghost, one
Windows case-H quality screenshot unrelated to the Mac color fault, and one
still frame duplicated by the retained source recording. The remaining ten
files tell the first four progressions below without repeating a stage. Four
later #125 artifacts add two source states and their exact enlarged crops.

## Naming, time and integrity

Names use `<timestamp>_<observed-content>.<ext>`.

* `YYYYMMDDTHHMMSSZ` is UTC. When media has no creation metadata it is
  filesystem birth time, the first recorded appearance on this filesystem.
* `YYYYMMDDTHHMMSS-local` preserves a timestamp in the original screenshot
  filename. Its timezone was not embedded, so it is not guessed.
* Renaming and categorization did not alter the screenshots. The retained
  movie is the deliberately shortened and transcoded excerpt documented
  below. `SHA256SUMS` pins all fourteen checked-in artifacts.
* Every retained artifact is an ordinary Git object. The 3,465,528-byte MP4
  does not require Git LFS.

## 1. AVC444v1 magenta fringe on colored text

Record: `tests/xrdp/avc444/FINDINGS_magenta_burr.md`.

| time | stage | evidence or change | meaning |
|---|---|---|---|
| 2026-07-14 01:22:45Z | bug | `magenta_burr/20260714T012245Z_avc444v1-green-text-magenta-fringe.png` | Saturated green terminal text acquires a magenta fringe under AVC444v1. |
| 2026-07-14 02:06:53Z | candidate result | `magenta_burr/20260714T020653Z_avc444v2-green-text-clean.png` | The original name labels this visibly clean comparison as v2. It predates the commits and has no recoverable build identity, so it does not validate them. |
| 2026-07-14 09:10–14:21Z | cause and final fix | `78448188`, `f5c148dc`, `40746436`, `c0bbcf32`, `91512cfd` | The faithful decoder repro showed that v1's chroma reconstruction creates the fringe. Emitting ChromaV2 as codec `0x000F`, gated on v2 client capability, fixed it; clients without v2 retain the v1 fallback. |

## 2. mstsc resize-comb / chroma split

Record: `tests/xrdp/avc444/repro_mbparity/FINDINGS_mstsc_split.md`.

| time | stage | evidence or change | meaning |
|---|---|---|---|
| 2026-07-14 18:46:55Z | bug | `resize_comb/20260714T184655Z_windows-rdcman-avc444-desktop-edge-fringe.png` | RDCMan shows colored fringing at high-contrast desktop edges. The exact build is not recoverable, so this establishes appearance only. |
| 2026-07-14 20:45:59Z | attempted fix | `80c41c0b` | A temporary metablock even-alignment change targeted region geometry. It did not address where the client splits the packed U and V halves. |
| 2026-07-14 21:24:50Z | bug persists | `resize_comb/20260714T212450Z_windows-rdcman-chroma-split-corruption.png` | A later RDCMan frame still shows the severe purple/green split class. Its timestamp follows the temporary checkpoint, but no build identity survives; the deterministic reproducer below, not this image alone, rejected metablock alignment as the fix. |
| 2026-07-14 22:30–23:13Z | cause and final fix | `ea62e029`, `b8183ddc` | The reproducer proved mstsc derives the U/V split from a 32-aligned width while FreeRDP derives it from a 16-aligned width. `chroma_align` made coded width and packing agree with the selected client family; the shipped default is 32. |

## 3. Dual-monitor drag ghosts

Harness and resolution: `PR-demo/multimon_burr/README.md`.

| time | stage | evidence or change | meaning |
|---|---|---|---|
| 2026-07-25 11:45:41 local | bug | `multimon_drag_ghost/20260725T114541-local_windows-dual-monitor-drag-ghost.png` | Owner's dual-monitor screenshot shows persistent thin edges after window drags. |
| 2026-07-25 12:41:24Z | reproduced | `multimon_drag_ghost/20260725T124124Z_dual-monitor-drag-ghost-monitor-seam.png` | Truth/client crop shows a dashed vertical ghost continuing across the seam, the distinctive cross-monitor failure. |
| 2026-07-25 13:50–14:24Z | cause and fix | xrdp `cc69ef05`, `0070ceb5`; xorgxrdp `dd431cc` | Every monitor wrote at offset zero in one capture shmem region. The other monitor overwrote persistent plane bytes, and the metablock fringe exposed them. The paired fix assigns disjoint per-monitor regions and transmits the offset. |
| 2026-07-25 14:46–14:50Z | corrected gate and fixed result | `c1c8e03a`; `multimon_drag_ghost/20260725T145013Z_dual-monitor-drag-ghost-fixed-clean.png` | The oracle was corrected for a parked live window and delayed pipeline flush. The settled dual- and single-monitor gates were clean, followed by owner onscreen PASS. |

This bug and fix are not the later #64 `rect_id` acknowledgement hypothesis,
which `docs/experiments/64-rect-id-ack-ghost.md` refuted.

## 4. macOS Windows App wrong-color accumulation

Mechanism and corrections:
`PR-demo/mac_bisect_matrix/CROSS_VIEW_REFERENCE_PROOF.md`.

| time | stage | evidence or change | meaning |
|---|---|---|---|
| 2026-07-26 21:41:53 local | bug | `mac_wrong_color/20260726T214153-local_macos-windows-app-full-screen-wrong-color.png` | The real macOS client shows the full-screen magenta/cyan wrong-color failure under AVC444. |
| 2026-07-26 22:37:47 local | provisional detour | `mac_wrong_color/20260726T223747-local_macos-windows-app-localized-wrong-color-case-h.png`; `a4cbff7b` | A VAAPI/CBR stand-in produced damage-localized color corruption. Later bitstream analysis showed case H was all-intra and could not prove the real NVENC mechanism; it remains here as the explicitly quarantined failed direction. |
| 2026-07-27 20:17:04Z | dynamic evidence | `mac_wrong_color/20260822T130353Z_macos-windows-app-wrong-color-two-minute-excerpt.mp4`; `37dbecfc` | This 119.598 s checked-in excerpt covers raw source 00:12 through 02:11.598 and retains the update-by-update chroma error accumulation. Raw-source t=110 is 01:38 after playback begins. The contemporaneous exact-one-frame-lag/header interpretation was superseded by the later DPB/reference proof. |
| 2026-07-27 21:56–22:56Z | cause and fix | `df443656`, `233d342e` | Per-view decoding reproduced the corruption and proved cross-view inter prediction poisoned a decoder using per-view reference topology. The fix partitions references: main pictures reference main pictures, and auxiliary pictures become non-reference intra leaves. |
| 2026-07-27 23:08Z to 2026-07-28 01:13Z | final validation | `b5ec5124`, `3274db32`, `39bb08a4` | The T4 wire passed auxiliary-drop identity; xfreerdp rendered clean; the owner confirmed the macOS client clean; reference partitioning then became unconditional. No separate post-fix screenshot was present in the imported media. |

The exact 209,610,437-byte QuickTime source is not checked in. It is retained
locally, mode `0600`, at
`/root/xrdp-media-backups/20260727T201704Z_macos-windows-app-chroma-probe-wrong-color_source.mov`.
Its SHA-256 is
`6fa727f79c9685d690b3e9eda3c11bfbda61584e17437738b6a41597d191cc26`,
its embedded creation time is 2026-07-27 20:17:04Z, and its duration is
224.068333 seconds.

The checked-in excerpt was produced from that source with the owner's recipe,
changing only the end time from 01:12 to 02:12 so source t=110 remains in the
evidence:

```sh
ffmpeg -copyts -ss 0:0:12 -to 0:2:12 -i input.mov \
    -vf "scale=1920:1080" -c:v libx265 -preset slower -b:v 2M \
    -enc_time_base:v demux -fps_mode:v passthrough \
    -video_track_timescale 90000 -tag:v hvc1 -c:a aac -b:a 128k output.mp4
```

## 5. Sparse AVC444v2 static chroma stall

Record: `docs/experiments/125-sparse-chroma-qualification.md`.

| time | stage | evidence or change | meaning |
|---|---|---|---|
| 2026-08-23 01:40:49 local | intended state | `sparse_chroma_stall/20260823T014049-local_sparse-codescroll-color-a.png` and its `hash-crop-x64-nearest` derivative | The Solarized `#` glyph is bright magenta with full chroma. |
| 2026-08-23 01:40:44 local | bug | `sparse_chroma_stall/20260823T014044-local_sparse-codescroll-color-b.png` and its `hash-crop-x64-nearest` derivative | After sparse updates and motion stops, the same glyph can remain faint and chroma-reduced. |

The category README records the exact inclusive crop, deterministic ffmpeg
operation, pixel statistics and source names. The enlarged files are views of
the same two evidence states, not two additional chronological stages.

## Use in the public PR

BACKLOG #300 owns the reviewer-facing evidence package. These timelines may
explain why a fix exists, but #300 must reconcile source paths and final claims
against the #142 clean-room tree. Historical research inputs are indexed in
`PR-demo/public_pr/research/README.md`.
