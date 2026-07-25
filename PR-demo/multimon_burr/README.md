# multimon_burr — dual-monitor AVC444 drag-ghost reproduction

End-to-end, self-driven reproduction of the dual-monitor "1-2px burr residual
when dragging a window" defect (owner report 2026-07-25, screenshot
`regression_ghost_edge_2026-07-25 114541.png`): thin stale lines (1px solid,
2px/3px dashed, including vertical lines striking through BOTH screens across
the monitor seam) are left along the drag path on the client and persist
after motion stops. Single-monitor sessions are clean.

## What it does

One command, no human in the loop:

1. Starts a client-side Xorg (dummy driver — Xvfb cannot host RandR virtual
   monitors) at the owner's exact layout: canvas 3840x3840, primary
   2560x1440 at +594+0 ON TOP, 4K 3840x2400 at +0+1440 below. The layout is
   asserted by counting the pure-black non-monitor pixels on the client
   framebuffer (expected 3840*3840 - 3840*2400 - 2560*1440 = 1843200).
2. Cold-logs a tester session via `xfreerdp3 /multimon /gfx:AVC444`.
3. Grabs a BASELINE truth/client pair before any drag; every mismatch in it
   (icons, panel, codec loss on fine detail) is masked from the analysis so
   only drag-caused residuals count (hard lesson: heuristic detectors
   produced false positives twice before this oracle).
   The mover window is spawned and PARKED at a fixed spot BEFORE the
   baseline pair, and every pass returns it to that park spot before its
   grab: a live window border is a high-contrast edge where lossy H.264
   error exceeds the tolerance, so an unparked window flags 1px lines at
   wherever it stands in the after-grab (third false-positive class,
   caught 2026-07-25 in BOTH modes after the shmem-split fix removed the
   real trail ghosts). Parking masks only the live window at grab time —
   the drag trail under test stays fully exposed, and the return move
   adds trail coverage.
4. Drags a real qterminal by itself (xdotool windowmove steps): a horizontal
   sweep across the 4K screen, a vertical sweep crossing the monitor seam,
   then seam crossings at four more x positions (ghost formation at a given
   edge is stochastic — more crossings, more chances).
5. After each pass settles (2.5s), grabs a SESSION-truth/client pair, then
   a second pair 6s later. The VERDICT uses the settled (+8.5s) pair:
   residual = structured mismatch that persists after the pipeline flushed.
   Lines present at +2.5s but gone at +8.5s are printed as `LAG` (frames
   still in flight at the first grab — the separate end-of-drag latency
   issue, self-corrected; observed 2026-07-25 as a full stale window image
   ~3.5s after the last move). The parked window's own rectangle is
   excluded from analysis (printed as `parked-window exclusion`); the drag
   trail is never excluded. Ghost lines are classified as vertical/
   horizontal, width in px, solid vs dashed, and checked for seam
   strike-through (same x hot on both screens, within the x-range where
   the screens overlap). Writes `annotated_client.png` with red boxes on
   every ghost line.

## Usage

    MODE=dual  bash multimon_burr_repro.sh   # owner layout, reproduces ghosts
    MODE=single bash multimon_burr_repro.sh  # 4K only; owner-reported clean control
    GFX=AVC444|AVC420|RFX                    # codec control (server config
                                             # may re-match; check xrdp.log)

Exit 1 + "RESIDUAL DETECTED" when ghosts are found. Output in
`/tmp/mmburr-$MODE-$GFX/` (truth_*/client_* grabs, annotated_client.png).

## Box assumptions (per PR-demo policy, env-overridable)

tester account with empty password (`KEYTEST_USER`), xrdp on
`127.0.0.1:3389` (`KEYTEST_HOST`), client Xorg on :97, Xorg dummy driver
installed. The container reaps background X servers between tool
invocations, so the script tears down and rebuilds everything within a
single run.

## Reference result (2026-07-25, xrdp 2f216a20 + xorgxrdp aa08c63)

- layout check: 1,843,258 black px (expected 1,843,200; +58 = genuine black
  content pixels)
- 1px SOLID vertical ghosts at former window edges; 1px solid horizontals
  spanning the whole drag path; 2px and 3px DASHED verticals
- STRIKE-THROUGH 2px dashed vertical ghosts at the seam-crossing drag
  columns (x 786/1286/2286), full primary height, continuing into the 4K
- MODE=single: clean (only live-edge codec ringing, no persistent lines)
- Owner visually confirmed same failure class as the mstsc screenshot.
