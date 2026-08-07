# Pre-registered prediction — written BEFORE the run

**The question.** A reviewer will ask why the credit frontier is needed
at all when the 2017-era path already has a settable window
(`XRDP_GFX_FRAMES_IN_FLIGHT`, range 1..16). If widening that knob to 3
reaches the same place as the frontier at wire_window 2, the frontier is
not load-bearing and should not ship as a new mechanism.

**The claim under test, read from the code before measuring.** The
legacy path grants the producer `frame_id_server` (xrdp_mm.c:1772), and
that variable advances only when a frame reaches the transport
(xrdp_mm.c:4423). The frontier grants
`min(frame_id_consumed, frame_id_server + 1, frame_id_client + C)`
(xrdp_encoder.h:114-130), where `frame_id_consumed` advances when the
encoder children finish with the pixels (xrdp_mm.c:4366) -- before
egress. So widening the legacy window changes HOW OFTEN its gate opens,
never WHAT VALUE passes through it, and the producer stays bounded by a
frame that has already been sent.

**Therefore, predicted:**

1. The legacy path at window 3 STILL STALLS. The wait between the
   encoder children absorbing a frame's pixels and the producer being
   told it may capture again stays at roughly p90 10 ms, with of the
   order of one cycle in six over 10 ms -- materially unchanged from the
   same path at window 2 (measured 10.502 ms p90, 16.4 %).
2. Its wire bound WIDENS to 4 frames outstanding at send, matching the
   frontier at wire_window 2 -- so the two are comparable on cost while
   differing on benefit.
3. The frontier arm in the same sitting reproduces its near-zero wait
   (p90 well under 1 ms, stalls under a few per cent).

**Falsified if:** the legacy path at window 3 shows the wait collapsing
the way the frontier does. That would mean the grant is not
egress-bound as read, the frontier is NOT load-bearing, and the
extension argument for shipping it fails.

**Also worth watching, not a prediction:** an older record has a WIDER
window measuring worse (98.1 ms period at window 4 against 87.0 at
window 2, 2026-07-31, taken before the tracing defect was fixed and
therefore suspect). If the period degrades at window 3 here, that old
observation gains support on a sound instrument.

**Design.** Four legs of 20 s, interleaved, one monitor at 3840x2400:
legacy-at-3 / frontier-at-2 / legacy-at-3 / frontier-at-2. The two arms
run the same image and their gfx.toml bodies differ only in the ack
mechanism; the widened window is set by environment variable, and the
legacy arm's gfx body is byte-identical to the window-2 legacy arm
already on record.
