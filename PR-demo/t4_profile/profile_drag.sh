#!/bin/sh
# T4 Xorg hotspot profiler under a reproducible drag load (2026-07-26).
# Run FROM the dev box. Opens a 4K AVC444 session on the T4 as the
# throwaway `tester` user (password in /root/.tester_cred ON THE T4,
# never printed), drives a scripted 2000x1000-window drag with xdotool,
# and perf-records the session Xorg.
#
# Box assumptions (env-overridable): T4 ssh reachable as $T4 with key
# $T4_KEY and passwordless sudo; xdotool + linux-tools installed on T4;
# /etc/xrdp/wm1.sh lets non-ubuntu users exec ~/.xsession (tester's is a
# bare busy xterm; no WM -- xdotool moves the window directly, so the
# damage pattern is the moving window content, without xfce compositor
# amplification); local xvfb-run + xfreerdp3.
#
# Reference result (2026-07-26, xrdp 52099149 + xorgxrdp e7ecf30, nvenc):
#   process split during drag:  ffmpeg ~21-27%  Xorg ~18%  xrdp ~13-15%
#   inside Xorg: pack loops 36.5%, avc444_decode_row.avx2 17.8%,
#                fbBlt (window move blit, X core) 12.2%, glyphs 2.9%
T4=${T4:-ubuntu@3.86.96.223}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
PORT=${PORT:-23393}
SECS=${SECS:-12}

set -e
ssh -f -o ExitOnForwardFailure=yes -i "$T4_KEY" -L "$PORT":127.0.0.1:3389 \
    "$T4" sleep 300
TPW=$(ssh -i "$T4_KEY" "$T4" 'sudo cat /root/.tester_cred')
nohup xvfb-run -a -s "-screen 0 3900x2500x24" xfreerdp3 \
    /v:127.0.0.1:"$PORT" /size:3840x2400 /gfx:AVC444 /cert:ignore \
    /sec:tls /u:tester /p:"$TPW" >/tmp/t4_profile_client.log 2>&1 &
CLIENT=$!
sleep 25
ssh -i "$T4_KEY" "$T4" SECS="$SECS" 'bash -s' <<'REMOTE'
set -e
export DISPLAY=:11 XAUTHORITY=/var/run/xrdp/1001/Xauthority
XPID=$(ps -eo pid,args | grep "[X]org :11" | awk '{print $1}')
WID=$(sudo DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY \
      xdotool search --class xterm | head -1)
sudo DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY \
    xdotool windowsize $WID 2000 1000
sudo DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY bash -c '
for pass in 1 2 3 4 5 6; do
  for x in $(seq 100 40 1800); do
    xdotool windowmove '"$WID"' $x $((100 + pass * 150)); sleep 0.02
  done
  for x in $(seq 1800 -40 100); do
    xdotool windowmove '"$WID"' $x $((175 + pass * 150)); sleep 0.02
  done
done' >/dev/null 2>&1 &
sleep 2
echo "=== process split during drag ==="
top -b -d 2 -n 5 | grep -E "Xorg|xrdp$|ffmpeg" \
    | awk '{printf "%-8s %5s%%\n", $12, $9}'
sudo perf record -q -o /tmp/xorg_drag.perf -p $XPID -F 499 -g \
    --call-graph fp -- sleep "$SECS"
sudo perf report -i /tmp/xorg_drag.perf --stdio --no-children \
    --percent-limit 2 | head -40
REMOTE
kill $CLIENT 2>/dev/null || true
