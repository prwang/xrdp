# #123 — T4/NVENC compatibility qualification

**Closed 2026-08-23 with one documented client limitation.** This item
qualifies the pinned development implementation on real Tesla T4 hardware; it
does not claim that every forced codec mode has identical fidelity on every
client.

## Identity and evidence boundary

The server was provisioned as `98.93.137.204` and subsequently migrated,
without replacing the instance, to `100.55.149.97`. The exact server-side
preflight, packages, hashes, modelines, profiles, logs and local FreeRDP
captures are under
`PR-demo/mac_bisect_matrix/captures/i123_t4_frontier_preinteractive_20260822T183935Z/`.
The paired source identities are xrdp `00bce44e8fea` and xorgxrdp
`c190343ff28a`; the encoder is stock ffmpeg 8.0.1 `h264_nvenc` on an NVIDIA
Tesla T4 with driver 580.173.02.

The visual findings below are owner observations from Windows host `5Q77` and
macOS host `Signals-iMac`. The server log can establish which codec was
confirmed, not what the client displayed. The exact client application
versions were not exposed to the server and remain unknown; do not promote
these observations into a claim about all Windows or macOS RDP clients.

## Real-client matrix

| forced mode | Windows observation | macOS observation | server evidence |
|---|---|---|---|
| AVC444v2 | GREEN: one-pixel red/blue stripes remained distinct | GREEN: stripes remained distinct, though fainter; accepted as expected display resampling/antialiasing on a Retina pipeline | codec `0x000F` confirmed |
| AVC444v1 | GREEN for compatibility: stripes distinct; green lxterminal text had the expected magenta burr | RED for per-pixel fidelity: large colour regions were mostly correct, but the alternating red/blue stripe rendered red | codec `0x000E` confirmed |
| AVC420 | GREEN for the intended degraded mode: violet result and blurred text edge | same expected 4:2:0 degradation | codec `0x000B` confirmed |

The retained `/var/log/xrdp.log` was checked after the migration. It contains
the corresponding AVC444v1, AVC420 and restored AVC444v2 confirmations and no
matched probe-failure, fallback, pair-sequence, picture-sequence, parser or
timeout line in the reviewed interval.

## AVC444v1 shipping decision

Keep AVC444v1 in the clean-room implementation, but do not recommend forced
`444v1` as a general deployment mode.

This is not merely a private experiment mode. Microsoft specifies that an
RDPGFX version 10.0 capability set without `AVC_DISABLED` means the client can
process H.264 YUV444, while AVC444v2 is separately implied by version 10.1;
the server selects from the capability sets the client advertises. See
[RDPGFX_CAPSET_VERSION10](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/d1899912-2b84-4e0d-9e6d-da0fd25d14bc)
and
[versioning and capability negotiation](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/31c6e2b1-335b-4a75-9454-bb2309958c21).
The v10.0 protocol revision was published in 2016, but this record does not
claim an exact Windows product/build boundary that the protocol documents do
not provide.

Therefore the supported policy is:

* `auto` remains the normal mode and selects AVC444v2 whenever the advertised
  capability permits it;
* AVC444v1 remains the automatic compatibility tier for a v1-only advertised
  capability, so removing its implementation would discard a real protocol
  tier;
* forced `444v1` is labelled legacy interoperability/diagnostic only and is
  not recommended for ordinary configuration;
* the observed macOS forced-v1 stripe failure stays RED. It is not hidden by
  a post-confirmation codec fallback. A deployment which needs fidelity on
  that client uses the normally selected v2 path, or explicitly selects
  AVC420 if v2 is unavailable and its chroma loss is acceptable.

## Closure

#123 closes because the real hardware and client characterization is now
complete enough to define the supported policy, not because the macOS v1
result became green. The numerical frame-throughput question remains in
#125B. The sparse-profile one-second flicker discovered afterward belongs to
#125A and does not retroactively change this dense-mode matrix.
