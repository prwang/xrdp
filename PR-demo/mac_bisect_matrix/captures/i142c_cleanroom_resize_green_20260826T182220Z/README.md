# #142C corrected clean-room replay and resize proof

Recorded 2026-08-26 after the transactional layout refresh was re-authored in
the owning clean-room slice 136 and every descendant through slice 142 was
replayed.

## Exact identities

- xrdp clean-room head: `f8d8d06d2ffda21cdf49477d3f62518c1323df94`;
- xorgxrdp clean-room head: `aca3c774cb8b828371c555dbed5886f176ddf5ef`;
- xrdp package SHA-256:
  `0c38c1c1a6003b4484414f8678ca55925be57a5e65df30f3325aeccb89352dc3`;
- xorgxrdp package SHA-256:
  `80312ed35177374f811e189cbfaaa247e8c4edf367e20f6ac969248e2b7e0b05`;
- acceptance image:
  `localhost/xrdp-bisect:cleanroom-f8d8d06d-aca3c774-u2404-acceptance.p0c38c1c1`;
- deployed image ID:
  `cbc27ea874f141822f0c47cdb548c57b75827ccae6f20f5bb93fb496b3dd3dfd`;
- automatic-profile SHA-256 prefix: `216aed1fd3b78c26`.

The image is Ubuntu 24.04 with FFmpeg 6/libx264. It retains XFCE,
LXTerminal, package-default xterm and the indexed payloads. It adds Thunar
4.18.8, official Chromium snapshot revision 1686537 with a hash-verified
archive, and an explicit container launcher. The test accounts can open the
root-owned render node in this privileged acceptance pod. The real RDP Xorg
log records `/dev/dri/renderD128 open ok`, `glamor init ok` and
`rdpScreenInit: glamor_init ok` on the Radeon device. Chromium rendered a
headless test document as `tester`, and its desktop entry passed
`desktop-file-validate`.

## Replayed slice gates

Slices 126 through 135 are byte-identical to the already retained exact
slice audit. The corrected tail was gated before each next slice was admitted:

| slice | xrdp commit | default daemon suite | trace-enabled daemon suite | static gate |
|---:|---|---:|---:|---|
| 136 | `353059eb` | 108/108 | 108/108 | green |
| 137 | `f7b579d8` | 115/115 | 115/115 | green |
| 138 | `d80e6952` | 147/147 | 147/147 | green |
| 139 | `a7304557` | 156/156 | 156/156 | green |
| 140 | `c0430f53` | 177/177 | 177/177 | green |
| 141 | `94670c28` | 188/188 | 188/188 | green |
| 142 | `f8d8d06d` | 204/204 | 204/204 | green |

Every xrdp static row is `git diff --check`, pinned astyle 3.4.14 and pinned
cppcheck 2.20.0. The paired producer slice 136 (`27ea93c`) and slice 140
(`aca3c77`) each built and passed their repository tests against the matching
checked-out xrdp header. The final producer has two passing yuv444 programs
and one passing yuv2rgb program; focused cppcheck and `git diff --check` are
green. xrdp's recursive astyle policy was not applied to the differently
styled xorgxrdp pinned base.

## Dynamic resize result

The repository resize client connected to port 40058 at 2196 by 1250, changed
to 2198 by 1250, then changed to 2412 by 1344. It remained connected for the
five-second observation and produced a lossless 2412-by-1344 rendered XFCE
frame. Xorg allocated the independently expected 19,611,648-byte mapping
before publishing the new geometry. No invalid-snapshot rejection occurred.

This closes the same dimensional arithmetic that failed on the original
clean-room arm: 2412 by 1344 requires 2,850,816 bytes more than the stale
16,760,832-byte login allocation. The changed mechanism and the downstream
result therefore agree.

Files:

- `resize/result.txt` — requested sizes, liveness and captured dimensions;
- `resize/client.log` and `resize/xvfb.log` — client-side diagnostics;
- `resize/post-resize-dimensions.txt` — independent 2412-by-1344 dimensions;
- `post-resize.png` — compact lossless rendered-frame evidence;
- `../../certs/x042-auto.cert` — pipe, wire-topology and decode certificate.

The visual Windows/macOS profile matrix is deliberately not claimed by this
record. It remains owner acceptance after this automated handoff.

The final rendered smoke was the last deployment gate. At both 1920 by 1080
and 1024 by 768 it reported 8/8 transitions, zero lag, edge fidelity 1.000 and
zero encoder errors. The smoke session exited cleanly; no tester or probe Xorg
remained before the onscreen handoff files were installed.
