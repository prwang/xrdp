# AVC444 v2 Mac wrong-color: cross-view inter prediction — mechanism proof

Date: 2026-07-27. Status: mechanism PROVEN offline (reference decoder,
deterministic, no live arms). Fix not yet implemented (awaiting owner
sign-off). All inputs are captures already committed under
`PR-demo/mac_bisect_matrix/captures/`; every number below is reproducible
with the commands given.

## The strict propagation principle (owner-demanded gate)

H.264 reconstruction of macroblock M in frame n is

    recon(M) = clip( prediction(M) + residual(M) )

`residual` and all syntax come bit-exactly from the wire. `prediction`
comes from (a) neighboring already-reconstructed pixels of the SAME frame
(intra MBs), or (b) pixels of a REFERENCE frame in the DPB (inter MBs).
Therefore, with byte-identical delivery, decoded output can diverge from
the encoder's reconstruction **only** if an inter MB's reference pixels
differ from the ones the encoder used. Once one MB diverges:

1. temporal propagation: every later inter MB predicting from the
   poisoned area inherits the error (DPB feedback loop);
2. spatial propagation: intra MBs predict from above/left neighbors, so a
   poisoned edge smears down-right as smooth gradients (DC/planar intra
   prediction of a wrong boundary) — the "beams" in the owner's video;
3. healing: only intra coverage overwrites it; an IDR heals everything.
   An identical repaint (EPOCH) is skip-coded by the encoder, and a skip
   MB *copies the poisoned reference verbatim* — no heal. Matches the
   owner's observations exactly.

Composition-side theories (stale pairing, recycled buffers) are
feedback-free: their error is bounded by real content values and cannot
grow. Measured on the owner's recording (`/tmp/rec.mov`, beam ROI
crop=1200:600:1800:1500 at t=110s): the beam steps monotonically redder,
(215.3,4.4,238.4) → (216.2,5.2,238.9) → (230.7,6.4,239.6), at discrete
session updates. Monotonic growth ⇒ feedback ⇒ the error lives in the
decoder's DPB. That kills all compose-side candidates in one measurement.

## The error event: reference identity depends on decode topology

Our AVC444 v2 encoder is ONE ffmpeg child / ONE codec context; main and
aux planes are alternating frames of a single stream
(`xrdp/xrdp_encoder_ffmpeg.c` pop_pair, packets popped in pairs; wire
frame_num runs 0,1,2,… across views — main even, aux odd). The previous
decode-order frame of every frame is the OTHER view. Any inter MB
therefore references **cross-view**.

- A client feeding ONE decoder in record order (xfreerdp; our oracle)
  reproduces the encoder's references exactly → bit-exact, clean.
- A client decoding the views SEPARATELY (two decoder sessions — the
  natural per-view reading of the two labeled AVC420 subframe streams)
  resolves the same inter MBs against the *same-view* previous frame.
  Wrong reference pixels → the error event above. Additionally the aux
  view as a standalone stream contains NO SPS/PPS and NO IDR at all
  (212-record T4 capture: every LC2 record is a P frame), so a per-view
  aux decoder starts from an empty DPB and is never re-anchored — and
  aux carries the 4:4:4 chroma supplement, which is why the on-screen
  damage is chroma-dominant while luma (main view, mostly-intra, errors
  clipped at black) survives.

## Measured MB-level trigger (why only nvenc+444v2)

`ffmpeg -debug mb_type` on committed captures, 5700 MBs/frame @1600x900:

| stream                     | P-frame MB mix                            |
|----------------------------|-------------------------------------------|
| T4 nvenc 444 (broken)      | ~5100 intra16 + ~270 intra4 + **~290–370 inter-L0**, inter count growing |
| arm-L VAAPI CQP 444 (clean)| 4799 intra16 + 901 intra4 = **100% intra, zero inter, every frame** |
| arm-H VAAPI CBR (quarantined) | all-intra as well (3600/3600)          |

nvenc finds cross-view inter prediction profitable in flat regions (black
screen luma vs chroma-neutral aux are both near-flat); VAAPI/CQP codes
everything intra because the cross-view reference is useless for normal
content. So:

- **VAAPI 444 clean**: stream never reads the DPB → immune to reference
  topology *by accident of its rate-control choices*, not by design.
- **nvenc 420 clean**: no aux view exists; single view, wire topology ==
  per-view topology; every reference is the true previous frame.
- **nvenc 444v2 broken**: the only cell where (a) the DPB is actually
  read and (b) reference identity depends on how the client feeds its
  decoder(s).

## Offline reproduction (reference decoder, no Mac, no VT)

Per-view decode of the committed T4 main stream (exactly what a
dual-session client's main decoder sees; frame_num 0,2,4,…):

    ffmpeg -fflags +genpts -i t4_ps0_main.h264 -fps_mode passthrough pv/m_%03d.png
    ffmpeg -fflags +genpts -i vt_probe/t4_broken_interleaved.h264 -fps_mode passthrough il/f_%03d.png
    # main frame k: per-view pv/m_k  vs  correct topology il/f_(2k+1)

| main frame | mean abs pixel diff | pixels >8 off |
|-----------:|--------------------:|--------------:|
| 0 (IDR)    | 0.00                | 0.0000        |
| 1          | 16.88               | 0.5967        |
| 5          | 47.82               | 0.8118        |
| 10         | 111.68              | 0.8320        |
| 20–105     | ~115–130            | ~0.83–0.86    |

Background ROI (y 500:800, x 1100:1500), black in correct decode, turns
bright magenta (240,80,247) by frame 10 in per-view decode — same color
family as the Mac video beams (215,4,238). Control: arm-L per-view vs
interleaved decode is **bit-identical** (mean 0, max 0).

Divergence starts at the FIRST P frame, saturates within ~10 frames, is
chroma-dominant over black (luma error clips at 0; chroma error does
not), spreads as smooth down-right gradients, and only an IDR resets it.
Every observed Mac symptom follows from one event class.

## What this does and does not prove

- PROVEN: our 444v2 stream is *topology-fragile* — its correctness
  requires single-decoder strict-interleave feeding. A per-view decode
  reproduces the exact corruption class deterministically in ffmpeg.
  The clean arms were immune by accident (all-intra), not by design.
- NOT claimed: the exact decoder topology inside the Mac client
  (closed source). Convicting its precise flavor would need the
  VideoToolbox harness; the fix below is correct under every topology,
  so that conviction is not required for the fix.
- Quarantined: arm-H's reported local bleed is NOT explained by this
  mechanism (its capture is all-intra); arm-H evidence remains under the
  stand-in honesty rule.
- The earlier "no remaining wire delta has a spec mechanism" conclusion
  was wrong because the delta inventory was header-syntax only; the
  mechanism lives in payload MB references + stream architecture.
  Header knobs could never have fixed this; the owner's monkeypatching
  veto was correct.

## Fix constraint (owner directive, 2026-07-27)

The ORIGINAL fix proposal here (two independent encoder contexts) is
WITHDRAWN — it is the previously rejected thread (BACKLOG ~line 171,
PRD §6.5): two frame_num chains + duplicate SPS into the client's
single decoder = desync garbage on Windows/xfreerdp clients.

Ground truth (PR-demo/win2022_ground_truth/GROUND_TRUTH_win2022_avc444.md, measured 2026-07-27):
the real Win2022 wire is ONE chain (all frames nri=3 reference Ps,
continuous frame_num, max_num_ref_frames=3) yet REFERENCE-PARTITIONED:
decoding gfxwin_anim with all 9 aux AUs dropped leaves 348/348 main
frames bit-identical to the full interleaved decode. Single chain does
not imply cross-view prediction.

**Binding constraint (owner, 2026-07-27): the reference discipline
must be fixed BY ITSELF — main frames never reference aux frames —
with NO restriction on how many aux frames are produced or when.
Aux-cadence changes (Lever 2 / FR-PROC-7) are IRRELEVANT as a
correctness mitigation: a fix that only holds under a particular aux
rate turns a deterministic defect into a load/timing-dependent
heisenbug.** Acceptance is the ground-truth robustness test on OUR
wire at full 1:1 main/aux alternation: drop all aux AUs (and,
separately, per-view decode) → main frames bit-identical to the
interleaved decode.

## Status addendum (2026-07-28): fix mandatory, constraint promoted to PRD

The reference-partitioning fix (main child + all-IDR leaf child,
`xrdp_h264_aux_to_leaf`) is owner-validated on macOS (bisect CLOSED,
BACKLOG 2026-07-28) and is now UNCONDITIONAL: the `aux_intra_leaf`
gfx.toml knob is removed and the AVC444 ffmpeg pair path always
partitions (PRD FR-H264-7, "decode-topology invariance"). Option 4b
(aux-refs-aux merged chains) is rejected as low-ROI — see FR-H264-7's
"Rejected alternative".

The "accidental immunity" claim about VAAPI above is now also
*measured* rather than argued: `tools/avc444_topology_check.sh` (the
FR-H264-7 regression, GREEN on the T4 leaf wire 217/217+217/217) goes
RED on BOTH pre-fix wires —

- t4_ps0 nvenc interleave: 105/106 main and 106/106 aux frames diverge
  from the single-decoder decode (the Mac-bleed class, as proven);
- arm-i VAAPI interleave (the Mac-CLEAN arm): 3/3 aux frames diverge
  in a per-view decoder. The old VAAPI wire was never topology
  invariant either — it merely happened not to get caught by the Mac's
  feeding pattern on the tested content.

Both encoders therefore REQUIRE the partitioned architecture; no
encoder is grandfathered on accidental immunity.

### Correction (2026-07-28, later the same day)

The 4b rejection referenced above is SUPERSEDED. Ground-truth trace of
the Win2022 reference machinery (PR-demo/win2022_ground_truth/GROUND_TRUTH_win2022_avc444.md,
LTR addendum) shows Windows itself ships aux-refs-previous-aux — via
constant long-term-reference slots (mmco6 self-mark, per-slice LTR
list modification), with no PicNum arithmetic and no sliding-window
dependence. The earlier risk framing ("per-frame arithmetic, silent
wrong-pixel failure modes, forfeits structural invariance") was
exaggerated: it priced a short-term-reference design Windows does not
use, and the LTR shape is exercised daily by every RDP client,
VideoToolbox included. The design is now specified as EXPERIMENTAL
FR-H264-8 (PRD); the leaf architecture remains the shipped default
and FR-H264-7 remains the requirement for it.
