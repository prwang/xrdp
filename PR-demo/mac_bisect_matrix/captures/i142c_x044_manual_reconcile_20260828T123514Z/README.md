# #142C manually reconciled development deployment

This is the trace-disabled development arm prepared for the owner's repeated
Windows resize reproduction. It does not close behavioral equivalence: the
required result is that development now reaches the same black-surface
precursor and subsequent disconnect as clean-room. A surviving interactive
session remains a red reconciliation result.

## Deployed identity

- Endpoint: `127.0.0.1:40060`; Kubernetes arm `x044`.
- Image:
  `localhost/xrdp-bisect:reconciled-manual-f7d8c2b-baf9658-u2404-xfce-notrace`
  (local image ID `1089f62d8480`).
- xrdp source/package: `f7d8c2b527c7`,
  `0.10.80+git20260828123212.f7d8c2b527c7`.
- xorgxrdp source/package: `baf9658c397d`,
  `1:0.10.80+git20260828123216.baf9658c397d`.
- Profile: `gfx/x044.toml`, dense automatic AVC444, auxiliary-leaf topology,
  eager slot acknowledgement and wire window 1. Its full SHA-256 is
  `5372197d3246b5da5fd29868c1f3b15b1dbd070a6b4be3b16cd0e56fdd93e22e`,
  identical in the pod.
- Pod after the profile-key correction:
  `xrdp-x044-6cc8964899-t22xk`, ready with zero container restarts.

## Automated gate

The complete development xrdp suite passed in default and trace-enabled
builds. The daemon totals were 218/218 and 219/219 respectively; the difference
is the compile-time tracer test. The paired xorgxrdp build and its two test
programs passed. The packaged build was then restored to the default
trace-disabled configuration.

The final deployment certificate is `../../certs/x044.cert`. Its three-second
1920-by-1080 oracle capture has both AVC444 views, a reference-bearing main
chain, non-reference auxiliary leaves, no undersized encoder pipe, six of six
wire assertions green and no decoded black frame. This certifies the deployed
bytes and configuration only; it is not a Windows resize substitute.

## Invalid first certificate retained

`invalid-missing-gfx.cert` is the first certificate and is invalid. The deploy
script mounted `gfx/x043.toml`, while `arm_certify.sh` tried to hash the absent
`gfx/x044.toml`. Because the script did not reject an unreadable profile, it
continued with default parser values, wrote an empty configuration hash and
printed `CERTIFIED`. The runtime bytes happened to pass, but the certificate
did not identify the configuration it claimed to cover.

The result was not used. The certifier now refuses an unreadable profile,
`gfx/x044.toml` owns an explicit byte-for-byte-equivalent runtime profile, the
deploy script mounts it, and the pod was restarted before the valid certificate
was produced. The invalid record is retained to preserve the failed gate and
explain why it was superseded.

## Open interactive result

Connect the same Windows client to port 40060 in automatic mode and repeat the
sequence which distinguished development from clean-room: resize repeatedly,
including both quick and settled changes, until either black regions appear
and the next resize disconnects, or at least the prior 64-resize control depth
is exceeded. Record the resize count, whether a black region preceded the
disconnect, and whether reconnecting the preserved session first shows a black
surface which another resize restores or tears down. No result has yet been
claimed for this deployment.
