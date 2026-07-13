# build_config.md — Build, package & deploy the dev xrdp, + headless checks

Non-interactive companion to `dev_config.md`. Everything here is run by an
agent/CI on the server: building this source tree (`xrdp 0.10.80`, branch
`dev/ipc_avc444`), packaging the `.deb`, deploying it over the
`normal_config.md` baseline, the version-matched **xorgxrdp** rebuild, and the
headless (`make check` / static-analysis) acceptance checks.

> This document is build/test infrastructure and is feature-agnostic. The
> feature under development is tracked in `BACKLOG.md`; its feature-specific
> acceptance criteria live there and (for interactive runs) in `dev_config.md`.

> For the **interactive** RDP smoke test (connect a client, confirm the session
> comes up) see **`dev_config.md`**. Do `normal_config.md` (stock baseline) first.

## 0. Verified environment

Executed in this sandbox and known to work:

| Fact | Value |
|---|---|
| OS | Debian GNU/Linux 13 (trixie), root (`sudo` present) |
| Source version | `xrdp 0.10.80` (`AC_INIT` in `configure.ac`) |
| Build | `bootstrap` + `configure` + `make -j32` → OK |
| Unit tests | `make check` → exit 0, all suites pass |
| Stage install | `make install DESTDIR=…` → OK |
| Packaging | `dpkg-deb --build` → OK |
| Stock pkgs | `xrdp 0.10.1-3.1+deb13u1`, `xorgxrdp 1:0.10.2-1` |
| Headless RDP client | `/usr/bin/xfreerdp3` (`freerdp3-x11 3.15`) |

---

## Part A — Build from source

### A.1 Build dependencies (exact set verified)

```bash
sudo apt-get update
sudo apt-get install -y autoconf automake libtool pkg-config nasm gcc make
sudo apt-get install -y \
    libpam0g-dev libssl-dev libx11-dev libxfixes-dev libxrandr-dev \
    libpixman-1-dev libxkbfile-dev libfuse3-dev
sudo apt-get install -y check libcmocka-dev      # unit-test frameworks
```

> `libcmocka-dev` is easy to miss: without it `tests/xrdp` fails to compile
> (`fatal error: cmocka.h`) and `make check` aborts. Mandatory for dev.

### A.2 Submodules (required — they are git submodules)

```bash
git submodule update --init        # librfxcodec, libpainter
git submodule status               # both lines start with a space, not '-'
```

### A.3 Bootstrap, configure, build (Debian-style paths)

```bash
./bootstrap
./configure \
    --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
    --libdir=/usr/lib/x86_64-linux-gnu \
    --with-systemdsystemunitdir=/lib/systemd/system
make -j"$(nproc)"
```

> **`--with-systemdsystemunitdir` is mandatory for the `.deb`** (gotcha #2 in
> Part Y). Without it `configure` reports *"systemd support: no"* and
> `make install` falls back to the SysV init script `/etc/init.d/xrdp` (one
> script that bundles both daemons) instead of the native
> `xrdp.service` + `xrdp-sesman.service`. On this box there is no
> `pkg-config systemd`, so the flag must be given **explicitly** (auto-detect
> fails). The stock Debian package ships the native units; a dev `.deb` built
> without the flag silently loses them.

Optional features need extra `-dev` packages (`--enable-fuse`, `--enable-rfxcodec`,
`--enable-painter`, …); enable only what the active `BACKLOG.md` item requires.

**Pass:** `make -j` exits 0, and `configure` prints *"systemd support: yes /
unit file directory /lib/systemd/system"*.

---

## Part B — Headless verification (no RDP client)

### B.1 Unit tests

```bash
make check ; echo "exit=$?"
```

**Pass:** `exit=0`. Capture the current upstream-clean baseline totals on this
branch with `make check` and treat them as the regression floor — they must not
drop. As the feature lands (per the active `BACKLOG.md` item), any new pure logic
and any new message-field round-trip must add tests that appear and pass here.

### B.2 Static/style gate (matches CI)

```bash
sudo apt-get install -y astyle cppcheck
astyle --options=astyle_config.as --dry-run "xrdp/*.c" "sesman/*.c" "libipm/*.c"
```

### B.3 Headless end-to-end (plumbing + regression only)

```bash
xfreerdp3 /v:127.0.0.1:3389 /u:tester /p: /size:3840x2160 /cert:ignore &
pgrep -a Xorg                          # read the actual X server argv
sudo grep -E 'Starting X server' /var/log/xrdp-sesman.log | tail
```

> `xfreerdp3` validates the **plumbing**, the **fallback path**, and
> **regression** (a normal client still starts a session). Feature behaviour
> that depends on client-specific capabilities `xfreerdp3` does not advertise
> must be checked interactively with **mstsc** (see `dev_config.md`).

---

## Part C — Package the `.deb`

Use `scripts/build_dev_deb.sh` (commit-tagged version; `Conflicts/Replaces/Provides:
xrdp`; `Breaks: xorgxrdp (<< 1:0.10.80~)` so an outdated xorgxrdp fails loudly at
install — see Part X). Manual equivalent:

```bash
rm -rf /tmp/xrdp-deb && make install DESTDIR=/tmp/xrdp-deb
cd /tmp/xrdp-deb && mkdir -p DEBIAN
KB=$(du -sk . | cut -f1)
cat > DEBIAN/control <<EOF
Package: xrdp-dev
Version: 0.10.80-dev1
Architecture: amd64
Maintainer: xrdp dev build <xrdp-devel@googlegroups.com>
Installed-Size: ${KB}
Depends: libc6, libssl3 | libssl1.1, libpam0g, libx11-6, libxfixes3, libxrandr2
Recommends: xorgxrdp (>= 1:0.10.80~), xserver-xorg-core
Conflicts: xrdp
Replaces: xrdp
Provides: xrdp
Breaks: xorgxrdp (<< 1:0.10.80~)
Section: net
Priority: optional
Description: xrdp built from source (devel 0.10.80), dev test build
 Local dev/test build (see BACKLOG.md). Not for production.
EOF
find etc -type f | sed 's,^,/,' > DEBIAN/conffiles    # preserve /etc/xrdp edits
cd / && dpkg-deb --root-owner-group --build /tmp/xrdp-deb /tmp/xrdp-dev_0.10.80-dev1_amd64.deb
```

**Pass:** payload contains `/usr/sbin/xrdp`, `/usr/sbin/xrdp-sesman`,
`/etc/xrdp/xrdp.ini`, `/etc/xrdp/sesman.ini`.

---

## Part D — Deploy, update, rollback

> Do `normal_config.md` first so `xorgxrdp`, XFCE, PAM and the `localhost:3389`
> listener already exist.

```bash
# First deploy (replaces stock xrdp binaries). Path matches the script's OUTDIR
# (./dist); the version is git-hash-tagged, so glob rather than hard-code it.
sudo apt-get install -y ./dist/xrdp-dev_*.deb
sudo systemctl daemon-reload                    # pick up the native units (Part Y #2)
sudo systemctl enable --now xrdp-sesman xrdp    # xrdp.service Requires xrdp-sesman.service
/usr/sbin/xrdp --version                        # confirm 0.10.80

# Update cycle: rebuild, bump Version:, rebuild .deb, --reinstall, restart.
# Rollback to stock:
sudo apt-get install -y --allow-downgrades xrdp=0.10.1-3.1+deb13u1
```

`conffiles` keeps `/etc/xrdp/*.ini` edits across upgrades.

---

## Part X — xorgxrdp must be version-matched (BREAKING; root cause)

*Background for the `dev_config.md` "before you connect" blocker. The fix lives
there; this is the why.*

**Symptom:** Xorg starts ("Display X11-N is working"), then the session dies with
`Can't connect to display server X11-N [No such file or directory]`
(`sesman/sesexec/session.c:1523`).

**Cause — upstream xrdp↔xorgxrdp socket-naming skew, NOT a feature change:**
- xrdp commit `c4727ad8` (matt335672, Feb 2026, *"Replace X11 display number with
  a display string"*) renamed the display socket `xrdp_display_<n>` →
  `xrdp_display_<displayname>` (e.g. `xrdp_display_X11-10`). It is in the branch
  base (`21d38d0c`), so it affects the **unmodified** dev branch too.
- sesexec exports `XRDP_SOCKET_PATH` (dir) + `XRDP_X11RDP_SOCKET` (basename)
  (`env.c:108`, `:424`) and connects to
  `<XRDP_SOCKET_PATH>/xrdp_display_<displayname>` (`session.c:1502`).
- Distro **xorgxrdp 0.10.2** reads `XRDP_SOCKET_PATH` (dir matches) but **ignores
  `XRDP_X11RDP_SOCKET`** and names the file `xrdp_display_<number>` →
  `…/xrdp_display_X11-10` (xrdp wants) ≠ `…/xrdp_display_10` (xorgxrdp made) → ENOENT.
- Matched xorgxrdp (HEAD; `set_sock_name()` in `module/rdpClientCon.c` honours
  `XRDP_X11RDP_SOCKET`) creates `xrdp_display_X11-10` and the session connects.

Verified by runtime + `git blame`; the skew is purely upstream socket-naming
modernization, independent of any feature work on this branch. The unix-socket
sesman (harmless *"Ignoring obsolete SCP port 3350"* log) is part of the same
0.10.x modernization, not the cause.

**Build & install a matched xorgxrdp (separate repo, NOT this tree):**
```bash
sudo apt-get install -y xserver-xorg-dev          # Xorg SDK (xorg-server.pc)
git clone https://github.com/neutrinolabs/xorgxrdp.git /workUpdateXorgXrdp
cd /workUpdateXorgXrdp
./bootstrap && ./configure --prefix=/usr && make -j"$(nproc)"
make install DESTDIR=/tmp/xxstage
#   DEBIAN/control: Package: xorgxrdp-dev / Version: 1:0.10.80+git<hash>
#                   Provides/Conflicts/Replaces: xorgxrdp
dpkg-deb --root-owner-group --build /tmp/xxstage \
    /work/dist/xorgxrdp-dev_0.10.80+git<hash>_amd64.deb
sudo apt-get install -y /work/dist/xorgxrdp-dev_*.deb
```

**.deb guard:** `scripts/build_dev_deb.sh` now emits `Breaks: xorgxrdp
(<< 1:0.10.80~)` + a versioned `Recommends:`, so a future dev xrdp `.deb` fails
loudly at install over an outdated xorgxrdp instead of breaking at login.

### X.1 GPU bare-metal: build xorgxrdp `--enable-glamor` (gotcha #3)

The plain `--prefix=/usr` build above has **no GLAMOR** (xorgxrdp default is
`--enable-glamor=no`). On a GPU bare-metal host that yields software rendering
(or a failed session). For hardware-accelerated rendering, build a second
xorgxrdp `.deb` with glamor:

```bash
# glamor build deps (in addition to xserver-xorg-dev)
sudo apt-get install -y libepoxy-dev libgbm-dev libegl-dev
cd /workUpdateXorgXrdp
make clean
./configure --prefix=/usr --enable-glamor      # prints "checking for gbm/epoxy ... yes"
make -j"$(nproc)"
# package as Package: xorgxrdp-dev, Version: 1:0.10.80+git<hash>+glamor,
#   Provides/Conflicts/Replaces: xorgxrdp,
#   Depends: xserver-xorg-core, libgbm1, libegl1, libepoxy0
```

- **Verify glamor is in:** `nm -D -u …/xrdpdev_drv.so | grep -c glamor` → `>0`
  (undefined `glamor_*` symbols, resolved by the X server's glamor at load time);
  a non-glamor build returns `0`.
- **Runtime is auto** — the shipped `/etc/X11/xrdp/xorg.conf` already sets
  `DRMDevice /dev/dri/renderD128`, `DRI3 "1"`,
  `DRMAllowList "amdgpu i915 msm radeon"`; no extra toggle. The target needs an
  accessible render node and a GPU driver in that allowlist.
- **NVIDIA:** not in the allowlist; the deb also ships
  `/etc/X11/xrdp/xorg_nvidia.conf` — point xrdp's Xorg config at it (or add
  `nvidia` to `DRMAllowList`).
- Tag the version `+glamor` so it never collides with the non-glamor `.deb` of
  the same commit.

---

## Part Y — Deploying on another machine: 3 host gotchas (NOT carried by the `.deb`)

These surfaced installing the dev `.deb` on a *fresh* host. None of them is a
regression in this tree's code; each is a host/build prerequisite the xrdp
package cannot ship. Verify all three on every new target.

| # | Symptom on the new host | Root cause | Fix |
|---|---|---|---|
| 1 | `X server could not be started`; `waitforx: Unable to open display :N`; `X server failed to start` | `/etc/X11/Xwrapper.config` default (`allowed_users=console`, elevated Xorg) blocks the **rootless** Xorg that xrdp launches with `-logfile`/`-config`. Owned by `xserver-xorg-legacy`, **not** by our `.deb`. | Set `allowed_users=anybody` and `needs_root_rights=no` (below). |
| 2 | `systemctl start xrdp` / `xrdp-sesman` fails; no `xrdp-sesman.service`; only a SysV-generated `xrdp.service` | dev `.deb` built **without** `--with-systemdsystemunitdir`, so it shipped `/etc/init.d/xrdp` instead of native units. | Rebuild with the flag (Part A.3); the `.deb` then ships both native units. |
| 3 | Session starts but software-rendered / slow / fails on a GPU box | xorgxrdp built **without** `--enable-glamor`. | Install the glamor `.deb` from Part X.1. |

**Gotcha #1 fix (run on the target as root):**
```bash
sudo sed -i 's/^allowed_users=.*/allowed_users=anybody/' /etc/X11/Xwrapper.config
grep -q '^needs_root_rights=' /etc/X11/Xwrapper.config \
  && sudo sed -i 's/^needs_root_rights=.*/needs_root_rights=no/' /etc/X11/Xwrapper.config \
  || echo 'needs_root_rights=no' | sudo tee -a /etc/X11/Xwrapper.config
# or: sudo dpkg-reconfigure xserver-xorg-legacy  (choose "Anybody"), then ensure needs_root_rights=no
```
`needs_root_rights=no` is required: an **elevated** Xorg rejects `-logfile` /
`-config` ("Invalid argument with elevated privileges"), so xrdp's rootless Xorg
must not be run setuid-root.

**Diagnostic** for #1 vs #3 — read the Xorg session log on the target,
`~/.xorgxrdp.<N>.log` (RDP user's home): missing/empty ⇒ `Xorg.wrap` refused
(gotcha #1); `module ABI … does not match` / `Failed to load module "xrdpdev"`
⇒ xorgxrdp built against a different Xorg version (rebuild xorgxrdp on/for the
target's Xorg; the prebuilt module is only portable to a matching server).

---

## Non-interactive acceptance checks

Validated headless / by code review — **not** part of the interactive run (which
lives in `dev_config.md`). Fill the feature-specific rows as the code lands per
the active `BACKLOG.md` item; the infra rows hold regardless.

| # | Check | Where | ACCEPT if |
|---|---|---|---|
| H1 | New pure-logic unit tests | `make check` | new logic tests pass; input validated for 0/neg/out-of-range |
| H2 | Message round-trip | `make check` | any new IPC field serializes/parses (value asserts, not bytes); bad value rejected |
| H3 | Service health | `ss`, `journalctl` | xrdp on `127.0.0.1:3389`; sesman on its unix socket (no TCP 3350) |
| H4 | Privilege & security boundary | code review | untrusted client data validated at each boundary; no auth/PAM/ownership/identity change |

---

## Appendix — one-shot headless verify

```bash
set -e
git submodule update --init
./bootstrap
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
            --libdir=/usr/lib/x86_64-linux-gnu \
            --with-systemdsystemunitdir=/lib/systemd/system
make -j"$(nproc)"
make check                       # unit tests + regression floor
echo "HEADLESS OK"
```
