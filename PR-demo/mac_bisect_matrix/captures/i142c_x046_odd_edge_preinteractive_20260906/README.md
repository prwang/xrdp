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

## Initial deployment: host pipe precondition RED

The source intervention is committed as `c729a50889a2`. The installed package
is `xrdp-dev_0.10.80+git20260906151219.c729a50889a2_amd64.deb`, paired with
unchanged xorgxrdp `baf9658c397d`. Image
`localhost/xrdp-bisect:dev-odd-edge-40062` has local ID
`78abda7880067cf8816104077c28512c3dc504e549acf39131a480d1ec45b31d`.
The pod is `xrdp-x046-88845dc4d-c6vz4`, address 10.42.0.25, port 40062.
The deployed profile SHA256 is
`565ad975e8a0d5b2ae4f445a37d6c0f667aae466ea14a21b38761b1c0613f585`.
FFmpeg remains `7:6.1.1-3ubuntu5`, libx264 remains
`2:0.164.3108+git31e19f9-1`. Packaging generated fresh TLS certificate/key
and RSA configuration, as the dpkg log records; a first-connection certificate
prompt is not the graphics defect. No other installed package was upgraded.

A provisional package carrying `76da657ae221+dirty` was produced after the
raw test-output EOF-blank-line whitespace check interrupted the first commit
attempt. It was never installed. The source was then committed and repackaged
with the exact clean identity above. A mistyped package path failed the
deployment script's first `test -f` before any deployment action; the corrected
path is the one in the retained successful install log.

The first three-second certificate is RED solely on its input-pipe guard.
The encoder probe and production children received 8192-byte pipes, below
the 65536-byte minimum; wire auxiliary-leaf checks pass 6/6 and all four
captured pictures decode without black frames. Neither the failed certificate
nor its intentional oracle-client disconnect proves the Windows hypothesis.
Raw result: `initial-certification.FAILED`; the even-size smoke result is
reported independently. No host setting was changed by the agent.

Read-only diagnosis found `fs.pipe-user-pages-soft=16384`, hard=0 and
per-pipe max=1048576. Container uid 0 maps to host uid 1000, so its quota is
shared with other pipes charged to that account. A later new-pipe probe got
65536 bytes and could retain that size, but a 1048576-byte enlargement failed
with EPERM. The failure is pressure-sensitive, not a proof that every future
pipe must remain 8192 bytes. A bounded inventory inside this container found
212 distinct accessible pipes under root-owned processes with 48300032 bytes
of capacity; that is not a complete host-uid accounting and cannot exclude
additional host processes. Existing 40060/40061 Windows captures had no
PIPE_TOO_SMALL warnings. Changing the encoder or accepting this certificate
as green would therefore conceal a real environment discrepancy.

The existing fleet README records the owner's previous temporary host value
as 262144 pages. Current kernel documentation describes the soft limit as
per-user total pipe pages, after which new pipes are capped at two pages and
enlargement is denied:
[Linux pipe sysctls](https://docs.kernel.org/admin-guide/sysctl/fs.html#pipe-user-pages-soft).
The host instruction is to restore that documented setting and persist it:

```sh
printf '%s\n' 'fs.pipe-user-pages-soft = 262144' |
  sudo tee /etc/sysctl.d/90-xrdp-pipe-budget.conf
sudo sysctl -p /etc/sysctl.d/90-xrdp-pipe-budget.conf
sysctl fs.pipe-user-pages-soft fs.pipe-user-pages-hard fs.pipe-max-size
```

Run this on the physical Incus host, not inside `/work` or a k3s pod. It sets
a host-wide per-user threshold of 1 GiB at 4096 bytes/page, not a reservation
of 1 GiB and not a container-specific privilege grant. Keep the existing
hard=0 and max=1048576 settings unchanged. The initial commentary suggested a
smaller 65536-page threshold; re-reading the fleet record corrected the
instruction to restore the owner's previously approved 262144-page value.
No reboot is needed. After the host correction, preserve the failed evidence,
recreate only this arm's test pod from the identical image, and rerun its
certificate and final smoke. Do not disturb the 40060/40061 controls. The
Windows handoff remains pending until that precondition is green.

The initial rendered smoke completed successfully at both 1920x1080 and
1024x768: each produced eight expected key transitions, no lag, edge fidelity
1.000, and no encoder restart/sequence errors. `initial-smoke.txt` and the
two keytest outputs retain the results. The smoke script's generic "safe to
hand over" line covers only those rendered checks; the failed pipe certificate
still blocks this arm's overall handoff. No Windows odd-size test was run.
