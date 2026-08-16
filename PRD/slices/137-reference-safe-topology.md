# Slice #137 — Reference-safe AVC444 topology

## Commit boundary

This xrdp-only commit enforces the mandatory shared-decoder topology for an
LC=1/LC=2 pair. It precedes optional LTR rewriting.

Target files are the auxiliary-leaf functions in
`xrdp/xrdp_h264_annexb.c`, `xrdp/xrdp_h264_annexb.h`, their integration in
`xrdp/xrdp_encoder_ffmpeg.c`, and the leaf cases in
`tests/xrdp/test_avc444_h264.c`.

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
* S137-R5: the transform shall be bounded and deterministic. It shall preserve
  unrelated NAL units admitted by #131 and shall not rebuild more syntax than
  the topology change requires.

## Required tests and gate

Enable the four `Avc444H264` leaf cases: byte-exact golden transform,
non-IDR rejection, required-main-reference rejection and truncation. Add two
independent reference-graph simulations, one for each AVC444 mode, that prove
no main node has an auxiliary ancestor and no leaf is retained. Run
`CK_RUN_SUITE=Avc444H264 tests/xrdp/test_xrdp` and the simulations, then the
README gate.
