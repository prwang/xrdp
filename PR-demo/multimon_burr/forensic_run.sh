#!/bin/bash
# Forensic capture for the dual-monitor AVC444 ghost: same dual layout and
# idle-primary protocol as causal_ab.sh, but a SHORT drag with the server's
# per-frame dumper enabled (XRDP_AVC444_DUMP -> conv NV12 + H.264 pair +
# damage rects per emitted frame), so the pipeline can be replayed offline.
# Requires the xrdp service to be running with XRDP_AVC444_DUMP set.
set -u
SU=${KEYTEST_USER:-tester}
SX=/var/run/xrdp/1000/Xauthority
HOST=${KEYTEST_HOST:-127.0.0.1:3389}
CD=:97
OUT=/tmp/mmburr-forensic
DUMP=${XRDP_AVC444_DUMP_DIR:-/tmp/avcdump}
mkdir -p "$OUT"; chmod 1777 "$OUT"; rm -f "$OUT"/*.png 2>/dev/null

P_W=2560; P_H=1440; P_X=594; P_Y=0
S_W=3840; S_H=2400; S_X=0;   S_Y=1440
CANVAS_W=3840; CANVAS_H=3840

cleanup(){
    pkill -9 -f xfreerdp3 2>/dev/null
    pkill -9 -f "Xorg $CD" 2>/dev/null
}
trap cleanup EXIT

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
sess pkill -STOP -u $SU xfce4-panel 2>/dev/null

# clean 4K planes as the last pre-drag frame, then position the window
sess xdotool windowmove "$qw" $((S_X + 200)) $HY 2>/dev/null; sleep 2
sess xrefresh 2>/dev/null; sleep 2
sess xrefresh -geometry ${S_W}x${S_H}+${S_X}+${S_Y} 2>/dev/null; sleep 3

# start dumping ONLY now: clear the dir so seq files = the drag window
rm -f "$DUMP"/* 2>/dev/null
grab_sess   "$OUT/truth_base.png"
grab_client "$OUT/client_base.png"

# SHORT drag: 30 steps of 24px
x=$((S_X + 200))
for i in $(seq 1 30); do
    x=$((x + STEP))
    sess xdotool windowmove "$qw" $x $HY 2>/dev/null
done
sleep 2.5
grab_sess   "$OUT/truth_after.png"
grab_client "$OUT/client_after.png"
sess pkill -CONT -u $SU xfce4-panel 2>/dev/null

echo "== dump frames =="
ls "$DUMP" | grep -c meta || true
du -sh "$DUMP"
