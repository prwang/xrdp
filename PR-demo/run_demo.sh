#!/bin/bash
# run_demo.sh -- end-to-end AVC444-vs-AVC420 iso-luminant demo.
#
# Generates the test pattern, shows it full-screen on the live xrdp session,
# captures it decoded through xfreerdp3 in each GFX mode, and builds the zoom
# comparisons in PR-demo/results/. Box-specific harness (a running xrdp, a
# passwordless RDP user, an offscreen client display); see PR-demo/README.md
# for the assumptions and the tunable environment variables.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/.." && pwd)"
OUT="$HERE/results"
mkdir -p "$OUT"

# --- box configuration (override via env) ---
: "${SESSION_DISPLAY:=:10}"        # the xrdp session's Xorg display
: "${SESSION_XAUTH:=/var/run/xrdp/1000/Xauthority}"
: "${SESSION_USER:=tester}"
: "${CLIENT_DISPLAY:=:99}"          # offscreen Xvfb for the xfreerdp client
: "${RDP_HOST:=127.0.0.1:3389}"
: "${RDP_USER:=tester}"             # passwordless
: "${GEOM:=1920x1080}"             # 32-multiple width avoids the resize comb
export SESSION_DISPLAY SESSION_XAUTH SESSION_USER CLIENT_DISPLAY RDP_HOST \
       RDP_USER GEOM
FONT=/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf
W=${GEOM%x*}; H=${GEOM#*x}

echo "== 1. generate pattern =="
python3 "$REPO/tools/gen_isoluma.py" --width "$W" --height "$H" \
    -o "$OUT/isoluma_src.png"

echo "== 2. offscreen client display ($CLIENT_DISPLAY) =="
if ! DISPLAY="$CLIENT_DISPLAY" xdotool getdisplaygeometry >/dev/null 2>&1; then
    Xvfb "$CLIENT_DISPLAY" -screen 0 "${W}x${H}x24" -nolisten tcp \
        >/tmp/xvfb_demo.log 2>&1 &
    sleep 2
fi

echo "== 3. show pattern full-screen on the session =="
bash "$HERE/lib/show_img.sh" "$OUT/isoluma_src.png"

echo "== 4. capture through each codec =="
bash "$HERE/lib/capture_codec.sh" AVC444 "$OUT/iso444.png"
bash "$HERE/lib/capture_codec.sh" AVC420 "$OUT/iso420.png"

echo "== 5. build comparisons =="
# 1px bars: 444 keeps stripes, 420 -> flat gray (the decisive frame)
ffmpeg -hide_banner -loglevel error -i "$OUT/iso444.png" -i "$OUT/iso420.png" \
  -filter_complex \
"[0:v]crop=480:60:60:110,scale=iw*4:ih*4:flags=neighbor,drawtext=fontfile=$FONT:text='AVC444  (1px bars)':x=8:y=6:fontsize=30:fontcolor=white:box=1:boxcolor=black@0.8[a];\
 [1:v]crop=480:60:60:110,scale=iw*4:ih*4:flags=neighbor,drawtext=fontfile=$FONT:text='AVC420  (1px bars)':x=8:y=6:fontsize=30:fontcolor=white:box=1:boxcolor=black@0.8[b];\
 [a]pad=iw:ih+8:0:0:white[ap];[ap][b]vstack" -frames:v 1 -y "$OUT/cmp_bars1px.png"
# iso-luminant text: 444 crisp, 420 muddy
ffmpeg -hide_banner -loglevel error -i "$OUT/iso444.png" -i "$OUT/iso420.png" \
  -filter_complex \
"[0:v]crop=1100:150:24:322,scale=iw*1.4:ih*1.4,drawtext=fontfile=$FONT:text='AVC444':x=10:y=8:fontsize=26:fontcolor=white:box=1:boxcolor=black@0.8[a];\
 [1:v]crop=1100:150:24:322,scale=iw*1.4:ih*1.4,drawtext=fontfile=$FONT:text='AVC420':x=10:y=8:fontsize=26:fontcolor=white:box=1:boxcolor=black@0.8[b];\
 [a]pad=iw:ih+6:0:0:white[ap];[ap][b]vstack" -frames:v 1 -y "$OUT/cmp_isotext.png"

echo "== 6. cleanup offscreen infra (leaves xrdp running) =="
pkill -9 -f xfreerdp3 >/dev/null 2>&1 || true
pkill -9 -x Xvfb >/dev/null 2>&1 || true
sudo -u "$SESSION_USER" env DISPLAY="$SESSION_DISPLAY" \
    XAUTHORITY="$SESSION_XAUTH" pkill -u "$SESSION_USER" ffplay >/dev/null 2>&1 || true

echo "done. results in $OUT:"
ls -1 "$OUT"/*.png
