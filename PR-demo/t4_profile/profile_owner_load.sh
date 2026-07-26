#!/bin/sh
# T4 user-acceptance performance harness: profile the OWNER's real xfce
# session under their reference load — a decorated Thunar window
# (2500x1800) driven in a circular trace on the 3840x2400 screen, with
# the xfwm compositor as configured. Complements profile_drag.sh (the
# WM-less synthetic baseline). Run FROM the dev box.
#
# Requirements: the owner's RDP client must be ATTACHED (no client ->
# xrdp requests no capture -> nothing to measure); a VISIBLE Thunar
# window in the session; xdotool + linux-tools on the T4.
#
# Box assumptions (env-overridable): owner session on $DISP as ubuntu
# (Xauthority under /var/run/xrdp/1000), T4 ssh as $T4 with key $T4_KEY
# and passwordless sudo.
#
# Reference result (2026-07-26, xrdp 52099149 + xorgxrdp e7ecf30, nvenc,
# dual monitor, compositing=true), in pp of one core:
#   Xorg ~46 (pack loops 17.4, decode.avx2 7.9, compositor render 4.7,
#             move blit 3.2, rest ~12.8)
#   ffmpeg x2 ~22, xrdp ~13; pipeline total ~81.
# Lever accounting derived from this run (see BACKLOG): LC2-skip
# 22-26pp, pack vectorization 10-12pp, compositor off 6-8pp.
#
# Post-lever-1 result (2026-07-26, xorgxrdp ee1ec01, trace fully on the
# bottom 4K monitor — baseline orbit straddled both monitors, so only
# per-loop shares compare cleanly):
#   Xorg ~44 (pack loops 3.2 [was 17.4], decode.avx2 7.7 [steady],
#             pixman composite ~11, pixman blt ~13, rest ~9)
#   ffmpeg x2 ~23, xrdp ~14.
#   Throughput roughly doubled: the same 400-move trace that filled the
#   baseline window now drains in <15s (~30 moves/s vs ~13), so per-
#   second CPU stays flat while the drag gets smoother; compositor and
#   blit share grew with the higher frame rate and full-monitor window.
T4=${T4:-ubuntu@3.86.96.223}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
DISP=${DISP:-:10}
XAUTH=${XAUTH:-/var/run/xrdp/1000/Xauthority}
SECS=${SECS:-14}
WIN_W=${WIN_W:-2500}
WIN_H=${WIN_H:-1800}
# Orbit of the circular window trace (window top-left corner). Defaults
# put the window on the bottom 4K monitor of the owner's stacked dual-mon
# layout (rdp0 3840x2400+0+1440 below rdp1 2560x1440+594+0).
ORBIT_X=${ORBIT_X:-670}
ORBIT_Y=${ORBIT_Y:-1740}
ORBIT_R=${ORBIT_R:-280}
# Revolutions of the trace. Must outlast top (12s) + perf ($SECS); the
# lever-1 pipeline drains xdotool moves faster, so 10 revs no longer
# covers the perf window (a run recorded 5 samples — trace was done).
REVS=${REVS:-30}

set -e
ssh -i "$T4_KEY" "$T4" DISP="$DISP" XAUTH="$XAUTH" SECS="$SECS" \
    WIN_W="$WIN_W" WIN_H="$WIN_H" \
    ORBIT_X="$ORBIT_X" ORBIT_Y="$ORBIT_Y" ORBIT_R="$ORBIT_R" \
    REVS="$REVS" 'bash -s' <<'REMOTE'
set -e
export DISPLAY=$DISP XAUTHORITY=$XAUTH
if ! ss -tn state established '( sport = :3389 )' | grep -q 3389; then
    echo "ABORT: no RDP client attached — nothing will be encoded"
    exit 1
fi
XD="sudo -u ubuntu env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY"
XPID=$(ps -eo pid,args | grep "[X]org ${DISPLAY} " | awk '{print $1}')
WID=$($XD xdotool search --onlyvisible --class thunar | head -1)
if [ -z "$WID" ]; then
    echo "no visible Thunar; launching one"
    $XD nohup thunar >/dev/null 2>&1 &
    sleep 4
    WID=$($XD xdotool search --onlyvisible --class thunar | head -1)
fi
echo "XPID=$XPID WID=$WID"
$XD xdotool windowactivate --sync $WID
$XD xdotool windowsize --sync $WID $WIN_W $WIN_H
$XD bash -c '
python3 -c "
import math
for rev in range('"$REVS"'):
    for s in range(40):
        t = 2*math.pi*s/40
        print(int('"$ORBIT_X"'+'"$ORBIT_R"'*math.cos(t)),
              int('"$ORBIT_Y"'+'"$ORBIT_R"'*math.sin(t)))
" | while read x y; do xdotool windowmove '"$WID"' $x $y; sleep 0.02; done' \
    2>/tmp/owner_drag_err.log &
DRAG=$!
sleep 3
sudo perf record -q -o /tmp/xorg_owner.perf -p $XPID -F 499 -g \
    --call-graph fp -- sleep "$SECS" &
PERF=$!
echo "=== process split (owner load) ==="
top -b -d 2 -n 6 | grep -E "Xorg|xrdp$|ffmpeg" \
    | awk '{printf "%-8s %5s%%\n", $12, $9}'
wait $PERF
kill $DRAG 2>/dev/null || true
head -3 /tmp/owner_drag_err.log 2>/dev/null || true
echo "=== Xorg hotspots (owner load) ==="
sudo perf report -i /tmp/xorg_owner.perf --stdio --no-children \
    --percent-limit 3 | head -40
REMOTE
