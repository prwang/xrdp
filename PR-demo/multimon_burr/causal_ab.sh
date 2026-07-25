#!/bin/bash
# Causal A/B for the dual-monitor AVC444 drag-ghost mechanism (see README).
#
# Hypothesis under test: ghosts = the client's 1-2px metablock fringe blit
# exposing 4K-monitor plane bytes that PRIMARY-monitor frames overwrote in
# the SHARED capture shmem (both monitors write their planes at offset 0
# with different geometry).
#
#   variant idleB:   primary monitor produces ZERO frames during the drag
#                    (xfce4-panel SIGSTOPped, nothing else on the primary;
#                    planes start clean via a full xrefresh)
#                    -> prediction: NO ghosts on the 4K screen.
#   variant activeB: same, plus an xclock -update 1 on the primary creating
#                    steady small damage during the drag
#                    -> prediction: ghosts on the 4K screen return.
#
# Both variants run in ONE login, each with its own pre-drag baseline pair.
# Analysis is restricted to the 4K region, so the xclock (on the primary)
# never pollutes the oracle. Exit codes: 0 = hypothesis-consistent
# (idleB clean AND activeB ghosted), 2 = refuted, 3 = inconclusive.
set -u
SU=${KEYTEST_USER:-tester}
SX=/var/run/xrdp/1000/Xauthority
HOST=${KEYTEST_HOST:-127.0.0.1:3389}
CD=:97
OUT=/tmp/mmburr-causal-${LAYOUT:-owner}
mkdir -p "$OUT"; chmod 1777 "$OUT"; rm -f "$OUT"/*.png 2>/dev/null

# LAYOUT=owner (default): primary on top at +594+0, 4K below at +0+1440.
# LAYOUT=flip: 4K on TOP at +0+0 (surface origin 0,0), primary below at
# +594+2400 — isolates whether the ghosts depend on the 4K monitor's
# nonzero session offset (both ghosting layouts so far had one; single
# monitor at origin is clean).
LAYOUT=${LAYOUT:-owner}
P_W=2560; P_H=1440
S_W=3840; S_H=2400
if [ "$LAYOUT" = flip ]; then
    S_X=0;   S_Y=0
    P_X=594; P_Y=2400
else
    P_X=594; P_Y=0
    S_X=0;   S_Y=1440
fi
CANVAS_W=3840; CANVAS_H=3840

cleanup(){
    pkill -9 -f xfreerdp3 2>/dev/null
    pkill -9 -f "Xorg $CD" 2>/dev/null
}
trap cleanup EXIT

# ---- client Xorg (dummy) + two monitors, owner layout ---------------------
pkill -9 -f "Xorg $CD" 2>/dev/null; sleep 1
ML=$(cvt $CANVAS_W $CANVAS_H 60 2>/dev/null | grep Modeline | sed 's/Modeline //' | tr -d '"')
MODENAME=$(echo "$ML" | awk '{print $1}')
MLREST=$(echo "$ML" | cut -d' ' -f2-)
cat > "$OUT/xorg.conf" <<EOF
Section "ServerFlags"
    Option "AutoAddDevices" "false"
    Option "DontVTSwitch" "true"
EndSection
Section "Device"
    Identifier "dummy"
    Driver "dummy"
    VideoRam 262144
EndSection
Section "Monitor"
    Identifier "mon"
    HorizSync 5.0-1000.0
    VertRefresh 5.0-200.0
    Modeline "$MODENAME" $MLREST
EndSection
Section "Screen"
    Identifier "screen"
    Device "dummy"
    Monitor "mon"
    DefaultDepth 24
    SubSection "Display"
        Depth 24
        Modes "$MODENAME"
        Virtual $CANVAS_W $CANVAS_H
    EndSubSection
EndSection
EOF
setsid Xorg $CD -config "$OUT/xorg.conf" -noreset -logfile "$OUT/xorg.log" \
    </dev/null >/dev/null 2>&1 &
for i in $(seq 1 20); do DISPLAY=$CD xrandr >/dev/null 2>&1 && break; sleep 0.5; done
DISPLAY=$CD xrandr --newmode "$MODENAME" $MLREST 2>/dev/null
DISPLAY=$CD xrandr --addmode DUMMY0 "$MODENAME" 2>/dev/null
DISPLAY=$CD xrandr --output DUMMY0 --mode "$MODENAME" 2>/dev/null
DISPLAY=$CD xrandr --setmonitor primary ${P_W}/677x${P_H}/381+${P_X}+${P_Y} DUMMY0 2>/dev/null
DISPLAY=$CD xrandr --setmonitor sub4k ${S_W}/1016x${S_H}/635+${S_X}+${S_Y} none 2>/dev/null

# ---- cold login ------------------------------------------------------------
pkill -9 -f xfreerdp3 2>/dev/null
sudo -u $SU pkill -u $SU -TERM xfce4-session 2>/dev/null; sleep 2
sudo -u $SU pkill -u $SU -KILL -f 'xfce4-session|Xorg :' 2>/dev/null
for i in $(seq 1 25); do pgrep -f 'Xorg :1[0-9]' >/dev/null || break; sleep 1; done
sleep 3
setsid env DISPLAY=$CD xfreerdp3 /v:"$HOST" /u:$SU /p: /multimon \
    /gfx:AVC444 /cert:ignore /log-level:WARN </dev/null >"$OUT/rdp.log" 2>&1 &
sleep 8
for r in 1 2 3; do
    fw=$(DISPLAY=$CD xdotool search --name FreeRDP 2>/dev/null | head -1)
    [ -n "$fw" ] && { DISPLAY=$CD xdotool windowactivate "$fw" 2>/dev/null; sleep 0.4;
                      DISPLAY=$CD xdotool key --window "$fw" Return 2>/dev/null; }
    sleep 5
done
sleep 6
SD=$(pgrep -a Xorg | grep -oE ':1[0-9]+' | head -1)
[ -z "$SD" ] && { echo "FAIL: no session"; exit 1; }
echo "== session $SD =="
sess(){ sudo -u $SU env DISPLAY=$SD XAUTHORITY=$SX "$@"; }
grab_client(){ ffmpeg -hide_banner -loglevel error -f x11grab \
    -video_size ${CANVAS_W}x${CANVAS_H} -i "$CD.0" -frames:v 1 -y "$1" 2>/dev/null; }
grab_sess(){ sess ffmpeg -hide_banner -loglevel error -f x11grab \
    -video_size ${CANVAS_W}x${CANVAS_H} -i "$SD.0" -frames:v 1 -y "$1" 2>/dev/null; }

# ---- mover window on the 4K ------------------------------------------------
sess pkill -u $SU qterminal 2>/dev/null; sleep 1
sess setsid qterminal </dev/null >/dev/null 2>&1 &
qw=""
for i in $(seq 1 15); do sleep 1;
    qw=$(sess xdotool search --class qterminal 2>/dev/null | tail -1)
    [ -n "$qw" ] && break
done
[ -z "$qw" ] && { echo "FAIL: no qterminal"; exit 1; }
QW=900; QH=700; STEP=24
sess xdotool windowsize "$qw" $QW $QH 2>/dev/null
HY=$((S_Y + 500))

# freeze the panel for the WHOLE experiment: the only primary damage source
# is then our xclock, giving controlled B-frame injection per variant
sess pkill -STOP -u $SU xfce4-panel 2>/dev/null
echo "panel: $(sess ps -o stat= -C xfce4-panel 2>/dev/null | tr -d ' ')"

drag_pass(){ # $1 = from-x, $2 = to-x (sweep at fixed HY)
    local x=$1 to=$2
    sess xdotool windowmove "$qw" $x $HY 2>/dev/null; sleep 2
    if [ "$x" -lt "$to" ]; then
        while [ $x -lt $to ]; do x=$((x + STEP));
            sess xdotool windowmove "$qw" $x $HY 2>/dev/null; done
    else
        while [ $x -gt $to ]; do x=$((x - STEP));
            sess xdotool windowmove "$qw" $x $HY 2>/dev/null; done
    fi
    sleep 2.5
}

run_variant(){ # $1 = name, $2 = from-x, $3 = to-x
    local name=$1
    # start from consistent planes. ORDER MATTERS: a full-root xrefresh
    # damages BOTH monitors and the capture loop rotates, so the primary's
    # full-frame write can land AFTER the 4K's and pollute it again. The
    # final pre-drag frame must therefore be a 4K-ONLY full repaint.
    sess xrefresh 2>/dev/null
    sleep 2
    sess xrefresh -geometry ${S_W}x${S_H}+${S_X}+${S_Y} 2>/dev/null
    sleep 3
    grab_sess   "$OUT/truth_base_$name.png"
    grab_client "$OUT/client_base_$name.png"
    drag_pass "$2" "$3"
    grab_sess   "$OUT/truth_$name.png"
    grab_client "$OUT/client_$name.png"
}

# variant 1: primary fully idle
run_variant idleB $((S_X + 40)) $((S_X + S_W - QW - 60))

# variant 2: steady small primary damage (xclock second hand, 1 Hz)
sess setsid xclock -update 1 -geometry 300x300+1400+500 </dev/null >/dev/null 2>&1 &
sleep 3
run_variant activeB $((S_X + S_W - QW - 60)) $((S_X + 40))
sess pkill -u $SU xclock 2>/dev/null
sess pkill -CONT -u $SU xfce4-panel 2>/dev/null

# ---- verdict: ghost lines in the 4K region only ---------------------------
python3 - "$OUT" "$S_X" "$S_Y" <<'EOF'
import sys, os
import numpy as np
from PIL import Image
out = sys.argv[1]
sx, sy = int(sys.argv[2]), int(sys.argv[3])
S = (sx, sy, sx + 3840, sy + 2400)
TOL = 48
def load(p): return np.asarray(Image.open(p).convert('RGB')).astype(int)
def ghosts(name):
    b = np.abs(load(f"{out}/truth_base_{name}.png")
               - load(f"{out}/client_base_{name}.png")).max(axis=2) > TOL
    mask = b.copy()
    for dy in (-2, -1, 0, 1, 2):
        for dx in (-2, -1, 0, 1, 2):
            mask |= np.roll(np.roll(b, dy, 0), dx, 1)
    hot = (np.abs(load(f"{out}/truth_{name}.png")
                  - load(f"{out}/client_{name}.png")).max(axis=2) > TOL) & ~mask
    x1, y1, x2, y2 = S
    reg = hot[y1:y2, x1:x2]
    lines = 0
    for axis in (0, 1):
        score = reg.sum(axis=(0 if axis == 0 else 1))
        idx = np.nonzero(score > 120)[0]
        runs = 0
        prev = -10
        for i in idx:
            if i > prev + 1:
                runs += 1
            prev = i
        lines += runs
    return lines, int(reg.sum())
li, pi = ghosts("idleB")
la, pa = ghosts("activeB")
print(f"idleB   (primary frozen):        ghost lines={li:2d}  hot px={pi}")
print(f"activeB (primary xclock 1Hz):    ghost lines={la:2d}  hot px={pa}")
if li == 0 and la > 0:
    print("VERDICT: hypothesis-CONSISTENT (cross-monitor shmem pollution)")
    sys.exit(0)
if li > 0:
    print("VERDICT: REFUTED — ghosts form without any primary frames")
    sys.exit(2)
print("VERDICT: inconclusive (activeB failed to ghost; stronger generator needed)")
sys.exit(3)
EOF
rc=$?
echo "rc=$rc"; exit $rc
