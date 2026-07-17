# Tail-frame withholding: controlled A/B (lag vs. spammer vs. async_depth)

End-to-end, on-box measurement of the interactive "last update withheld until the
next input" defect, through the real xrdp → FreeRDP GFX AVC444 path.

## Method

A fullscreen qterminal in the persistent `tester` session (`:10`) paints four
full-screen colours 0.4s apart, ending on **RED** as the final single damage
event, then goes idle (no prompt after). A headless `xfreerdp3` (on Xvfb `:99`)
decodes the H.264 GFX stream; we grab its framebuffer after a 5s idle settle and
classify the screen centre. Because a withheld tail frame is held until the
*next* damage, the long settle cannot drain it — so:

* screen **RED**   → the final frame was **delivered**.
* screen **not-RED** (green/cyan) → the final frame is **withheld** (the client
  is showing an earlier colour).

`gfx.toml` is re-read per connection (`xrdp_wm.c`), so each group only reconnects
a fresh client — no `xrdp` restart, session persists. 5 trials per group.
Encoder is the live one: `h264_vaapi … -bf 0 -async_depth N`.

Harness: `ab_harness.sh` (+ `setcfg.py`, `fill.sh`, `classify.py`). The
encoder-isolation probe that established pipeline depth is
`ffmpeg_pipeline_depth_probe.py`.

## Results

| Group | `async_depth` | `tail_flush` | Trials | Verdict |
|-------|:---:|:---:|:---:|---------|
| **LAG**       | 2 | off | 5/5 **WITHHELD** (green) | tail frame never arrives while idle |
| **SPAMMER**   | 2 | **on** | 5/5 **DELIVERED** (red) | 33ms same-frame drain pushes it out |
| **ASYNC-FIX** | 1 | off | 5/5 **DELIVERED** (red) | shallow pipeline emits every frame |

Screenshots (client-side, decoded): `results/lag_withheld_green.png`,
`results/spammer_delivered_red.png`, `results/asyncdepth1_delivered_red.png`.

Isolated-encoder corroboration (`ffmpeg_pipeline_depth_probe.py`, feeding frames
with stdin held open and counting emitted vs. withheld):

| encoder / config | frames withheld while idle |
|---|---|
| `h264_vaapi -async_depth 1` (single & main+aux pairs) | **0** |
| `h264_vaapi -async_depth 2` | **+1** (one-behind) |
| `h264_vaapi -async_depth 4` | **+3** |
| `libx264 -tune zerolatency` / `-threads 1` | **0** |
| `libx264` default (frame-threading) | all, until EOF |

## Conclusions

1. The withhold is **real end-to-end** and is a property of the encoder's
   **pipeline depth**, not the pipe or the fftools scheduler: withheld frames =
   `async_depth − 1` (VAAPI) or the frame-thread window (x264), **as seen by an
   xfreerdp client**. `libx264 -tune zerolatency`/`-threads 1` drives that to
   zero.
   **RESOLVED — the field bug was runner content/region desync, not encoder
   withholding.** A live mstsc repro on the same GPU (stuck full-screen frame;
   hovering a tooltip revealed the newer colour only inside the tooltip rect)
   plus the `GFX_TRACE` forensic chain showed the pipelined runner permanently
   one-behind (`returned_seq = submitted_seq − 1` on every frame after a slow
   first frame): frame N−1's pixels were emitted under frame N's damage
   region. mstsc blits region rects strictly → stale screen; FreeRDP presents
   the whole decoded surface → all xfreerdp A/Bs read "delivered" (their false
   negative). Fix: synchronous encode (bounded wait for the submitted picture
   + `desktop_sequence` verification) in `xrdp_encoder_ffmpeg.c`. Verified with
   the keystroke-driven `colorkey.sh` harness: correct colour on every
   keypress, `submitted_seq == returned_seq`, `inflight=0`, no timeouts.
   **Method notes:** config binds at session **login**, not TCP reconnect; and
   a client-side screenshot of a lenient client (xfreerdp) cannot detect
   region-desync — use the seq trace.
2. The **`tail_flush` spammer** independently eliminates the withhold (5/5),
   confirming it as a valid **last-resort** for encoders whose depth cannot be
   lowered. It is **off by default** and opt-in via
   `[avc444_ffmpeg] tail_flush = true`.
3. Side note: `async_depth = 4` reliably **fails to connect** — the connect-time
   AVC444 encoder probe cannot drain that deep a pipeline within its timeout.
   Another reason to keep the pipeline shallow.

Prior claim "stock ffmpeg withholds and only a flushed frame can drain it" is
**not supported**: with the shipped low-latency args ffmpeg withholds nothing.
