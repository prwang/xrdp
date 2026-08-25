# Slice #137 — Reference-safe AVC444 topology

## Commit boundary

This xrdp-only commit enforces the mandatory shared-decoder topology for an
LC=1/LC=2 pair. It precedes optional LTR rewriting.

Target files are the auxiliary-leaf functions in
`xrdp/xrdp_h264_annexb.c`, `xrdp/xrdp_h264_annexb.h`, their integration in
`xrdp/xrdp_encoder_ffmpeg.c` and `xrdp/xrdp_encoder_ffmpeg.h`, and the leaf
and production-probe cases in `tests/xrdp/test_avc444_h264.c` and
`tests/xrdp/test_avc444_ffmpeg.c`.

## Requirements

* S137-R1: a main picture may reference prior main pictures only. No main
  reference-list operation may select an auxiliary picture.
* S137-R2: an auxiliary encoder IDR shall be transformed into a non-IDR intra
  leaf that is decoded after and may depend only on the corresponding main
  reference where the format requires it. The leaf shall not become a future
  short-term reference.
* S137-R3: SPS/PPS identity, frame numbering, POC and deblocking syntax shall
  remain consistent with the main chain and the selected AVC444 mode.
* S137-R4: input that is not the expected auxiliary IDR, lacks the paired main
  reference VCL, contains unsupported slice structure, or is truncated shall
  fail before output publication.
* S137-R5: the transform shall return a stable typed rejection reason for
  incompatible main/aux parameter fields, unsupported slice structure,
  missing reference VCL, non-IDR auxiliary input, truncation and bounded
  allocation failure. A caller shall not have to infer a static stream
  incompatibility from one generic error.
* S137-R6: the transform shall be bounded and deterministic. It shall preserve
  unrelated NAL units admitted by #131 and shall not rebuild more syntax than
  the topology change requires.
* S137-R7: the built-in libx264 recipe shall explicitly select CABAC. The
  `ultrafast` preset otherwise selects CAVLC, whose slice payload cannot be
  transformed into the supported auxiliary leaf. CAVLC input shall fail with
  the typed entropy reason; it shall not be accepted and retried at runtime.
* S137-R8: extend #133's behavioral probe to instantiate the exact production
  leaf topology: one ordinary main child, one forced-IDR auxiliary child and
  the production leaf transform over one matched pair. It shall use the same
  executable, argv and header policy for both roles. A successful one-child or
  alternating-pair probe shall not certify this topology. A leaf rejection
  shall return the typed content-rejection result and add the untouched main
  and auxiliary encoded access units plus the typed reason to #133's bounded
  first-failure record.

## Required tests and gate

Enable the four `Avc444H264` leaf cases: byte-exact golden transform,
non-IDR rejection, required-main-reference rejection and truncation. Cover
every typed compatibility class, including the CABAC/CAVLC distinction. Add
two independent reference-graph simulations, one for each AVC444 mode, that
prove no main node has an auxiliary ancestor and no leaf is retained. Run
`CK_RUN_SUITE=Avc444H264 tests/xrdp/test_xrdp` and the simulations. Extend the
real-ffmpeg probe test to enable the production auxiliary-leaf topology, spawn
both the ordinary main child and forced-IDR auxiliary child, encode one pair
and run the production transform. The CABAC recipe shall pass; changing only
its entropy mode to CAVLC shall produce the typed content rejection and one
bounded encoded-unit bundle before any activation exists. A single-child
alternating pair is not this test. Run it against the supported FFmpeg 6 CPU
baseline and the repository host ffmpeg, then the README gate.
