# Slice #142 — Configuration, activation and final gate

## Commit boundary

Only this final commit exposes the assembled backend to a session. It adds
configuration, capability activation and operator documentation for behavior
already implemented and green.

Target files are `xrdp/xrdp_tconfig.c`, `xrdp/xrdp_tconfig.h`,
`xrdp/xrdp_types.h`, `xrdp/xrdp_mm.c`, `xrdp/xrdp_encoder.c`,
`xrdp/xrdp.h`, `xrdp/xrdp_encoder.h`, `xrdp/xrdp_encoder_ffmpeg.c`,
`xrdp/xrdp_encoder_ffmpeg.h`, `xrdp/gfx.toml`,
`docs/man/gfx.toml.5.in`, `tests/xrdp/Makefile.am`,
`tests/xrdp/test_avc444_ffmpeg.c`, `tests/xrdp/test_tconfig.c`,
`tests/xrdp/test_xrdp_egfx.c`, `tests/xrdp/check_operator_surface.sh`, and
these fixtures, all below `tests/xrdp/gfx/`:
`gfx_avc444_ffmpeg.toml`, `gfx_avc444_empty_args.toml`,
`gfx_avc444_intra_refresh.toml`, `gfx_avc444_intra_refresh_bad.toml`,
`gfx_avc444_rekey.toml`, `gfx_avc444_rekey_bad.toml`,
`gfx_avc444_rekey_churn.toml`, `gfx_avc444_rekey_nochurn.toml`,
`gfx_avc444_excluded_key.toml`,
`gfx_avc444_sparse_aux.toml`, `gfx_avc444_sparse_aux_bad.toml`,
`gfx_avc444_wire_window.toml`, `gfx_avc444_wire_window_bad.toml` and
`gfx_avc444_removed_emit_thread.toml`. Activation may touch the existing
capability-response seam but shall not introduce a new mechanism there.

## Requirements

* S142-R1: the backend is opt-in through an `[avc444_ffmpeg]` table and codec
  order. Absence of the table preserves the base behavior exactly.
* S142-R2: before RDPGFX confirmation, resolve the requested mode with #132,
  construct the complete runner configuration from the loaded operator values
  and invoke the behavioral probe already extended by #133, #137 and #138.
  AVC420 supplies the single-main topology. AVC444 supplies either the
  two-child leaf topology or the selected LTR topology. No field may be
  restored from a simpler default between configuration loading and the probe.
  Every monitor probe shall use the exact coded geometry which the capture
  contract will supply, including 16-row height alignment. Session geometry
  used when no monitor layout is available follows the same rule; for example,
  1920-by-1080 visible geometry is probed as 1920-by-1088. A probe of a smaller
  even-height frame does not certify the production capture.
  Advertise/select only a mode supported by the client, configuration and that
  complete probe. The selected mode is immutable afterward. This slice wires
  the probe to capability selection and connects confirmed raw captures to the
  already-tested worker batch and wire assembly. It does not add a new probe,
  transform, scheduler or encoder process model.
* S142-R3: document and validate executable path, bounded encoder argv,
  `avc_mode` (`auto`, forced AVC444, forced v1, forced AVC420),
  `chroma_align` (16 or 32), `dump_extra`, `strip_sei`, `sanitize_hrd`,
  `strip_pic_struct`, `aux_ltr_chain`, `ltr_rekey_frame_num`,
  `ltr_rekey_surface_reset`, independent main/aux intra intervals, eager slot
  acknowledgement, wire window and sparse-chroma intervals.
  The sample and man page shall recommend `auto`; they shall label forced v1
  as a legacy interoperability/diagnostic mode and record that the qualified
  macOS client rendered one-pixel red/blue detail incorrectly when v1 was
  forced. The v1 implementation remains required for clients which advertise
  v1 without v2.
* S142-R4: defaults shall be `avc_mode=auto`, `chroma_align=32`,
  `eager_slot_ack=true`, `wire_window=1`, sparse chroma disabled, and optional
  interoperability/LTR transforms disabled. An operator may enable the
  documented transforms required by a hardware profile. The executable path
  defaults to `/usr/bin/ffmpeg`; the built-in argv is `-c:v`, `libx264`,
  `-bf`, `0`, `-preset`, `ultrafast`, `-tune`, `zerolatency`, `-crf`, `18`,
  `-g`, `240`, `-x264-params`, `repeat-headers=1:aud=1:cabac=1`. CABAC is
  explicit because the auxiliary-leaf transform requires it and libx264's
  `ultrafast` preset otherwise selects Constrained Baseline/CAVLC. An omitted
  or empty argument array shall select the already-tested #137 runner default;
  the loader shall not carry an independent divergent copy. The commented
  software example shall reproduce that default exactly.
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
* S142-R7: activation shall connect #134's already-tested terminal backend
  result to one connection hangup. A child-creation, stream-contract or rewrite
  failure after confirmation shall preserve damage, use the bounded forensic
  record assembled by #133 and #137, tear down the children once and signal the
  hangup. A creation failure or an already exited child has no access unit to
  retain; a leaf rejection retains the untouched encoded pair. Later damage
  shall observe the terminal latch and shall not recreate a child or switch
  codec. This slice shall not add a retry policy, a fallback, or a second
  forensic mechanism.
* S142-R8: the man page and sample config shall state process cardinality,
  required host pipe capacity, security model, resize behavior, multi-monitor
  support, shipped credit values, sparse guarantee, sparse visual limitation
  and trace build option. They shall state that dense mode is the default and
  quality-preserving choice. Sparse mode is an opt-in bandwidth/quality
  tradeoff: apparently static fine-chroma content inside an affected update
  may alternate between 4:2:0 and 4:4:4 while other damage continues because
  the policy tracks per-monitor time, not application objects or pixels. It
  guarantees bounded chroma refresh and full-chroma convergence after actual
  quiescence, not stable full chroma during recurrent damage.
  Trace instructions shall name `XRDP_PERF_TRACE=<prefix>` as the sink arm,
  `XRDP_GFX_TRACE=1` as the graphics-event selector and
  `XRDP_ACK_TRACE=1` as the xrdp credit selector. Records are in
  `<prefix>.<pid>`, not the normal log. The documentation shall state that no
  producer timestamp crosses xup and explain the one-active-session
  limitation of a non-forking xrdp test configuration.
* S142-R9: installed configuration, manual text, warnings and informational
  messages shall use operator-facing terms only. They shall not expose PRD,
  backlog, experiment or clean-room identifiers. The template shall be a
  concise operable example and shall not advertise removed or inert keys. The
  man page shall document every supported administrator key, its default,
  range or accepted values, dependencies and material quality/latency tradeoff.
  Claims derived from measurements shall appear only when their retained
  evidence is admissible. A TAP test shall reject internal work labels on the
  installed template, manual source, compiled runtime strings and shipped help,
  require the principal paired-reference, credit and sparse keys in the
  template and manual, and reject any advertised removed key.
* S142-R10: configuration is an end-to-end contract with three independent
  final checks. A commented sample is executable operator surface: restoring
  it verbatim shall produce an operational configuration, agree with the
  built-in default it replaces and not depend on a hidden correction elsewhere.
  The manual shall explain the correctness, latency or quality purpose of every
  non-obvious argument in the default software encoder block rather than
  present a magic string. The configuration-to-probe test shall prove that the
  exact loaded executable, argv, mode, child roles and transform selection
  reach the earlier probe unchanged. The post-confirm test shall prove the
  earlier terminal latch and forensic path remain effective after activation.
  Passing any one check does not compensate for omitting either of the others.

## Required tests and final gate

The `GfxLoad` suite shall cover absence, defaults, every override, invalid and
dependency values, removed-key warning, no excluded keys, codec order and the
complete config-to-encoder transfer. The operator-surface gate shall require
the complete copy-safe software argument token in both the installed template
and manual, and shall prove the full commented software block agrees with the
runner default. Capability tests shall prove the exact loaded configuration is
passed to the earlier probe before confirmation, immutable choice, no fallback,
all client capability versions, 16-row coded geometry and legacy codec
preservation. The CABAC/CAVLC
and LTR probe mechanisms are already gated by #137 and #138; this slice proves
their results control advertisement. A deterministic post-confirm rejection
fixture shall prove one bounded forensic bundle, one teardown, session hangup
and zero later respawns under repeated damage. Resize tests shall prove
terminate/reap/new reset and no old bytes.

Run every targeted AVC and PerfTrace suite, full `make check`, astyle and
cppcheck in default and trace-enabled builds; run the paired xorgxrdp build
against the exact header; and verify the default binary has no trace footprint.
These checks include the complete README gate.
The retained live gate shall exercise AVC420, AVC444v1 and AVC444v2 where the
client supports them, Windows and macOS clients, one and two monitors, resize,
still full-chroma detail and motion. For sparse AVC444, the motion run shall
contain both main-only and main-plus-auxiliary cycles. Its audit shall report
LC=1 and LC=2 command counts and transmitted bytes, close their sum against
the audited video-command total using #141's explicit `video_cmd` frame
identity, confirm the configured chroma-gap bound, and
show that the one-shot trailing capture restores static one-pixel chroma after
motion stops without later application damage.
Visible 4:2:0/4:4:4 churn inside an affected update while damage continues is
the documented sparse-mode limitation, not a failing visual gate. This final
slice is an accounting and compatibility replay, not an opportunity to change
the selected behavior. Every run records the paired commit IDs, client
identity, resolution, selected mode and trace-build state. No timing claim
derived from a synchronous per-frame logger is admissible.

The retained numerical replay is a repeated 2-by-2 comparison of
`wire_window` 1/2 and dense/sparse chroma. Conditions A/B use window 1; C/D use
window 2; A/C set both chroma intervals to zero; B/D set refresh/idle to
1000/100 ms. Run eight 20-second legs in `A B C D D C B A` order. Report the
sparse effect at both windows, the window effect under both chroma policies,
and their interaction. A wider-window effect requires telemetry proving that
the run reached a state window 1 could not admit. Every sparse leg shall prove
both auxiliary omission and restoration on its own trace.
