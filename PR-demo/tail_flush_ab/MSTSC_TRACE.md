# Localising the tail-frame withhold on a real client (mstsc)

The xfreerdp screenshot A/B in `RESULTS.md` cannot see a client/transport-level
withhold, because it observes xfreerdp's own framebuffer. To find where the last
frame is held on the client you actually use (mstsc), the server can log a
timestamped per-frame trace.

## Enable the trace

Start xrdp with `XRDP_GFX_TRACE=1` in its environment. With systemd:

```sh
sudo systemctl edit xrdp      # add the three lines below, then save
# [Service]
# Environment=XRDP_GFX_TRACE=1
sudo systemctl daemon-reload && sudo systemctl restart xrdp
```

(Disable later by removing the drop-in and restarting.)

## Run the test

**Rule 0 — fresh login, always.** `[avc444_ffmpeg]` config (encoder_args /
`async_depth` / `tail_flush`) is observed to take effect only at a **fresh
login** (logoff → login), *not* on disconnect/reconnect to an existing
session (verified on-box 2026-07-17; earlier reconnect-only A/Bs gave false
results because of this). Before ANY onscreen test after a config change:
log off the tester session first —

```sh
sudo -u tester pkill -u tester -TERM xfce4-session; sleep 3
pgrep -f 'Xorg :1[0-9]' || echo "session gone — next connect is a cold login"
```

`keytest.sh`/`smoke.sh` enforce this automatically (they kill the session and
cold-login every run); manual mstsc runs must do it by hand.

1. `mstsc` into the box (fresh login), open a terminal with the cursor blink off
   if possible.
2. Type a short burst, then **one** more character and **stop** — the symptom is
   that last character not appearing until you type again.
3. Note the wall-clock moment you typed the last character.

## Read `journalctl -u xrdp` / the xrdp log

Each frame logs two kinds of line:

```
GFX_TRACE send bytes=<n> last=<0|1> frame_id=<id> id_server=<s> id_client=<c> fif=2
GFX_TRACE ack  frame_id=<id> queue_depth=<q> decoded=<d> id_server=<s> ack_off=<0|1>
```

Interpretation for the last keystroke:

| What the trace shows | Where the frame is stuck |
|---|---|
| a `send last=1 frame_id=N` appears right when you typed, but nothing on screen | **client/transport** — mstsc received it but did not present it (or it sat in the socket). tail_flush works by re-emitting it. |
| **no** `send` line until you type the *next* char | **server/encoder** — the frame was never produced/sent while idle (encoder pipeline depth, or capture-side hold). |
| `ack_off=1` appears | mstsc **suspended frame acks** — flow control then acks the module directly; compare cadence vs xfreerdp (`ack_off=0`, acks every frame). |
| `send last=1 frame_id=N` present but its `ack frame_id=N` is much later / missing | mstsc is **acking lazily**; the next capture is gated on that ack. |

Capture ~10 s of log around the last keystroke (both with `tail_flush=false` and
`=true`) and compare. That pins the stage without guessing.

## Baseline (xfreerdp, for comparison)

xfreerdp on this box: every `send last=1 frame_id=N` is followed by
`ack frame_id=N` within ~10-15 ms, `fif=2`, `ack_off=0` — i.e. it acks and
presents each frame immediately, which is why the screenshot test reads
"delivered".
