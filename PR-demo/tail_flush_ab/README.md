# tail_flush_ab — interactive tail-withholding A/B

Box-specific reproduction harness for the AVC444 "last frame withheld on idle"
interactive-lag defect and its two fixes. See `RESULTS.md` for the measured
outcome and interpretation.

## What it shows

An encoder with a deep pipeline (`h264_vaapi -async_depth N>1`) holds the last
frame of an idle-bounded burst until the next input, so the final on-screen
update is not delivered while the user pauses. Two independent remedies:

* **root cause** — `-async_depth 1` (shallow pipeline; the shipped default), and
* **last resort** — `[avc444_ffmpeg] tail_flush = true` (a bounded 33ms
  same-frame drain, off by default).

## Config binds at fresh LOGIN — not reconnect

`[avc444_ffmpeg]` changes in `/etc/xrdp/gfx.toml` take effect at the next
**fresh login** (logoff → login), not on disconnect/reconnect to a running
session (on-box observation, 2026-07-17). Any test of a config change against
a reconnected session is invalid. `../smoke_gate/keytest.sh` and
`../smoke_gate/smoke.sh` (moved there — they are the general deploy gate,
not tail-flush-specific) enforce a cold
login every run; for manual mstsc tests, log the tester session off first —
see "Rule 0" in `MSTSC_TRACE.md`.

## Assumptions (this dev box)

* persistent `tester` xfce session on `:10`, `XAUTHORITY=/var/run/xrdp/1000/Xauthority`
* headless Xvfb `:99` to host the FreeRDP client; `xfreerdp3`, `xdotool`,
  `ffmpeg`, python3 + PIL/numpy present
* `/dev/dri/renderD128` VAAPI render node; xrdp on `127.0.0.1:3389`
* `/etc/xrdp/gfx.toml` with `order=["H.264",...]`, `h264_encoder="ffmpeg"`

## Run

```sh
bash ab_harness.sh          # runs the LAG / SPAMMER / async_depth groups
```

Each group edits only the `[avc444_ffmpeg]` block of `/etc/xrdp/gfx.toml`
(`setcfg.py`), reconnects a fresh client, paints the colour sequence
(`fill.sh`), grabs the client framebuffer and classifies the centre
(`classify.py`). Restore your real config afterwards (a copy is expected at
`/tmp/ab/gfx.toml.bak` during a session; keep your own backup).

`ffmpeg_pipeline_depth_probe.py` needs no session — it drives ffmpeg directly to
show withheld-frames == `async_depth − 1`.
