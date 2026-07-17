#!/bin/bash
# Fresh-LOGIN reproduction (matches "logoff/login", not reconnect). Ends the
# tester session so the next xfreerdp connect is a cold login -> fresh Xorg +
# fresh gfx.toml + fresh encoder + fresh xorgxrdp backend.
set -u
SU=tester; SX=/var/run/xrdp/1000/Xauthority
DEPTH=$1; TF=$2; TAG=${3:-d${DEPTH}_${TF}}
python3 /tmp/ab/setcfg.py "$DEPTH" "$TF" >/dev/null
echo "=== FRESH LOGIN: async_depth=$DEPTH tail_flush=$TF ($TAG) ==="

# end any existing tester session + client
pkill -9 -f xfreerdp3 2>/dev/null
sudo -u $SU pkill -u $SU -TERM xfce4-session 2>/dev/null; sleep 2
sudo -u $SU pkill -u $SU -KILL -e -f 'xfce4-session|Xorg :' 2>/dev/null
for i in $(seq 1 25); do pgrep -f 'Xorg :1[0-9]' >/dev/null || break; sleep 1; done
sleep 3
echo "  session ended; Xorg present now: $(pgrep -af 'Xorg :1[0-9]' | head -1 || echo none)"

# cold login
setsid env DISPLAY=:99 xfreerdp3 /v:127.0.0.1:3389 /u:$SU /p: /size:1920x1080 \
    /gfx:AVC444 /cert:ignore /log-level:WARN </dev/null >/tmp/ab/login_$TAG.log 2>&1 &
sleep 8
for r in 1 2 3; do
    fw=$(DISPLAY=:99 xdotool search --name FreeRDP 2>/dev/null | head -1)
    [ -n "$fw" ] && { DISPLAY=:99 xdotool windowactivate "$fw" >/dev/null 2>&1; sleep 0.4; DISPLAY=:99 xdotool key --window "$fw" Return >/dev/null 2>&1; }
    sleep 5
done
sleep 6

SD=$(pgrep -a Xorg | grep -oE ':1[0-9]+' | head -1)
echo "  fresh session display=$SD"
[ -z "$SD" ] && { echo "  FAIL: no fresh Xorg"; exit 1; }
sess(){ sudo -u $SU env DISPLAY=$SD XAUTHORITY=$SX "$@"; }

# fullscreen terminal + colour fill
sess pkill -u $SU qterminal 2>/dev/null; sleep 1
sess setsid qterminal -e bash --norc </dev/null >/dev/null 2>&1 &
qw=""
for i in $(seq 1 15); do sleep 1; qw=$(sess xdotool search --class qterminal 2>/dev/null | tail -1); [ -n "$qw" ] && break; done
[ -z "$qw" ] && { echo "  FAIL: no terminal"; exit 1; }
sess xdotool windowactivate --sync "$qw" >/dev/null 2>&1
sess xdotool key --window "$qw" F11 >/dev/null 2>&1; sleep 1
sess xdotool type --window "$qw" "PS1=''; tput civis; clear"$'\n'; sleep 1
sess xdotool type --window "$qw" "bash /tmp/ab/fill.sh"$'\n'
sleep 5
ffmpeg -hide_banner -loglevel error -f x11grab -video_size 1920x1080 -i :99.0 -frames:v 1 -y /tmp/ab/login_$TAG.png 2>/dev/null
printf "  RESULT [%s]: " "$TAG"; python3 /tmp/ab/classify.py /tmp/ab/login_$TAG.png
