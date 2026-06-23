# dev_config.md — Interactive DPI-1 test

**What this test is:** connect an RDP client to `localhost:3389`, log into the
**Xorg** desktop, and confirm the session DPI tracks the client monitor's
physical size (the DPI-1 feature). Everything non-interactive — build, package,
deploy, `make check`, the xorgxrdp rebuild, code-review checks — is in
**`build_config.md`**; do that (and `normal_config.md`) first.

> **Status:** DPI-1 is **not implemented yet** (see `BACKLOG.md`). Today every
> scenario shows the **BEFORE** state (no `-dpi`, ~96 DPI). After implementation,
> HiDPI must show `-dpi 139 or 140`; the other rows must stay unchanged.

---

## 1. Before you connect — clear all blockers

These gate **every** session (both were real blockers during testing):

- [ ] **Xorg wrapper allows non-console users** — `/etc/X11/Xwrapper.config`:
      ```ini
      allowed_users=anybody
      needs_root_rights=no
      ```
      Else: *"X server could not be started"* at login. (Takes effect next
      connection; no restart needed.)
- [ ] **Version-matched xorgxrdp installed** — else the desktop dies with
      *"Can't connect to display server"*. Check:
      `dpkg -l xorgxrdp-dev` shows a `0.10.80+git…` version (NOT the stock
      `xorgxrdp 0.10.2`). (Build/why: `build_config.md` Part X.)
- [ ] **Services up, listener loopback-only** —
      `pgrep xrdp xrdp-sesman` alive and `ss -ltn | grep 127.0.0.1:3389`.
- [ ] **Dev build deployed** — `/usr/sbin/xrdp --version` → `0.10.80`.

---

## 2. Connect

```bash
# 1. tunnel from your machine. <host> = the server running xrdp.
#    Skip this whole step if you are already on that host.
ssh -N -L 127.0.0.1:3389:127.0.0.1:3389 <host>
# 2. connect — USE mstsc: it sends physical monitor size (mm); xfreerdp3 does not,
#    so the HiDPI cases only work from mstsc.
mstsc /v:127.0.0.1:3389      # user: tester  /  password: (blank)  /  session: Xorg
```

`tester` is the passwordless RDP user for this container (`user1` in the
`normal_config.md` baseline).

---

## 3. Observe

**In the session** — as `tester`, e.g. in qterminal:

```bash
xdpyinfo | grep resolution                            # the live DPI
pgrep -a Xorg | tr ' ' '\n' | grep -A1 -x -- -dpi     # the -dpi argv, if any
```

**On the server** — as the operator (root). `tester` has no sudo, so this is
**not** run in the session; read the logs directly on the host:

```bash
grep -E 'Login screen monitor height|client DPI|Starting X server' \
    /var/log/xrdp.log /var/log/xrdp-sesman.log | tail
```

**Today (BEFORE):** `xdpyinfo` shows ~`96x96` and the `-dpi` grep prints nothing
— that is the expected current PASS (DPI-1 not implemented yet).

---

## 4. Scenarios — run each, check accept/reject

DPI value may be the rounded nearby integer (139 or 140) per PRD §6.1.

| Scenario | How | ACCEPT (AFTER DPI-1) | REJECT |
|---|---|---|---|
| **HiDPI** | mstsc, 3840×2160 HiDPI monitor | `Xorg … -dpi 139/140` **and** `xdpyinfo` matches | `xdpyinfo` 96, or no `-dpi`, or wrong value |
| **Normal** | mstsc / xfreerdp3, ~96-DPI monitor | no enlargement; `xdpyinfo ≈ 96` | session > 96 (regression) |
| **Admin override** | add `param=-dpi` / `param=144` to `[Xorg]` in `/etc/xrdp/sesman.ini`, restart sesman, reconnect HiDPI | exactly one `-dpi`, **144** (admin wins) | duplicate `-dpi`, or admin value overridden |
| **Invalid metadata** | client with no/zero physical size (e.g. `xfreerdp3` windowed) | session starts; no `-dpi` (never `-dpi 0/1/10000`) | bad `-dpi`, or session fails to start |

**BEFORE (today):** HiDPI shows 96 / no `-dpi` (the regression DPI-1 fixes);
Normal and Invalid already pass; Admin-override already works via stock config.

> After the **Admin override** scenario, remove the `param=-dpi`/`param=144`
> lines from `[Xorg]` and restart sesman before re-running HiDPI, or the admin
> value will mask the client DPI.
