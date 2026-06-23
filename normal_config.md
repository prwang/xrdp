> **Role of this document.** This is the **stock-package baseline** on
> Debian 13 (trixie): it installs the distribution `xrdp` + `xorgxrdp` + XFCE and
> brings up a working RDP-to-XFCE session on `127.0.0.1:3389` for `user1`. It is
> the **regression control** and the **BEFORE state** (96 DPI) for the DPI-1
> feature, see **`build_config.md`** (build, package, deploy, headless checks)
> and **`dev_config.md`** (the interactive RDP test). Both assume this baseline is
> already in place — in particular `xorgxrdp`, a separate Xorg module **not** built
> from this tree (and which must be version-matched — see `build_config.md` Part X).
>
> Package names and config below are Debian 13; the linked manpages are the Ubuntu
> renderings of the same `xrdp.ini(5)` / `sesman.ini(5)` / `pam_succeed_if(8)`.

Below is the clean second-VM procedure, split into validation gates. It preserves the physical headed session because it does **not** touch `~user1/.xsession`, `~user1/.xinitrc`, display-manager config, or `user1`’s password.

The key controls are:

* `xrdp.ini [Globals] port=tcp://127.0.0.1:3389` for localhost-only RDP. XRDP’s manpage says a bare port listens on all interfaces, while an interface-qualified port binds only there. ([Ubuntu Manpages][1])
* `sesman.ini EnableUserWindowManager=false` and `DefaultWindowManager=...` to prevent user-specific startup scripts from affecting XRDP; `DefaultWindowManager` is the sesman-controlled startup script. ([Ubuntu Manpages][2])
* PAM must include a successful credential-setting path; the previous `pam_deny`-only auth tail caused `pam_setcred failed`.

---

# Gate 0 — Starting assumptions

Run:

```bash
id user1
getent passwd user1
```

Pass condition:

```text
user1 exists
```

Do **not** run `passwd`, `chsh`, `chage`, or write anything into `/home/user1/.xsession`.

---

# Step 1 — Install XRDP, xorgxrdp, and XFCE

```bash
sudo apt update
sudo apt install -y xrdp xorgxrdp xfce4 xfce4-session dbus-x11
sudo adduser xrdp ssl-cert
```

## Gate 1 — Packages and service exist

```bash
dpkg -l xrdp xorgxrdp xfce4 xfce4-session dbus-x11 | awk '/^ii/ {print $2, $3}'
systemctl status xrdp --no-pager
```

Pass condition:

```text
xrdp installed
xorgxrdp installed
xfce4 installed
xfce4-session installed
dbus-x11 installed
xrdp service exists
```

`xorgxrdp` matters because the target session type is XRDP’s independent Xorg backend, not console sharing.

---

# Step 2 — Configure XRDP listener: localhost-only 3389

Edit:

```bash
sudoedit /etc/xrdp/xrdp.ini
```

In the **`[Globals]`** section, set:

```ini
port=tcp://127.0.0.1:3389
autorun=Xorg
```

Do not worry about later section lines such as:

```ini
port=-1
port=ask5900
port=ask3389
```

Those are backend/session-section ports, not the client-facing XRDP listener. The relevant `port=` is the one in `[Globals]`.

## Gate 2A — Verify only `[Globals]`

```bash
awk '
  /^\[/{section=$0}
  section=="[Globals]" && /^(port|autorun)=/ {print}
' /etc/xrdp/xrdp.ini
```

Pass condition:

```text
port=tcp://127.0.0.1:3389
autorun=Xorg
```

---

# Step 3 — Configure sesman to ignore physical/user session choices

Edit:

```bash
sudoedit /etc/xrdp/sesman.ini
```

In the **`[Globals]`** section, set:

```ini
ListenAddress=127.0.0.1
ListenPort=3350
EnableUserWindowManager=false
DefaultWindowManager=/etc/xrdp/startwm-xfce-user1-only.sh
```

`EnableUserWindowManager=false` is the important isolation control. With it disabled, sesman does not use the user’s home-directory startup script; it uses `DefaultWindowManager` instead. ([Ubuntu Manpages][2])

## Gate 3A — Verify exact sesman values

```bash
awk '
  /^\[/{section=$0}
  section=="[Globals]" && /^(ListenAddress|ListenPort|EnableUserWindowManager|DefaultWindowManager)=/ {print}
' /etc/xrdp/sesman.ini
```

Pass condition:

```text
ListenAddress=127.0.0.1
ListenPort=3350
EnableUserWindowManager=false
DefaultWindowManager=/etc/xrdp/startwm-xfce-user1-only.sh
```

## Gate 3B — Verify no trailing space on `DefaultWindowManager`

This is one of the failure points from the first VM.

```bash
grep -n '^DefaultWindowManager=' /etc/xrdp/sesman.ini | cat -A
```

Pass condition:

```text
DefaultWindowManager=/etc/xrdp/startwm-xfce-user1-only.sh$
```

Fail condition:

```text
DefaultWindowManager=/etc/xrdp/startwm-xfce-user1-only.sh $
```

If there is a space before `$`, fix it.

---

# Step 4 — Create the XRDP-only XFCE launcher with its own log

Create:

```bash
sudo tee /etc/xrdp/startwm-xfce-user1-only.sh >/dev/null <<'EOF'
#!/bin/sh

LOG="$HOME/.cache/xrdp-startwm-xfce.log"
mkdir -p "$HOME/.cache"

exec >>"$LOG" 2>&1
set -x

echo "===== XRDP XFCE startup: $(date -Is) ====="
echo "USER=$USER"
echo "LOGNAME=$LOGNAME"
echo "HOME=$HOME"
echo "SHELL=$SHELL"
echo "PWD=$PWD"
id
env | sort

if [ "$USER" != "user1" ] && [ "$LOGNAME" != "user1" ]; then
    echo "Denied: USER=$USER LOGNAME=$LOGNAME"
    exit 1
fi

unset DBUS_SESSION_BUS_ADDRESS
unset SESSION_MANAGER

export XDG_SESSION_DESKTOP=xfce
export DESKTOP_SESSION=xfce
export XDG_CURRENT_DESKTOP=XFCE

echo "Checking commands..."
command -v xfce4-session || exit 20
command -v dbus-run-session || true

echo "Starting XFCE..."

if command -v dbus-run-session >/dev/null 2>&1; then
    exec dbus-run-session -- xfce4-session
else
    exec xfce4-session
fi
EOF

sudo chmod 0755 /etc/xrdp/startwm-xfce-user1-only.sh
```

## Gate 4A — Verify executable and exact path

```bash
ls -l /etc/xrdp/startwm-xfce-user1-only.sh
test -x /etc/xrdp/startwm-xfce-user1-only.sh && echo executable
```

Pass condition:

```text
executable
```

## Gate 4B — Verify XFCE command exists

```bash
command -v xfce4-session
command -v dbus-run-session
```

Pass condition:

```text
/usr/bin/xfce4-session
/usr/bin/dbus-run-session
```

---

# Step 5 — Configure XRDP-only passwordless PAM for exactly `user1`

Back up the original:

```bash
sudo cp /etc/pam.d/xrdp-sesman /etc/pam.d/xrdp-sesman.orig.$(date +%F-%H%M%S)
```

Replace `/etc/pam.d/xrdp-sesman`:

```bash
sudo tee /etc/pam.d/xrdp-sesman >/dev/null <<'EOF'
#%PAM-1.0
# XRDP-only passwordless login for exactly user1.
# Does not change user1's Unix password.

auth    required    pam_env.so
auth    required    pam_succeed_if.so quiet user = user1
auth    required    pam_permit.so

account required    pam_succeed_if.so quiet user = user1
account include     common-account

password required   pam_deny.so

session required    pam_loginuid.so
session include     common-session
EOF
```

`pam_succeed_if` is suitable here because it can match fields such as `user`, and the `user = string` condition requires an exact username match. ([Ubuntu Manpages][3]) The `pam_permit.so` line is intentionally present because XRDP later calls `pam_setcred`; without a module that succeeds in the auth stack, sesman can fail before the desktop launcher runs.

## Gate 5A — Verify PAM file

```bash
sudo cat /etc/pam.d/xrdp-sesman
```

Pass condition:

```text
auth required pam_succeed_if.so quiet user = user1
auth required pam_permit.so
account required pam_succeed_if.so quiet user = user1
```

Critical fail condition:

```text
auth sufficient pam_succeed_if.so quiet user = user1
auth required pam_deny.so
```

That was the pattern that produced:

```text
pam_setcred failed
Can't start PAM session
```

---

# Step 6 — Restart XRDP cleanly

```bash
sudo systemctl restart xrdp xrdp-sesman
```

Depending on packaging, `xrdp-sesman` may be part of the `xrdp` unit or a separate unit. If the second name is unknown, use:

```bash
sudo systemctl restart xrdp
```

## Gate 6A — Service health

```bash
systemctl status xrdp --no-pager
sudo journalctl -u xrdp -n 80 --no-pager
```

Pass condition:

```text
active/running
no fatal config parse error
```

---

# Step 7 — Validate listener exposure

```bash
sudo ss -ltnp | grep -E ':(3389|3350)\b'
```

Pass condition for the client-facing RDP listener:

```text
127.0.0.1:3389
```

Fail conditions for 3389:

```text
0.0.0.0:3389
[::]:3389
*:3389
```

For `3350`, these are acceptable because it is the internal sesman listener:

```text
127.0.0.1:3350
[::1]:3350
```

The required security property is that neither `3389` nor `3350` is bound to a non-loopback address. `sesman.ini` defines `ListenAddress` and `ListenPort`, with default sesman port `3350`. ([Ubuntu Manpages][2])

---

# Step 8 — Connect from Windows through SSH tunnel

Use a local tunnel:

```powershell
ssh -N -L 127.0.0.1:3389:127.0.0.1:3389 <ssh_user>@<server>
```

Then:

```powershell
mstsc /v:127.0.0.1:3389
```

XRDP login:

```text
Username: user1
Password: leave blank
Session: Xorg
```

If local Windows already has something on `3389`, use `3390` locally:

```powershell
ssh -N -L 127.0.0.1:3390:127.0.0.1:3389 <ssh_user>@<server>
mstsc /v:127.0.0.1:3390
```

---

# Gate 8 — First login debug checks

Keep this running during first login:

```bash
sudo tail -f /var/log/xrdp.log /var/log/xrdp-sesman.log
```

After login attempt:

```bash
sudo -u user1 cat /home/user1/.cache/xrdp-startwm-xfce.log
```

## Interpretation

| Observation                                   | Meaning                                                                     |
| --------------------------------------------- | --------------------------------------------------------------------------- |
| `pam_setcred failed`                          | PAM file is wrong; check Step 5                                             |
| `Can't start PAM session`                     | PAM file is wrong; check Step 5                                             |
| no `/home/user1/.cache/xrdp-startwm-xfce.log` | `DefaultWindowManager` was not executed; check Steps 3–4                    |
| log exists and reaches `Starting XFCE...`     | PAM and launcher are working; any remaining problem is inside XFCE/Xorgxrdp |
| `xfce4-session: not found`                    | missing package; repeat Step 1                                              |
| `Denied: USER=...`                            | XRDP login was not actually `user1`                                         |

---

# Gate 9 — Capture the baseline (BEFORE) DPI evidence

Once a session is up, record the stock behaviour. This is the **regression
control** that `dev_config.md` §4 (interactive scenarios) compares against.
Run inside the RDP session:

```bash
pgrep -a Xorg
xdpyinfo | grep resolution
xrandr --verbose | sed -n '/ connected/,/^$/p' | grep -iE 'connected|mm'
sudo grep -E 'Login screen monitor height|Starting X server' \
    /var/log/xrdp.log /var/log/xrdp-sesman.log | tail
```

Expected stock baseline (the behaviour DPI-1 will change for HiDPI clients):

```text
Xorg argv has NO `-dpi`
xdpyinfo: resolution: 96x96 dots per inch
xrandr: rdp0 connected ... 0mm x 0mm
xrdp.log may still report a client DPI on the login screen (~139), but it is
  NOT propagated to the session
```

Save this output; after deploying the dev build (`build_config.md`), the HiDPI
scenario (`dev_config.md` §4) must change to `-dpi 139/140` and `139x139`, while the
**Normal** scenario must stay ~`96x96`.

---

# Final invariants to preserve

Do not modify these on the second VM:

```text
/home/user1/.xsession
/home/user1/.xinitrc
/home/user1/.profile
/home/user1/.bashrc
user1's Unix password
GDM / LightDM / SDDM config
the physical desktop-session selection
```

The only place where XFCE is forced is:

```text
/etc/xrdp/startwm-xfce-user1-only.sh
```

That is what keeps the XRDP session isolated from the physical headed session.

[1]: https://manpages.ubuntu.com/manpages/jammy/man5/xrdp.ini.5.html "Ubuntu Manpage: xrdp.ini - Configuration file for xrdp(8)"
[2]: https://manpages.ubuntu.com/manpages/noble/en/man5/sesman.ini.5.html "Ubuntu Manpage: sesman.ini - Configuration file for xrdp-sesman(8)"
[3]: https://manpages.ubuntu.com/manpages/focal/man8/pam_succeed_if.8.html "Ubuntu Manpage: pam_succeed_if - test account characteristics"

