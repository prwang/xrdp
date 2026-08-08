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

## Status

The server-side implementation is complete and green in CI (207/207,
including a live run through two real ffmpeg children). The arm is
certified and deployed. What remains is what needs a person or a slow
link: whether an alternating luma-only/full stream renders correctly on
the macOS and Windows clients, and whether the byte saving converts to
rate on a bandwidth-limited link.
