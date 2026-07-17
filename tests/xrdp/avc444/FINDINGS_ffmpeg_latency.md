# AVC444 ffmpeg persistent-pipe latency — root cause

Reproduce with `./repro_ffmpeg_latency.py` (needs a real ffmpeg + libx264).
This documents *why* the AVC444 runner is pipelined and what actually causes
the "one-frame lag", superseding the memory-level note in `BACKLOG.md`.

> **Status update (2026-07-16).** The finding below (withholding is *encoder-side
> output delay*, cured by a shallow/low-latency pipeline) is confirmed and is now
> the shipped behaviour: the default `encoder_args` **include `-tune
> zerolatency`**, so the historical "ships without zerolatency" line in the
> Consequence section is stale — see the current `gfx.toml`. The same mechanism
> generalises beyond libx264 to hardware encoders: `h264_vaapi` withholds
> `async_depth − 1` frames (measured with the isolated depth probe and an
> xfreerdp end-to-end A/B). **Resolution (2026-07-17):** a live mstsc repro on
> the same GPU exposed the real field bug — not encoder withholding but a
> **runner content/region desync**: the pipelined runner returned the *oldest*
> completed picture while the metablock carried the *current* damage region, so
> after any slow first frame it ran permanently one-behind
> (`returned_seq = submitted_seq − 1` in the trace) and region-strict clients
> (mstsc) displayed stale full-screen content forever. Fixed by making
> `encode_pair()/encode_single()` **synchronous** (bounded wait for the
> submitted picture + sequence verification). This *relies on* the shallow
> pipeline documented below — a deep pipeline now errors loudly instead of
> desyncing. See PRD §25 and `PR-demo/tail_flush_ab/`.

## Symptom

xrdp feeds the ffmpeg child one raw NV12 picture at a time over a persistent
pipe and reads NUT/H.264 back, never closing the input mid-session. Stock
ffmpeg emits **no encoded picture** until the input hits EOF: everything is
withheld, so a synchronous "write a pair, read a pair" transaction never
completes for a live desktop.

## Measurements (320x240 NV12, 12 frames, one at a time)

`per-frame` = output bytes seen while that input frame's window is open;
`tail@EOF` = bytes that only appeared after closing the input.

| config | per-frame | tail@EOF | verdict |
|---|---|---|---|
| production (no tune) | `[304,0,0,0,0,0,0,0,0,0,0,0]` | 5092 | all pictures withheld until EOF |
| `-threads 1` only | `[304,0,0,0,0,0,0,0,0,0,0,0]` | 5092 | threading is **not** the sole cause |
| `bframes=0:rc-lookahead=0:sync-lookahead=0` (default threads) | `[287,0,0,0,0,0,0,3403,147,106,90,76]` | 654 | first packet at frame **8** — threaded frame delay |
| same + `-threads 1` | `[287,3403,147,106,90,76,...]` | 102 | per-frame from frame 2 — essentially synchronous |
| `-tune zerolatency` | `[3967,337,193,187,150,...]` | 26 | **per-frame from frame 1** |
| `-tune zerolatency -threads 2` | `[3838,243,151,138,...]` | 26 | still per-frame (sliced-threads) |

The 304-byte "frame 1" output in the withheld cases is only the NUT
header/stream metadata, not an encoded picture.

## Root cause

The withholding is **entirely x264 encoder-side output delay**, not ffmpeg
input/AVIO/demux/probe buffering. It has two independent, additive parts:

1. **Lookahead / frame reordering.** x264's `rc-lookahead` (default ~40),
   `sync-lookahead`, and B-frame reordering buffer a window of frames before
   emitting the first. This is the dominant "several frames before first
   output" term. Confirmed: `-threads 1` alone changes nothing (still fully
   withheld); only disabling lookahead moves the first packet earlier.

2. **Threaded frame-parallelism.** x264's default threading is frame-based
   (threads ≈ 1.5×cores) and keeps ~(threads−1) frames in flight, adding that
   many frames of output delay. Confirmed: with lookahead off but default
   threads, the first real packet lands at frame 8 (~7 frame threads); adding
   `-threads 1` drops it to frame 2.

`-tune zerolatency` eliminates both at once — it sets `bframes=0`,
`rc-lookahead=0`, `sync-lookahead=0`, `sliced-threads=1` (intra-frame
parallelism, so multithreading adds no frame delay), and `force-cfr` — giving
true one-in/one-out streaming (`tail@EOF` 5092 → 26 bytes).

## Why the earlier "low-latency flags break it" note was about the wrong side

Those flags were **input-side**: `-fflags nobuffer` breaks the encode path
("No filtered frames for output stream") and `-probesize 32` / `-avioflags
direct` corrupt/mis-size the rawvideo demuxer ("Invalid buffer size"). None of
them touch the encoder, which is where the delay actually is. The repro
reproduces those failures too (`fflags_nobuffer`, `probesize_32`,
`avioflags_direct` rows) so the distinction is on the record.

## Source-level confirmation (ffmpeg 8.0.git 597036b; identical paths in 7.x)

A read of the ffmpeg source corroborates the measurements and pins the exact
mechanism:

- **Input side costs ~1 frame, not several.** The rawvideo demuxer delivers
  exactly one frame per packet (`libavformat/rawvideodec.c:158`,
  `av_get_packet` → direct read for frame-sized requests
  `libavformat/aviobuf.c:623`). `avformat_find_stream_info()` consumes a single
  frame for rawvideo (width/pix_fmt come from the CLI so `has_codec_parameters`
  is immediately true, `tb_unreliable`==0 for RAWVIDEO → no fps-probe loop;
  `libavformat/demux.c`). The fftools thread queues (`ffmpeg_sched.c:810`,
  `thread_queue.c:156`) are capacity ceilings, not prefill thresholds — the
  first packet passes straight through. Isolation test: rawvideo demux →
  `-c:v copy` → nut (encoder removed) emits output at **input frame #1** under
  every input-flag combination, so the ~3-frame stall is not input-side.

- **The stall is libx264 output delay.** ffmpeg's default `-preset medium`
  (`libavcodec/libx264.c:1496`) leaves x264 with `i_bframe=3`, `rc_lookahead=40`
  and threaded lookahead. During startup `x264_encoder_encode()` returns
  `nnal==0` while it fills the reorder/lookahead buffer, so `encode_nals()`
  returns 0 with no packet (`libx264.c:160`, `:609-661`) and fftools swallows
  the `EAGAIN` (`fftools/ffmpeg_enc.c:666`) — nothing reaches the muxer. The
  tail only drains on EOF via the `x264_encoder_delayed_frames()` loop
  (`libx264.c:658`). `-tune zerolatency` calls
  `x264_param_default_preset(...,"zerolatency")` (`libx264.c:1083`) which sets
  `bframes=0`, `rc_lookahead=0`, `sync-lookahead=0`, `sliced-threads=1` → one
  NAL per input frame, zero delay.

- **Why the input-side knobs fail.** `-flush_packets 1` is purely output-side
  (`libavformat/mux.c:429`, flushes the output AVIO after each packet — needed
  so produced packets reach stdout, but it does nothing for the encoder stall).
  `-fflags nobuffer` makes `find_stream_info` discard the probe packet instead
  of buffering it for replay (`demux.c` nobuffer discard vs `packet_buffer`
  replay at `:1597`), dropping the first rawvideo frame → the filtergraph hits
  EOF with `got_frame==0` → "No filtered frames for output stream"
  (`fftools/ffmpeg_filter.c:2657/2689`). `-flags low_delay` is a **no-op** for
  libx264 (the wrapper never reads `AV_CODEC_FLAG_LOW_DELAY`).

- **`-probesize 32` caveat (honesty note).** The subagent could not reproduce
  actual pixel corruption on the 8.0 build — `-probesize 32` there only
  degraded fps/duration estimation ("not enough frames to estimate rate",
  `demux.c`). The "corrupts the demuxer" symptom is likely 7.x-specific or tied
  to the full decode/duration path, not a universal framing bug. Either way it
  is not a latency fix and should be avoided; if shrinking probesize, use one
  frame's byte count, never 32.

## Consequence / recommendation

The runner is pipelined (`encode_pair()` returns the oldest completed pair) so it
is correct at any pipeline depth. **The shipped `encoder_args` now include
`-tune zerolatency`** (software) and `-async_depth 1` (the VAAPI example), so the
child streams one encoded picture per input frame and the pipelined runner
returns the just-submitted pair immediately — no display lag, and the tail-flush
never fires. Keeping the pipeline shallow via `encoder_args` is the root-cause
guarantee; it is also the correct tune for an interactive remote-desktop encoder
(the in-tree x264 GFX path likewise uses `tune = "zerolatency"`). This was
validated on-screen with the harness in `PR-demo/tail_flush_ab/` (5/5 delivered
at `async_depth 1`; 5/5 withheld at `async_depth 2` with the flush off).
