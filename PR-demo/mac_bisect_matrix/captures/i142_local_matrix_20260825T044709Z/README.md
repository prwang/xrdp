# #142 local numerical replay — logout shakedown stopped before timing

No timing leg ran and no performance number from this directory is usable.

The corrected auxiliary-leaf audit certified the dense/window-1 profile. The
next sparse/window-1 certification produced AVC bytes, but the harness stopped
before auditing them because XFCE session logout did not return inside its
20-second bound. The session remained active and was subsequently ended using
the same permitted whole-session logout operation.

The lifecycle helper had requested a normal logout, which asks XFCE to save
session state and can wait on the full-screen payload. Deployment certification
does not reuse that state. The helper now requests XFCE's documented `--fast`
whole-session logout, still under the same explicit timeout, and verifies that
the user's Xorg process ended. This run remains red and was not resumed.
