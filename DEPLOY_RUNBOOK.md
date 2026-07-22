# AVC444 GPU-accelerated GFX — deploy runbook

Deploy and validate the cleanroom AVC444/AVC420 external-ffmpeg GFX encoder on a
fresh box. This is a **dev-branch** operational doc; the upstream PR itself lives
on branch `avc444-ffmpeg-upstream` (source only).

The design shells out to the stock system `ffmpeg`; xrdp links **no** GPU
library, so the encoder backend (VAAPI / NVENC / QSV / CPU) is selected entirely
by `gfx.toml` `encoder_args` — no recompile to move between GPUs.

---

## 0. Artifacts (the cleanroom pair)

Both prebuilt under `/work/dist` (rebuild recipe in §4):

| Package | File | What it is |
|---|---|---|
| `xrdp-dev` | newest `xrdp-dev_0.10.80+git*_amd64.deb` (dev-branch build; exact name pinned below in §2) | xrdp with the AVC444/AVC420 ffmpeg GFX encoder + caps negotiation. **Must carry the dump_extra fix for NVENC/global-header encoders** — cleanroom `287423b4` and earlier fail the probe on nvenc; the fix folds into the clean-room slices at porting time. |
| `xorgxrdp-dev` | `xorgxrdp-dev_0.10.80+gite86bff0+glamor_amd64.deb` | xorgxrdp `e86bff0`, **`--enable-glamor`**. Full-chroma XRGB8888 capture (`CC_GFX_AVC444`) the encoder depends on. |

The two are version-coupled: the `xrdp-dev` deb declares
`Breaks: xorgxrdp (<< 1:0.10.80~)`, so an outdated xorgxrdp fails **loudly** at
install rather than silently at login. Install both.

---

## 1. Dependencies to install (runtime)

### 1a. Common (all backends)

```sh
sudo apt-get update
# external encoder — the whole GFX pipeline execs this binary:
sudo apt-get install -y ffmpeg
# xorgxrdp GLAMOR capture runtime (also pulled as deb Depends):
sudo apt-get install -y xserver-xorg-core libgbm1 libegl1 libepoxy0
# a session desktop + PAM/session stack if not already present — see
# normal_config.md (xfce4, dbus-x11, etc.). Baseline xrdp, unchanged by AVC444.
```

Confirm the ffmpeg on the box actually has the encoder you intend to use
(stock Ubuntu/Debian ffmpeg ships all of these — verified on this box):

```sh
ffmpeg -hide_banner -encoders | grep -E 'h264_vaapi|h264_nvenc|h264_qsv|libx264'
```

### 1b. Pick ONE GPU backend and install its driver

| Backend | GPU | Packages | Device / check |
|---|---|---|---|
| **VAAPI** (this box) | Intel iGPU | `intel-media-va-driver va-driver-all libva2 vainfo` | `/dev/dri/renderD128`; `vainfo` lists `VAEntrypointEncSlice` |
| VAAPI | AMD | `mesa-va-drivers libva2 vainfo` | `/dev/dri/renderD128` |
| **NVENC** | Nvidia (e.g. T4) | vendor `nvidia-driver-###` (ships `libnvidia-encode`) | `nvidia-smi`; T4 = unlimited NVENC sessions |
| QSV | Intel | `intel-media-va-driver libmfx-gen1.2` | `/dev/dri/renderD128` |
| CPU | any | none (libx264 built into ffmpeg) | fallback / no-GPU boxes |

For VAAPI/QSV/NVENC the xrdp service user must reach the render node — on Intel/AMD
that means membership in the `render` group (owner of `/dev/dri/renderD128`).

> **NVENC portability caveat:** moving Intel→Nvidia needs **no** new deb — only a
> `gfx.toml` `encoder_args` edit (§3). But the NVENC arg recipe below is
> **untested in this project** (this box has no Nvidia GPU); validate it before
> relying on it.

---

## 2. Install the packages

```sh
sudo apt-get install -y /work/dist/xorgxrdp-dev_0.10.80+gite86bff0+glamor_amd64.deb
sudo apt-get install -y /work/dist/xrdp-dev_0.10.80+git71179f670fd5_amd64.deb
sudo systemctl enable --now xrdp xrdp-sesman
systemctl is-active xrdp xrdp-sesman          # both -> active
```

xrdp must stay **systemd-managed** and bound to `127.0.0.1:3389` (default) unless
you have a reason to change it. The debs `Conflicts/Replaces` the distro `xrdp`
/`xorgxrdp`, so they supersede stock cleanly; `/etc/xrdp/*` are `conffiles`
(admin edits preserved on upgrade).

### 2a. Fresh-box X-server prerequisites (do this BEFORE first login)

The shipped `sesman.ini` launches the X server as `param=Xorg` — the bare name
resolves to the Debian/Ubuntu **suid wrapper**, whose default policy
(`allowed_users=console`) refuses non-console users. An RDP login then fails
with `Can't create session … X server could not be started`. Pick ONE fix:

```sh
# (a) allow the wrapper for RDP users (needs xserver-xorg-legacy):
printf 'allowed_users=anybody\nneeds_root_rights=no\n' \
    | sudo tee /etc/X11/Xwrapper.config
# (b) or bypass the wrapper: in /etc/xrdp/sesman.ini [Xorg] set
#     param=/usr/lib/xorg/Xorg        (the non-suid binary; path per distro,
#     see the comment block in sesman.ini itself)
sudo systemctl restart xrdp-sesman
```

The dev box passes only because its `/etc/X11/Xwrapper.config` was hand-set to
`allowed_users=anybody`; a stock cloud image will not have this.

**Triage order for "X server could not be started"** (`xrdp.log` cannot tell
you more — the front-end is already past its part when this appears):

1. `sudo tail -50 /var/log/xrdp-sesman.log` and
   `journalctl -u xrdp-sesman -e` — sesexec prints the Xorg exit reason here.
   Wrapper refusal shows as *"Only console users are allowed to run the X
   server"*.
2. `tail -50 ~<user>/.xorgxrdp.1*.log` — Xorg's own log (`-logfile` is
   relative, so it lands in the session user's `$HOME`).
3. **Module ABI mismatch:** the prebuilt `xorgxrdp-dev` deb was compiled on
   Debian 13 against xserver 21.1.16 (video ABI 25, input ABI 24). If the
   Xorg log says *"module ABI major version … doesn't match the server"*,
   rebuild xorgxrdp on the target (§4) — the deb's unversioned
   `Depends: xserver-xorg-core` cannot catch this.
4. **GLAMOR/EGL init (Nvidia):** the deb is a GLAMOR build; on Nvidia the
   proprietary driver needs `nvidia-drm.modeset=1` for GBM/EGL. Look for
   glamor/EGL errors in the Xorg log.

---

## 3. Configure the encoder — `/etc/xrdp/gfx.toml`

Enable the ffmpeg AVC444 backend. `encoder_args` are passed to the child
**verbatim** (each array element = one execve argv token); xrdp injects the
input framing (`-f rawvideo -pixel_format nv12 …`) and the NUT output muxing
around them, so `encoder_args` only carries `-c:v` + tuning.

**VAAPI (this box — Intel/AMD):**
```toml
[avc444_ffmpeg]
path        = "/usr/bin/ffmpeg"
avc_mode    = "auto"          # negotiate AVC444 v2 / v1 / AVC420 by client caps
encoder_args = [
  "-vaapi_device", "/dev/dri/renderD128",
  "-vf", "format=nv12,hwupload",
  "-c:v", "h264_vaapi", "-rc_mode", "CQP", "-qp", "20",
  "-bf", "0", "-async_depth", "1",
]
```

**NVENC (Nvidia T4):** standalone encode with the exact probe argv and the
resulting bitstream through the xrdp demuxer/validators were verified
2026-07-22; a full live session is still pending. Requires an xrdp build with
the dump_extra fix (see §0) — nvenc has no in-band SPS/PPS repeat option, so
older builds fail the probe by design. nvenc uploads the sysmem NV12 itself,
so no `hwupload`/`vaapi_device`:
```toml
encoder_args = [
  "-c:v", "h264_nvenc", "-preset", "p1", "-tune", "ll",
  "-rc", "constqp", "-qp", "20", "-bf", "0", "-delay", "0", "-g", "240",
]
```

Latency rule (any backend): the runner is **synchronous** — it restarts loudly if
the encoder withholds/reorders. Keep `-bf 0` and a zero-latency knob
(`-async_depth 1` VAAPI / `-delay 0` NVENC / `-tune zerolatency` libx264).

Config binds at **fresh login** (logoff→login), not TCP reconnect. Restart after
editing: `sudo systemctl restart xrdp`.

**Probe-failure signature.** At connect time xrdp test-runs the configured
encoder. If `xrdp.log` shows
`probing ffmpeg AVC444 … ffmpeg probe FAILED; removing external AVC candidate`
followed by `Matched RFX mode`, the session is running **RFX, not AVC444** —
the args/driver don't work on this box (e.g. VAAPI args on an Nvidia host,
missing render node, ffmpeg without the encoder). Reproduce by hand and read
ffmpeg's stderr:

```sh
ffmpeg -f lavfi -i testsrc2=size=1280x720:rate=5 -frames:v 5 \
    <your encoder_args here> -f null -
```

Do not report AVC444 results until the log shows `ffmpeg AVC444 probe OK`.

---

## 4. Rebuild the debs from source (optional)

Build deps, then the two builder scripts:
```sh
# xrdp build deps:
sudo scripts/install_xrdp_build_dependencies_with_apt.sh
# xorgxrdp build deps + GLAMOR headers:
sudo /workUpdateXorgXrdp/scripts/install_xorgxrdp_build_dependencies_with_apt.sh
sudo apt-get install -y libgbm-dev libegl1-mesa-dev libepoxy-dev xserver-xorg-dev

# xrdp deb (from the avc444-ffmpeg-upstream worktree at /work-PR):
( cd /work-PR && ./bootstrap && ./configure --prefix=/usr --sysconfdir=/etc \
    --localstatedir=/var --libdir=/usr/lib/x86_64-linux-gnu \
    --with-systemdsystemunitdir=/lib/systemd/system && make -j"$(nproc)" )
BUILDDIR=/work-PR OUTDIR=/work/dist scripts/build_dev_deb.sh

# xorgxrdp GLAMOR deb (separate repo):
( cd /workUpdateXorgXrdp && ./bootstrap && ./configure --prefix=/usr \
    --enable-glamor && make -j"$(nproc)" )
# then stage make install DESTDIR=… + DEBIAN/control (see build_config.md Part X).
```

---

## 5. Validate BEFORE onscreen testing

### 5a. Smoke gate (mandatory — run against the live binary+config)
```sh
bash PR-demo/tail_flush_ab/smoke.sh
```
Drives `xfreerdp` keystroke-colour tests at **both** 1920×1080 and 1024×768
(the ffmpeg probesize failure is resolution-keyed), asserting every keypress
renders, zero lag, zero encoder restarts since a timestamp mark. **Exit 0 =
`SMOKE PASS`.** Expect log lines: `ffmpeg AVC444 probe OK`,
`Matched H264/AVC444 (ffmpeg) mode, AVC444 v2 (0x000F)`.

### 5b. Scope of the xfreerdp gate (honest limits)
xfreerdp is a **lenient** client (presents the whole decoded surface). The smoke
gate proves: encoder startup at both resolutions, per-keypress delivery without
lag, no encoder restarts, AVC444 v2 path engaged. It does **not** exercise
**region-strict** rendering (metablock even-alignment / region sync) — a lenient
client structurally cannot show that fringe.

### 5c. Onscreen (region-strict) — human step
Region-strict correctness is confirmed with **mstsc / rdcman / RD Client**: drive
a high-contrast edge on an odd-origin region and confirm no chroma fringe on the
region's top/left edge. See `AVC444_metablock_reachability_PROOF.md` for the A/B
method (before = revert `rect.left/top &= ~1`, after = branch as-is).

---

## 6. Rollback
```sh
sudo apt-get install -y --allow-downgrades \
    /path/to/previous/xrdp-dev_*.deb /path/to/previous/xorgxrdp-dev_*.deb
# or reinstall distro stock:
sudo apt-get install --reinstall -y xrdp xorgxrdp
sudo systemctl restart xrdp xrdp-sesman
```
`/etc/xrdp` edits are preserved as conffiles; keep a backup of `gfx.toml` before
swapping packages.
