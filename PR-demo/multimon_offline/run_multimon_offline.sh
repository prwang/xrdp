#!/bin/bash
# Offline dual-monitor AVC444 test: drive a real 2-monitor `xfreerdp /multimon`
# at 2x1024x768 against the deployed xrdp, and assert the SERVER took the
# multimonitor AVC444 path — monitorCount=2, ONE ffmpeg probe at the LARGEST
# single monitor (1024x768, NOT the 2048x768 virtual desktop), AVC444
# negotiated, and TWO surfaces created (one per monitor).
#
# This exercises the code in `feat(avc444): enable multimonitor` end to end.
# The deterministic CI backstop for the probe geometry is the unit test
# tests/xrdp/test_avc444_multimon.c; this harness is the live visual/behaviour
# check, per PR-demo policy.
#
# PREREQUISITES (run on an unguarded host — see README "Sandbox note"):
#   - deployed xrdp built from this branch (multimon), avc_mode="444"
#     (install the clean dev .deb per CLAUDE.md "Deployment"), OR set HOST to
#     an isolated instance.
#   - xserver-xorg-video-dummy installed (client-side 2-monitor X server).
#   - tester account, empty password (as in smoke_gate/keytest.sh).
#
# Box assumptions (documented per PR-demo policy, env-overridable):
set -u
D=$(cd "$(dirname "$0")" && pwd)
SU=${MM_USER:-tester}
HOST=${MM_HOST:-127.0.0.1:3389}
CLI=${MM_CLIENT_DISPLAY:-:95}
SRVLOG=${MM_SERVER_LOG:-/var/log/xrdp.log}
FRDP=${MM_XFREERDP:-xfreerdp3}
OUT=/tmp/mm_offline
mkdir -p "$OUT"

MARK=$(date '+%Y-%m-%dT%H:%M:%S')
echo "mark=$MARK host=$HOST client=$CLI"

# --- client-side X server with TWO 1024x768 monitors ---------------------
if ! DISPLAY=$CLI xrandr --query >/dev/null 2>&1; then
    echo "starting dummy Xorg on $CLI ..."
    setsid Xorg "$CLI" -config "$D/xorg-dummy-2mon.conf" -noreset \
        -logfile "$OUT/xorg.log" </dev/null >/dev/null 2>&1 &
    sleep 3
fi
DISPLAY=$CLI bash "$D/setup_monitors.sh" || {
    echo "ABORT: could not present 2 client monitors"; exit 1; }

# --- drive a multimon login ---------------------------------------------
pkill -9 -f "$FRDP" 2>/dev/null
sudo -u "$SU" pkill -u "$SU" -TERM xfce4-session 2>/dev/null; sleep 2
sudo -u "$SU" pkill -u "$SU" -KILL -f 'xfce4-session|Xorg :' 2>/dev/null
for i in $(seq 1 25); do pgrep -f 'Xorg :1[0-9]' >/dev/null || break; sleep 1; done
sleep 3

echo "connecting $FRDP /multimon ..."
setsid env DISPLAY=$CLI "$FRDP" /v:"$HOST" /u:"$SU" /p: /multimon \
    /gfx:AVC444 /cert:ignore /log-level:WARN \
    </dev/null >"$OUT/xfreerdp.log" 2>&1 &
sleep 8
for r in 1 2 3; do
    fw=$(DISPLAY=$CLI xdotool search --name FreeRDP 2>/dev/null | head -1)
    [ -n "$fw" ] && { DISPLAY=$CLI xdotool key --window "$fw" Return >/dev/null 2>&1; }
    sleep 5
done
sleep 6

# --- assert the SERVER took the multimon AVC444 path ---------------------
# only lines at/after MARK (full date+time compare, per the tail_flush smoke
# gate lesson about time-of-day-only marks matching stale lines)
since() { awk -v m="[$MARK" 'substr($1,1,length(m)) >= m' "$SRVLOG"; }

pass=1
check() { # <label> <regex> <min-count>
    local n; n=$(since | grep -cE "$2")
    if [ "$n" -ge "$3" ]; then echo "  ok   [$n] $1"; else
        echo "  FAIL [$n<$3] $1"; pass=0; fi
}
echo "== server-side assertions (since $MARK) =="
check "reset_graphics monitorCount 2"      "monitorCount 2"                     1
check "probe at single-monitor 1024x768"   "probing ffmpeg AVC444 .*1024x768"   1
check "AVC444 (ffmpeg) matched"            "Matched H264/AVC444 \(ffmpeg\)"     1
check "two surfaces mapped (id 0 and 1)"   "map surface_id [01] "               2
# must NOT have fallen back or errored on the encoder
if since | grep -qE "ffmpeg probe FAILED|restarting encoder|removing external AVC"; then
    echo "  FAIL encoder error/fallback after mark"; pass=0
else
    echo "  ok   no encoder error/fallback"
fi

echo "server log slice: $OUT/server_since_mark.txt"
since > "$OUT/server_since_mark.txt"

if [ "$pass" -eq 1 ]; then
    echo "MULTIMON OFFLINE PASS"; exit 0
fi
echo "MULTIMON OFFLINE FAIL — see $OUT/ and $SRVLOG"; exit 1
