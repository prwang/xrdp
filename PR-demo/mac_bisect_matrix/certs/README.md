# certs/ — per-arm bitstream certificates

Written by `arm_certify.sh`, once per deploy, from
`build_and_deploy.sh`. Read by `e_gate_run.sh`, which refuses to measure
an arm with no certificate or a certificate that does not match the
running image + `gfx.toml`.

These files are the record that a deployed arm's REAL encoder stack —
this image's ffmpeg, this host's VAAPI driver, this arm's
`encoder_args` — emits conforming, decodable bytes. They are not a
property of the xrdp build: the rewriter logic is pinned byte-exactly by
CI (`tests/xrdp/test_avc444_ltr.c`, 26 golden-vector assertions).

Committed, because "which arms were certified, when, against which
image" is exactly the kind of thing that must survive a container
restart and be reviewable in git.
