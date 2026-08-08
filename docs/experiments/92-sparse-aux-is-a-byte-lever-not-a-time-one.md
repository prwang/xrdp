# #92 — dropping the chroma view in motion is a BYTE lever, not a time one

*2026-08-08. BACKLOG #92, PRD FR-H264-9. Implementation `7b550f6ae87f`;
measurement `PR-demo/mac_bisect_matrix/captures/i92_sparse_aux_ab_20260808_211023_s20`.*

## What was built

The AVC444 aux view carries the chroma detail. It is 44.8 % of the bytes
and costs a second full-frame pack and a second encode. Chroma detail is
least perceptible while the screen is moving, which is also when the
frame period is longest — so this feature sends the aux view only when
the screen settles, and guarantees it at least every
`chroma_refresh_ms` whatever the screen is doing.

The decision takes two configured times and three timestamps and **never
sees a pixel**. That is a requirement, not an implementation note: a
reviewer settles "is the server inspecting the user's screen?" by reading
the signature of `xrdp_gfx_chroma_due()`.

When chroma is not due, the aux child is not fed, not armed in the
encoder's poll set, not waited for and not popped, and the frame ships as
an LC=1 luma PDU with no LC=2 behind it — which is what the AVC444 `LC`
field exists for. The aux long-term reference LT1 keeps the last chroma
picture across any number of luma-only frames, so the next aux P still
predicts from it and nothing needs re-seeding.

Because the two children then see different numbers of pictures, each
keeps its own intra refresh interval counted in its own pictures:
`intra_refresh_frames` and `intra_refresh_frames_aux`, two plain
integers, no time anywhere (owner directive). The same commit moved the
main default 240 → 250 to match libx264's `i_keyint_max`, which
`xrdp_encoder_x264.c` has never overridden.

## What it bought, measured

One arm (x030), two configurations of one gfx.toml, interleaved
off/on/off/on in one sitting. One monitor at 3840×2400, textflood, oracle
client, VAAPI 444 CQP 20. `chroma_refresh_ms = 1000`,
`chroma_idle_ms = 100` — the owner's values.

| | control: chroma every frame | treatment: chroma when settled | ratio |
|---|---|---|---|
| frame interval | 24.086 ms (41.5 fps) | 24.485 ms (40.8 fps) | 0.984× |
| wait for the ffmpeg children | 22.826 ms | 23.853 ms | 0.957× |
| **bytes per frame on the wire** | **3.468 MB** | **1.948 MB** | **−43.8 %** |

**43.8 % of the bytes, and zero milliseconds.** The frame interval did
not move; the difference between the conditions (0.4 ms) is smaller than
the difference between the two control legs (0.84 ms).

The byte figure closes against the prediction: the aux view is 44.8 % of
the bytes, 2.6 % of frames still carried it, and
44.8 % × (1 − 0.026) = 43.6 % against 43.8 % measured.

## Why the time did not move — the chroma encode was never on the critical path

A child cannot begin encoding until its whole raw picture has arrived, so
its encode window is `[feedend, outfirst]`. Windows paired by child
identity and sequence number:

| leg | luma encode | chroma encode | overlap | luma runs on after chroma ends |
|---|---|---|---|---|
| a1 (control) | 13.90 ms | 11.96 ms | 11.87 ms (99.2 %) | 1.12 ms |
| a2 (control) | 14.08 ms | 12.08 ms | 11.85 ms (98.1 %) | 0.88 ms |
| b1 (treatment) | 13.80 ms | 12.13 ms | 11.65 ms (96.1 %) | 0.60 ms |
| b2 (treatment) | 13.95 ms | 12.51 ms | 11.68 ms (93.3 %) | 0.74 ms |

The chroma encode runs almost entirely inside the luma encode. The pump
waits for the *later* of the two, not for their sum, so the whole
opportunity available to a change that deletes the chroma encode is the
0.6–1.1 ms by which the luma encode outlasts it. It took off nothing
measurable, which is what those numbers predict.

The treatment legs are the control on that claim: the 18 frames that
*did* carry chroma there show the same 93–96 % overlap, so the mechanism
is unchanged and it is the opportunity that was never there.

This is the same finding as #91's, arrived at from the other side. #91
established that our code does not serialise two screens' encodes; this
establishes that it does not serialise one screen's two views either. The
poll set is doing its job, and a consequence of it doing its job is that
removing one of two concurrent encodes buys no time.

## Would a deeper pipeline help? No — there is no idle to fill

Asked by the owner on 2026-08-08, once the null was in: this is an
encoder-latency-dominated pipeline, so would letting xrdp run further
ahead (more frames in flight) keep the encoder busier and raise the rate?

Answered from the archived rings, with no new run. The four segments
below are built to **sum** to the cycle, and the residual is printed, so
a decomposition that does not close cannot be read as one.

| leg | feed | encode | drain | between | sum | cycle | residual |
|---|---|---|---|---|---|---|---|
| a1 control | 7.497 | 13.898 | 1.030 | 1.398 | 23.823 | 23.820 | −0.003 |
| a2 control | 7.987 | 14.081 | 1.160 | 1.391 | 24.618 | 24.619 | +0.001 |
| b1 treatment | 9.054 | 13.803 | 1.172 | 0.762 | 24.791 | 24.790 | −0.001 |
| b2 treatment | 8.515 | 13.950 | 1.211 | 0.783 | 24.459 | 24.462 | +0.003 |

* **feed** — the raw picture entering the child's 1 MiB pipe. One picture
  at 3840×2400 is 13.82 MB, or 13.2 pipefuls. xrdp's side is `vmsplice`,
  which moves page references and is near-free, so this elapsed time is
  the **child's `read()` copying the frame in**, at 1.5–1.8 GB/s.
* **encode** — the child holds a whole picture and emits its first
  encoded byte; on this VAAPI arm that includes the upload to the GPU.
* **drain** — xrdp reading the rest of the encoded frame.
* **between** — collect, the reference rewrite, emit, slot release.

**The direct answer: no. The encoder is not waiting, and it cannot accept
faster.** Two independent readings say so.

1. **The worker never has nothing to encode.** Across ~690 cycles per
   leg, 1 to 3 waits exceed a millisecond and every one of those but the
   session-start wait is under two. The median wait is **1.2
   microseconds**. The fifo always holds a frame, so the producer is not
   the limit and xrdp is not idling for want of work.
2. **The cycle is fully accounted for by work.** Feed + encode + drain +
   between closes to within 0.003 ms on all four legs. There is no
   unexplained gap for a deeper pipeline to occupy. The child is busy
   90–92 % of the cycle; the rest is xrdp's own drain and rewrite.

### Which flow-control term actually holds the producer back

The producer captures only when xrdp grants it a frame id, and that
credit is `min(frame_id_consumed, frame_id_server + 1,
frame_id_client + C)` — the children have absorbed it / one frame of
pipeline inventory / the end-to-end wire window. Which of the three is
the minimum answers "would turning a flow-control knob raise the rate?"
without turning one. Read off the `ackslot` record, which carries all
three plus C. Ties are counted **as ties**, because if two terms are
equal, relaxing either one alone moves nothing.

| leg | C | binding term(s) |
|---|---|---|
| a1 control | 2 | absorbed **and** inventory, tied, on 706 of 711 acks; all three tied on the other 5 |
| a2 control | 2 | tied on 682 of 688; all three on 6 |
| b1 treatment | 2 | tied on 681 of 682; all three on 1 |
| b2 treatment | 2 | tied on 691 of 692; all three on 1 |

`frame_id_consumed − (frame_id_server + 1)` is **0 on every single ack**
in every leg. The two terms are not merely often equal; they are always
equal, which follows from the pipeline being strictly one frame at a
time — the children absorb frame N, it is encoded, it egresses, and only
then is N+1 admitted.

**The wire window is never the sole binder and has a frame of headroom it
never uses** (`(client + C) − (server + 1)` is 1 on 99.3–99.9 % of acks).
So raising `wire_window` on this link cannot move anything; C = 2 is
already one more than the pipeline can use. That is consistent with #80's
finding that on loopback the client is not the limit, arrived at
independently.

**And the capture already overlaps the encode.** The worker's median wait
for something to encode is 1.2 microseconds, so by the time a cycle ends
the next frame is already in the fifo. The producer is not on the
critical path; it is waiting for the credit, and the credit is waiting
for the child.

Which leaves exactly one place the time is spent, and it is inside one
process: read 8.8 ms, then encode 13.9 ms, on one thread.

### ADDENDUM 2026-08-08 — most of the 8-9 ms feed is a container artefact, not xrdp

The owner's follow-up: 2 GB/s is slow for a copy; can the handover be
reproduced independently, outside xrdp and xorgxrdp? It can, and the
answer changes what the feed segment means.

`tools/vmsplice_pipe_bench.c` reproduces just the handover — a parent
that `vmsplice`s a buffer into a pipe exactly as `feed_vmsplice()` does
and a forked child that reads and discards it. Run **inside arm x030**,
same pod, same binary, same 13.82 MB picture, differing only in uid:

| runs as | maps to host uid | pipe granted | handover |
|---|---|---|---|
| container root — what xrdp runs as | **1000** | **8 KiB**, resize REFUSED | **5.84 ms** |
| `tester` | 1001000 | 1 MiB, granted | **0.68 ms** |

memcpy of the same bytes: **0.29 ms**. So the pipe mechanism with a
normal pipe costs 2.3× a copy and is **not** a bottleneck; clamped to
8 KiB it costs 20× and a picture crosses in 1688 round trips instead of
14.

**The cause is this box's containerisation, not the code.** It is an
Incus unprivileged container whose `uid_map` is `0 1000 1` — container
root is host uid 1000, an ordinary host account — and the pods inherit
it. `fs/pipe-user-pages-soft` is 64 MiB of pipe buffers per host user,
host-wide; over it, pipes are clamped to the kernel minimum and
`F_SETPIPE_SZ` is refused unless the caller is `capable(CAP_SYS_RESOURCE)`
**in the initial** user namespace, which container root never is. A
bare-metal xrdp running as real root is exempt from the rule entirely.

**So the feed segment above is inflated.** The 7.5–9.1 ms measured
in situ is consistent with the clamped mechanism (5.8–7.1 ms) plus
ffmpeg's own work; on a normally privileged host it would be near
0.7 ms. **The A/B in this record is unaffected** — both conditions shared
the handicap — but the absolute period of 24.5 ms contains roughly 5 ms
that a normal deployment would not pay, which would put it near 19 ms.
Projected, not measured: proving it needs the host sysctl raised, which
is outside this container.

**And it changes the ranking of the levers below.** "Remove the copy" was
listed as the big one on the strength of an 8–9 ms feed. At 0.68 ms with
a working pipe, the copy is 2.8 % of a frame and the case for
re-architecting the handover is much weaker than these numbers first
suggested. Filed as BACKLOG #103; evidence in
`PR-demo/mac_bisect_matrix/captures/i103_pipe_handover_20260808/`.

**What the prize would be, if the serialisation could be broken.**
Arithmetic on the measured segments, not a measurement: with the feed of
the next picture running entirely concurrently with the encode of this
one, the cycle floor is `max(feed, encode) + drain + between` =
**15.7–16.6 ms** against the measured 23.8–24.8 — about 1.5×, and bounded
below by the encode alone at 13.9 ms.

**xrdp's frames-in-flight knob cannot collect it, and this is the load-
bearing point.** Submitting frame N+1 earlier does not make the child
*read* it earlier. The child is one ffmpeg process with `-async_depth 1
-bf 0` — deliberately, because a deeper encoder pipeline is what
`pair_timeout_ms` exists to catch — so it cannot read N+1 while encoding
N. A deeper xrdp-side pipeline can only pre-fill the pipe, which holds
1 MiB of a 13.82 MB picture: 7.6 % of the feed, ≈0.6 ms of a 24.5 ms
cycle, ≈2.5 %.

So the 8–9 ms feed is not latency to be hidden by running ahead. It is a
**copy**, performed by the same thread that then encodes. The levers that
could actually reach it are of a different kind, and each is its own
item, unmeasured here:

* a larger input pipe (`F_SETPIPE_SZ` is set to 1 MiB best-effort);
  bounded above by ~0.6 ms as computed, so small;
* removing the copy — handing the child shared memory instead of a pipe,
  which is a change to how the encoder is invoked, not a knob;
* letting the child overlap read and encode (`-async_depth` > 1, or an
  encoder thread), which trades the one-in/one-out cadence the runner and
  its timeout are built on.

One observation recorded without a story: the treatment legs' feed is
~1 ms *longer* than the control's (9.05/8.52 against 7.50/7.99) while
their "between" is ~0.6 ms shorter. A plausible mechanism is that with
two children the poll loop wakes more often and tops the main child's
pipe up more frequently. Not measured, not claimed.

## What this does NOT say, stated because the temptation is obvious

* **Nothing about a bandwidth-limited link, which is the case the feature
  was justified by.** This is loopback: bytes are nearly free, so a
  43.8 % byte saving cannot show up as rate here *by construction*.
  BACKLOG #98 measured that on a limited link `fps = link_rate /
  frame_bytes` holds within 3 % and the delay sits at the window bound.
  That predicts the saving converts to rate on a WAN. It does not show
  it, and this run is not evidence either way.
* **Nothing about fidelity.** The oracle client acknowledges before
  decoding and renders nothing.
* **Nothing comparable to the archived 18.5 ms** (x014, 2026-08-02, same
  geometry and payload). Different xrdp build, different day, and this
  host's own drift band that week spanned 17.6–27.4 ms on arms that were
  not changed between legs (`i87_eager_ab_20260806_180910_s20`). The
  same-arm control leg is the only baseline this run supports, which is
  why the experiment was built that way rather than as one arm against
  the archive.

## RED — the guarantee is exceeded by one frame, and CI could not have caught it

Chroma went missing for a maximum of **1022.0 ms** (leg b1) and
**1021.2 ms** (leg b2) against a **1000 ms** guarantee.

Sixteen of the seventeen gaps in each leg are the guarantee firing,
between 1000.0 and 1022.0 ms; the seventeenth is 130 ms (b1) / 136 ms
(b2), at the start of the leg before the payload had begun drawing
continuously, which is the settle rule working on a quiet screen. That
one value pulls the mean to 958 ms, so the mean describes nothing real
here and the max is the statistic the bound is about.

The cause is not a defect in the decision. The decision exists only *at*
a frame — there is no mechanism to send chroma between frames — so once
the bound expires the earliest chroma can go is the first frame at or
after it. At a 24.5 ms frame interval the achievable bound is
`chroma_refresh_ms + one frame interval` = 1024.5 ms, and 1022 is that.

**The test could not have found it, and that is the part worth keeping.**
`tests/xrdp/test_avc444_chroma_due.c` drives the decision with frames
exactly 20 ms apart and asserts a worst gap of exactly 1000 ms. Twenty
divides a thousand, so a frame lands exactly on the bound and the
overshoot is unreachable in that fixture. The assertion was derived from
the specification by hand and is not wrong; the *fixture* chose a frame
gap that hides the effect. A test can be correct in every assertion and
still be unable to fail.

Two ways out, and the choice is the owner's because it changes what the
feature promises:

1. **Correct the stated bound** to `chroma_refresh_ms` + one frame
   interval, and set the config value with that in mind. No code change.
   Honest, and it makes the guarantee's real shape visible to whoever
   configures it.
2. **Fire the guarantee one frame early** — send chroma when
   `now − last_chroma + (last inter-frame gap) ≥ chroma_refresh_ms`, so
   the bound holds as stated in steady state. It costs a prediction (the
   next frame arrives like the last one did), sends chroma slightly
   sooner than asked, and is a heuristic in a decision function whose
   selling point is that it has none.

Either way the fixture gains a case with a frame gap that does not divide
the bound, and that is a separate, announced change to the test.

### RESOLVED 2026-08-08 — option 1, the owner's ruling

**Accepted: state the achievable bound, add no heuristic.** The delivered
guarantee is now written as "the FIRST FRAME AT OR AFTER
`chroma_refresh_ms` carries chroma", and the bound an administrator gets
is `chroma_refresh_ms` + one frame interval — about 25 ms of headroom at
40 fps. The owner's note: a system administrator who wants a hard second
can ask for 975 ms. No code changed; PRD FR-H264-9, the `gfx.toml` key
comment and the decision function's own documentation were corrected.

The test gained `test_chroma_due_bound_is_refresh_plus_one_frame`,
announced as its own change. It drives six frame rates, none of which
divides 1000, and pins the bound in **both** regimes: while frames arrive
faster than the settle threshold the worst gap is `refresh` rounded up to
a multiple of the frame gap; once they arrive slower than it, every frame
is already a settle and the worst gap is simply the frame gap. The second
regime was missing from the first draft of that test and it went red at
a 300 ms frame gap — 1200 expected against 300 measured. The rule was
incomplete, not the code. Worth recording: a fixture with a single frame
rate could not have shown either the overshoot or this.

## Status

The server-side implementation is complete and green in CI (207/207,
including a live run through two real ffmpeg children). The arm is
certified and deployed. What remains is what needs a person or a slow
link: whether an alternating luma-only/full stream renders correctly on
the macOS and Windows clients, and whether the byte saving converts to
rate on a bandwidth-limited link.
