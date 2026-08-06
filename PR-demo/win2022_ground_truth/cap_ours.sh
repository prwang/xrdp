#!/bin/bash
# Capture OUR xrdp wire with the patched FreeRDP dumper, driving a full-screen
# color animation on the tester session (:10) so frames route to AVC444.
DUMPDIR="${1:-/work/vm/gfxours_reframe}"; SECS="${2:-14}"
export DISPLAY=:99
pgrep Xvfb >/dev/null || { Xvfb :99 -screen 0 1400x900x24 >/work/vm/xvfb.log 2>&1 & sleep 2; }
rm -rf "$DUMPDIR"; mkdir -p "$DUMPDIR"

# background root-color animation on the tester session
( export DISPLAY=:10 XAUTHORITY=/var/run/xrdp/1000/Xauthority
  for i in $(seq 1 400); do
    r=$(( (i*37)%256 )); g=$(( (i*91)%256 )); b=$(( (i*53)%256 ))
    xsetroot -solid "$(printf '#%02x%02x%02x' $r $g $b)" 2>/dev/null
    sleep 0.05
  done ) &
ANIM=$!

export RDPGFX_DUMP_DIR="$DUMPDIR"
export LD_LIBRARY_PATH="/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/client/common:/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/libfreerdp:/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/winpr/libwinpr:$LD_LIBRARY_PATH"
BIN=/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/client/X11/xfreerdp
timeout "$SECS" "$BIN" /v:127.0.0.1:3389 /u:tester /p:"" \
  /cert:ignore /sec:tls +clipboard /gfx:AVC444 /w:1280 /h:800 \
  /log-level:ERROR >"$DUMPDIR/frdp.log" 2>&1 &
FR=$!
sleep 3
# submit the login (empty password) via Enter on the FreeRDP window
for i in $(seq 1 20); do
  W=$(DISPLAY=:99 xdotool search --class xfreerdp 2>/dev/null | head -1)
  [ -n "$W" ] && { DISPLAY=:99 xdotool key --window "$W" Return 2>/dev/null; break; }
  sleep 0.3
done
wait $FR 2>/dev/null
kill $ANIM 2>/dev/null
echo "exit; dumped=$(ls "$DUMPDIR"/*.bin 2>/dev/null | wc -l) frames"
