#!/bin/sh
# Bring up an OFFSCREEN tester session on the T4 that reproduces the
# owner's dual-monitor layout — 2560x1440 (top, +594+0) stacked on
# 3840x2400 (bottom 4K, +0+1440), virtual screen 3840x3840 — without
# any human client attached. Run FROM the dev box.
#
# How: Xvfb :99 at the virtual-screen size on the T4, two FAKE RandR
# monitors via `xrandr --setmonitor` matching the owner geometry, then
# a fresh xfreerdp3 /multimon login as tester (wm1.sh gives tester the
# same xfce stack as the owner, so drag workloads are representative).
# On success prints the session display, Xauthority path and server-
# side monitor list; the frame-accounting/demo harnesses then run with
#   DISP=<display> XAUTH=/var/run/xrdp/<uid>/Xauthority SESS_USER=tester
#
# Box assumptions (env-overridable): tester account with empty password
# (same as the smoke gate), xrdp on 127.0.0.1:3389, xfreerdp3 + Xvfb
# installed on the T4.
T4=${T4:-ubuntu@3.86.96.223}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
CLI=${CLI:-:99}
MON0=${MON0:-2560x1440+594+0}
MON1=${MON1:-3840x2400+0+1440}
VSIZE=${VSIZE:-3840x3840}
RDP_USER=${RDP_USER:-tester}
# root-owned credential file on the T4 (never printed; see /args-from)
RDP_PASS_FILE=${RDP_PASS_FILE:-/root/.tester_cred}

set -e
ssh -i "$T4_KEY" "$T4" CLI="$CLI" MON0="$MON0" MON1="$MON1" \
    VSIZE="$VSIZE" RDP_USER="$RDP_USER" RDP_PASS_FILE="$RDP_PASS_FILE" \
    'bash -s' <<'REMOTE'
set -e
geom() { echo "$1" | sed 's/[x+]/ /g'; }   # "WxH+X+Y" -> "W H X Y"

# client-side X server at the full virtual-screen size
if ! DISPLAY=$CLI xdotool getdisplaygeometry 2>/dev/null \
        | grep -q "^${VSIZE%x*} ${VSIZE#*x}$"; then
    pkill -9 -f "Xvfb $CLI" 2>/dev/null || true
    sleep 1
    setsid Xvfb "$CLI" -screen 0 "${VSIZE}x24" </dev/null >/dev/null 2>&1 &
    sleep 2
fi
# fake RandR monitors: xfreerdp3 /multimon announces one RDP monitor
# per RandR monitor, which is how the server ends up with the owner's
# stacked rdp0/rdp1 layout
DISPLAY=$CLI xrandr --delmonitor fake0 >/dev/null 2>&1 || true
DISPLAY=$CLI xrandr --delmonitor fake1 >/dev/null 2>&1 || true
set -- $(geom "$MON0")
DISPLAY=$CLI xrandr --setmonitor fake0 "$1/${1}x$2/$2+$3+$4" none
set -- $(geom "$MON1")
DISPLAY=$CLI xrandr --setmonitor fake1 "$1/${1}x$2/$2+$3+$4" none
echo "=== client RandR monitors ==="
DISPLAY=$CLI xrandr --listmonitors

# cold tester login (same teardown discipline as the smoke gate)
pkill -9 -f xfreerdp3 2>/dev/null || true
sudo -u "$RDP_USER" pkill -u "$RDP_USER" -TERM xfce4-session 2>/dev/null || true
sleep 2
sudo -u "$RDP_USER" pkill -u "$RDP_USER" -KILL -f 'xfce4-session|Xorg :' \
    2>/dev/null || true
for i in $(seq 1 25); do
    pgrep -u "$RDP_USER" -f 'Xorg :1[0-9]' >/dev/null || break
    sleep 1
done
sleep 2

# credential via env + /args-from: never in the process list or logs
PW=""
if [ -n "${RDP_PASS_FILE:-}" ]; then
    PW=$(sudo cat "$RDP_PASS_FILE")
fi
# one argument per line (that is how /args-from splits its input)
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:3389" "/u:$RDP_USER" "/p:$PW" \
                        "/multimon" "/gfx:AVC444" "/cert:ignore" \
                        "/log-level:WARN")
setsid env DISPLAY=$CLI RDPARGS="$RDPARGS" \
    xfreerdp3 /args-from:env:RDPARGS \
    </dev/null >/tmp/offscreen_login.log 2>&1 &
unset PW RDPARGS
sleep 8
for r in 1 2 3; do
    fw=$(DISPLAY=$CLI xdotool search --name FreeRDP 2>/dev/null | head -1)
    [ -n "$fw" ] && DISPLAY=$CLI xdotool key --window "$fw" Return \
        >/dev/null 2>&1
    sleep 5
done
sleep 5
fw=$(DISPLAY=$CLI xdotool search --name FreeRDP 2>/dev/null | head -1)
if [ -z "$fw" ]; then
    echo "FAIL: no FreeRDP window (login failed)"
    tail -5 /tmp/offscreen_login.log
    exit 1
fi
SD=$(sudo -u "$RDP_USER" pgrep -a -u "$RDP_USER" Xorg \
     | grep -oE ':[0-9]+' | head -1)
if [ -z "$SD" ]; then
    echo "FAIL: no tester Xorg session"
    exit 1
fi
UIDN=$(id -u "$RDP_USER")
XA=/var/run/xrdp/$UIDN/Xauthority
echo "=== server-side session ==="
echo "SESSION_DISPLAY=$SD"
echo "XAUTH=$XA"
sudo -u "$RDP_USER" env DISPLAY=$SD XAUTHORITY=$XA xrandr --listmonitors
echo "=== xrdp encoder mode (latest login) ==="
sudo grep -E "probe OK|Matched .* mode" /var/log/xrdp.log | tail -3
REMOTE
