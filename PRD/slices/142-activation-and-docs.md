# Slice #142 — Configuration, activation and final gate

## Commit boundary

Only this final commit exposes the assembled backend to a session. It adds
configuration, capability activation and operator documentation for behavior
already implemented and green.

Target files are `xrdp/xrdp_tconfig.c`, `xrdp/xrdp_tconfig.h`,
`xrdp/xrdp_types.h`, `xrdp/xrdp_mm.c`, `xrdp/gfx.toml`,
`docs/man/gfx.toml.5.in`, `tests/xrdp/test_tconfig.c`,
`tests/xrdp/test_avc444_ffmpeg.c`, `tests/xrdp/test_xrdp_egfx.c`, and these
fixtures, all below `tests/xrdp/gfx/`:
`gfx_avc444_ffmpeg.toml`, `gfx_avc444_empty_args.toml`,
`gfx_avc444_intra_refresh.toml`, `gfx_avc444_intra_refresh_bad.toml`,
`gfx_avc444_rekey.toml`, `gfx_avc444_rekey_bad.toml`,
`gfx_avc444_rekey_churn.toml`, `gfx_avc444_rekey_nochurn.toml`,
`gfx_avc444_sparse_aux.toml`, `gfx_avc444_sparse_aux_bad.toml`,
`gfx_avc444_wire_window.toml`, `gfx_avc444_wire_window_bad.toml` and
`gfx_avc444_removed_emit_thread.toml`. Activation may touch the existing
capability-response seam but shall not introduce a new mechanism there.

## Requirements

* S142-R1: the backend is opt-in through an `[avc444_ffmpeg]` table and codec
  order. Absence of the table preserves the base behavior exactly.
* S142-R2: before RDPGFX confirmation, resolve requested mode with #132 and run
  #133's behavioral probe. Advertise/select only a mode supported by client,
  configuration and probe. The selected mode is immutable afterward.
* S142-R3: document and validate executable path, bounded encoder argv,
  `avc_mode` (`auto`, forced AVC444, forced v1, forced AVC420),
  `chroma_align` (16 or 32), `dump_extra`, `strip_sei`, `sanitize_hrd`,
  `strip_pic_struct`, `aux_ltr_chain`, `ltr_rekey_frame_num`,
  `ltr_rekey_surface_reset`, independent main/aux intra intervals, eager slot
  acknowledgement, wire window and sparse-chroma intervals.
* S142-R4: defaults shall be `avc_mode=auto`, `chroma_align=32`,
  `eager_slot_ack=true`, `wire_window=1`, sparse chroma disabled, and optional
  interoperability/LTR transforms disabled. An operator may enable the
  documented transforms required by a hardware profile. The executable path
  defaults to `/usr/bin/ffmpeg`; the built-in argv is `-c:v`, `libx264`,
  `-bf`, `0`, `-preset`, `ultrafast`, `-tune`, `zerolatency`, `-crf`, `18`,
  `-g`, `240`, `-x264-params`, `repeat-headers=1:aud=1`.
* S142-R5: invalid mode, alignment, window, interval, dependency or argument
  shall produce a clear warning/error and keep the backend unavailable or the
  documented safe default. It shall never be silently clamped into a
  materially different requested behavior.
  `wire_window` accepts 1 through 64; nonzero `chroma_refresh_ms` accepts 16
  through 60000; `chroma_idle_ms` accepts 0 through 60000; each encoder argv
  has at most 64 tokens of at most 255 bytes. An empty argv uses the documented
  built-in default; excess tokens are ignored only with an explicit warning.
  Re-key accepts 64 through 65024 and defaults to 65024; each nonzero intra
  interval accepts 24 through 4096 and defaults to 250.
* S142-R6: the removed `emit_thread` key shall produce one migration warning
  and have no effect. `tail_flush`, `fault_aux_delay` and
  `fault_strip_mmco` shall not be accepted or documented.
  `ltr_rekey_surface_reset` defaults false; true is documented only as a
  diagnostic reproduction of the known client-visible surface-churn flash,
  not as the normal wrap-protection mechanism.
* S142-R7: runtime failure shall not switch codec. It shall preserve damage,
  tear down children and fail the affected connection/path visibly.
* S142-R8: the man page and sample config shall state process cardinality,
  required host pipe capacity, security model, resize behavior, multi-monitor
  support, shipped credit values, sparse guarantee and trace build option.
  Trace instructions shall name `XRDP_PERF_TRACE=<prefix>` as the sink arm,
  `XRDP_GFX_TRACE=1` as the graphics-event selector and
  `XRDP_ACK_TRACE=1` as the paired producer/credit selector. Records are in
  `<prefix>.<pid>`, not the normal log. The documentation shall explain how
  the producer selector reaches the xorgxrdp session and the one-active-session
  limitation of a non-forking xrdp test configuration.

## Required tests and final gate

The `GfxLoad` suite shall cover absence, defaults, every override, invalid and
dependency values, removed-key warning, no excluded keys, codec order and the
complete config-to-encoder transfer. Capability tests shall prove probe before
confirmation, immutable choice, no fallback, all client capability versions
and legacy codec preservation. Resize tests shall prove terminate/reap/new
reset and no old bytes.

Run every targeted AVC and PerfTrace suite, full `make check`, astyle and
cppcheck in default and trace-enabled builds; run the paired xorgxrdp build
against the exact header; and verify the default binary has no trace footprint.
These checks include the complete README gate.
The retained live gate shall exercise AVC420, AVC444v1 and AVC444v2 where the
client supports them, Windows and macOS clients, one and two monitors, resize,
still full-chroma detail and motion. Every run records the paired commit IDs,
client identity, resolution, selected mode and trace-build state. No timing
claim derived from a synchronous per-frame logger is admissible.
