#!/bin/bash
# Keystroke-driven end-to-end colour test (the smoke-gate workhorse).
#
# Fresh-LOGIN xfreerdp session on Xvfb :99 -> fullscreen colorkey.sh in the
# tester session -> press r/g/b/w twice each through the RDP client -> after
# every keypress, screenshot the CLIENT framebuffer and assert the screen
# centre shows that key's colour. A withheld/stale frame shows the PREVIOUS
# colour => "LAG". Prints one "ok"/"LAG" line per keypress.
#
# Box assumptions (documented per PR-demo policy, env-overridable):
#   tester account, empty password, xrdp on 127.0.0.1:3389, Xvfb on :99.
set -u
SU=${KEYTEST_USER:-tester}
SX=/var/run/xrdp/1000/Xauthority
HOST=${KEYTEST_HOST:-127.0.0.1:3389}
CLI=${KEYTEST_CLIENT_DISPLAY:-:99}
# Session size. MUST be exercised at more than one size: a resolution-
# dependent encoder failure (ffmpeg probesize analysis window) once passed
# every 1920x1080 run while freezing every 1024x768 (mstsc) login.
SIZE=${KEYTEST_SIZE:-1920x1080}
SW=${SIZE%x*}
SH=${SIZE#*x}
D=$(cd "$(dirname "$0")" && pwd)
OUT=/tmp/ab
mkdir -p "$OUT"
chmod 1777 "$OUT"   # colorkey.sh (running as tester) appends keylog.txt here

# client-side X server for xfreerdp
# Client framebuffer size; must be >= the session size. Overridable so a
# large non-16-aligned session (e.g. a 4K window resize) can be reproduced.
CLIENT_SIZE=${KEYTEST_CLIENT_SIZE:-1920x1080}
if ! DISPLAY=$CLI xdotool getdisplaygeometry 2>/dev/null | grep -q "^${CLIENT_SIZE%x*} ${CLIENT_SIZE#*x}$"; then
    pkill -9 -f "Xvfb $CLI" 2>/dev/null; sleep 1
    setsid Xvfb "$CLI" -screen 0 "${CLIENT_SIZE}x24" </dev/null >/dev/null 2>&1 &
    sleep 2
fi

# end any existing tester session + client so this is a cold login
pkill -9 -f xfreerdp3 2>/dev/null
sudo -u $SU pkill -u $SU -TERM xfce4-session 2>/dev/null; sleep 2
sudo -u $SU pkill -u $SU -KILL -f 'xfce4-session|Xorg :' 2>/dev/null
for i in $(seq 1 25); do pgrep -f 'Xorg :1[0-9]' >/dev/null || break; sleep 1; done
sleep 3

# Credential: empty by default (dev box). On boxes where the tester
# account has a real password, point KEYTEST_PASS_FILE at a root-owned
# credential file; the secret rides an env var into /args-from so it
# never appears in the process list, shell history or logs.
PW=""
if [ -n "${KEYTEST_PASS_FILE:-}" ]; then
    PW=$(sudo cat "$KEYTEST_PASS_FILE")
fi
# one argument per line (that is how /args-from splits its input)
RDPARGS=$(printf '%s\n' "/v:$HOST" "/u:$SU" "/p:$PW" "/size:$SIZE" \
                        "/gfx:AVC444" "/cert:ignore" "/log-level:WARN")
setsid env DISPLAY=$CLI RDPARGS="$RDPARGS" \
    xfreerdp3 /args-from:env:RDPARGS </dev/null >$OUT/keytest_login.log 2>&1 &
unset PW RDPARGS
sleep 8
fw=""
for r in 1 2 3; do
    fw=$(DISPLAY=$CLI xdotool search --name FreeRDP 2>/dev/null | head -1)
    [ -n "$fw" ] && { DISPLAY=$CLI xdotool windowactivate "$fw" >/dev/null 2>&1;
                      sleep 0.4;
                      DISPLAY=$CLI xdotool key --window "$fw" Return >/dev/null 2>&1; }
    sleep 5
done
sleep 6
fw=$(DISPLAY=$CLI xdotool search --name FreeRDP 2>/dev/null | head -1)
[ -z "$fw" ] && { echo "FAIL: no FreeRDP window (login failed)"; exit 1; }

SD=$(pgrep -a Xorg | grep -oE ':1[0-9]+' | head -1)
[ -z "$SD" ] && { echo "FAIL: no fresh Xorg session"; exit 1; }
echo "session display=$SD client=$CLI"
sess(){ sudo -u $SU env DISPLAY=$SD XAUTHORITY=$SX "$@"; }

# fullscreen terminal running the colour-key app
sess pkill -u $SU qterminal 2>/dev/null; sleep 1
sess setsid qterminal -e bash "$D/colorkey.sh" </dev/null >/dev/null 2>&1 &
qw=""
for i in $(seq 1 15); do
    sleep 1
    qw=$(sess xdotool search --class qterminal 2>/dev/null | tail -1)
    [ -n "$qw" ] && break
done
[ -z "$qw" ] && { echo "FAIL: no terminal"; exit 1; }
# dismiss any polkit prompt (e.g. colord on fresh login) — it floats over
# the screen centre and would be read instead of the terminal colour
for i in 1 2; do
    pw=$(sess xdotool search --name '^Authenticate$' 2>/dev/null | head -1)
    [ -z "$pw" ] && break
    sess xdotool key --window "$pw" Escape >/dev/null 2>&1
    sleep 1
done
sess xdotool windowactivate --sync "$qw" >/dev/null 2>&1
sess xdotool key --window "$qw" F11 >/dev/null 2>&1
sleep 3

shot(){ ffmpeg -hide_banner -loglevel error -f x11grab -video_size "$CLIENT_SIZE" \
        -i "$CLI.0" -frames:v 1 -y "$1" 2>/dev/null; }
# sample the centre of the SESSION-sized window (top-left of the client
# display), not of the full client framebuffer
classify(){ python3 - "$1" "$SW" "$SH" <<'EOF'
import sys, numpy as np
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
cx, cy = int(sys.argv[2]) // 2, int(sys.argv[3]) // 2
a = np.asarray(im.crop((cx-120, cy-120, cx+120, cy+120))).reshape(-1, 3).mean(0)
names = {'red': (178, 24, 24), 'green': (24, 178, 24), 'blue': (24, 24, 178),
         'white': (229, 229, 229), 'black': (20, 20, 20)}
best = min(names, key=lambda n: sum((a[i]-names[n][i])**2 for i in range(3)))
print(best)
EOF
}

declare -A NAME=( [r]=red [g]=green [b]=blue [w]=white )
fail=0; i=0
for k in r g b w r g b w; do
    i=$((i+1))
    DISPLAY=$CLI xdotool key --window "$fw" "$k"
    sleep 1.5
    shot "$OUT/keytest_$i.png"
    got=$(classify "$OUT/keytest_$i.png")
    want=${NAME[$k]}
    if [ "$got" = "$want" ]; then
        echo "press $i key=$k want=$want got=$got  ok"
    else
        echo "press $i key=$k want=$want got=$got  LAG"
        fail=1
    fi
done

# colour-edge fidelity after settle (FR-PROC-7 §8): narrow red/blue stripes,
# screenshot well past the deferred-aux window (one aux encode ~15ms; 2s is
# generous). The fraction of strongly-saturated pixels in the centre crop
# collapses if chroma is stuck at 4:2:0. smoke.sh asserts the threshold.
DISPLAY=$CLI xdotool key --window "$fw" e
sleep 2
shot "$OUT/keytest_edge.png"
python3 - "$OUT/keytest_edge.png" "$SW" "$SH" <<'EOF'
import sys, numpy as np
from PIL import Image
im = Image.open(sys.argv[1]).convert('RGB')
cx, cy = int(sys.argv[2]) // 2, int(sys.argv[3]) // 2
a = np.asarray(im.crop((cx-200, cy-100, cx+200, cy+100))).astype(int)
rb = np.abs(a[:, :, 0] - a[:, :, 2])
print("EDGE_FIDELITY %.3f" % float((rb > 110).mean()))
EOF
exit $fail
