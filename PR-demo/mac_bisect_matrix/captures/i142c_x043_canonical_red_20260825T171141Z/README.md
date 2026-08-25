# Canonical development resize reproduction (40059)

## Verdict

The trace-disabled packages rebuilt literally from `/work` and
`/workUpdateXorgXrdp` reproduce the accepted 40059 failure. The versioned
client remained connected through the 2198 x 1250 resize, then exited with
status 12 after the 2412 x 1344 resize. xrdp rejected the stale full-chroma
snapshot at 17:11:48.420 UTC.

Xorg resized its screen to 2412 x 1344 while reusing the 16,760,832-byte
mapping created for the 2196 x 1250 login geometry. The current two-slot
packed-view layout requires 19,611,648 bytes.

## Provenance

- Source paths: `/work` and `/workUpdateXorgXrdp`
- Source commits: xrdp `253efd0a41f9`; xorgxrdp `8cf120e5db7f`
- Kubernetes deployment and port: `xrdp-x043`, host TCP 40059
- Container image:
  `localhost/xrdp-bisect:reconciled-253efd0-8cf120e-u2404-xfce-notrace`
- xrdp package:
  `0.10.80+git20260825160820.253efd0a41f9`
- xorgxrdp package:
  `1:0.10.80+git20260825160036.8cf120e5db7f`
- OS and desktop: Ubuntu 24.04, XFCE
- Encoder and mode: stock Ubuntu ffmpeg/libx264, dense AVC444
- Performance tracing: compile-time disabled

The owner had already confirmed the equivalent prior 40059 deployment
interactively. This rerun corrects build-path provenance; it does not replace
that visual observation.

## Reproduction

The repository's `run_resize_contract.sh` script ran FreeRDP inside a
2800 x 1600 Xvfb display:

```text
xfreerdp3 /v:127.0.0.1:40059 /u:probe444 /cert:ignore /sec:tls \
  /gfx:AVC444,small-cache:off,thin-client:off /size:2196x1250 \
  +dynamic-resolution /client-hostname:xrdp-resize-contract
```

The password argument is omitted. The script requested 2198 x 1250 after five
seconds, waited two seconds, requested 2412 x 1344 and observed the connection
for five seconds.

## Evidence index

- `result.txt`: requested dimensions, client liveness and exit status.
- `client.log`: FreeRDP output and terminal status.
- `xrdp.log`: completed monitor updates and snapshot rejection.
- `xorgxrdp.log`: screen resize and mapping reuse.
- `xrdp-sesman.log`: session lifecycle context.

SHA-256:

```text
98d10c94f7b5513305b1df0ac0f27be23067a9e91a18a260b83ad480caccb953  client.log
5577e63ac5c6f98b71896a9c2574cfda80af10b5ba4fc5bd04b88334275ad933  result.txt
f72f8a2472558b95fb697a6c330064a9d3f28749921617bd89f2c34b5f7531b7  xorgxrdp.log
206d07f38d50e6396c12f9e8e528f7fc10aae327a611739d339c8189196c9f1a  xrdp-sesman.log
930e9721a0d6305a3181eeffab46f4f14987a6a2e523cf33c7a9790e1e5c8d89  xrdp.log
```
