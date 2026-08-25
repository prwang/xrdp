# #142 x042 profile certification correction

This record supersedes the deployment certificates described by
`i142_x042_interactive_20260825T060226Z`. The xrdp and xorgxrdp binaries did
not change. The certifier did, because closure review found that its claimed
1920x1080 sessions had actually negotiated 1024x768.

## Invalid certificates retained

The first forced-AVC420 run is
`forced-420-before-harness-fix.cert.FAILED`. It exposed three independent
certifier defects:

* dummy Xorg started at 1024x768 without the modeline setup used by the
  numerical gate, so xfreerdp clamped its requested 1920x1080 window;
* the AVC420 decoder check interpreted the single-view record as an AVC444
  two-view record;
* the single-view wire audit required two SPS NALs in a three-second window,
  although the normative contract requires each reset to carry its own SPS
  and does not require a second reset in that window.

`before-recert-x042-auto.cert`, `before-recert-x042-444.cert` and
`before-recert-x042-444v1.cert` are the earlier false greens. Their wire
audits disclose SPS geometry 1024x768 even though their headers say
1920x1080; they are not deployment evidence.

The first correction then exposed a fourth defect. The certifier passed its
wire-audit option `--aux-leaf` to the black-frame checker as if it were a
capture filename. The checker raised `FileNotFoundError`, but the final loose
search for `VERDICT: PASS` matched the wire audit's `ASSERT VERDICT: PASS`.
Those false greens are retained as
`false-green-*-after-geometry-fix.cert`. They were retracted before commit.

## Corrected certifier

The corrected gate:

* uses the existing smoke gate's exact-size Xvfb setup for this
  single-monitor certificate;
* verifies both the client framebuffer and the server's
  `xrdp_egfx_reset_graphics` dimensions;
* parses AVC420 records explicitly as single-view records;
* requires at least one in-band SPS, proving that the observed reset is
  self-describing; and
* accepts only the exact final line
  `NO MID-STREAM BLACK FRAME: PASS` as the decoder verdict.

All five exact image/config pairs then passed at negotiated 1920x1080. Their
H.264 coded geometry is 1920x1088 where the SPS is visible in the retained
tail, matching the capture contract's 16-row height alignment.

| profile | topology | pipe | wire | decoded pictures | black-frame gate |
|---|---|---:|---:|---:|---:|
| automatic | AVC444v2 auxiliary leaf | pass | 6/6 | 6/6 | pass |
| forced 444 | AVC444v2 auxiliary leaf | pass | 6/6 | 6/6 | pass |
| forced 444v1 | AVC444v1 auxiliary leaf | pass | 6/6 | 4/4 | pass |
| forced 420 | AVC420 single view | pass | 4/4 | 3/3 | pass |
| sparse 444 | AVC444v2 auxiliary leaf | pass | 6/6 | 6/6 | pass |

The exact successful certificates are `final-auto.cert`, `forced-444.cert`,
`forced-444v1.cert`, `forced-420.cert` and `sparse-444.cert`.

## Final deployment state

x042 is on `127.0.0.1:40058` with image
`localhost/xrdp-bisect:cleanroom-e0ee1961-aa03d860-u2404-xfce-notrace.pf254dc5b`
and image ID
`7af111d77b3873de3b9a153dd682d9839d1eb0a8a599512d95531a4604ad683c`.
The paired commits are xrdp
`e0ee19616fcd4f4ca20fa95df88207e69360c79f` and xorgxrdp
`aa03d860137d9be95a6bbf41c9ba29608fc6ec5d`.

The final live profile is automatic dense AVC444v2, SHA-256
`216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971`.
The post-restore rendered smoke passed both required geometries:

| geometry | transitions | lag | settled edge fidelity | encoder errors |
|---|---:|---:|---:|---:|
| 1920x1080 | 8/8 | 0 | 1.000 | 0 |
| 1024x768 | 8/8 | 0 | 1.000 | 0 |

The raw smoke logs are retained beside this README. The final pod has no
tester or probe Xorg session, no smoke marker and no performance-trace file.
The remaining #142 work is the owner's Windows/macOS visual matrix; local
oracle certification cannot substitute for those clients.
