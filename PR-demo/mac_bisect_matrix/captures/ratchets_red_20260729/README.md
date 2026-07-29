# BACKLOG #45 step 0 — the ratchets, RED against today's code

Recorded 2026-07-29 at xrdp `cd856fad` (pre-#45-implementation) and
xorgxrdp `957fa79`. A validator that cannot fail is worthless, so each
ratchet's RED output is captured here before the code that makes it
green exists.

## `make_check_ltr_RED.txt` — the C ratchets

`tests/xrdp/test_xrdp` with `CK_RUN_SUITE=Avc444Ltr`. 25 checks,
**3 failures**, all three new:

| ratchet | why it is RED today |
|---|---|
| `test_ltr_cut_sequence_byte_exact` | the 12-picture scheduled-cut chain: the rewriter returns 1 at the first non-IDR I picture |
| `test_ltr_cut_nonidr_i_accepted_both_views` | `slice_ltr_rewrite()` rejects a non-IDR I outright (`goto unsupported /* B/SP/SI or non-IDR I */`) |
| `test_ltr_cut_midstream_idr_keeps_chain` | same reject reached first; today a mid-stream main IDR also resets the shared counter to 0 and clears `aux_seeded` (the behaviour `test_ltr_emitter_epoch_restart_byte_exact` asserts as correct at this commit) |

The fourth new test, `test_ltr_dpb_scheduled_paired_cut_both_modes`,
PASSES here and is expected to: it is a model check over the in-test DPB
simulator (what a scheduled paired cut must mean in a decoder), not a
check of the C rewriter. It is recorded as the invariant half of the
ratchet, and it is stated as such rather than counted as RED.

## `wire_audit_assert_RED.txt` — the wire gate

`tools/avc444_ltr_wire_audit.py --assert --intra-refresh 240` over the
pre-#45 arm-o capture (`captures/arm_o_rekey_noblank_20260729/oracle_avc_s0.bin`,
aux_ltr_chain with `-g 30000`, 2144 pictures). The same tool's
descriptive verdict on that capture is **PASS** — which is exactly why
the assert mode had to be added:

```
VERDICT: both views are inter-coded from their OWN previous picture ...
=== ASSERT GATE (BACKLOG #45 step 0) ===
  A1 no mid-stream IDR                     FAIL  3 mid-stream IDR(s) at decode indices [536, 1072, 1608]
  A2 intra only on a scheduled index       FAIL  unscheduled intra: main [268, 536, 804] aux [268, 536, 804]
  A3 cuts are paired across views          PASS
  A4 no scheduled cut skipped              FAIL  scheduled ordinals with no intra: main [240, 480, 720, 960] ...
  A5 own-slot refs and self-marks          PASS
  A6 one contiguous frame_num chain        FAIL  3 gap(s) at decode indices [536, 1072, 1608]
  A7 chain depth <= intra_refresh_frames   FAIL  worst depth: main 267 aux 267 (bound 240)
ASSERT VERDICT: FAIL -- 5 of 7 checks violated
exit=1
```

Every failure is a real property of the shipped pre-#45 stream: the
GOP IDR arrives unscheduled, flushes the DPB, breaks the shared chain
(A1/A6), and no scheduled refresh exists at all so the prediction chain
runs 267 pictures deep (A7). A3 and A5 pass because the aux respawn does
pair the intra pictures and the slot targeting was already correct — the
gate is not vacuous in the other direction either.

## Vector provenance

The cut vectors (`tests/xrdp/test_avc444_ltr_cut_vectors.h`) come from
the independent python reference splicer
(`PR-demo/mac_bisect_matrix/ltr_splice_ref.py --emit-cut-header`) fed the
children in `PR-demo/mac_bisect_matrix/ltr_vectors/cut/` (regenerate with
`make_cut_children.sh`). Two validations, both run:

- regenerating the EXISTING `test_avc444_ltr_vectors.h` from the changed
  generator reproduces the committed file **byte-identically** (from
  both `ltr_vectors/run1` and `run2`) — the generator change did not
  alter any shipped behaviour;
- the generated cut streams decode under `ffmpeg -err_detect explode`
  with **zero** errors, and the 1-context (interleaved) and 2-context
  (per-view) decodes are **bit-identical** frame for frame — including
  the aux P after the converted main IDR, which proves LT1 survived the
  cut.

The non-IDR I input is SYNTHESISED from a child IDR slice (header
re-emitted in the non-IDR form, CABAC payload byte-verbatim) because no
nvenc-capable GPU exists on the dev box: it is a decodable picture in
the shape `h264_nvenc` emits at a forced key frame without
`-forced-idr`, not a capture of one. The real encoder's shape — in
particular its `nal_ref_idc`, which the walker requires to be non-zero —
is confirmed on the T4 at deploy time, and until then it is an
assumption stated out loud, not a measurement.
