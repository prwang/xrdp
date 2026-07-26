#!/bin/sh
# Bring up an OFFSCREEN dual-monitor RDP session on the T4 that reproduces
# the owner's layout — 2560x1440 (top, +594+0) stacked on 3840x2400
# (bottom 4K, +0+1440), virtual screen 3840x3840 — with no human client
# attached. Runs ENTIRELY on the dev box per CLAUDE.md "T4 test
# methodology": local Xorg+dummy client display with two ACTIVE outputs
# (fake `xrandr --setmonitor` monitors are inactive and FreeRDP's
# XRRGetMonitors(get_active=1) ignores them — learned 2026-07-26), local
# H264-capable xfreerdp3 with /multimon, ssh -L forward to the T4's
# loopback RDP port, login as ubuntu (owner-equivalent account).
#
# NOTE: dummy-driver client X validated on the old T4; first run on this
# dev box should verify Xorg+dummy starts in this container.
T4=${T4:-$(cat /root/.t4_host 2>/dev/null)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
[ -z "$T4" ] && { echo "ABORT: set T4=user@host or /root/.t4_host"; exit 1; }
RDP_USER=${RDP_USER:-ubuntu}
CRED_FILE=${RDP_PASS_FILE:-/root/.ubuntu_cred}
LPORT=${TUNNEL_PORT:-33890}
CLI=${CLI:-:99}
MON0=${MON0:-2560x1440+594+0}
MON1=${MON1:-3840x2400+0+1440}
VSIZE=${VSIZE:-3840x3840}

set -e
t4() { ssh -i "$T4_KEY" "$T4" "$@"; }

if ! ss -tln 2>/dev/null | grep -q ":$LPORT "; then
    ssh -i "$T4_KEY" -f -N -o ExitOnForwardFailure=yes \
        -L "$LPORT:127.0.0.1:3389" "$T4"
fi

# local client X server: Xorg + dummy driver, two VirtualHeads so both
# monitors are ACTIVE RandR monitors that FreeRDP announces via /multimon
if ! DISPLAY=$CLI xrandr --listactivemonitors 2>/dev/null \
        | grep -q "Monitors: 2"; then
    cat > /tmp/xorg-dummy.conf <<'EOF'
Section "ServerFlags"
    Option "AutoAddDevices" "false"
    Option "DontVTSwitch" "true"
EndSection
Section "Device"
    Identifier "dummy"
    Driver "dummy"
    VideoRam 262144
    Option "VirtualHeads" "2"
EndSection
Section "Monitor"
    Identifier "mon0"
EndSection
Section "Screen"
    Identifier "screen0"
    Device "dummy"
    Monitor "mon0"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Virtual 3840 3840
    EndSubSection
EndSection
EOF
    pkill -9 -x Xorg 2>/dev/null || true
    pkill -9 -x Xvfb 2>/dev/null || true
    sleep 1
    setsid Xorg "$CLI" -config /tmp/xorg-dummy.conf -noreset -nolisten tcp \
        -logfile /tmp/xorg99.log </dev/null >/dev/null 2>&1 &
    sleep 4
    geom() { echo "$1" | sed 's/[x+]/ /g'; }
    set -- $(geom "$MON0")
    M1=$(cvt -r "$1" "$2" 60 | grep Modeline | sed 's/Modeline //;s/"[^"]*"//')
    P0="+$3+$4"; MODE0="${1}x$2"
    set -- $(geom "$MON1")
    M2=$(cvt -r "$1" "$2" 60 | grep Modeline | sed 's/Modeline //;s/"[^"]*"//')
    P1="+$3+$4"; MODE1="${1}x$2"
    DISPLAY=$CLI xrandr --newmode own0 $M1
    DISPLAY=$CLI xrandr --newmode own1 $M2
    DISPLAY=$CLI xrandr --addmode DUMMY0 own0
    DISPLAY=$CLI xrandr --addmode DUMMY1 own1
    DISPLAY=$CLI xrandr --fb "$VSIZE" \
        --output DUMMY0 --mode own0 --pos "${P0#+}" \
        --output DUMMY1 --mode own1 --pos "${P1#+}"
fi
echo "=== client RandR monitors ==="
DISPLAY=$CLI xrandr --listactivemonitors

# cold login as the owner-equivalent account
pkill -9 -x xfreerdp3 2>/dev/null || true
t4 "pkill -TERM -u $RDP_USER xfce4-session" 2>/dev/null || true
sleep 3

# credential via env + /args-from: never in a process list or log
PW=$(t4 "sudo cat $CRED_FILE")
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$LPORT" "/u:$RDP_USER" "/p:$PW" \
                        "/multimon" "/gfx:AVC444" "/cert:ignore" \
                        "/log-level:WARN")
setsid env DISPLAY=$CLI RDPARGS="$RDPARGS" \
    xfreerdp3 /args-from:env:RDPARGS \
    </dev/null >/tmp/offscreen_login.log 2>&1 &
unset PW RDPARGS
sleep 16
fw=$(DISPLAY=$CLI xdotool search --name FreeRDP 2>/dev/null | head -1)
if [ -z "$fw" ]; then
    echo "FAIL: no FreeRDP window (login failed)"
    tail -5 /tmp/offscreen_login.log
    exit 1
fi
SD=$(t4 'pgrep -a -u $(id -u) -x Xorg' | grep -oE ' :[0-9]+ ' | head -1 \
     | tr -d ' ')
if [ -z "$SD" ]; then
    echo "FAIL: no $RDP_USER Xorg session on the T4"
    exit 1
fi
echo "=== server-side session ==="
echo "SESSION_DISPLAY=$SD"
t4 "DISPLAY=$SD XAUTHORITY=/var/run/xrdp/\$(id -u)/Xauthority xrandr --listmonitors"
echo "=== xrdp encoder mode (latest login) ==="
t4 "sudo grep -E 'probe OK|Matched .* mode' /var/log/xrdp.log | tail -3"
