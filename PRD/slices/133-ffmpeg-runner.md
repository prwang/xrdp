# Slice #133 — Secure ffmpeg runner and behavioral probe

## Commit boundary

This xrdp-only commit adds a test-reachable stock-ffmpeg runner for one main
or main/aux pair. LTR, multi-monitor polling and server selection are later.

Target files are `xrdp/xrdp_encoder_ffmpeg.c`,
`xrdp/xrdp_encoder_ffmpeg.h`, `tests/xrdp/test_avc444_ffmpeg.c`, and
`tests/xrdp/gfx/fake_encoder_hang.sh`. Register source and suite in
`xrdp/Makefile.am`, `tests/xrdp/Makefile.am`,
`tests/xrdp/test_xrdp.h` and `tests/xrdp/test_xrdp_main.c`.

## Requirements

* S133-R1: construct a fixed argv vector from validated executable/profile
  fields and call the repository's spawn wrappers without a shell. No client
  value or unsanitized arbitrary option becomes a format string or command.
  Validated administrator encoder tokens shall pass unchanged as individual
  argv elements between xrdp's fixed input and output contract tokens.
* S133-R2: each child shall have one rawvideo input and one NUT output.
  Intended endpoints survive exec; all others are close-on-exec. Parent ends
  are nonblocking and owned by one handle.
* S133-R3: input shall be committed only after the complete view is available.
  The pump shall handle partial writes and reads, `EINTR`, `EAGAIN`, child exit
  and a monotonic deadline without blocking one direction behind the other.
  Default stream-ready, picture and pair deadlines are each 2000 ms; a batch
  uses one shared deadline rather than multiplying it by the child count.
* S133-R4: request at least 64 KiB pipe capacity and verify the effective size.
  If the host cannot supply the required size, fail the backend explicitly;
  do not run a timing-sensitive path with a smaller silent substitute.
  Input shall be fed exclusively with nonblocking `vmsplice`; the borrowed
  slot bytes remain owned until fully consumed. There is no `write()` fallback.
* S133-R5: parse output with #130, validate it with #131, enforce one packet
  per submitted view and bounded total bytes, and associate by FIFO submission
  identity. Extra, missing or late packets fail.
* S133-R6: before capability confirmation, run a bounded behavioral probe at
  representative aligned geometry. It shall verify NUT, Annex-B, parameter
  placement, exactly one SPS per keyframe, and supported `dump_extra`
  behavior. A timeout or mismatch marks the backend unavailable.
* S133-R7: the ffmpeg input command shall include an explicit one-frame
  `probesize` so small geometry cannot wait for additional frames.
* S133-R8: close/recycle shall terminate, reap and release every descriptor
  and parser buffer. Partial startup shall use the same cleanup path.
* S133-R9: `dump_extra` shall statically select whether ffmpeg reinserts global
  parameter sets. `strip_sei` shall add ffmpeg's complete-NAL SEI removal
  filter before Annex-B conversion; it shall not scan VCL bytes in xrdp. The
  behavioral probe shall verify the selected policy.
* S133-R10: the runner shall emit `feedend` with `sequence`, `main` and
  `monitor` when a whole input view has been consumed, and `outfirst` with
  those fields plus `bytes` when its first output arrives. These events exist
  only in a trace-enabled build.

## Required tests and gate

`Avc444Ffmpeg` shall initially enable probe success, global-header handling,
duplicate-header rejection, timeout classification, one-SPS policy,
synchronous pair identity, synchronous single identity and resize/reap.
Add a ninth deterministic static-bitstream-filter argv/policy case. Tests
requiring LTR, sparse cadence, pump sets or server config belong to
later slices. The fake hanging encoder shall prove the deadline and reap path.
Before the result can be green, verify `/usr/bin/ffmpeg` is an executable stock
build with libx264, then run
`XRDP_TEST_FFMPEG_PATH=/usr/bin/ffmpeg CK_RUN_SUITE=Avc444Ffmpeg tests/xrdp/test_xrdp`.
A missing executable or an early-return "skip" is not a pass. Then run the
README gate.
