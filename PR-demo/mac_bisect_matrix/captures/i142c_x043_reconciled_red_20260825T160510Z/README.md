# Reconciled development resize reproduction (40059)

## Verdict

The final reconciled development arm reproduces the clean-room resize
disconnect without temporary resize diagnostics.  The versioned client script
reports that the client remained connected through the 2198 x 1250 resize,
but exited with status 12 five seconds after the 2412 x 1344 resize.  The
server rejected the first full-chroma capture after that resize at
16:05:18.024 UTC.

The producer resized the Xorg screen to 2412 x 1344 but reused the
16,760,832-byte shared-memory mapping created for the 2196 x 1250 login
geometry.  A current 2412 x 1344 two-slot packed-view layout requires
19,611,648 bytes.  The consumer derived the latter layout from the completed
resize and rejected the stale producer snapshot.

## Arm identity

- Kubernetes deployment and port: `xrdp-x043`, host TCP 40059
- Container image:
  `localhost/xrdp-bisect:reconciled-b282b0c-8cf120e-u2404-xfce-notrace`
- xrdp package:
  `0.10.80+git20260825160036.b282b0c19ec1`
- xorgxrdp package:
  `1:0.10.80+git20260825160036.8cf120e5db7f`
- OS and desktop: Ubuntu 24.04, XFCE
- Encoder and mode: stock Ubuntu ffmpeg/libx264, dense AVC444
- Performance tracing: compile-time disabled

## Reproduction

The repository's `run_resize_contract.sh` script ran FreeRDP inside a
2800 x 1600 Xvfb display with this command shape (the password argument is
deliberately omitted):

```text
xfreerdp3 /v:127.0.0.1:40059 /u:probe444 /cert:ignore /sec:tls \
  /gfx:AVC444,small-cache:off,thin-client:off /size:2196x1250 \
  +dynamic-resolution /client-hostname:xrdp-resize-contract
```

After five seconds, the script resized the client window to 2198 x 1250.
After two more seconds, it resized the window to 2412 x 1344 and observed the
client for five seconds.

## Evidence index

- `result.txt`: requested dimensions, client liveness and exit status.
- `client.log`: FreeRDP output; the server disconnect appears at 16:05:18.
- `xrdp.log`: both dynamic resize completions and the full-chroma snapshot
  rejection.
- `xorgxrdp.log`: screen geometry changes and mapping reuse.  This log also
  contains the preceding equivalent manual reproduction at 16:02 UTC.
- `xrdp-sesman.log`: session lifecycle context.

SHA-256:

```text
ddd6e101def5b84ba82192d99c641dd300fd1d4fdae50dd38873ad9ce2eb6afa  client.log
3a5fc484caad7b4eec9a57efe0dea00a6ab29427c3d3c848563ca86c4ae51422  result.txt
b3aaa20abbd984c3fb79e29451ee3cb570b6d19cadc96bc62ad8f480d3b267af  xorgxrdp.log
6c4a9d6cb8e51a883e6eb01de86ac02c8ec8df047c7c0ed01aba9bda981b21ab  xrdp-sesman.log
36382b68679bd44feb729e6d66190b7994c434513d954be954a32c4690379b39  xrdp.log
```
