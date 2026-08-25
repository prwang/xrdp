# Canonical development resize repair (40060)

## Verdict

The trace-disabled 40060 arm passes the same dynamic-resolution sequence that
disconnects 40059. The client remained connected after the 2198 x 1250 update
and for the five-second observation following the 2412 x 1344 update. A
lossless screenshot captured at the end is 2412 x 1344 and shows the rendered
XFCE desktop.

The intervention changed the mechanism it targets. At 2412 x 1344, Xorg
allocated a 19,611,648-byte mapping before resizing the screen and invalidating
the new area. The pre-fix arm instead reused the login-sized 16,760,832-byte
mapping. xrdp completed the repaired resize in 37 ms and did not reject a
full-chroma snapshot.

## Controlled comparison

| property | pre-fix 40059 | repaired 40060 |
|---|---|---|
| xrdp package | `0.10.80+git20260825160820.253efd0a41f9` | identical |
| xorgxrdp package | `1:0.10.80+git20260825160036.8cf120e5db7f` | `1:0.10.80+git20260825171647.985bc42d335a` |
| server OS / desktop | Ubuntu 24.04 / XFCE | identical |
| encoder / mode | stock FFmpeg 6 libx264 / dense AVC444v2 | identical |
| GFX configuration | `gfx/x043.toml` | identical |
| resize client and sequence | repository script; 2196 x 1250, 2198 x 1250, 2412 x 1344 | identical |
| performance tracing | compile-time absent | compile-time absent |
| 2412 x 1344 mapping | stale 16,760,832 bytes reused | 19,611,648 bytes allocated |
| result | client exits 12 | connected; rendered 2412 x 1344 frame captured |

The only runtime component changed is xorgxrdp. Its repair commit rebuilds and
validates the complete layout transactionally before changing allocation,
screen geometry or capture ownership. The xrdp-side transactional helper and
its independent failure-preserves-old-layout test are at `346624e5b632`; the
40060 xorgxrdp package contains `985bc42d335a`.

## Provenance

- Source paths: `/work` and `/workUpdateXorgXrdp`
- Source branch in both repositories: `dev/avc444_metablock_checkpoint`
- Compared xrdp source: `253efd0a41f9` in both deployed arms
- Repair source: xrdp `346624e5b632`; xorgxrdp `985bc42d335a`
- Kubernetes deployment and port: `xrdp-x044`, host TCP 40060
- Container image:
  `localhost/xrdp-bisect:resize-fixed-253efd0-985bc42-u2404-xfce-notrace`
- Local image ID: `254153c5aa1a1b722d274253f053266cce916ed525b0c31974c8f62d168d4ec2`
- Imported image digest:
  `sha256:03c4d8d50893a0ad56eb87172c48be668594e126db827efff17be266f76315eb`
- `gfx/x043.toml` SHA-256:
  `f99cfc2d505debf9730655c4a5aa00e4323209d275e06847d565dd61ffffcd0c`
- Client-script SHA-256:
  `9f60ab54f9fd0f91b26f73900142eb75877d3488ebf7260f2826dc44f7d57672`

The final `xrdp_process_loop failed` and client cancellation messages are the
test harness deliberately terminating its still-running client after the
screenshot. They occur after the recorded green assertion and are not the
resize failure seen on 40059.

## Evidence index

- `result.txt`: requested sizes, liveness and captured frame dimensions.
- `post-resize.png`: lossless 2412 x 1344 rendered-frame evidence.
- `post-resize-dimensions.txt`: dimensions decoded from the original XWD.
- `client.log`: FreeRDP connection and deliberate harness termination.
- `xorgxrdp.log`: allocation, screen resize and post-resize invalidation.
- `xrdp.log`: completed monitor updates and absence of snapshot rejection.
- `xrdp-sesman.log`: session lifecycle context.

SHA-256:

```text
b541148063e2fed8ebec34c248e3976b163c4fe0c565ea0ebf9b619438f2e410  client.log
87b5c7e708f5f07792275cea43f73639cbc28ad0caafbe178b2ec7e2960a37f1  result.txt
0d7ef54543724f1aea39833e7d670328b9ae6d2e85843708f987b7deca3756bc  post-resize-dimensions.txt
47dcb1ba10fb9ab8b1bdda2aeb9a8c1ab73bcbbd3cdc3c777611dd6d9558b4ce  post-resize.png
6bcb101600e99ea85032db9a0093fe03bda314e5006b0d1ffdeb5878762f00a3  xrdp.log
a7c00331498168a430a1d66f8bd7aa05ba6025d5c82fa135378aa11476d86be2  xrdp-sesman.log
3bb1b7b3c4b50f13bfb892786f5cbd8dda21e919a216cf0e2fe60fb637750694  xorgxrdp.log
```
