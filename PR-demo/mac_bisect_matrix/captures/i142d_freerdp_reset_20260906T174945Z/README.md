# Deliberate FreeRDP reset: lifecycle RED

On 2026-09-06, one rendering FreeRDP connection deliberately resent capabilities
after displaying the colour-key login payload. The corrected development
server handled the repeat, created another encoder worker without retiring the
old worker or its children, and sent no repaint before the post-reset snapshot.
The client stayed connected and its output window retained stale red pixels.
This independently reproduces the ownership/missing-repaint transition with
unchanged even geometry. It does not reproduce the later Windows peer EOF.

## Exact condition

One connection, one control request, 1024×768, `tester` on
127.0.0.1:40062, existing colour-key autostart, rendering enabled and oracle
dump disabled. The server image was
`localhost/xrdp-bisect:dev-visible-clip-1cd9b5513637`; the complete deployment
is in `deployment.json`, and its actual configuration is in `server-logs.tar`.
This is the existing `auto` profile selecting AVC444v2 with auxiliary
intra-leaf encoding, not the auxiliary LTR-chain configuration.
No server package or codec setting changed for this observation.
The paired producer checkpoint is `baf9658c397d9f022172255ef4c42292c84e219b`.
The working development HEAD was `fb156632d78b0a74dd9f44bf53830c451360d859`;
the deployed server is the earlier serializer-corrected package named above.

`events.json` contains control replies and UTC operation times. The connection
began at 17:49:45.670 UTC. The reset was requested after the red desktop was
observed. The post-reset state was read before screenshot acquisition, at the
operation recorded as 17:49:54.886 UTC. The harness then logged off its whole
desktop and terminated its local client; ensuing SIGHUP/transport errors are
cleanup, not a spontaneous disconnect. `final-processes.txt` has no tester
Xorg or per-session xrdp process.

## Evidence by identity

* `wire-trace.txt` is the complete captured perf-sink file for server PID 1577.
  It has two capability advertisements on the same connection. The second is
  receive sequence 8, capability sequence 2, after transmit sequence 252.
  The last preceding frame is frame 4, whose end at transmit sequence 252 was
  already acknowledged by receive sequence 7. The repeat therefore does not
  depend on an unacknowledged or malformed odd-edge frame.
* Transmits 253–256 confirm version 10.7, reset to 1024×768, create surface 0
  and map it to the output. There is no frame between that mapping and the
  post-reset control read. The first later frame begins during logoff.
  `wire-transition.txt` is the existing analyzer's report, preserving explicit
  frame/transaction identities; it found no dropped/malformed trace records.
* The client reports one request and two confirmations (initial plus repeat).
  Its decoded-frame counter is six before reset and zero after reset. It
  reports `waiting=0` after the second confirmation, so the lack of frames is
  not a client stuck ignoring data. The live ignore count is zero; the added
  deterministic parser test exercises that path separately.
* `before-processes.txt` shows server thread IDs 1577, 1578 and 1708.
  `after-processes.txt` contains those same threads plus 2085. The trace's
  pre-reset worker identity is 124340693673664, and a new worker identity
  124340685280960 enters its wait after reset.
  The process snapshots independently show that the old thread survives.
  Production ffmpeg children 1858/1859 survive as well. The capability probe's
  temporary children 1999/2000 are distinct and are absent in the after
  snapshot. This is consistent with the live-owner overwrite at
  `xrdp_mm_egfx_caps_advertise()`, not evidence of a stale write by the orphan.
* `before.png` and `after.png` show the client's red output window. They do not
  prove repaint: no new frame was decoded. Their visual similarity is exactly
  why connection liveness or screenshot equality alone is an inadequate gate.

## Bounds and remaining work

The reset used the installed renderer and its real channel serializer. The
instrument changes client graphics state and synchronization but adds no
server per-frame logging or new trace sink. This is a binary lifecycle
observation; no performance claim is made from it.

This run deliberately reset a settled frame. It proves the actual callback,
surviving worker/children and absence of repaint, but does not establish
queued-completion or borrowed-capture disposition, shutdown-timeout safety,
replacement-allocation failure behavior, or complete repaint after correction.
The current server trace does not contain encoder allocation/deletion pointers
or exhaustive capture-release records. No stale pipe write is observed.

The server fix remains outstanding. Its tests must cover complete worker
retirement before freeing/replacing state, old queued output disposal without
transmission, exactly-once borrowed-capture and terminal-ack disposition,
failure during replacement/shutdown, and one full producer repaint after
successor readiness. A simple call to the current delete function is unsafe:
it frees state after an unconfirmed shutdown, and its queue destructors do not
fully release capture ownership. The unchanged authorized condition must be
repeated after correcting these paths. Windows interoperability, reconnect,
mode/resize controls and clean-room re-authoring remain open.

Reproduce using `PR-demo/gfx_reset_client/run.py` under its recorded image
guard. A zero exit from that capture driver is apparatus completion only;
this server lifecycle result is RED.
