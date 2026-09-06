# Controlled graphics capability reset: specification and apparatus gap

Research date: 2026-09-06. The owner asks for a deterministic trigger independent
of odd-edge metadata, preferably callable from the remote session terminal.
This record is a protocol/source review, not a successful live test.

## Microsoft contract

The current published MS-RDPEGFX revision is 19.0 (2026-05-11). Its
[client confirmation rule, section 3.3.5.19](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/a720af02-0a47-44e2-a934-dc8534062b44)
allows a client confirmed at versions 10.3 through 10.7 to resend capabilities
within the existing connection. The client resets channel state and ignores
server graphics messages until the next capability confirmation. The captured
Windows connection selected version 10.7. A malformed frame is not a stated
precondition for this operation.

The [server receive rule, section 3.2.5.18](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/478c0920-22a7-4817-bb25-683dc54ee570)
requires another confirmation and a protocol reset for a repeated advertisement
at version 10.3 or later. The server must assume earlier channel messages were
disregarded by the client. This establishes the legal reset operation for the
observed version; it supersedes only the earlier uncertainty about whether a
repeat was allowed, not any unknown Windows decoder rejection mechanism.

[Client higher-layer events, section 3.3.4](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/519351ca-fff2-48bc-9d49-34ad89f82733)
and [other local events, section 3.3.7](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/1496b9f8-d2c8-4e1a-8989-bcd76d43b895)
specify none. They do not prescribe a terminal command or Windows UI action
which forces this choice. [ResetGraphics processing, section 3.3.5.14](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/f39e0c2d-004e-400e-8ef2-7f52d1f62d49)
requires resizing the graphics output buffer; it does not require resending
capabilities. Therefore resizing with xrandr or changing a window size is not
a specification-backed deterministic renegotiation trigger. No documented
stock MSTSC command or remote-session API for forcing the resend was found.
This is a bounded search result, not proof that no private client mechanism
exists. GPU-reset shortcuts, minimizing, reconnecting and malformed traffic
were not tried or promoted to equivalent tests.

The Learn browser fetch failed for some section URLs. The same canonical
pages were successfully read directly with accept=text/markdown, using their
URLs from the official MS-RDPEGFX toc.json; the server rule's source metadata
identifies Microsoft documentation commit 1a26d38e66d1f0bacd50c134f42ad2c04da4f4b4.

## Proposed controlled apparatus, not yet implemented

The capability advertisement travels from client to server. An ordinary
application in the Linux desktop cannot generate that packet on behalf of
stock MSTSC. A script can be the control interface only if a cooperating
client already owns the RDP connection.

A dedicated rendering test client should expose a private control command
which requests one graphics reset, marshalled onto its protocol-processing
thread. Retain the same TCP/RDP connection and GFX dynamic channel; require an
already confirmed 10.3-or-later version and a rendered desktop before accepting
the command. Resend the client's actual supported capabilities, reset its
protocol/decoder/surface state and follow the ignore-until-confirm rule. Resume
normal decoding and acknowledgements after confirmation. Do not simply replay
bytes while retaining old client state.

FreeRDP 3.15.0 is a concrete implementation seam: its
[rdpgfx client](https://github.com/FreeRDP/FreeRDP/blob/3.15.0/channels/rdpgfx/client/rdpgfx_main.c)
assigns RdpgfxClientContext.CapsAdvertise to its real channel serializer;
rdpgfx_recv_caps_confirm_pdu handles the response. The installed headers expose
that callback. Reading this API proves a sender seam, not a complete reset
implementation or a ready command. Client reset state, synchronization and
rendering must be implemented and validated. No client was built or changed
in this research turn.

A session-terminal wrapper could reach that client control over a dedicated,
private test channel or an authenticated tunnel established before login.
That would work only for the cooperating test client, not an existing stock
MSTSC connection. Select and implement the control transport with the harness;
no general server debug endpoint or callback-invocation hook is needed.

## Required first observation

Use valid, unchanged visible geometry on the actual corrected development
server. Prove one control request produces exactly one new inbound capability
advertisement on the same connection after desktop attachment, followed by
its matching confirmation. A send-only client success is insufficient.
Preserve current faulty retirement behavior until it is captured. Record
encoder installation/deletion identity, pending capture/completion ownership
and first full producer repaint through the existing trace sink; the current
40062 bounded wire trace alone does not prove all lifetime obligations.

The first resulting capture must show whether that deliberate valid reset
exposes the orphan/no-full-repaint defect. Only then change retirement, keeping
the same instrument and trigger. Include in-flight-work timing as a separately
specified condition if required; do not assume a frame-boundary reset exercises
all ownership races. The client must render and acknowledge normally: the
existing save-only oracle does not prove repaint fidelity.

This validates the real xrdp lifecycle against a deliberate protocol-valid
client request. It does not establish why Windows chooses a recovery action
or replace subsequent Windows interoperability acceptance. Apparatus and live
reproduction remain open. No experiment, deployment, production edit or
clean-room change was performed for this review.


## 2026-09-06 continuation: rendering control built and first reset reproduced

The earlier apparatus-gap statements describe the research stopping point.
`PR-demo/gfx_reset_client/` now contains the pinned FreeRDP graphics-channel
build, private local control, deterministic receive-parser checks and bounded
rendering capture driver. The control is dispatched by FreeRDP's protocol
event loop and serialized with its graphics receive callback. The server's
real inbound callback receives the client's actual supported capabilities.

The first authorized run, on corrected development port 40062 at unchanged
1024×768 geometry, is retained in
[`i142d_freerdp_reset_20260906T174945Z`](../../PR-demo/mac_bisect_matrix/captures/i142d_freerdp_reset_20260906T174945Z/README.md).
The previous frame was acknowledged before the deliberate repeat. The server
confirmed and recreated its surface, but an additional worker appeared while
the previous worker and production ffmpeg children remained alive. No repaint
was received before the post-reset snapshot. The client retained stale red
pixels and stayed connected. This reproduces the lifecycle independently of
odd-edge metadata; it does not reproduce or explain Windows' later EOF.

The initial scaffold failed to build because warning-as-error settings caught
pinned upstream header diagnostics. Its build now excludes those two known
header warning classes and explicitly links the required SDK libraries. The
new receive-parser test initially lacked the settings context used by
FreeRDP's post-confirmation cache offer; adding that fixture context resolved
the test setup failure without changing its assertions. The parser checks
now pass. The normal repository test suite also passed, with its existing
root-only permission-test skip. These checks do not test server retirement.

No server production code, package, codec configuration or clean-room tree
changed in this continuation. The remaining correction must cover shutdown
failure and exhaustive queued input/completion ownership before replacement;
the existing deletion function frees shared state even after an unconfirmed
worker stop, and its queue destructors do not fully dispose of borrowed
captures. The settled-frame reproduction does not prove any of those failure
paths. The matching corrected run, rendered repaint proof and broader
interoperability acceptance remain open.
