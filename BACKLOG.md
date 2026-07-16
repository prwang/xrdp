# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

Only **upcoming** work lives here. Completed work is recorded in `PRD.md` §25
("Delivered"), with detailed root-cause writeups under `tests/xrdp/avc444/`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

## AVC444/AVC420 tail-frame withholding — interactive input lag — IN PROGRESS (2026-07-16)

**Symptom (field report).** On a real deployment, qterminal did not show the
last typed character and felt laggy. Three tells: (1) running `glxgears` in
another window made it disappear; (2) typing a 3rd char revealed the (hidden)
2nd; (3) hovering an unrelated tooltip revealed the hidden char. Classic
"the last update is withheld until the next update pushes it out."

**Root cause (traced statically + measured).** The external stock-`ffmpeg`
child runs a threaded transcode over pipes with a **~2–3 frame pipeline depth**:
it emits one output per input but delayed by that depth, and **withholds the
tail frame(s) until more input arrives or EOF**. Measured on-box (feed one NV12
frame, read stdout): frame 0 not emitted within 1 s; steady state ~1.7 ms/frame
once primed; a lone final frame never emerges until the next input/close. Ruled
out as the cause: our NUT demux (raw `-f h264` behaves identically), the ffmpeg
output AVIO buffer (`-avioflags direct` — no change), and libx264 frame
threading (`-threads 1` — no change). So the hold is inside ffmpeg's
fftools transcode pipeline, upstream of our code. The linked x264/OpenH264 path
does NOT have this: `x264_encoder_encode()` is a synchronous in-process call
(threads=1) returning this frame's bitstream immediately — pipeline depth 0.

Our runner compounds it: `encode_pair`/`encode_single` return the oldest
completed frame after a brief pump, and **nothing flushes the tail on idle**
(`flush_next` has no live caller; the worker `proc_enc_msg` blocks on
`g_obj_wait(..., timeout=-1)` until the next damage). So the final update of any
idle-bounded burst sits in the ffmpeg pipeline indefinitely.

**Why not "just make it synchronous".** ffmpeg won't emit frame N with no
further input (verified), so a runner that blocks waiting for frame N would hang
until the timeout. The depth is inherent to driving a persistent stock ffmpeg
over a pipe; only the linked encoder gives depth 0.

**Fix — short-timer tail-flush (the "33 ms same-frame drain").** After a real
frame is emitted (pipeline now holds a tail frame), arm a ~33 ms timeout in the
encoder worker loop. If no new damage arrives, re-feed the **same** retained
NV12 (`conv->main_nv12`/`aux_nv12`) once — a duplicate encodes to a near-empty
skip/P-frame — which pushes the withheld real frame out; wrap and send it. Feed
at most one duplicate per idle burst (do not re-arm after a flush, since the new
held frame is content-identical to what was just sent), so idle does not become
a perpetual dup stream. Tail latency becomes ~one frame (~16–33 ms), not
indefinite; nothing changes during continuous input. 33 ms ≈ one frame at the
30 fps interactive floor; make it a small constant (tunable).

**Scope.**
- `xrdp_encoder_ffmpeg.{c,h}`: a `flush_pending`-style entry that re-feeds the
  last frame and returns the freed tail frame (or reuse `encode_*` with the
  retained buffers).
- `xrdp_encoder.c`: per-surface retained emit context (surface_id, dst_rect,
  pixel_format, mon_index) + a flush emit path mirroring `gfx_wiretosurface1_*`;
  arm/clear a `flush_deadline` and pass a finite `timeout` to `g_obj_wait` in
  `proc_enc_msg`.
- No wire/format change; AVC420 and AVC444 share the mechanism.

**Acceptance.** Sparse keystrokes appear within ~one frame of typing (no
withheld last char) with no client-side action; continuous content unchanged; no
extra encode while fully idle beyond the single catch-up frame; unit/live check.

**Open input (research in flight).** A subagent is statically analyzing the
FFmpeg `fftools` scheduler (`ffmpeg_sched.c`) to name the exact queue(s) behind
the ~2–3 frame depth and confirm no flag drives it to 0; fold its findings in.

## H.265 / HEVC via ffmpeg — OUT OF SCOPE / BLOCKED (2026-07-14)

**Decision: not pursued.** The encode side is nearly free (`-c:v libx265` /
`hevc_nvenc` in `encoder_args`; the converter and NUT demux are codec-agnostic;
only a new HEVC Annex-B validator — 2-byte NAL header, VPS/SPS/PPS 32/33/34,
IDR 19/20 — would be genuinely new). **The wire is the wall.** Research against
the current MS-RDPEGFX spec (rev 19.0, 2026-05-11): **no HEVC codecId, no HEVC
caps flag** — the public codecId enum ends at AVC444V2 `0x000F`, all H.264. The
AVD *feature* is documented (a dedicated **"Configure H.265/HEVC hardware
encoding"** GPO, but in the **AVD** admin template `terminalserver-avd.admx`,
not in-box RDS; client shows "Codecs Used: HEVC", event 162 "HevcProfile"), yet
the *protocol carriage* (codecId, enabling capset, container framing) is
undocumented. The FreeRDP-labelled "Azure undocumented" capsets `0x000B0101/
0200/0300` are pinned by the spec as behaviorally == 10.7 with no HEVC
semantics. FreeRDP has an experimental AV1 custom codec but **no HEVC decoder at
all**. So a server emitter would need packet-capture reverse-engineering of a
live AVD↔Windows App session (three unknown values) and would only reach recent
Microsoft clients with the HEVC Video Extension + capable GPU. Revisit only if
those values get documented or captured; the spec-grounded, interoperable
ceiling for xrdp remains AVC444/AVC444v2 (shipped).
