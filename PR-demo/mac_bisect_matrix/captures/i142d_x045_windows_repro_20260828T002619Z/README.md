# #142D x045 Windows lifecycle reproduction

## Result

The owner reproduced the persistent-black-then-disconnect sequence on the
fault-preserving x045 arm at port 40061. The xrdp connection ran from
2026-08-28 00:26:19 UTC through 00:26:40 UTC. Its process was PID 1056. The
tester Xorg and desktop remained alive after the RDP transport closed and were
not restarted or otherwise manipulated while this evidence was collected.

This capture proves two server defects at the second capability advertisement:

1. The callback resets the client graphics state and installs a new encoder
   while a direct-Xorg session is attached, but requests no current Xorg
   frame. The replacement encoder remains at server/client/consumed frame
   counters 0/0/0 for 6.6 seconds, until the owner's next resize requests a
   full producer repaint. This is the cause of the persistent black state.
2. The callback overwrites encoder `0x5a9396525ff0` without deleting it. The
   connection creates ten encoders and records nine complete deletions; the
   unmatched object is exactly that pre-advertisement encoder.

The capture does not show the orphan emitting a stale graphics PDU. After the
next resize, the reachable replacement is cleanly deleted, the producer sends
one full-screen two-slot update, xrdp successfully transmits logical frames 1
and 2, and the peer then closes the TLS transport before acknowledging either
frame. The server records no fatal decision or send failure before that EOF.
Consequently the invalid capability-reset transaction is the correction seam,
but this capture does not justify the narrower claim that the orphan alone
made the Windows client close.

It also does not establish development/clean-room equivalence. A later run
with the same Windows client, nine capsets, v10.7 confirmation, dense
AVC444v2 software profile and interactive resize method completed 64 resizes
on development 40060 without another capability advertisement,
black regions or disconnect. In this capture the client did not acknowledge
the first frame after the seventh resize and sent the replacement capability
advertisement 53 ms later. The upstream clean-room-only wire/state divergence
between that frame and the advertisement remains open; the callback defects
documented here are the proven second layer.

## Event sequence

- `00:26:33.386`: Xorg supplies the full 1820-by-1171 frame requested by a
  completed dynamic resize.
- `00:26:33.435` through `.436`: xrdp sends logical frame 1 from that update.
- `00:26:33.439`: the Windows client re-advertises graphics capabilities while
  the direct-Xorg module and encoder `0x5a9396525ff0` are live.
- `00:26:33.552`: generation 2 resets the surface. The diagnostic route is
  `direct-xorg-none`.
- `00:26:33.563`: generation 2 installs encoder `0x5a93964a2c60` with its first
  producer update still pending. No delete-begin or delete-complete event for
  `0x5a9396525ff0` precedes this assignment or appears later.
- `00:26:40.164`: the next resize begins. The still-pending replacement reports
  frame counters 0/0/0, independently confirming that no Xorg update followed
  the capability reset.
- `00:26:40.170` through `.202`: the resize deletes that replacement, resets to
  1804 by 1171, creates its successor and requests a direct producer repaint.
- `00:26:40.213`: Xorg supplies the full 1804-by-1171 update.
- `00:26:40.260` through `.264`: xrdp successfully sends logical frames 1 and
  2. The trace records no acknowledgement for either.
- `00:26:40.307`: `SSL_read` reports the peer's unexpected EOF. Encoder cleanup
  begins at `.313`; the first graphics send errors appear only at `.326`, after
  the transport is already gone.
- Xorg sees its local xup peer disappear at monotonic time `45508.916` and
  removes only that client connection. The desktop session remains alive.

The intervals above establish ordering and the absence of a repaint. They are
not performance results.

## Instrument integrity

`xrdp-perf.1056.log` contains one clock base and 648 total records. It contains
no `trace_dropped` or format-failure record. Its terminal
`event=perfnoring count=3` is emitted while the sink is closing after the peer
EOF; it is not an in-run ring overflow. The ordinary log contains only
lifecycle-rate instrumentation. Per-frame damage, encode, send and
acknowledgement events use `common/perf_trace`.

The arm is clean-room xrdp `f8d8d06d2ffd` plus diagnostic commit
`6e22c7fdac7f`, paired with unchanged clean-room xorgxrdp `aca3c774cb8b`.
`gfx.toml` is the same `auto` profile used by x042 and has SHA-256
`216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971`.

## Files

- `xrdp.log`: complete server log containing the lifecycle identities and the
  terminal TLS ordering.
- `xrdp-perf.1056.log`: the reproduced connection's named perf trace.
- `xrdp-perf.1054.log`: the one-record preliminary connection trace retained
  to make PID attribution auditable.
- `xorgxrdp.10.log`: the surviving producer/session log.
- `xrdp-sesman.log`: session-manager lifecycle.
- `gfx.toml`, `arm_label`, `session_kind`: deployed identity and configuration.

SHA-256:

```text
9268eefc9ed58cf16e2a276cf52db4cedaa72f4c653b9693754be3fc65331f47  arm_label
216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971  gfx.toml
0a54d7f46a223187396830932cb261aca6b184ac66babfd0891b7417ae04a80f  session_kind
bd1fa0538e2f5088789229d3562d6f7bde9dfd6aea81449f943ffc371447a4d4  xorgxrdp.10.log
ddd6f2300136ec806d03291eb43d5f40d75db66468815b17af876a8a29083586  xrdp-perf.1054.log
38ade4eb62c22471e4e9308b91e4c192898b6edd14493a155b7cdb31b82feefa  xrdp-perf.1056.log
2e42c019d53b75c008707e2a18533d2c29a29f04136898c78a4bad58d6adb3e9  xrdp-sesman.log
0ccc4d0994091cf7eecbe89f2acef9562afeb72ff83993bf57daa4f3a99ad7c0  xrdp.log
```
