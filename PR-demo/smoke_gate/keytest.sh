#!/bin/bash
# Keystroke-driven end-to-end colour test (the smoke-gate workhorse).
#
# Architecture (CLAUDE.md "T4 test methodology"): EVERYTHING client-side
# runs on THIS dev box — Xvfb, the H264-capable xfreerdp3, screenshots,
# classification — reaching the T4's loopback-bound RDP port through an
# ssh -L forward. The test account is `ubuntu` (the owner-equivalent
# session; no special test users, no session-policy edits); its RDP
# credential is fetched from root-owned /root/.ubuntu_cred ON the T4 at
# use time and never printed or stored locally. Session-side actions
# (colour-key terminal, window placement) are single ssh commands; the
# colour-key app is a persistent checksum-gated install on the T4.
#
# Flow: fresh RDP login -> full-screen colorkey.sh in the session ->
# press r/g/b/w twice each through the client -> after every keypress,
# screenshot the CLIENT framebuffer and assert the screen centre shows
# that key's colour ("LAG" otherwise) -> colour-EDGE fidelity after
# settle (FR-PROC-7 §8).
#
# TARGET (added 2026-07-29). SMOKE_TARGET=t4 (default) is the flow above.
# SMOKE_TARGET=pod runs the SAME gate against a bisect-fleet pod instead,
# reached on its host loopback port with no tunnel, session-side commands
# issued through kubectl exec, and the log read with kubectl logs. That
# exists because the gate must run against whatever pair is actually
# deployed (BACKLOG #45 E1) and the T4 is not always up; it is the same
# assertions against a package-installed xrdp + gfx.toml, which is what
# E1 asks for. Say which target produced a result -- a pod pass is not a
# T4 pass, and the T4 is the representative old-CPU box.
set -u
TARGET=${SMOKE_TARGET:-t4}
T4=${T4:-$(cat /root/.t4_host 2>/dev/null)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
NS=${SMOKE_NS:-bisect-matrix}
ARM=${SMOKE_ARM:-arm-r}
if [ "$TARGET" = pod ]; then
    SU=${KEYTEST_USER:-probe444}
    CRED_FILE=${KEYTEST_PASS_FILE:-/root/.oracle_cred}
    LPORT=${SMOKE_PORT:-40017}
    POD=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
          -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)
    [ -z "$POD" ] && { echo "ABORT: no running pod for $ARM"; exit 1; }
else
    [ -z "$T4" ] && { echo "ABORT: set T4=user@host or /root/.t4_host"; exit 1; }
    SU=${KEYTEST_USER:-ubuntu}
    CRED_FILE=${KEYTEST_PASS_FILE:-/root/.ubuntu_cred}
    LPORT=${KEYTEST_TUNNEL_PORT:-33890}
fi
# :98, NOT :99 — :99 belongs to the dual-monitor layout rig's Xorg+dummy
# (offscreen_owner_layout.sh). Sharing it broke the gate (2026-07-26):
# Xvfb silently failed to bind the busy display, the client mapped at the
# rig's first-monitor origin +594+0 on the 3840x3840 fb, and the fixed
# sample coordinates read black — a false FAIL with the server rendering
# perfectly.
CLI=${KEYTEST_CLIENT_DISPLAY:-:98}
# Session size. MUST be exercised at more than one size: a resolution-
# dependent encoder failure (ffmpeg probesize analysis window) once passed
# every 1920x1080 run while freezing every 1024x768 (mstsc) login.
SIZE=${KEYTEST_SIZE:-1920x1080}
SW=${SIZE%x*}
SH=${SIZE#*x}
D=$(cd "$(dirname "$0")" && pwd)
OUT=/tmp/ab
mkdir -p "$OUT"

if [ "$TARGET" = pod ]; then
    t4() { kubectl -n "$NS" exec "$POD" -- bash -lc "$*"; }
    TGT_NAME="$ARM/$POD"
else
    t4() { ssh -i "$T4_KEY" "$T4" "$@"; }
    TGT_NAME="$T4"
    # ssh tunnel to the T4's loopback RDP socket (pod mode needs none:
    # the arm's port is already bound on the host's loopback)
    if ! ss -tln 2>/dev/null | grep -q ":$LPORT "; then
        ssh -i "$T4_KEY" -f -N -o ExitOnForwardFailure=yes \
            -L "$LPORT:127.0.0.1:3389" "$T4"
    fi
fi

# persistent session-side colour-key app (checksum-gated install)
LSUM=$(md5sum "$D/colorkey.sh" | cut -d' ' -f1)
RSUM=$(t4 "md5sum /usr/local/bin/colorkey.sh 2>/dev/null | cut -d' ' -f1")
if [ "$LSUM" != "$RSUM" ]; then
    if [ "$TARGET" = pod ]; then
        kubectl -n "$NS" cp "$D/colorkey.sh" "$POD:/usr/local/bin/colorkey.sh"
        t4 "chmod 755 /usr/local/bin/colorkey.sh"
    else
        scp -q -i "$T4_KEY" "$D/colorkey.sh" "$T4:/tmp/colorkey.sh"
        t4 "sudo install -m 755 /tmp/colorkey.sh /usr/local/bin/colorkey.sh"
    fi
fi

# client-side X server for xfreerdp (local); geometry VERIFIED after
# start — a silent bind failure on a busy display must abort, not fall
# through onto whatever X happens to own it
CLIENT_SIZE=${KEYTEST_CLIENT_SIZE:-1920x1080}
if ! DISPLAY=$CLI xdotool getdisplaygeometry 2>/dev/null | grep -q "^${CLIENT_SIZE%x*} ${CLIENT_SIZE#*x}$"; then
    pkill -9 -f "Xvfb $CLI" 2>/dev/null; sleep 1
    setsid Xvfb "$CLI" -screen 0 "${CLIENT_SIZE}x24" </dev/null >/dev/null 2>&1 &
    sleep 2
    DISPLAY=$CLI xdotool getdisplaygeometry 2>/dev/null \
        | grep -q "^${CLIENT_SIZE%x*} ${CLIENT_SIZE#*x}$" \
        || { echo "FAIL: client X $CLI not at $CLIENT_SIZE (display busy?)"; exit 1; }
fi

# end any existing session + client so this is a cold login
pkill -9 -x xfreerdp3 2>/dev/null
if [ "$TARGET" = pod ]; then
    # a disposable fleet session, never the owner's: end it so this is a
    # cold login, then wait for its Xorg to go
    t4 "pkill -TERM -u $SU -x xterm; pkill -TERM -u $SU Xorg" 2>/dev/null
    t4 "for i in \$(seq 1 25); do pgrep -u $SU -x Xorg >/dev/null || break; \
        sleep 1; done" 2>/dev/null
else
    t4 "pkill -TERM -u $SU xfce4-session" 2>/dev/null; sleep 2
    t4 'for i in $(seq 1 25); do pgrep -u $(id -u) -x Xorg >/dev/null || break; sleep 1; done'
fi
sleep 2

# credential: root-owned file on the T4 -> env var -> /args-from (never in
# any process list, log or local file); one argument per line
if [ "$TARGET" = pod ]; then
    # the pod's probe credential lives on the HOST, root-only, and is
    # never copied into the pod or printed
    PW=$(cat "$CRED_FILE")
else
    PW=$(t4 "sudo cat $CRED_FILE")
fi
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$LPORT" "/u:$SU" "/p:$PW" \
                        "/size:$SIZE" "/gfx:AVC444" "/cert:ignore" \
                        "/log-level:WARN")
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

if [ "$TARGET" = pod ]; then
    SD=$(t4 "ls /tmp/.X11-unix/ | head -1 | sed 's/^X/:/'")
else
    SD=$(t4 'pgrep -a -u $(id -u) -x Xorg' | grep -oE ' :[0-9]+ ' \
         | head -1 | tr -d ' ')
fi
[ -z "$SD" ] && { echo "FAIL: no fresh Xorg session"; exit 1; }
echo "session display=$SD client=$CLI target=$TGT_NAME via :$LPORT"
if [ "$TARGET" = pod ]; then
    # three levels of quoting (kubectl exec -> bash -lc -> su -c), so the
    # inner payload is wrapped in DOUBLE quotes: callers pass single
    # quotes of their own (pkill -f 'xterm.*colorkey') and redirections.
    sess() { t4 "su -s /bin/bash $SU -c \"DISPLAY=$SD \
XAUTHORITY=/home/$SU/.Xauthority $*\""; }
else
    sess() { t4 "DISPLAY=$SD XAUTHORITY=/var/run/xrdp/\$(id -u)/Xauthority $*"; }
fi

# fullscreen terminal running the colour-key app. xterm, launched
# OVERSIZED at +0+0 instead of resized afterwards: xfwm's compositor on
# headless xrdp sessions repaints window moves but can freeze the
# on-screen image across window RESIZES (server-side framebuffer proven
# stale vs xdotool geometry, 2026-07-26).
sess "pkill -f 'xterm.*colorkey'" 2>/dev/null; sleep 1
COLS=$((SW / 6 + 10))
ROWS=$((SH / 13 + 10))
sess "setsid xterm -geometry ${COLS}x${ROWS}+0+0 -e bash /usr/local/bin/colorkey.sh </dev/null >/dev/null 2>&1 & sleep 0.1"
qw=""
for i in $(seq 1 15); do
    sleep 1
    qw=$(sess "xdotool search --class 'XTerm|xterm'" 2>/dev/null | tail -1)
    [ -n "$qw" ] && break
done
[ -z "$qw" ] && { echo "FAIL: no terminal"; exit 1; }
# dismiss any polkit prompt (e.g. colord on fresh login) — it floats over
# the screen centre and would be read instead of the terminal colour
for i in 1 2; do
    pw=$(sess "xdotool search --name '^Authenticate$'" 2>/dev/null | head -1)
    [ -z "$pw" ] && break
    sess "xdotool key --window $pw Escape" >/dev/null 2>&1
    sleep 1
done

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

# Synchronize on the DISPLAYED state, not a blind sleep: wait until the
# client actually shows colorkey's initial black screen. Window geometry
# is retried inside the loop (applying it right after launch races the
# WM's initial mapping; move only — never resize, see above), and the
# cold-login paint flood can back up the client for a few seconds; the
# press loop must measure steady-state responsivity, not login catch-up.
settled=0
for i in $(seq 1 20); do
    for w in $(sess "xdotool search --class 'XTerm|xterm'" 2>/dev/null); do
        sess "xdotool windowactivate --sync $w; xdotool windowmove --sync $w 0 0" \
            >/dev/null 2>&1
    done
    sleep 1
    shot "$OUT/keytest_settle.png"
    got_pre=$(classify "$OUT/keytest_settle.png")
    if [ "$got_pre" = "black" ]; then
        settled=1
        break
    fi
done
[ "$settled" = "1" ] || { echo "FAIL: colorkey screen never displayed"; exit 1; }

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
