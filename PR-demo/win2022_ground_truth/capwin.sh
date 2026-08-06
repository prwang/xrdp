#!/bin/bash
# Capture RDPGFX wire from the remote real-Windows GRID host via patched FreeRDP.
# usage: capwin.sh <dumpdir> <seconds> [extra xfreerdp args...]
. /root/.testvm_cred
DUMPDIR="${1:?dumpdir}"; SECS="${2:-12}"; shift 2 || shift $#
export DISPLAY=:99
pgrep Xvfb >/dev/null || { Xvfb :99 -screen 0 1280x800x24 >/work/vm/xvfb.log 2>&1 & sleep 2; }
rm -rf "$DUMPDIR"; mkdir -p "$DUMPDIR"
export RDPGFX_DUMP_DIR="$DUMPDIR"
export LD_LIBRARY_PATH="/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/client/common:/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/libfreerdp:/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/winpr/libwinpr:/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/channels:$LD_LIBRARY_PATH"
BIN=/work/vm/frdbuild/freerdp3-3.15.0+dfsg/bld/client/X11/xfreerdp
timeout "$SECS" "$BIN" /v:43.98.187.122:3389 /u:Administrator /p:"$VM_PASS" \
  /cert:ignore +clipboard /gfx:AVC444 /w:1280 /h:800 \
  /log-level:WARN "$@" >"$DUMPDIR/frdp.log" 2>&1
echo "exit=$? dumped=$(ls "$DUMPDIR"/*.bin 2>/dev/null | wc -l) frames"
