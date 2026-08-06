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
| `xrdp-dev` | `xrdp-dev_0.10.80+gitc74a09e7d000_amd64.deb` | Cleanroom xrdp, branch `avc444-ffmpeg-upstream` @ `c74a09e7` (history rewritten 2026-07-22: dump_extra fix folded into slice 7). AVC444/AVC420 ffmpeg GFX encoder + caps negotiation, NVENC/global-header capable. The T4 currently runs the equivalent dev-branch build `71179f67` (same encoder code + inert trace scaffold). |
| `xorgxrdp-dev` | `xorgxrdp-dev_0.10.80+gite86bff0+glamor_amd64.deb` | xorgxrdp `e86bff0`, **`--enable-glamor`**. Full-chroma XRGB8888 capture (`CC_GFX_AVC444`) the encoder depends on. |

The two are version-coupled: the `xrdp-dev` deb declares
`Breaks: xorgxrdp (<< 1:0.10.80~)`, so an outdated xorgxrdp fails **loudly** at
install rather than silently at login. Install both.

> **Currently deployed on the T4 (2026-07-30)** — the #45 steps 0–7 pair,
> measured by the #55 E5-2 gate (1.67×) and smoke-gated:
> `xrdp-dev 0.10.80+git20260730013346.52b8798839ad` +
> `xorgxrdp-dev 1:0.10.80+git20260729225933.d77d05463e52`, with
> `/etc/xrdp/gfx.toml` = `PR-demo/t4_profile/gfx-t4-nvenc-ltr-g240-gate.toml`
> (`aux_intra_leaf = false`, `aux_ltr_chain = true`,
> `intra_refresh_frames = 240` — **not** `-g 30000` any more). Also on the
> box: a `XRDP_GFX_TRACE=1` systemd drop-in (measurement instrument, harmless
> otherwise) and `nvidia-smi -pm 1` (§2b(e)).
> Previous state, for rollback: `xrdp-dev 2a0279ef3aa1` +
> `xorgxrdp-dev 5b9650cafbc3` with `gfx-t4-nvenc-ltr.toml`.
> The xorgxrdp side of both pairs carries the capture-shmem up-front
> reservation (§2b(d)).
> Whenever you change what is deployed, update this line — it is the only
> place that records what a box is actually running.

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
| **NVENC** (tested: T4) | Nvidia (e.g. T4) | vendor `nvidia-driver-###` (ships `libnvidia-encode`) | `nvidia-smi`; T4 = unlimited NVENC sessions |
| QSV | Intel | `intel-media-va-driver libmfx-gen1.2` | `/dev/dri/renderD128` |
| CPU | any | none (libx264 built into ffmpeg) | fallback / no-GPU boxes |

For VAAPI/QSV/NVENC the xrdp service user must reach the render node — on Intel/AMD
that means membership in the `render` group (owner of `/dev/dri/renderD128`).

> **NVENC portability:** moving Intel→Nvidia needs **no** new deb — only a
> `gfx.toml` `encoder_args` edit (§3). Validated live 2026-07-22 on an x86 +
> Tesla T4 (driver 580.159.03, CUDA 13.0, Ubuntu ffmpeg 8.0.1): session up,
> display correct, and `nvidia-smi` shows the session's `/usr/bin/ffmpeg` as
> a compute process (~200 MiB) — i.e. real GPU encode, not a CPU fallback.
> Requires the dump_extra fix (§0/§3).

### 1c. GPU environment check (run BEFORE deploying, takes 30 s)

Verifies the box can actually hardware-encode, and gives you the baseline to
compare against when performance looks wrong later.

```sh
# 1. driver + engine present
nvidia-smi --query-gpu=name,driver_version,persistence_mode --format=csv
ffmpeg -hide_banner -encoders | grep -E 'nvenc|vaapi'      # h264_nvenc / h264_vaapi
vainfo | grep -i h264                                       # VAAPI boxes only

# 2. the encoder really works with the EXACT args gfx.toml will use —
#    catches "encoder exists but this arg combination fails" before a deploy
ffmpeg -hide_banner -f lavfi -i testsrc=size=1920x1088:rate=30:duration=2 \
  -pix_fmt yuv420p -c:v h264_nvenc -profile:v high -preset p1 -tune ll \
  -refs 1 -dpb_size 1 -rc constqp -qp 20 -bf 0 -delay 0 -g 240 \
  -f h264 -y /tmp/probe.h264 && ls -l /tmp/probe.h264

# 3. stream SHAPE check (needed before enabling aux_ltr_chain, §3):
python3 tools/avc444_ltr_wire_audit.py --annexb /tmp/probe.h264 "this box"
#    -> prints every ltr_cache_ok() condition; all nine must PASS

# 4. utilisation baseline under load (single most useful perf datum)
nvidia-smi dmon -c 10        # columns: sm%, enc%, pclk MHz
```

Read `dmon` honestly: **`enc%` is the NVENC engine, `sm%` is the shader core**
— H.264 encoding lives almost entirely in `enc`. A low `enc%` with low fps
means the pipeline is *latency*-bound (serialised round-trips), NOT that the
GPU is too slow; see §7. `pclk` well below `clocks.max.sm` under load is
normal for a bursty duty cycle and is a symptom of the same thing.

---

## 2. Install the packages

Install **both** packages in ONE transaction, non-interactively, keeping
local conffiles:

```sh
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    -o Dpkg::Options::=--force-confold \
    /work/dist/xorgxrdp-dev_1%3a0.10.80+git<ts>.<hash>_amd64.deb \
    /work/dist/xrdp-dev_0.10.80+git<ts>.<hash>_amd64.deb
sudo systemctl enable --now xrdp xrdp-sesman
systemctl is-active xrdp xrdp-sesman          # both -> active
dpkg -l | grep -E '^ii +(xrdp-dev|xorgxrdp-dev)'   # BOTH must be 'ii'
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

xrdp sessions are also not polkit-"local": on every fresh xfce login polkit
pops **"Authentication is required to create a color managed device"**,
which blocks unattended and onscreen testing (hit live 2026-07-27). Allow
the colord actions once per box:

```sh
sudo tee /etc/polkit-1/rules.d/45-allow-colord.rules >/dev/null <<'RULES'
polkit.addRule(function(action, subject) {
    if (action.id.indexOf("org.freedesktop.color-manager.") == 0) {
        return polkit.Result.YES;
    }
});
RULES
sudo systemctl restart polkit
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

### 2b. Six install hazards that have each cost real time

**(a) Conffile prompt hangs the install (2026-07-28, T4).** Any box whose
`/etc/xrdp/cert.pem` (or `gfx.toml`, `xrdp.ini`, …) was locally modified makes
dpkg prompt *"Modified (by you or by a script) since installation"*. Over a
non-interactive ssh there is no stdin, so dpkg aborts with `end of file on
stdin at conffile prompt` and leaves the package **unpacked but unconfigured**
(`dpkg -l` shows `iU`, and the box is in a half-installed state):

```sh
sudo DEBIAN_FRONTEND=noninteractive dpkg -i --force-confold <deb>   # recover
```
Always pass `--force-confold` (keep the local file). Replacing the TLS
`cert.pem` silently changes the box's certificate identity, and replacing
`gfx.toml` silently reverts the encoder config the test depends on.

**(b) Version strings must sort monotonically.** dpkg compares leading digit
runs numerically, so a **bare commit hash** does not sort: `1:0.10.80+git5b9650…`
is *older* than `1:0.10.80+git251bc4d…` (5 < 251). apt then refuses with
`Packages were downgraded and -y was used without --allow-downgrades`, and a
paired `Breaks:` guard can reject a genuinely newer build. `build_dev_deb.sh`
already emits `+git<commit-timestamp>.<hash>` for xrdp; **package xorgxrdp the
same way** (§4). Never reach for `--allow-downgrades` to paper over it — fix
the version.

**(c) `xrdp-dev` Breaks old `xorgxrdp`.** The xrdp deb carries
`Breaks: xorgxrdp (<< 1:0.10.80~)`, so installing it can *remove* a stale
xorgxrdp (and with it `/etc/X11/xrdp/xorg.conf`), which breaks every session
creation. This is why the `dpkg -l` check above is mandatory **after** every
install, not before.

**(d) `/dev/shm` must fit the capture segment (containers).** The capture
shmem is `2 slots × (main + aux packed views) × all monitors`; a dual-4K
layout needs ~74 MB and a 3840×2400 single monitor ~88 MB, while Docker/k8s
default `/dev/shm` to **64 MB**. Since 2026-07-28 xorgxrdp reserves the whole
segment up front and fails the client connection with a loud
`can not allocate N bytes of shared memory … /dev/shm is probably too small`
instead of SIGBUSing Xorg mid-session. Size it explicitly:

```sh
df -h /dev/shm                    # bare metal: usually RAM/2, fine
# k8s: emptyDir { medium: Memory, sizeLimit: 512Mi } mounted at /dev/shm
# docker: --shm-size=512m
```

**(e) A cold GPU silently downgrades the session to RFX (2026-07-30, T4).**
On the first connection after boot — or after any idle gap long enough for
the NVIDIA driver to unload — xrdp's AVC444 verification times out and
removes H.264 for the life of the connection:

```
xrdp_ffmpeg: probe TIMEOUT ... (3840x2400, packets=0, elapsed=4008 ms)
  ffmpeg verification FAILED (TIMEOUT); removing external AVC candidate
Codec search order is H264, RFX
Matched RFX mode          <-- the session comes up looking fine, at RFX quality
```

Cold `h264_nvenc` at 3840×2400 measured **3.95 s** to first output against
**1.40 s** warm, and the probe deadline is ~4 s. Mitigate on any NVIDIA box
by keeping the driver resident, and **check the log before trusting a
session**:

```sh
sudo nvidia-smi -pm 1                                  # persistence mode
grep -E "Matched (RFX|H264) mode|probe TIMEOUT" /var/log/xrdp.log | tail
```

There is no visible symptom other than quality, and no retry — see
BACKLOG #56 for the fix.

**(f) `apt-get` needs the conffile option spelled its own way.** Hazard (a)
above shows the `dpkg -i` form. Through apt it is:

```sh
sudo DEBIAN_FRONTEND=noninteractive apt-get install -y \
    --allow-downgrades -o Dpkg::Options::=--force-confold /tmp/xrdp-dev_*.deb
```

Without it a plain `apt-get install -y` still stops at the prompt.

### 2c. Credentials — where they live, how to use them

Binding rules (CLAUDE.md). A credential is **never** printed, never stored off
the box it belongs to, never passed as an argv token (argv is world-readable in
`/proc`), and never committed.

| Credential | Lives in | Used for |
|---|---|---|
| T4 `ubuntu` RDP password | `/root/.ubuntu_cred` on the T4 (root, 0600) | onscreen + harness logins as the owner-equivalent account |
| oracle/probe client password | `/root/.oracle_cred` on the dev box | automated `probe444` wire captures |
| bisect-fleet `tester` hash | `/etc/xrdp-matrix/tester.hash` (copied from host `/etc/shadow`) | container arms reuse the host password without embedding it in an image |
| WS2022 ground-truth VM | `/root/.testvm_cred`, `/root/.win_askpass.sh` (0700) | Windows reference captures |

Correct use — read into a shell variable at the moment of use, hand it to the
client through an **environment** file/var, then unset:

```sh
PW=$(ssh -i "$KEY" "$T4" 'sudo cat /root/.ubuntu_cred')     # never echoed
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:ubuntu" "/p:$PW" \
                        "/size:1600x900" "/gfx:AVC444" "/cert:ignore")
env RDPARGS="$RDPARGS" xfreerdp /args-from:env:RDPARGS
unset PW RDPARGS
```

**Recreated cloud box gotcha (2026-07-28, cost ~15 min).** When the T4 is
rebuilt from an AMI, cloud-init **re-locks** the default `ubuntu` account
(`passwd -S ubuntu` → state `L`) while `/root/.ubuntu_cred` survives inside the
image. RDP login then fails with `pam_authenticate failed: Authentication
failure`, which reads exactly like a broken deploy. Restore the invariant:

```sh
bash PR-demo/t4_profile/t4_restore_cred.sh     # idempotent; prints L -> P only
```
Also re-check `/root/.t4_host` (the one place the instance address lives) — a
recreated instance gets a new IP and the stale value sends every helper script
at the wrong box.
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

**NVENC (Nvidia T4 — TESTED 2026-07-22):** live session validated by the owner
on an x86 + T4 box (Ubuntu, ffmpeg 8.0.1, driver 580.159.03): probe OK,
persistent ffmpeg child during the session, display correct, and `nvidia-smi`
lists that ffmpeg as a GPU compute process (~200 MiB) — genuine hardware
encode. Requires an xrdp build with the dump_extra fix (see §0) — nvenc has no
in-band SPS/PPS repeat option, so older builds fail the probe by design.
Owner-validated in addition (2026-07-22): **small session sizes work** and
**no chroma fringe** was observed across multiple small width/height sessions
on region-strict rendering — validation coverage orthogonal to the dump_extra
fix (different defect classes: probesize hold, metablock alignment). The
scripted multi-resolution smoke gate (`PR-demo/smoke_gate/smoke.sh`) is
dev-box-specific and was NOT run on the T4. nvenc uploads the sysmem NV12
itself, so no `hwupload`/`vaapi_device`:
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

### 3a. The `[avc444_ffmpeg]` knobs, in full

Everything below defaults OFF/absent, i.e. omitting the whole block keeps the
pre-AVC444 behaviour. Versioned reference profiles live in
`PR-demo/t4_profile/gfx-t4-nvenc-{ltr,leaf}.toml` (T4) and
`PR-demo/mac_bisect_matrix/gfx/arm-*.toml` (fleet) — copy one rather than
hand-writing a new file, and keep any box's live file in git.

| Knob | Meaning / when to use |
|---|---|
| `path` | the ffmpeg binary to exec (`/usr/bin/ffmpeg`) |
| `avc_mode` | `"auto"` (prefer AVC444, else AVC420) \| `"444"` \| `"420"`. Force `"420"` to exercise the 420 path on clients like mstsc that always advertise AVC444 |
| `encoder_args` | verbatim argv tokens: `-c:v` + tuning only (see above) |
| `dump_extra` | write SPS/PPS in-band for encoders that cannot repeat headers. **Required for nvenc** — without it the probe fails by design |
| `strip_sei` | drop SEI NALs the client's decoder may reject |
| `sanitize_hrd` | remove `nal_hrd` from the SPS (fixes macOS Windows App decode; see BACKLOG arm-e) |
| `strip_pic_struct` | drop `pic_struct` from picture-timing SEI |
| `aux_intra_leaf` | FR-H264-7: encode the aux view as non-referencing intra leaves. **Currently the shipped default topology** |
| `aux_ltr_chain` | FR-H264-8 (**EXPERIMENTAL**): aux predicts from the previous aux via Windows-style long-term reference slots. Mutually exclusive with `aux_intra_leaf` — set that to `false`. Cuts aux bytes dramatically (−70…−99% depending on workload). Requires the child encoder to pass all nine `ltr_cache_ok()` conditions — clear it with the §1c step 3 check first; a rejected encoder silently stays on the leaf topology |

`aux_ltr_chain` also logs a WARN at every connect
(`gfx.toml aux_ltr_chain is ON: EXPERIMENTAL …`) so a box in the experimental
topology is never mistaken for a default deployment.

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
    --enable-glamor && make -j"$(nproc)" CPPFLAGS='-I/work/common' )
# then stage make install DESTDIR=… + DEBIAN/control (see build_config.md Part X).
```

Two things the xorgxrdp build will bite you with:

* **Build against the branch's `xup_client_info.h`, not the installed one.**
  xorgxrdp includes that header from `/usr/include` (shipped by `xrdp-dev`).
  If the branch has advanced the XUP contract, the build fails with
  `XUP_CAP_AVC444_SLOT_COUNT undeclared` / `too many arguments to
  xup_cap_h264_shmem_layout`. Pass `CPPFLAGS='-I/work/common'` (as above) so
  it compiles against the tree you are deploying.
* **Version the deb monotonically**, exactly like `build_dev_deb.sh` does for
  xrdp — `Version: 1:0.10.80+git<commit-timestamp>.<hash>` (e.g.
  `1:0.10.80+git20260728175938.5b9650cafbc3`). A bare hash does not sort; see
  §2b(b). Verify before shipping it:
  ```sh
  dpkg --compare-versions "$NEW" gt "$OLD" && echo "sorts newer: OK"
  ```

---

## 5. Validate BEFORE onscreen testing

### 5a. Smoke gate (mandatory — run against the live binary+config)
```sh
bash PR-demo/smoke_gate/smoke.sh
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
Empirical status 2026-07-22: fringe absent across multiple small width/height
sessions on the T4 (NVENC, region-strict client, owner-tested).

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

---

## 7. Performance triage — "the GPU box feels slow"

Before blaming the encoder, establish **which** resource is short. Measured on
the T4 (Tesla T4, 4 vCPU, 3840×2400 session, orbit-drag load, 2026-07-28):

| Signal | Reading | Verdict |
|---|---|---|
| `nvidia-smi dmon` `enc%` | 25–28 % (peak 43) | NVENC ~3/4 idle |
| `dmon` `sm%` | 4–5 % | shader core idle |
| `dmon` `pclk` | 585 MHz of 1590 max | never boosts — bursty duty cycle |
| `top` ffmpeg children | ~6 % CPU each, load 0.22/4 cores | CPU idle |
| `ENCODE duration` p50 | 67.5 ms/pair | the entire frame period |

So nothing is saturated, yet the pair costs 67.5 ms. **The pipeline is
latency-bound, not throughput-bound.** Two structural reasons:

1. **main and aux encode strictly sequentially.** `encode_pair()`
   (`xrdp/xrdp_encoder_ffmpeg.c`) calls `encode_single(main)` and only then
   `encode_single(aux)`; each is a synchronous write-frame → block-for-packet
   round trip to a separate ffmpeg child. One encode is in flight at a time.
2. **`-delay 0` disables NVENC's internal pipelining** (correct for
   interactivity — we must not buffer frames — but it means no overlap).

Isolated measurements on the same box, same args, no xrdp involved:

```
single 4K stream, file input   fps= 51      (~19.6 ms/frame)
single 4K stream, PIPE input   fps= 52      -> the pipe costs nothing
TWO 4K streams in PARALLEL     fps= 53 each -> concurrency is FREE
without -delay 0               fps= 99      -> batch-only artifact (see below)
```

Reading these correctly:

- Two concurrent 4K encodes run at **full speed each** (53 fps), i.e. the T4
  absorbs main+aux simultaneously at no cost. Serialising them is pure loss:
  ~2 × 19.6 ms instead of ~19.6 ms, plus per-round-trip overhead — which is
  the bulk of the 67.5 ms pair.
- The `-delay 0` → 99 fps figure is **batch throughput only**. In the live
  path frames arrive one at a time as damage occurs, so there is nothing to
  pipeline within a single view; dropping `-delay 0` would add latency without
  adding live fps. Do **not** "optimise" it away.
- The fix that the measurements actually support is **overlapping main and
  aux** — backlog FR-PROC-7 / Lever 2 (submit/collect with preempt/breadth/
  depth policies), still TODO.

Order-of-magnitude sanity check for any geometry: one 4K encode ≈ 20 ms, so a
serialised pair ≈ 40 ms + overhead ≈ 14–15 fps; at 1600×912 (6.3× fewer
pixels) the same structure gives ≈ 34 fps. A 4K session reporting ~14 fps is
therefore the *expected* behaviour of the current serial design, not a fault —
and it is identical on both `aux_intra_leaf` (66.3 ms) and `aux_ltr_chain`
(67.5 ms), so it is not attributable to the FR-H264-8 topology.

**Caveat when reading fps at all:** `frame period` only equals `ENCODE
duration` while damage is arriving faster than the encoder drains it. On a
quiet session the fps figure measures the *workload*, not the pipeline (a
2026-07-28 run showed 5 fps with a 66.8 ms encode simply because the drag had
stopped). Always check `enc_pair` rate against `ENCODE duration` before
concluding anything.

An older BACKLOG record (2026-07-26) shows 4K `ENCODE` at 30.1 ms vs today's
67.5 ms. That delta is **not** explained here and was not bisected; candidates
are a different EC2 instance after recreation, the `-refs 1 -dpb_size 1` args
added during the 2026-07-27 nvenc bisect, and ffmpeg 8.0.1. It is recorded as
open rather than guessed at.

---

## 6. Measuring the frame interval on the T4 (BACKLOG #52 / E5-2)

Protocol, gates and artifact inventory: **`PR-demo/t4_profile/E5-2_T4_PROTOCOL.md`**.
It is the procedure for an A/B on a box with ONE xrdp instance — both debs
named, the version-sort trap (a baseline deb built later sorts *newer*, so
the swap is a downgrade and the deployed hash must be verified before every
measurement), the four gates a number must pass to count, the 5–11 GB oracle
dumps and how they are audited on a prefix and then deleted, the pack-bench
and smoke-gate obligations, and the cleanup that stops a session from burning
a core after the run.

Two things there generalise beyond the T4 and belong in any deploy:

- **After ANY xrdp-dev install, confirm `xorgxrdp-dev` is still installed**
  (§2b hazard). `e_gate_run.sh` now aborts instead of measuring without it.
- **Log the session off after a deb swap, before measuring or handing over.**
  A surviving session keeps the previous xorgxrdp module loaded; if the xup
  contract version happens to match, it pairs silently and you test the old
  code.
