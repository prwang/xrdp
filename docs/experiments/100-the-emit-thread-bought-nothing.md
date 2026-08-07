# #100 — the emit thread bought nothing, so it is gone

2026-08-07.

Records in this directory are kept verbatim. Supersede with a dated
note; do not tidy.

---

## The answer first

**The EGFX assembly thread is removed from the code.** Measured one
config line apart on the same build and the same host, at one monitor
and 3840×2400, the assembly stage is *cheaper* run inline on the
encoder worker (0.230 ms mean) than handed to its own thread
(0.313 ms mean) — the hand-off through a depth-1 slot and two counting
semaphores costs more than the concurrency returns. The frame period
differs by 0.12 ms at p50 (thread ahead) and 0.63 ms at p90 (inline
ahead), both inside this host's leg-to-leg spread.

Against at most a tenth of a millisecond of frame period, the thread
cost a permanent thread, two semaphores, a hand-off slot, join logic
and an unarmed-drop counter and its one-shot warning.

Owner decision, 2026-08-07: *"if it is a strong result, we will take it
and it won't appear in the clean-room version."*

## The measurement

Capture
`PR-demo/mac_bisect_matrix/captures/i80_c1_nonregression_20260807_141752_s20`,
legs F1 and E1 — arms x023 and x024, the same server build
(`1d5bc0960db8`), the same producer, the same payload, both at
`eager_slot_ack = true, wire_window = 1`, differing only in
`emit_thread`. Both arms certified at deploy: 7 checks clean, 0 black
frames.

| | emit thread ON (F1) | emit thread OFF (E1) |
|---|---|---|
| frame period p50 | 17.65 ms | 17.77 ms |
| frame period p90 | 26.40 ms | 25.77 ms |
| the assembly stage itself | 0.313 ms mean | **0.230 ms mean** |
| encoder wait (the control) | 16.33 ms | 16.26 ms |

"Encoder wait" is the worker blocked on the two ffmpeg children. It is
the control: it must not move between the two conditions, and it does
not (0.07 ms apart).

## The limit, which bounds what this record may be cited for

**One monitor.** Assembly work scales with monitor count, and this is a
single-monitor result. It does not say the thread would be worthless at
m ≥ 2; it says it was worth nothing at m = 1, which is the only
geometry measured. The same caution the capture states for the wire
bound applies here.

If concurrency in assembly is ever wanted for m ≥ 2, the design that
was built — the join point and why its position is a correctness
requirement, the one-permanent-thread argument, the shared-state rules
— is retained in full in PRD FR-ACK-2 as the record of that work.

## What was removed

From `xrdp/xrdp_encoder.{c,h}`:

* `proc_emit_msg()`, the permanent assembler thread, and its creation
  in `xrdp_encoder_create()`.
* `emit_req_sem`, `emit_idle_sem`, `emit_gone_sem` and their creation,
  failure fallback and deletion.
* `struct xrdp_encoder_emit_slot`, `gfx_emit_slot_fill()` and
  `gfx_emit_dispatch()` — the depth-1 hand-off.
* `gfx_emit_join()` and its three call sites (two in the batch cycle,
  one at worker teardown).
* `gfx_emit_may_encode_inline()`, `emit_unarmed_drops`,
  `emit_unarmed_logged` and the one-shot warning — the refusal to
  encode an unarmed monitor inline, which existed only because a second
  thread must not drive a child the worker owns.
* `emit_thread`, `emit_outstanding`, `emit_quit`.

From the configuration: `avc444_ffmpeg_emit_thread` in
`xrdp/xrdp_tconfig.h`, its default and its assignment in
`xrdp/xrdp_tconfig.c`, `avc444_emit_thread` in `xrdp/xrdp_types.h`, and
the arming plus the aux-LTR-chain refusal warning in `xrdp/xrdp_mm.c`.

## What deliberately survives

**The separation of assembly from the encode path**, which is what the
#70B work actually fixed and is independent of which thread runs it:

* `gfx_emit_run_set()` is unchanged and is now simply always called
  inline, at the end of the batch cycle, with the same `emit_beg` /
  `emit_end` trace brackets and the same output bytes.
* The emit pass still reads the coded geometry `collect` snapshotted
  (`avc444_batch_cw` / `avc444_batch_ch`) rather than dereferencing an
  ffmpeg handle.
* The aux-LTR re-key teardown is still deferred to the top of the next
  cycle (`avc444_teardown_req`) rather than performed inside the emit
  pass. This is the use-after-free fix: `collect_pair` calls `grow()`,
  `grow` reallocs, and the emit pass reads through `pair.main_data`.
* `gfx_batch_publish()` is still the single writer of the per-monitor
  arm state, applied after the pump and before the collect, with the
  submit pass carrying its results in the caller's locals. Its five
  unit tests are unchanged.

## The gfx.toml key: warn once, do not fail

An existing `gfx.toml` in the field may still say `emit_thread = true`.
The loader now reads the key only to say it is gone:

```
avc444_ffmpeg emit_thread was removed (BACKLOG #100): the EGFX assembly
always runs on the encoder worker. The key is ignored; delete it from
gfx.toml
```

Silence was the other option and was rejected. tomlc99 only reads the
keys the loader asks for, so a key nobody asks for is ignored with no
warning — but every value in this table that *cannot be honoured* says
so out loud today (an out-of-range `wire_window`, a bad `avc_mode`, a
bad `chroma_align`, and `eager_slot_ack` set without `aux_ltr_chain`).
A deployment that set `emit_thread = true` believes its assembly is
threaded; that belief is exactly what the existing pattern exists to
correct. Parsing is unaffected — the file loads and every key around it
is honoured, which is pinned by
`test_tconfig_gfx_avc444_removed_key_still_parses` against
`tests/xrdp/gfx/gfx_avc444_removed_emit_thread.toml`.

## Assertions retired

Seven, all in `tests/xrdp/test_avc444_emit_split.c` and
`tests/xrdp/test_tconfig.c`, listed here because CLAUDE.md requires the
retirement of a test to be a separate announced act rather than a quiet
deletion. Every one of them asserts something about a function or a
field that no longer exists; none was changed to agree with new code.

`gfx_emit_may_encode_inline()` — the predicate is gone with the thread,
because with assembly on the worker there is no second thread that
might drive an ffmpeg child:

1. `test_emit_inline_allowed_when_split_off`
2. `test_emit_inline_refused_for_unarmed_monitor`
3. `test_emit_ready_pair_is_not_refused`
4. `test_emit_failed_pair_is_not_this_predicates_business`

`gfx_emit_slot_fill()` — the depth-1 hand-off slot is gone:

5. `test_slot_fill_copies_in_order`
6. `test_slot_fill_refuses_oversized_set`
7. `test_slot_fill_rejects_bad_args`

And one assertion inside a surviving test,
`test_tconfig_gfx_avc444_defaults`:

8. `ck_assert_int_eq(gfxconfig.avc444_ffmpeg_emit_thread, 0)` — added
   earlier the same day to state that the thread was deliberately not
   part of the shipped default. The field no longer exists, so there is
   nothing left to assert.

`make check` after the removal: 176 libcommon, 35 libipm, 13 libxrdp,
1 memtest, 192 XRDP daemon — all pass, 0 failures, 0 errors. The XRDP
daemon suite went from 198 to 192: seven assertions retired above, one
new test added.

## Dangling references left in place, on purpose

* `PR-demo/mac_bisect_matrix/gfx/*.toml` — 20 live fleet arm configs
  still carry an `emit_thread` line. They still load; each logs one
  warning on deploy. **Three of those arm pairs (x001/x002, x003/x004,
  x023/x024) were A/Bs whose only difference was that line, so they are
  now A/A pairs and must not be re-run as split comparisons.**
* `PR-demo/mac_bisect_matrix/i70b_stage_split.py` and
  `i61e_period_attribute.py` — both detect an assembler thread in a
  trace and exclude its stages from the serial sum. Archived captures
  contain such traces, and the scripts must keep reading them
  correctly; on a post-#100 trace they simply print `(inline)`.
* `docs/experiments/61e-*.md`, `docs/experiments/87-*.md`, the capture
  READMEs and their `gfx.toml` copies — records, kept verbatim.
