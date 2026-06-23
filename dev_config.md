# dev_config.md — Build, package, deploy & test the dev xrdp (DPI-1)

Developer runbook for **building this source tree** (`xrdp 0.10.80`, branch
`fix/client-dpi-propagation`), packaging it as a **self-built `.deb`**, deploying
it over the stock baseline from `normal_config.md`, and **testing the DPI-1
feature** (client monitor DPI propagated to the Xorg session via `-dpi`).

> Relationship to `normal_config.md`: that doc sets up the **stock-package
> baseline** (the BEFORE state, 96 DPI) on `localhost:3389`. This doc replaces the
> stock `xrdp` binaries with a locally built `.deb` and verifies the AFTER state.
> `xorgxrdp` is a separate Xorg driver module, **not** in this tree — but the
> stock apt `xorgxrdp` (0.10.2) is **incompatible** with this dev branch and must
> be rebuilt/version-matched. See **Part 0.5** (read it before any interactive
> session — it is a hard blocker, unrelated to DPI).

## 0. Verified environment

Everything below was actually executed in this sandbox and is known to work:

| Fact | Value |
|---|---|
| OS | Debian GNU/Linux 13 (trixie), running as root (`sudo` present) |
| Source version | `xrdp 0.10.80` (`AC_INIT` in `configure.ac`) |
| Build | `bootstrap` + `configure` + `make -j32` → **OK** |
| Unit tests | `make check` → **exit 0**, all suites pass |
| Stage install | `make install DESTDIR=…` → **OK** (133 files) |
| Packaging | `dpkg-deb --build` → **OK** (`xrdp-dev_0.10.80-dev1_amd64.deb`) |
| Stock pkg in repo | `xrdp 0.10.1-3.1+deb13u1`, `xorgxrdp 1:0.10.2-1` |
| RDP client | `/usr/bin/xfreerdp3` (`freerdp3-x11 3.15`) |

> **Feature status:** DPI-1 is **not implemented yet** (see `BACKLOG.md`). On the
> current branch the test plan therefore reproduces the BEFORE state (no `-dpi`,
> 96 DPI). After implementing DPI-1, the same plan must show the AFTER state.

---

## Part 0.5 — CRITICAL environment prerequisites (learned during live GUI test)

Two environment issues block **every** interactive Xorg session on this dev
branch, independently of DPI. Both were hit during real RDP testing and cost
significant debugging; fix both before expecting a desktop.

### 0.5.1 Xorg wrapper must allow non-console users

**Symptom:** login authenticates, then *"Can't create session for user … —
X server could not be started"*; `xrdp-sesman.log` shows `waitforx: Unable to
open display :N` and no Xorg log is written.

**Cause:** Debian's `xserver-xorg-legacy` ships `/etc/X11/Xwrapper.config` with
`allowed_users=console`; xrdp sessions are not on a console, so the setuid
`Xorg.wrap` refuses to launch Xorg (*"Only console users are allowed to run the
X server"*).

**Fix** (environment, not a code change):
```ini
# /etc/X11/Xwrapper.config
allowed_users=anybody
needs_root_rights=no
```
`needs_root_rights=no` is required: when Xorg keeps root it rejects the
`-logfile`/`-config` args xrdp passes (*"Invalid argument -logfile with elevated
privileges"*). xorgxrdp's driver is fully virtual, so rootless Xorg is correct.
Takes effect on the next connection (no daemon restart needed).

### 0.5.2 xorgxrdp must be version-matched to the dev branch (BREAKING CHANGE)

*Supersedes the old "just keep apt's xorgxrdp" guidance.*

**Symptom:** Xorg starts ("Display X11-N is working"), the desktop briefly
initialises, then the session dies with
`Can't connect to display server X11-N [No such file or directory]`
(`sesman/sesexec/session.c:1523`).

**Cause — an upstream xrdp↔xorgxrdp socket-naming skew, NOT a DPI change:**
- xrdp commit **`c4727ad8`** (matt335672, Feb 2026, *"Replace X11 display number
  with a display string"*) renamed the Xorg display socket from
  `xrdp_display_<n>` to `xrdp_display_<displayname>` (e.g. `xrdp_display_X11-10`).
  It is in the branch base (`21d38d0c`), so it affects the **unmodified** dev
  branch too — not our work.
- sesexec exports the socket dir via `XRDP_SOCKET_PATH` and the basename via
  `XRDP_X11RDP_SOCKET` (`env.c:108`, `:424`), then connects to
  `<XRDP_SOCKET_PATH>/xrdp_display_<displayname>` (`session.c:1502`).
- Distro **xorgxrdp 0.10.2** predates this: it reads `XRDP_SOCKET_PATH` (so the
  *directory* matches, `/var/run/xrdp/<uid>`) but **ignores `XRDP_X11RDP_SOCKET`**
  and names the file `xrdp_display_<number>`. So `…/xrdp_display_X11-10` (xrdp
  wants) ≠ `…/xrdp_display_10` (xorgxrdp made) → ENOENT.
- A matched xorgxrdp (HEAD; `set_sock_name()` in `module/rdpClientCon.c` honours
  `XRDP_X11RDP_SOCKET`) creates `xrdp_display_X11-10` and the session connects.

Verified by runtime + `git blame`; no DPI commit (`480c596e`, `40455218`)
touches the socket-naming or connect code. The `[Globals] port=tcp://127.0.0.1`
listener and the unix-socket sesman (the harmless *"Ignoring obsolete SCP port
3350"* log) are part of the same 0.10.x modernization and are **not** the cause.

**Fix — build & install a version-matched xorgxrdp (separate repo, NOT this tree):**
```bash
sudo apt-get install -y xserver-xorg-dev          # Xorg SDK (provides xorg-server.pc)
git clone https://github.com/neutrinolabs/xorgxrdp.git /workUpdateXorgXrdp
cd /workUpdateXorgXrdp
./bootstrap && ./configure --prefix=/usr && make -j"$(nproc)"
# package as a .deb that Provides/Conflicts/Replaces xorgxrdp, git-tagged:
make install DESTDIR=/tmp/xxstage
#   DEBIAN/control: Package: xorgxrdp-dev / Version: 1:0.10.80+git<hash>
#                   Provides: xorgxrdp / Conflicts: xorgxrdp / Replaces: xorgxrdp
dpkg-deb --root-owner-group --build /tmp/xxstage \
    /work/dist/xorgxrdp-dev_0.10.80+git<hash>_amd64.deb
sudo apt-get install -y /work/dist/xorgxrdp-dev_*.deb
```
**Verify** the socket name flips (with an Xorg launched as the session user and
`XRDP_SOCKET_PATH=/var/run/xrdp/<uid>`, `XRDP_X11RDP_SOCKET=xrdp_display_X11-<n>`):
```bash
ls /var/run/xrdp/<uid>/xrdp_display_X11-*   # present  => matched, session works
ls /var/run/xrdp/<uid>/xrdp_display_[0-9]*  # present  => still the old 0.10.2 driver
```

### 0.5.3 The dev xrdp .deb now guards against 0.5.2

`scripts/build_dev_deb.sh` declares `Breaks: xorgxrdp (<< 1:0.10.80~)` plus a
versioned `Recommends:`. A future install of the dev xrdp `.deb` over an outdated
xorgxrdp now **fails loudly at install time** with a clear conflict, instead of
breaking silently at login. (The currently-installed dev `.deb` predates the
guard; it stays as-is — rebuild to apply the guard.)

---

## Part A — Build from source

### A.1 Install build dependencies (exact set verified)

```bash
sudo apt-get update
# toolchain
sudo apt-get install -y autoconf automake libtool pkg-config nasm gcc make
# xrdp build deps
sudo apt-get install -y \
    libpam0g-dev libssl-dev libx11-dev libxfixes-dev libxrandr-dev \
    libpixman-1-dev libxkbfile-dev libfuse3-dev
# unit-test frameworks (REQUIRED for `make check` to build all suites)
sudo apt-get install -y check libcmocka-dev
```

> `libcmocka-dev` is easy to miss: without it `tests/xrdp` fails to compile
> (`fatal error: cmocka.h`) and `make check` aborts even though every other suite
> passes. The DPI-1 helper unit tests will live in `tests/xrdp` (or
> `tests/common`), so this is mandatory for dev.

### A.2 Checkout submodules (required — they are git submodules)

```bash
git submodule update --init        # librfxcodec, libpainter
git submodule status               # both lines should start with a space, not '-'
```

### A.3 Bootstrap, configure, build

Use Debian-style paths so the build overlays the stock package cleanly:

```bash
./bootstrap
./configure \
    --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
    --libdir=/usr/lib/x86_64-linux-gnu
make -j"$(nproc)"
```

Optional features (need extra `-dev` packages): `--enable-fuse`,
`--enable-rfxcodec`, `--enable-painter`, `--enable-mp3lame`, `--enable-opus`.
None are needed for DPI-1; the minimal build above is sufficient.

**Pass condition:** `make -j` exits 0 (`BUILD OK`).

---

## Part B — Headless verification (no RDP client needed)

This is the part **CI / an agent can run unattended.**

### B.1 Unit tests

```bash
make check ; echo "exit=$?"
```

**Pass condition:** `exit=0`. Current baseline suite totals (must not regress):

```text
libcommon   157   libipm 35   libxrdp 13   memtest 1   "XRDP daemon" 26
```

After DPI-1 is implemented, the new tests must appear and pass:

- DPI calc helper (PRD §10.1): `2160/392 → 139`, `1440/392 → 93`,
  `1080/286 → 96`, and invalid cases `2160/0`, `0/392`, `2160/-1`, `dpi<50`,
  `dpi>400` → invalid.
- libipm SCP/EICP create-session serialization round-trip incl. the new DPI
  field (`tests/libipm`).

Run a single suite while iterating:

```bash
make -C tests/xrdp check && cat tests/xrdp/test-suite.log
make -C tests/libipm check
```

### B.2 Static/style gate (matches CI)

```bash
sudo apt-get install -y astyle cppcheck
astyle --options=astyle_config.as --dry-run "xrdp/*.c" "sesman/*.c" "libipm/*.c"
```

---

## Part C — Package the self-built `.deb`

### C.1 Stage-install into a packaging root

```bash
rm -rf /tmp/xrdp-deb
make install DESTDIR=/tmp/xrdp-deb
```

### C.2 Write packaging metadata and build the `.deb`

```bash
cd /tmp/xrdp-deb
mkdir -p DEBIAN
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
Section: net
Priority: optional
Description: xrdp built from source (devel 0.10.80) with client-DPI propagation
 Local test build of the DPI-1 feature. Not for production.
EOF
# Preserve admin edits to /etc/xrdp across upgrades:
find etc -type f | sed 's,^,/,' > DEBIAN/conffiles
cd /
dpkg-deb --root-owner-group --build /tmp/xrdp-deb \
    /tmp/xrdp-dev_0.10.80-dev1_amd64.deb
```

### C.3 Inspect

```bash
dpkg-deb -I /tmp/xrdp-dev_0.10.80-dev1_amd64.deb     # control + metadata
dpkg-deb -c /tmp/xrdp-dev_0.10.80-dev1_amd64.deb | grep -E 'sbin/xrdp|sesman'
```

**Pass condition:** `.deb` builds; payload contains `/usr/sbin/xrdp`,
`/usr/sbin/xrdp-sesman`, `/etc/xrdp/xrdp.ini`, `/etc/xrdp/sesman.ini`.
`Conflicts/Replaces/Provides: xrdp` lets it cleanly supersede the stock package.

---

## Part D — Deploy & update

> Do `normal_config.md` (stock baseline) **first** so `xorgxrdp`, XFCE, PAM, the
> `localhost:3389` listener and the working session already exist.

### D.1 First deploy (replace stock xrdp binaries, keep xorgxrdp)

```bash
sudo apt-get install -y ./tmp/xrdp-dev_0.10.80-dev1_amd64.deb   # pulls deps, replaces xrdp
# or: sudo dpkg -i /tmp/xrdp-dev_0.10.80-dev1_amd64.deb ; sudo apt-get -f install
sudo systemctl daemon-reload
sudo systemctl restart xrdp xrdp-sesman
dpkg -l | grep -E '^ii  (xrdp|xorgxrdp)'   # expect xrdp-dev + stock xorgxrdp
```

### D.2 Update cycle (after a code change)

```bash
# rebuild
make -j"$(nproc)"
# bump version (so apt sees an upgrade)
sed -i 's/^Version:.*/Version: 0.10.80-dev2/' /tmp/xrdp-deb/DEBIAN/control   # or regen Part C
rm -rf /tmp/xrdp-deb && make install DESTDIR=/tmp/xrdp-deb   # then redo C.2 with new version
sudo apt-get install -y --reinstall ./tmp/xrdp-dev_0.10.80-dev2_amd64.deb
sudo systemctl restart xrdp xrdp-sesman
```

`conffiles` means your edits in `/etc/xrdp/*.ini` survive the upgrade (dpkg
prompts on conflict). Verify the running binary is yours:

```bash
/usr/sbin/xrdp --version    # should report 0.10.80
```

### D.3 Rollback to stock

```bash
sudo apt-get install -y --allow-downgrades xrdp=0.10.1-3.1+deb13u1
sudo systemctl restart xrdp xrdp-sesman
```

---

## Part E — Test plan

Two tiers: **(E1) headless**, runnable by an agent/CI on the server; **(E2)
interactive**, run by you from a real RDP client to `localhost:3389`.

### E1 — Headless (server-side, no GUI)

1. **Unit tests** — Part B.1 (`make check` exit 0; DPI helper + libipm cases).
2. **Service smoke** — after deploy:
   ```bash
   systemctl is-active xrdp xrdp-sesman
   sudo ss -ltnp | grep ':3389'                  # RDP: 127.0.0.1 only (sesman is a unix socket, no TCP 3350)
   sudo journalctl -u xrdp -u xrdp-sesman -n 60 --no-pager   # no fatal parse errors
   ```
3. **Optional headless end-to-end (plumbing + fallback)** — drive an RDP login
   with the bundled client and inspect the spawned Xorg from the server:
   ```bash
   xfreerdp3 /v:127.0.0.1:3389 /u:user1 /p: /size:3840x2160 \
       /cert:ignore +auth-only:off &     # opens a session
   pgrep -a Xorg                          # read the actual X server argv
   sudo grep -E 'client DPI|Starting X server' /var/log/xrdp-sesman.log | tail
   ```
   **Scope/honesty note:** `xfreerdp3` drives login and lets us read the Xorg
   argv and logs, so it fully validates the **plumbing**, the **96-DPI / fallback
   path**, and **regression** (normal client stays 96, session still starts). It
   does **not** reliably emit `TS_MONITOR_ATTRIBUTES` *physical size in mm*, which
   is what produces a HiDPI value — so the true "139 DPI" case is an **E2
   (interactive, mstsc)** check, not headless.

### E2 — Interactive (you, from a client on `localhost:3389`)

Tunnel + connect as in `normal_config.md` Step 8 (`ssh -N -L
127.0.0.1:3389:127.0.0.1:3389 …`, then `mstsc /v:127.0.0.1:3389`, user `user1`,
blank password, session `Xorg`). Use **mstsc** (sends physical monitor size) for
the HiDPI cases. Inside each session collect evidence:

```bash
pgrep -a Xorg | tr ' ' '\n' | grep -A1 -x -- -dpi   # the -dpi argv, if any
xdpyinfo | grep resolution
sudo grep -E 'Login screen monitor height|client DPI|Starting X server' \
    /var/log/xrdp.log /var/log/xrdp-sesman.log | tail
```

Scenarios to run:

- **HiDPI**: connect from a 3840×2160 HiDPI monitor (mstsc).
- **Normal**: connect from a standard ~96-DPI monitor.
- **Admin override**: add `param=-dpi` / `param=144` to `[Xorg]` in
  `/etc/xrdp/sesman.ini`, restart, reconnect HiDPI.
- **Invalid metadata**: client with no/zero physical size (e.g. `xfreerdp3`
  windowed) — session must still start, no bad `-dpi`.

---

## Part F — Accept / Reject matrix

Maps each scenario to the required observation. (BEFORE = current branch / stock;
AFTER = once DPI-1 is implemented.) DPI value may be the rounded nearby integer
(139 or 140) per PRD §6.1.

| # | Scenario | Client / where | Expected (AFTER DPI-1) | ACCEPT if | REJECT if |
|---|---|---|---|---|---|
| F1 | DPI calc unit tests | headless `make check` | helper returns 139/93/96; invalid for 0/neg/out-of-range | all cases pass | any case wrong or missing |
| F2 | libipm round-trip | headless `make check` | DPI field serializes/parses on SCP+EICP | round-trip equal; bad value rejected | mismatch, parse error, or crash |
| F3 | Service health | headless | xrdp on `127.0.0.1:3389`; sesman on its **unix socket** (`/run/xrdp/sesman.socket`) — this build has **no TCP 3350** | both active, RDP loopback-only, sesman not on any TCP port | down, or 3389 bound to a non-loopback address |
| F4 | HiDPI client | E2 mstsc 3840×2160 | `Xorg … -dpi 139/140`; `xdpyinfo` 139/140 | `-dpi` present **and** xdpyinfo matches | xdpyinfo 96, or no `-dpi`, or wrong value |
| F5 | Normal 96-DPI client | E2 mstsc / xfreerdp3 | **no enlargement**; ~96×96 | xdpyinfo ≈ 96 | session > 96 (regression) |
| F6 | Invalid / missing phys size | E1 xfreerdp3 windowed | no client `-dpi`; session starts | session up, no `-dpi 0/1/10000` | bad `-dpi`, or session fails to start |
| F7 | Admin `-dpi` in sesman.ini | E2 mstsc HiDPI | exactly one `-dpi`, **admin value wins** | single `-dpi 144` | duplicate `-dpi`, or admin value overridden |
| F8 | Privilege boundary | code review + E1 | DPI consumed after `env_set_user` drop; no auth/PAM change | unchanged auth path; Xorg runs as user | any auth/ownership/ordering change |

**BEFORE state (current branch) acceptance:** F1/F2 are N/A (tests not written
yet); F3/F5/F6/F8 must already hold; F4 will show 96 DPI / no `-dpi` (the
documented regression this feature fixes); F7 (admin static `-dpi`) already works
via stock config.

> **Why no Xvnc row:** an earlier matrix had an "Xvnc backend unchanged" row. It
> was dropped — DPI-1 only edits `prepare_xorg_xserver_params()` /
> `xorg_params_contain_dpi()` (the Xvnc param builder is untouched, verifiable
> from the `480c596e` diff), so Xvnc is unaffected by construction. A live Xvnc
> check also isn't runnable here (no `Xvnc` binary; needs `tigervnc-standalone-
> server`), and stock `sesman.ini [Xvnc]` already ships a static `-dpi 96`, which
> a live check would misread as a DPI-1 leak. The "no Xorg-only-isolation
> regression" guarantee is covered by code review, not a backend connect.

---

## Appendix — One-shot headless verify script

```bash
set -e
git submodule update --init
./bootstrap
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
            --libdir=/usr/lib/x86_64-linux-gnu
make -j"$(nproc)"
make check                       # F1, F2, regression
echo "HEADLESS OK"
```
