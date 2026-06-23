# build_config.md — Build, package & deploy the dev xrdp (DPI-1), + headless checks

Non-interactive companion to `dev_config.md`. Everything here is run by an
agent/CI on the server: building this source tree (`xrdp 0.10.80`, branch
`fix/client-dpi-propagation`), packaging the `.deb`, deploying it over the
`normal_config.md` baseline, the version-matched **xorgxrdp** rebuild, and the
headless (`make check` / argv-inspection) acceptance checks.

> For the **interactive** RDP test (connect a client, observe desktop DPI) see
> **`dev_config.md`**. Do `normal_config.md` (stock baseline) first.

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
    --libdir=/usr/lib/x86_64-linux-gnu
make -j"$(nproc)"
```

Optional features need extra `-dev` packages (`--enable-fuse`, `--enable-rfxcodec`,
`--enable-painter`, …); none are needed for DPI-1.

**Pass:** `make -j` exits 0.

---

## Part B — Headless verification (no RDP client)

### B.1 Unit tests

```bash
make check ; echo "exit=$?"
```

**Pass:** `exit=0`. Baseline suite totals (must not regress):

```text
libcommon 157   libipm 35   libxrdp 13   memtest 1   "XRDP daemon" 26
```

After DPI-1, the new tests must appear and pass:
- DPI calc helper (PRD §10.1): `2160/392 → 139`, `1440/392 → 93`, `1080/286 → 96`;
  invalid for `2160/0`, `0/392`, `2160/-1`, `dpi<50`, `dpi>400`.
- libipm SCP/EICP create-session round-trip incl. the new DPI field.

### B.2 Static/style gate (matches CI)

```bash
sudo apt-get install -y astyle cppcheck
astyle --options=astyle_config.as --dry-run "xrdp/*.c" "sesman/*.c" "libipm/*.c"
```

### B.3 Headless end-to-end (plumbing + fallback only)

```bash
xfreerdp3 /v:127.0.0.1:3389 /u:tester /p: /size:3840x2160 /cert:ignore &
pgrep -a Xorg                          # read the actual X server argv
sudo grep -E 'client DPI|Starting X server' /var/log/xrdp-sesman.log | tail
```

> `xfreerdp3` validates the **plumbing**, the **96-DPI/fallback path**, and
> **regression** (normal client stays 96, session starts). It does **not**
> reliably emit `TS_MONITOR_ATTRIBUTES` physical size, so the real "139 DPI"
> case must be checked interactively with **mstsc** (see `dev_config.md`).

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
Maintainer: puruiwang6 <puruiwang6@gmail.com>
Installed-Size: ${KB}
Depends: libc6, libssl3, libpam0g, libx11-6, libxfixes3, libxrandr2, libpixman-1-0
Conflicts: xrdp
Replaces: xrdp
Provides: xrdp
Breaks: xorgxrdp (<< 1:0.10.80~)
Section: net
Priority: optional
Description: xrdp built from source (devel 0.10.80) with client-DPI propagation
 Local test build of the DPI-1 feature. Not for production.
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
# First deploy (replaces stock xrdp binaries)
sudo apt-get install -y ./tmp/xrdp-dev_0.10.80-dev1_amd64.deb
sudo systemctl restart xrdp xrdp-sesman        # or start the daemons directly
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

**Cause — upstream xrdp↔xorgxrdp socket-naming skew, NOT a DPI change:**
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

Verified by runtime + `git blame`; no DPI commit (`480c596e`, `40455218`) touches
the socket-naming/connect code. The unix-socket sesman (harmless *"Ignoring
obsolete SCP port 3350"* log) is part of the same 0.10.x modernization, not the cause.

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

---

## Already-verified acceptance (non-interactive)

These DPI-1 acceptance criteria are validated headless / by code review — they are
**not** part of the interactive run (which lives in `dev_config.md`):

| # | Check | Where | ACCEPT if |
|---|---|---|---|
| H1 | DPI calc unit tests | `make check` | helper returns 139/93/96; invalid for 0/neg/out-of-range |
| H2 | libipm round-trip | `make check` | DPI field serializes/parses on SCP+EICP; bad value rejected |
| H3 | Service health | `ss`, `journalctl` | xrdp on `127.0.0.1:3389`; sesman on its unix socket (no TCP 3350) |
| H4 | Privilege boundary | code review | DPI consumed after `env_set_user` drop; no auth/PAM/ownership change |

**Why no Xvnc check:** DPI-1 only edits `prepare_xorg_xserver_params()` /
`xorg_params_contain_dpi()`; the Xvnc param builder is untouched (verifiable from
the `480c596e` diff), so Xvnc is unaffected by construction. A live Xvnc check
also isn't runnable here (no `Xvnc` binary) and stock `sesman.ini [Xvnc]` already
ships a static `-dpi 96` that a live check would misread as a leak.

---

## Appendix — one-shot headless verify

```bash
set -e
git submodule update --init
./bootstrap
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
            --libdir=/usr/lib/x86_64-linux-gnu
make -j"$(nproc)"
make check                       # H1, H2, regression
echo "HEADLESS OK"
```
