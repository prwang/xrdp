# Offline dual-monitor AVC444 harness

Drives a real **2 × 1024×768** `xfreerdp /multimon` login against the deployed
xrdp and asserts the server took the **multimonitor AVC444** path introduced by
`feat(avc444): enable multimonitor — one ffmpeg child per monitor`:

- client sends `monitorCount = 2`;
- the ffmpeg backend is probed **once at the largest single monitor**
  (`1024x768`), **not** the `2048×768` virtual desktop (which is what would
  overflow a backend's per-session limit, e.g. NVENC 4096×4096);
- AVC444 is negotiated (not silently dropped to RFX as the old
  `monitorCount <= 1` gate did);
- **two** GFX surfaces are created — one per monitor — each driven by its own
  lazily-created ffmpeg child (`avc444_ffmpeg_handle[mon_index]`).

## Files
- `xorg-dummy-2mon.conf` — headless client X server, two DUMMY outputs.
- `setup_monitors.sh` — arranges them into two side-by-side 1024×768 RandR
  monitors; **fails loudly** if it cannot produce exactly 2 (never proceeds
  with 1 — that would silently test the single-monitor path).
- `run_multimon_offline.sh` — the driver + server-log assertions.

## Run (on an unguarded host)
```
sudo apt-get install -y xserver-xorg-video-dummy   # one-time
# deploy the multimon build first (clean dev .deb, per CLAUDE.md "Deployment")
sudo bash PR-demo/multimon_offline/run_multimon_offline.sh
```
Env knobs: `MM_HOST` (default `127.0.0.1:3389`), `MM_USER` (`tester`),
`MM_CLIENT_DISPLAY` (`:95`), `MM_SERVER_LOG` (`/var/log/xrdp.log`),
`MM_XFREERDP` (`xfreerdp3`). Exit 0 = PASS.

## Sandbox note (why this was not auto-run in the dev container)
This repo's dev *container* has a process guard that **reaps background X
servers/clients** (`Xvfb`/`Xorg`/`xfreerdp`) at tool-call boundaries — the same
guard that blocked the earlier `chroma_strip_anim` live tester. A live
end-to-end run needs a persistent client X server + `xfreerdp` + xrdp held
across the RDP handshake, which the guard kills (observed: dummy `Xorg :95`
reaped with exit 144, even via a detached background launch). So the live run
of THIS harness is executed on an **unguarded host** (the dev box directly, or
the owner's rig), not from inside the container.

What *is* proven inside the container, deterministically and in CI:
`tests/xrdp/test_avc444_multimon.c` — the probe-geometry unit test (largest
single monitor, per-axis max, 16-align, no-layout fallback). That is the
durable regression backstop; this harness is the live behaviour confirmation
on top of it.

## Isolated instance (optional, avoids touching a live :3389 session)
To test the freshly-built binary without redeploying the system service, run a
second xrdp front-end from the build tree on another port, sharing the running
sesman, and point the harness at it:
```
# build tree binary, e.g. xrdp/.libs/xrdp ; copy /etc/xrdp to a temp dir and
# set Port=3390 in its xrdp.ini, keep gfx.toml avc_mode="444"
sudo xrdp/.libs/xrdp --nodaemon -c /tmp/xrdp-test/xrdp.ini &   # listens :3390
MM_HOST=127.0.0.1:3390 MM_SERVER_LOG=/tmp/xrdp-test.log \
    sudo -E bash PR-demo/multimon_offline/run_multimon_offline.sh
```
