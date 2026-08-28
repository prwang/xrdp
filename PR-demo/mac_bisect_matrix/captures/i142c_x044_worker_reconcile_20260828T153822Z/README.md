# #142C worker-boundary development deployment

## Purpose

This is the replacement 40060 red-equivalence arm after the order-64-only
control remained green through twenty owner resize interactions. Unlike that
control, xrdp retains the validated raw capture through the encoder FIFO and
constructs the bounded GFX command on the encoder worker. It deliberately does
not include the later replacement-capability callback repair.

The arm is ready for the same Windows repeated-resize sequence. A green owner
result leaves #142C red and requires another divergence audit. Reproduction of
the clean-room replacement capability advertisement establishes the precursor
needed before #142D can change the callback.

## Exact deployment

- Endpoint: `127.0.0.1:40060`; arm `x044`.
- Pod: `xrdp-x044-98977b666-khmd6`, ready with zero restarts after
  certification.
- Image:
  `localhost/xrdp-bisect:reconciled-worker-b11655a-baf9658-u2404-xfce-notrace`.
- Image ID:
  `sha256:ad457453048d8eeca4ebca74ab2a393893b8c964b08a96c6f00d1ee88501cac0`.
- xrdp package: `0.10.80+git20260828153626.b11655aa0b02`, built from
  canonical `/work` at `b11655aa0b02`; functional change `31f0df2b`.
- xorgxrdp package: `1:0.10.80+git20260828123216.baf9658c397d`, built from
  canonical `/workUpdateXorgXrdp` at `baf9658c397d`.
- Profile SHA-256:
  `5372197d3246b5da5fd29868c1f3b15b1dbd070a6b4be3b16cd0e56fdd93e22e`.
- Package SHA-256: xrdp
  `cc2f8c636c5edf8585bf9a5ca3516a2b70aecbc19928556b56098169de693dab`;
  xorgxrdp
  `90f59ebaeb4471cd376db3694d3b1c399aa8368b0b36f6fa6f061429a1cba984`.

## Gates

- Default complete suite: daemon 218/218, all other suites green.
- Trace-enabled complete suite: daemon 219/219, all other suites green.
- Restored default build: trace-disabled footprint gate green; the package is
  trace-disabled.
- Three-second deployed certificate: encoder pipe adequate, both AVC444 views
  present, six auxiliary-leaf wire assertions pass, four pictures decode and
  no black frame is present.

The certificate forcibly ends its oracle client and logs the certification
session off. Its expected peer EOF/send errors and child SIGTERM are not
backend failures; there is no invalid-capture, encoder, probe or pipe error,
and the container restart count remains zero.

## Files

- `x044.cert`: exact deployed profile/image certificate.
