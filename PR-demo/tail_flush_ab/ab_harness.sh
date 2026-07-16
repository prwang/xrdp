#!/bin/bash
# Robust A/B: gfx.toml is re-read per CONNECTION (xrdp_wm.c), so we never restart
# xrdp -- we just reconnect a fresh client after changing config. The tester X
# session on :10 persists throughout. Each group runs 3 trials with a long 5s
# settle so a persistent tail withhold is distinguishable from render latency.
set -u
SU=tester; SD=:10; SX=/var/run/xrdp/1000/Xauthority
sess(){ sudo -u $SU env DISPLAY=$SD XAUTHORITY=$SX "$@"; }
grab(){ ffmpeg -hide_banner -loglevel error -f x11grab -video_size 1920x1080 -i :99.0 -frames:v 1 -y "$1" 2>/dev/null; }
classify(){ python3 /tmp/ab/classify.py "$1"; }

setup_terminal(){
    sess pkill -u $SU qterminal >/dev/null 2>&1; sleep 1
    sess xdotool key Escape >/dev/null 2>&1
    sess setsid qterminal -e bash --norc </dev/null >/dev/null 2>&1 &
    QWIN=""
    for i in $(seq 1 12); do sleep 1; QWIN=$(sess xdotool search --class qterminal 2>/dev/null | tail -1); [ -n "$QWIN" ] && break; done
    [ -z "$QWIN" ] && { echo "no terminal"; exit 1; }
    sess xdotool windowactivate --sync "$QWIN" >/dev/null 2>&1
    sess xdotool key --window "$QWIN" F11 >/dev/null 2>&1; sleep 1
    sess xdotool type --window "$QWIN" "PS1=''; tput civis; clear"$'\n'; sleep 1
}

in_session(){   # 0 if the client shows the session (not the login backdrop)
    grab /tmp/ab/pre.png
    python3 - /tmp/ab/pre.png <<'PY'
import sys,numpy as np
from PIL import Image
a=np.asarray(Image.open(sys.argv[1]).convert('RGB'));H,W,_=a.shape
c=a[H//2-150:H//2+150,W//2-150:W//2+150].reshape(-1,3).mean(0)
sys.exit(1 if (abs(c[0])<25 and abs(c[1]-48)<25 and abs(c[2]-87)<25) else 0)
PY
}

connect(){   # full reconnect retries; returns 0 if in session
    for attempt in 1 2 3 4 5; do
        pkill -9 -f xfreerdp3 >/dev/null 2>&1; sleep 2
        setsid env DISPLAY=:99 xfreerdp3 /v:127.0.0.1:3389 /u:$SU /p: /size:1920x1080 \
            /gfx:AVC444 /cert:ignore /log-level:WARN </dev/null >/tmp/ab/frdp.log 2>&1 &
        sleep 7
        # press Return several times to clear the xrdp login form
        for r in 1 2 3 4; do
            fwin=$(DISPLAY=:99 xdotool search --name FreeRDP 2>/dev/null | head -1)
            [ -n "$fwin" ] && { DISPLAY=:99 xdotool windowactivate "$fwin" >/dev/null 2>&1; sleep 0.3; DISPLAY=:99 xdotool key --window "$fwin" Return >/dev/null 2>&1; }
            sleep 3
            if in_session; then return 0; fi
        done
    done
    return 1
}

run_group(){
    local NAME=$1 DEPTH=$2 TF=$3
    echo "===== $NAME (async_depth=$DEPTH tail_flush=$TF) ====="
    python3 /tmp/ab/setcfg.py "$DEPTH" "$TF" >/dev/null
    if ! connect; then echo "  $NAME: FAIL(login)"; return; fi
    # re-fullscreen/clear in case reconnect changed focus
    sess xdotool windowactivate --sync "$QWIN" >/dev/null 2>&1
    for t in 1 2 3 4 5; do
        sess xdotool windowactivate --sync "$QWIN" >/dev/null 2>&1; sleep 0.3
        sess xdotool type --window "$QWIN" "bash /tmp/ab/fill.sh"$'\n'
        sleep 5.0                     # long settle: withhold is persistent
        grab /tmp/ab/shot_${NAME}_$t.png
        printf "  trial %d: " "$t"; classify /tmp/ab/shot_${NAME}_$t.png
        sess xdotool key --window "$QWIN" ctrl+c >/dev/null 2>&1; sleep 0.5
        sess xdotool type --window "$QWIN" "clear"$'\n'; sleep 1
    done
}

setup_terminal
run_group CONTROL 4 false
run_group SPAMMER 4 true
run_group ASYNCDEPTH1 1 false
run_group ASYNCDEPTH2 2 false
echo "AB2 DONE"
