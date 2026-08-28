# #142D development 40060 Windows control

## Result

The owner exercised the development resize-repair arm on port 40060 with the
same Windows client (`5Q77`) and interactive window-resize method used for the
clean-room reproduction. The connection negotiated the same nine GFX capsets,
confirmed v10.7 with flags zero and selected dense software AVC444v2 under the
`auto` profile.

From 00:41:34 through 00:43:16 UTC the server completed 64 dynamic resizes.
The connection has exactly one GFX capability advertisement, before login and
Xorg attachment. It has no second advertisement, black-region report or
transport EOF after the interactive sequence. The owner independently
reported that arbitrary resetting produced neither black regions nor a
disconnect. The four `SSL_read` errors earlier in the file belong to two
short certificate/probe connections at 00:41:19 and 00:41:21, before the
Windows connection was accepted at 00:41:23; they are not failures of this
control.

This is a behavioral divergence from the clean-room arm, not proof that the
shared capability callback is safe. In clean-room the first frame after the
seventh resize was not acknowledged and the same Windows client sent another
capability advertisement 53 ms later. Development did not enter that
transition despite the stronger 64-resize sequence.

## Deployed identity

```text
xrdp-dev 0.10.80+git20260825160820.253efd0a41f9
xorgxrdp-dev 1:0.10.80+git20260825171647.985bc42d335a
image localhost/xrdp-bisect:resize-fixed-253efd0-985bc42-u2404-xfce-notrace
image ID sha256:254153c5aa1a1b722d274253f053266cce916ed525b0c31974c8f62d168d4ec2
```

The paired xorgxrdp identity is the current canonical development head. The
subsequent xrdp runtime delta through canonical head `752f6f2b50ca` only adds
the pure transactional-layout helper used by the paired producer and its
tests; it does not change the xrdp capability, graphics-wire or encoder paths
under comparison.

## Files and hashes

```text
f99cfc2d505debf9730655c4a5aa00e4323209d275e06847d565dd61ffffcd0c  gfx.toml
bc11604365704e5b26ec86a8fc752353a1d3463023324a6675589e8beac8ccd4  xorgxrdp.10.log
55abdec8ad6a79ce9a0356b64f9e2e99de55fe00fba24848471cf0e12e54147b  xrdp.log
```

The log snapshot is the retained control evidence. It is not a performance
result.
