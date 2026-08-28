# #142D repeated-resize transport teardown

## 2026-08-27 owner observation

The owner reported that the corrected clean-room acceptance session on local
port 40058 hung up after two interactive resizes from the Windows client. The
arm was still running the trace-disabled xrdp `f8d8d06d` and xorgxrdp
`aca3c774` candidate. The pod was not rolled or restarted during the failure,
and the existing Xorg session was left untouched for inspection.

This is a red acceptance result. It supersedes the handoff statement that the
remaining work was only a visual profile matrix; it does not supersede the
earlier automated result or recast that narrower result as false.

## What the surviving server state proves

The xrdp log records successful completion of every resize request in the
session. In the final sequence, it reset the graphics surface to 1624 by 889,
received the producer's matching memory-allocation-complete message, created
the matching surface, created the software encoder and cleared the resize in
41 ms. Xorg independently records a 1624-by-889 screen and an 8,781,824-byte
shared-memory allocation before capture resumed. There is no stale mapping,
layout rejection, ffmpeg creation failure or server process crash in this
sequence.

The teardown follows the completed resize:

* `22:57:30.353 UTC`: xrdp reports the 1624-by-889 resize complete;
* `22:57:30.436 UTC`: xrdp's TLS read fails with an unexpected EOF;
* `22:57:30.451 UTC`: sesman reports that the xrdp client connection exited;
* at the same boundary, Xorg's xup receive returns zero and it removes the
  xrdp connection while keeping the desktop session alive.

The 83 ms interval is quoted only to establish ordering. It is not a latency
measurement and carries no performance conclusion.

The server-side causal boundary is therefore narrower than the symptom: the
transport visible to xrdp closed without a TLS close notification, then the
xrdp connection process closed the local xup connection to Xorg. The logs do
not identify the initiator. The source visible inside the pod is the k3s host
address, so an unexpected EOF alone cannot distinguish the Windows client
from a forwarding/tunnel failure or an unlogged server-side protocol path.
Endpoint-side evidence is required before assigning root cause.

## Difference from the corrected stale-layout defect

The #142C defect changed Xorg's screen geometry while retaining a login-sized
capture mapping. The consumer correctly rejected the first stale snapshot.
Here, producer and consumer agree on every observed geometry and allocation,
including the final size, and the transport EOF comes only after the resize
state machine reports completion. The #142C allocation correction is active
and has not been falsified by this observation.

The earlier repository client result also remains accurately scoped: its
2196-by-1250 to 2198-by-1250 to 2412-by-1344 sequence stayed connected for the
five-second observation. It did not cover the Windows client's longer series,
rapid grow/shrink changes or the graphics-capability re-advertisement seen in
this session. Treating those as equivalent sequences would be an
apples-to-apples error.

## Missing onscreen handoff files

The previously checked instruction copy and profile reference were absent
from `/home/tester` when this failure was inspected. The same pod had restarted
after those files were copied. They had been copied into the container's
writable root filesystem after rollout, not supplied by an image or mounted
volume, so restart removed them. The historical checked draft and profile
reference were restored without touching the live GUI session; their SHA-256,
owner and mode were rechecked.

The durable correction belongs to the #142 acceptance deployment: make the
checked handoff files restart-stable and prove their presence after one
container restart. The ordinary desktop identity must remain a non-root user.
At the time of inspection its uid and primary gid were 1100, but it also had
supplementary gid 0 from the earlier render-node workaround. That is not an
unqualified "unprivileged tester" configuration and must be resolved or
explicitly constrained before final handoff rather than hidden in the test
instructions.

## 2026-08-27 repeated reproduction supersedes the endpoint-only framing

The owner clarified and then reproduced the sequence multiple times. The
first visible failure is persistent black regions after the penultimate
resize. A following resize tears down the connection. This means the TLS EOF
in the first inspection is not an adequate root-cause boundary; it can be a
client reaction to an already-invalid graphics state.

The retained logs contain three concise repetitions:

* 2450 by 1344 completes, 1720 by 1163 completes, then TLS EOF;
* a later connection resizes to 1750 by 1163 and then reaches TLS EOF; and
* 2450 by 1344 completes, 1750 by 1163 completes, then TLS EOF.

In each case Xorg records the same requested geometry, a newly allocated
mapping sized for it and the full-screen invalidate request. The stale-layout
fix remains active. The owner's visual observation supplies the fact the
server logs cannot: successful state-machine completion does not mean the new
surface contains a complete desktop.

### Retracted hypothesis

An initial source read suggested that the resize-time full-screen capture was
dropped while the encoder was absent and never requested again. Following
`xrdp_bitmap_invalidate()` into `xrdp_bitmap.c` falsified that claim: for the
screen bitmap it sends `WM_INVALIDATE` to the backend module after encoder
creation. Xorg's post-resize `rdpClientConProcessMsgClientInput: invalidate`
lines are consistent with that request. The dropped-redraw explanation must
not be quoted as the cause.

### Confirmed reachable lifecycle defect

One connection can advertise RDPGFX capabilities before login and advertise
them again after xrdp attaches the existing Xorg desktop. The live log shows
both callbacks and two external-H.264 encoder creations in the same connection
lifecycle. In both canonical development and clean-room,
`xrdp_mm_egfx_caps_advertise()` assigns
`self->encoder = xrdp_encoder_create(self)` without first deleting a non-null
encoder. No intervening assignment or module transition deletes it.

The second assignment therefore loses the only owner pointer to the first
encoder, including its worker, ffmpeg children, completion queues, captured
shared-memory mappings and frame/acknowledgement state. The next resize can
delete only the replacement. This is a real lifecycle defect whether or not
it proves to be the sole cause of the observed visual sequence, and it is
present as latent source behavior in development rather than introduced by the
clean-room rewrite. The later same-client control establishes that development
does not reach it under the clean-room reproduction sequence.

The same callback also explains the black surface directly. It sends
`ResetGraphics` and creates a new surface before replacing the encoder. Its
only subsequent repaint call is `xrdp_mm_egfx_invalidate_wm_screen()`. That
helper intentionally does nothing after a direct-GFX backend such as Xorg is
loaded, because repainting the WM screen would conflict with module-owned
updates. The callback does not instead call the module's full-screen
invalidate operation. The new client surface therefore receives no complete
current Xorg frame; subsequent ordinary damage paints only portions of its
initially blank contents. Persistent black regions are the predicted result.

This differs from the dynamic-resize completion path itself. That path calls
`xrdp_bitmap_invalidate()` after creating the successor encoder, and for the
screen bitmap that function sends `WM_INVALIDATE` to Xorg. The defect is the
repeated capability path resetting the surface without the corresponding
direct-backend invalidation, compounded by replacing its live encoder without
teardown.

### Why 40060 did not reproduce it

The retained 40060 configuration also used `avc_mode = "auto"`, and its log
records the same resulting dense AVC444v2 software path. `auto` is therefore
not the missing ingredient.

The retained 40060 xrdp log records one capability callback at
17:20:43.714 UTC, before its direct-credential FreeRDP probe logged in and
attached Xorg. It records no second callback after attachment. The following
two resizes use the dynamic-resize path, which ends by invalidating Xorg after
the successor encoder exists; the connection stayed rendered and alive.

The failing Windows sequence is different at the precise lifecycle seam. For
example, one connection advertises at 23:07:36.456 UTC while displaying the
login screen, attaches the existing tester Xorg session at 23:07:39.356, and
advertises again at 23:07:39.480. The second callback resets the now-live
surface without requesting a direct-Xorg repaint and overwrites the first
encoder. Two subsequent resizes complete, and the connection reaches an
unexpected TLS EOF 230 ms after the second completion. Other retained Windows
connections re-advertise capabilities immediately after a resize rather than
immediately after attachment. The common missing ingredient is therefore a
capability re-advertisement while a direct-Xorg graphics session is already
live, not the codec-selection mode, session geometry or the mere fact that an
old session exists.

That statement identifies the immediate trigger, not behavioral equivalence.
The same Windows client later exercised development 40060 with the same nine
capsets, v10.7 confirmation, dense AVC444v2 software profile and interactive
resize method. Development completed 64 resizes without a
second capability advertisement, black regions or disconnect. In the
clean-room reproduction the client acknowledged both post-resize frames for
the preceding resize, but after the seventh resize it did not acknowledge the
first transmitted frame. It instead sent another capability advertisement
53 ms after that frame. The clean-room path therefore caused or exposed an
earlier client-visible difference before the known callback defect ran.

The source-level fact is narrower: both trees contain the unsafe callback
behavior *if* a second advertisement is delivered while an encoder is live.
It does not show that development reaches that state. The live comparison
proves that it did not. The current root-cause boundary is between the last
clean-room ResetGraphics/full-frame transmission and the client's replacement
advertisement. Candidate seams are the independently implemented capture
ingress, graphics-command serialization, frame/ack ordering and encoded frame
payload. None is yet convicted. Configuration, capset selection, geometry
allocation and `auto` mode are excluded by the paired observations.

The development control log and exact package/config identities are retained
under
[`i142d_x044_windows_control_20260828T004123Z/`](../../PR-demo/mac_bisect_matrix/captures/i142d_x044_windows_control_20260828T004123Z/README.md).

The earlier 40060 and replacement-40058 automated result did not qualify this
condition. Its FreeRDP probe account checked one fixed growth sequence for
five seconds. It never entered the live-session capability-replacement path.
Although its final screenshot proved that one final frame was rendered rather
than wholly black, it did not compare the whole client frame against a desktop
truth image capable of detecting persistent black subregions.

### Reconciliation of the visible outcomes

One state sequence explains the black outcomes without treating every outcome
as already proven to share one cause:

1. A capability re-advertisement on a live direct-Xorg session sends
   ResetGraphics and creates a blank replacement surface. The callback's
   WM-screen invalidation is intentionally a no-op for this backend, so the
   surface remains black except where later ordinary damage happens to paint
   it. This covers a wholly black reconnect and persistent black regions.
2. A later dynamic resize deletes the encoder still reachable through
   `mm->encoder`, resets and recreates the surface, resizes Xorg, creates a
   successor encoder and explicitly sends Xorg a full-screen invalidate. If
   that sequence completes coherently, its full producer frame repairs the
   black display. This covers the observed resize-to-normal outcome.
3. The earlier capability callback has nevertheless orphaned the encoder it
   overwrote. Its worker, children, queues, captures and acknowledgement state
   were not retired by deleting the replacement during resize. A client close
   after a later reset is consistent with that invalid lifecycle, and the
   repeated ordering makes it the leading cause of the teardown branch, but
   the surviving server log still records only the client's TLS EOF. It does
   not independently prove which stale object or protocol transition made the
   Windows client close.

The analysis rejects an `auto`-mode explanation and explains black *after* the
second callback, but it no longer claims to explain why development 40060
missed the defect. Development did not merely miss a test condition; it did
not produce the same client-visible transition under a stronger interactive
resize sequence. The development/clean-room divergence must be isolated and
made reproducible on canonical development before a callback repair can count
as causal. If teardown persists after both the precursor and callback
transition are corrected, it remains a separate defect rather than being
hidden under this diagnosis.

### 2026-08-27 instrumentation-first correction

The owner rejected proceeding from source reachability to a behavior change.
That is the controlling decision. The existing record shows a defect and a
correlation, but the teardown remains a heisenbug until the failing clean-room
sequence itself records the complete lifecycle. Treating the orphaned encoder
as causal before that capture would risk chasing a red herring.

The next arm is therefore not a fix. It is a temporary branch from the exact
clean-room candidate, paired with the unchanged clean-room xorgxrdp commit and
the same `auto` configuration on port 40061. It preserves the current behavior
while recording capability generations, encoder identity and retirement,
surface/reset state, module attachment, the first producer update after each
reset, resize transitions and terminal transport ordering. Human-rate events
belong in the normal log. Per-frame damage, acknowledgement and send ordering
use the existing compile-time-opt-in `common/perf_trace` ring only.

No correction may enter canonical development or the clean-room slice history
until this instrumented arm reproduces the observation and its trace either
connects or separates the black-surface and teardown mechanisms.

### Port-40061 diagnostic deployment

The temporary xrdp branch is `diag/142d_caps_lifecycle`, based directly on
clean-room head `f8d8d06d2ffd`. Its committed diagnostic head is
`6e22c7fdac7f`. The paired xorgxrdp remains the unchanged clean-room
`aca3c774cb8b`; no xup contract or producer behavior changed.

The installed packages are:

* `xrdp-dev 0.10.80+git20260827233349.6e22c7fdac7f`;
* `xorgxrdp-dev 1:0.10.80+git20260826181120.aca3c774cb8b`.

The xrdp package SHA-256 is
`5c25d380e28f680beeb2ec51bab74987b8974da72439efb4905c5dac669d8834`.
The deployed image ID is
`453ee4addabd16d26bcbaea33d8f1a9b0bac58d50ce54d895eb31cef49c166f8`.
The mounted `gfx.toml` and source x042 `auto` profile both hash to
`216aed1fd3b78c26ee3aaa1d922ee53e970f5cece8f8ccb17e5e8a0cff9f3971`.

The arm enables `XRDP_GFX_TRACE=1`, `XRDP_ACK_TRACE=1` and the compile-time
perf sink at `/var/log/xrdp-perf/xrdp`. That directory is backed by the k3s
host path `/var/lib/xrdp-matrix/x045-lifecycle-trace`, so a pod restart does
not erase the capture. The ordinary log records only lifecycle-rate events.

The mandatory post-deploy smoke passed at both 1920 by 1080 and 1024 by 768.
Each size rendered all eight requested key changes with zero lag, edge fidelity
1.000 and zero encoder restart or sequence errors. The ring produced nonempty
records for both runs, the lifecycle log paired each created encoder with one
completed deletion, and the smoke left no tester Xorg session. This proves the
arm is operable and the instruments are active; it does not reproduce or close
the Windows bug.

### Required proof before correction

The next cheap gate must count encoder ownership at capability replacement,
assert complete teardown before a successor becomes visible, and assert one
full producer invalidation only after that successor is ready. The rendered
gate must independently compare the entire reset/resize client frame, so
"connection stayed alive" cannot pass with black regions. The black-region
cause is established by the reachable reset-without-repaint path. The same
Windows-like sequence must cover both a fresh-session control and reconnection
to an existing desktop, and must be exercised with `auto` and forced-444
configuration so mode selection cannot hide the lifecycle result. It must
still determine whether repairing that lifecycle also prevents the later EOF;
until that A/B, the client teardown is a consequence with a strongly
implicated server cause, not a separately proven protocol verdict.

## 2026-08-28 instrumented Windows reproduction

The owner reproduced the fault on x045 without a pod restart or a server-side
GUI process intervention. The closed connection's complete xrdp, xorgxrdp,
sesman and perf-trace records are retained under
[`PR-demo/mac_bisect_matrix/captures/i142d_x045_windows_repro_20260828T002619Z/`](../../PR-demo/mac_bisect_matrix/captures/i142d_x045_windows_repro_20260828T002619Z/README.md).
The tester Xorg and desktop remained alive after xrdp observed the peer EOF.

The lifecycle instrument converted both source-reachable defects into observed
facts. The second capability callback begins with live encoder
`0x5a9396525ff0`, a loaded direct-Xorg module and an established GFX channel.
It resets the graphics state with repaint route `direct-xorg-none`, then
installs encoder `0x5a93964a2c60` without a delete record for the former
object. Across the reproduced connection there are ten encoder installations
and nine paired delete-begin/delete-complete sequences. The sole unmatched
installation is the encoder overwritten by the second capability callback.

The missing repaint is independently visible in state, not inferred from the
absence of a log line. The generation-2 replacement is installed at
00:26:33.563 UTC with a producer update pending. When the owner initiates the
next resize 6.6 seconds later, that same encoder still reports
server/client/consumed counters 0/0/0 and the pending flag is still set. The
capability reset therefore delivered no Xorg frame at all. This closes the
persistent-black cause: a client surface reset on a live direct producer must
be followed by a current producer repaint, and this path has none.

The following resize then behaves coherently through the server. It deletes
the reachable zero-frame replacement, resets to 1804 by 1171, creates the
successor, requests Xorg's full repaint and receives the matching full-screen
two-slot capture. The perf trace records successful transmission of logical
frames 1 and 2. There is no client acknowledgement for either. The peer's TLS
EOF follows the last successful send; xrdp's cleanup and send errors follow
the EOF. No server fatal decision, graphics send error, trace drop or format
failure precedes it.

This evidence narrows rather than inflates the teardown verdict. The old
encoder is certainly orphaned, but the trace contains no stale PDU emitted by
that object after replacement. On Linux each encoder's wait objects are
independent anonymous pipes; the repeated diagnostic names do not alias one
encoder's wakeups to another. The orphan-alone explanation is therefore not
proven and must not be quoted as the Windows close cause. What is proven is
that clean-room receives another v10.7 capability advertisement after its
first unacknowledged post-resize frame, and then handles that advertisement as
non-transactional initial setup: it loses one encoder owner and publishes a
blank surface without a producer frame. Whether the advertisement is a normal
client recovery action or a reaction to an invalid preceding clean-room
transmission is still open; calling it independently "legal" overstates the
evidence.

The next causal gate must first reconcile the precursor on canonical
development. Compare the exact client-visible resize transition and identify
which clean-room-only behavior makes the same Windows client withhold its
frame acknowledgement and re-advertise capabilities. Development must
reproduce that red transition after the normative behavior is reconciled;
only then can a development correction prove complete retirement before state
replacement, one successor, and one current direct-producer repaint after the
successor is ready. The same Windows sequence must establish whether the peer
EOF disappears. If it does not, teardown remains a second defect; the
already-proven black and ownership defects are not permission to hide it.
