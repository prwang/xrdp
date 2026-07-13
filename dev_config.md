# dev_config.md — Interactive smoke test for the custom dev `.deb`

**What this test is:** connect an RDP client to `localhost:3389`, log into the
**Xorg** desktop, and confirm the custom-built xrdp `.deb` (see `build_config.md`)
brings up a working session. This is the interactive companion to the headless
checks; the feature-specific acceptance criteria live in `BACKLOG.md`.

Everything non-interactive — build, package, deploy, `make check`, the xorgxrdp
rebuild, code-review checks — is in **`build_config.md`**; do that (and the
`normal_config.md` baseline) first.

---

## 1. Before you connect — clear all blockers

These gate **every** session and are specific to running a *custom-built* xrdp
`.deb` (all were real blockers during earlier testing):

- [ ] **Xorg wrapper allows non-console users** — `/etc/X11/Xwrapper.config`:
      ```ini
      allowed_users=anybody
      needs_root_rights=no
      ```
      Else: *"X server could not be started"* at login. (Takes effect next
      connection; no restart needed.) Owned by `xserver-xorg-legacy`, not by our
      `.deb` — see `build_config.md` Part Y #1.
- [ ] **Version-matched xorgxrdp installed** — else the desktop dies with
      *"Can't connect to display server"*. Check:
      `dpkg -l xorgxrdp-dev` shows a `0.10.80+git…` version (NOT the stock
      `xorgxrdp 0.10.2`). Build/why: `build_config.md` Part X.
- [ ] **Services up, listener loopback-only** —
      `pgrep xrdp xrdp-sesman` alive and `ss -ltn | grep 127.0.0.1:3389`.
- [ ] **Dev build deployed** — `/usr/sbin/xrdp --version` → `0.10.80`.

---

## 2. Connect

```bash
# 1. tunnel from your machine. <host> = the server running xrdp.
#    Skip this whole step if you are already on that host.
ssh -N -L 127.0.0.1:3389:127.0.0.1:3389 <host>
# 2. connect. mstsc and xfreerdp3 both work for a basic session; use whichever
#    the feature under test needs (some client capabilities are mstsc-only —
#    note them in BACKLOG.md when relevant).
mstsc /v:127.0.0.1:3389      # user: tester  /  password: (blank)  /  session: Xorg
```

`tester` is the passwordless RDP user for this container (`user1` in the
`normal_config.md` baseline).

---

## 3. Observe — session smoke test

**In the session** — as `tester`, e.g. in qterminal:

```bash
xrandr                                # outputs present, expected resolution
xdpyinfo | grep -E 'dimensions|resolution'
```

**On the server** — as the operator (root). `tester` has no sudo, so this is
**not** run in the session; read the logs directly on the host:

```bash
grep -E 'Starting X server|session|error' \
    /var/log/xrdp.log /var/log/xrdp-sesman.log | tail
```

**Pass:** the desktop comes up, `xrandr` reports the expected output/resolution,
and the logs show a clean session start with no errors.

---

## 4. Feature-specific interactive checks

Record the feature's own accept/reject scenarios in `BACKLOG.md` as the spec
lands, and run them here against the deployed dev `.deb`. Keep the **regression**
expectation in mind: a normal (~96-DPI) client and a client with missing/invalid
metadata must both still start a session unchanged — see `normal_config.md` for
the stock baseline to compare against.
