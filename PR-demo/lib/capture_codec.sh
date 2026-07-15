#!/bin/bash
# capture_codec.sh <AVC444|AVC420> <outfile> -- connect one xfreerdp3 client in
# the given GFX mode on the offscreen client display, log the passwordless user
# in, and grab one frame. Box-specific: see PR-demo/README.md.
set -u
MODE="$1"; OUT="$2"
: "${CLIENT_DISPLAY:=:99}"
: "${RDP_HOST:=127.0.0.1:3389}"
: "${RDP_USER:=tester}"
: "${GEOM:=1920x1080}"
: "${XRDP_LOG:=/var/log/xrdp.log}"
export DISPLAY="$CLIENT_DISPLAY"

pkill -9 -f xfreerdp3 >/dev/null 2>&1
sleep 2
: > "$XRDP_LOG" 2>/dev/null || true
setsid env DISPLAY="$CLIENT_DISPLAY" xfreerdp3 /v:"$RDP_HOST" /u:"$RDP_USER" \
    /p: /size:"$GEOM" /gfx:"$MODE" /cert:ignore /log-level:WARN \
    </dev/null >"/tmp/frdp_${MODE}.log" 2>&1 &
sleep 6
fwin=$(xdotool search --name "FreeRDP" | head -1)
xdotool windowactivate "$fwin" >/dev/null 2>&1; sleep 0.5
xdotool key --window "$fwin" Return >/dev/null 2>&1     # submit passwordless login
sleep 9
ffmpeg -hide_banner -loglevel error -f x11grab -video_size "$GEOM" \
    -i "${CLIENT_DISPLAY}.0" -frames:v 1 -y "$OUT"
echo "capture_codec: mode=$MODE frdp=$(pgrep -c xfreerdp3) out=$OUT"
grep -iaE "Matched H264/AVC(444|420)|starting ffmpeg AVC(444|420)" \
    "$XRDP_LOG" 2>/dev/null | tail -2
