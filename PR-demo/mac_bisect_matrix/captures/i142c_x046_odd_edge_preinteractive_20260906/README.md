# Canonical development odd-edge comparison, port 40062

Owner authorization: continue the existing linear development branch, retain
one red overflow commit, and deploy a new comparison on port 40062. Only if
the predicted Windows transition reproduces, explicitly undo the intervention
and correct the normative specification, validate development green, then
re-author the clean-room correction. This record does not pre-claim that
conditional result.

## Intervention and controls

Development stays in `/work` on `dev/avc444_metablock_checkpoint`; the paired
producer stays in `/workUpdateXorgXrdp` on the same branch. The prior capture
and plan are committed as `76da657a`. There is no branch/worktree split or
history rewrite. The producer remains `baf9658c397d` and is not modified.

The external AVC444 view serializer uses the existing metadata builder but
omits its final visible-bound clip after even alignment. The input damage
still gets development's existing one-pixel expansion and visible clipping
before alignment. Only right/bottom edges which end at an odd visible boundary
can now increase by one. This is the exact overflow being tested, not a port
of clean-room's other region policy differences. The outer WIRETOSURFACE
destination, surface allocation, encoded bytes, LC ordering, frame and ACK
handling, callback and ffmpeg lifecycle remain unchanged. The ordinary
AVC420 metadata entry point retains visible clipping. There is no runtime
fallback, timeout change or extra encoder path.

The intentional branch in the metadata builder is labelled diagnostic in the
source. Existing visible-bound and wire-layout test assertions are unchanged.
A separate diagnostic test derives the even ceilings for widths 2360/2361
and heights 1032/1033, exercising both LC=1 and LC=2. This test records the
intervention, not a shipping contract; retire it explicitly with the forward
undo rather than rewriting existing shipping assertions.

The common wire inspector records the first inner region's four coordinates
from an additional fixed eight bytes in the existing first payload segment.
It never scans H.264 bodies. An independent fixture gives outer bottom 1033
and inner bottom 1034, asserting both are preserved and every truncation is
rejected. The existing short-payload tests remain unchanged. Trace-disabled
builds do not retain these fields or reads.

## Source gates

The initial trace-enabled complete `make check` passes: daemon 223/223.
The trace-disabled complete run passes: daemon 219/219, including the existing
visible-bounds tests, and the disabled trace footprint test proves no side
effect, trace symbol or event string. `default-check.txt` and
`default-suite.txt` retain that run. The final trace-enabled rebuild and gates
are retained separately before packaging. Passing even-size/logic gates is
not a green result for the intentionally faulty odd-edge behavior.

The formatter initially matched two generated `.o` files in addition to
source. Only those two disposable objects were removed and rebuilt from
canonical source before testing; no user source or Git state was discarded.
The prior evidence commit retains raw blank package-query fields, including
their original tabs; its diff whitespace warnings are capture content, not
production formatting errors. The actual package identities are separately
retained in that capture's `installed-packages.txt`.

## Deployment design

`deploy_x046_odd_edge.sh` refuses to replace an existing comparison and checks
the canonical branch plus the exact local base image ID:
`b5657410a41da495f83b7231bb3c7763229fa3708f9079bb0008d67d25061d44`.
It layers only the committed xrdp package onto the original port-40060 image.
No image pull, apt refresh or unrelated package upgrade is performed. Thus
Ubuntu 24.04, FFmpeg 6/libx264, xorgxrdp, XFCE, LXTerminal, default xterm,
Thunar, Chromium and demo helpers remain those of the development control.
The profile is semantically identical: CPU libx264, `auto`/AVC444v2,
dense chroma, wire window 1. Only the new arm's configuration map and deployment
are created; no shared payload map or existing arm is changed.

The port binds to host loopback 40062. Trace files go to
`/var/lib/xrdp-matrix/x046-odd-edge/xrdp.<pid>` via the existing perf sink,
with `XRDP_WIRE_TRACE=1`. The first rectangle suffices for the captured
one-full-screen-region trigger; it is not a complete multi-region wire audit.

Acceptance before interactive handoff is the ordinary three-second arm
certificate and rendered smoke at 1920x1080 plus 1024x768. These even-size
controls must remain green. The Windows test must then prove an inner
right/bottom bound outside the outer destination, followed by the specific
missing ACK and replacement-capability sequence. A generic disconnect alone
is insufficient. Retain the working 40060 and failing clean-room 40061
unchanged while the owner tests 40062. Final identities, gate results and
timestamped tester instructions follow after deployment.
