# Slice #141 — Sparse auxiliary cadence

## Commit boundary

This xrdp-only commit makes LC=2 optional per frame while preserving its
reference topology and bounded refresh. It does not add another thread or an
after-encode discard path.

Target files are sparse-policy portions of `xrdp/xrdp_encoder.c`,
`xrdp/xrdp_encoder.h`, `xrdp/xrdp_encoder_ffmpeg.c`,
`xrdp/xrdp_encoder_ffmpeg.h`, `xrdp/xrdp_h264_annexb.c`,
`xrdp/xrdp_h264_annexb.h`, `xrdp/xrdp_mm.c`,
`tests/xrdp/test_avc444_chroma_due.c`,
`tests/xrdp/test_avc444_convert.c`, and sparse cases in the LTR, ffmpeg and
encoder suites. Register `Avc444ChromaDue` in
`tests/xrdp/Makefile.am`, `tests/xrdp/test_xrdp.h` and
`tests/xrdp/test_xrdp_main.c`.

## Requirements

* S141-R1: zero refresh interval disables sparsity and emits auxiliary output
  for every AVC444 frame, byte-equivalent to #140.
* S141-R2: the decision to omit auxiliary work shall occur before input commit
  to the auxiliary child. A skipped frame shall not advance its ffmpeg, DPB,
  frame-number, observed-cut or LTR state.
* S141-R3: under continuous motion, the interval between published chroma
  updates shall not exceed `chroma_refresh_ms` plus one actual frame period.
* S141-R4: after the configured settle interval, auxiliary submissions shall
  be limited by `chroma_idle_ms`; setting idle to zero disables the settle
  mechanism but not the refresh guarantee. A final main-only update shall not
  leave any region displayed at 4:2:0 indefinitely: if no newer frame arrives
  before the settle deadline, the encoder worker shall ask the main thread for
  one full-screen xorgxrdp capture. That current main-plus-auxiliary update
  shall restore static regions. A newer main-only frame rearms the deadline;
  a main-plus-auxiliary frame cancels it; expiry is one-shot.
* S141-R5: bootstrap/reset, geometry change, reference re-key and a due
  auxiliary intra cut shall force the auxiliary work needed to leave a
  decodable state.
* S141-R6: main and auxiliary cut schedules remain independent. A skipped
  auxiliary frame shall not falsely satisfy a requested cut.
* S141-R7: time arithmetic shall be monotonic and overflow-safe. Invalid
  intervals are refused, not clamped silently.
* S141-R8: a trace-enabled build shall emit `auxdue` with monitor, decision,
  elapsed time since auxiliary and previous submissions, and configured
  refresh interval. Its schema-1 fields are `monitor`, `due`, `since_aux_ms`,
  `since_previous_ms` and `refresh_ms`. A disabled build shall not perform
  these diagnostic reads. A trailing request shall emit
  `chroma_restore_request` with `monitor_mask`; it is a settle-rate event,
  never a per-frame `LOG()`.
* S141-R9: trailing restoration shall neither retain borrowed capture pages
  past collect nor copy a full capture per moving frame. Module transport I/O
  shall remain on the xrdp main thread. Failure to queue or send a restoration
  request shall be logged as an error, not masked by silently switching dense.

## Required tests and gate

All ten `Avc444ChromaDue` cases shall pass: disabled equivalence,
continuous-motion guarantee, gap bound, refresh-plus-one-frame definition,
settle clamp, rate bound, no-idle behavior, bootstrap/boundaries, trailing
deadline behavior and overflow safety. `Avc444Convert` shall independently
model a full-chroma display followed by main-only 4:2:0 persistence and later
auxiliary restoration. Enable
the sparse DPB and independent-cut cases in `Avc444Ltr` and
`test_ffmpeg_sparse_aux_independent_schedules_live`. Configuration tests shall
be added only when the keys become reachable in #142. Run all affected suites
and the README gate. This internal slice has no real-client or throughput
gate. Live client replay occurs only after the backend becomes selectable in
#142; it is not a substitute for these deterministic tests.
