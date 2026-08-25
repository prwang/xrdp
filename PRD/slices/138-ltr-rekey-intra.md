# Slice #138 — LTR chains, re-key and scheduled intra refresh

## Commit boundary

This xrdp-only commit adds the optional long-term-reference topology and
bounded intra refresh. It does not add multi-monitor polling or sparse
submission policy.

Target files are LTR portions of `xrdp/xrdp_h264_annexb.c`,
`xrdp/xrdp_h264_annexb.h`, `xrdp/xrdp_encoder_ffmpeg.c`,
`xrdp/xrdp_encoder_ffmpeg.h`, `xrdp/xrdp_encoder.c`,
`xrdp/xrdp_encoder.h`, `xrdp/xrdp_mm.c`,
`tests/xrdp/test_avc444_ltr.c`,
`tests/xrdp/test_avc444_ltr_vectors.h`,
`tests/xrdp/test_avc444_ltr_cut_vectors.h`, and LTR/re-key/intra cases
in `tests/xrdp/test_avc444_ffmpeg.c`. Register the LTR suite in
`tests/xrdp/Makefile.am`, `tests/xrdp/test_xrdp.h` and
`tests/xrdp/test_xrdp_main.c`.

## Requirements

* S138-R1: main and auxiliary views shall use independent long-term slots and
  counters. A main access unit shall never select the auxiliary slot.
* S138-R2: emitted MMCO and reference-list operations shall match the supported
  Windows decoder field sequence and the golden SPS/PPS policy. Existing
  unsupported list modification, B slices and unknown syntax shall fail.
* S138-R3: startup shall seed a valid main chain before any predicted
  auxiliary output. An unseeded auxiliary P picture shall fail loudly.
* S138-R4: frame-number wrap and the configured re-key threshold shall produce
  a bounded encoder-pair restart and full-surface repaint. The normal path
  preserves EGFX surface identity. A midstream IDR from ffmpeg shall be
  normalized into the same chain or rejected; it shall not silently reset
  only one view.
* S138-R5: main and auxiliary intra schedules shall be independent. Requested
  cuts are not considered complete until the validated output observes the
  cut. Failure or timeout shall not advance the schedule.
* S138-R6: reference rewriting shall copy unchanged payload ranges and patch
  only required syntax. It shall enforce output capacity and be byte-stable
  for an identical input/state pair.
* S138-R7: disabling the optional LTR chain shall preserve #137 topology and
  existing reset behavior.
* S138-R8: extend the behavioral probe again when LTR is selected. Using the
  exact production executable, argv, child roles and LTR settings, it shall
  process a matched main/auxiliary pair through both production LTR rewriters
  before reporting support. Leaf-probe success shall not certify LTR. A stream
  shape rejected by either LTR rewriter shall be unavailable before
  activation, with the stable content-rejection class from #133.

## Required tests and gate

The `Avc444Ltr` suite shall contain all 28 current specification cases:
resolution-independent DPB behavior, 512-frame wrap, sparse auxiliary
cadence, slot replacement, IDR restart, missing references, sliding-window
interaction, range violation, golden slots/emission/Windows fields, malformed
and unsupported syntax, high counters, epoch restart, seed/leaf differences,
SPS recipe, exact cut sequences, non-IDR cuts, midstream IDR handling,
observed-vs-requested scheduling, independent auxiliary period and both-mode
paired/sparse cuts. Golden inputs are immutable fixtures.

Enable `test_ffmpeg_ltr_rekey_cycle` and
`test_ffmpeg_scheduled_paired_cut_live` in `Avc444Ffmpeg`. Add a direct LTR
probe case which proves both rewriters execute on the positive configuration
and that an independently incompatible stream shape is rejected by the probe,
not deferred to a live encode. Run both targeted suites, then the README gate.
