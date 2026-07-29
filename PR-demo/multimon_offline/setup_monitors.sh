#!/bin/bash
# Configure the dummy X server into TWO side-by-side RandR monitors. Run with
# DISPLAY pointing at that server.
# Fails LOUDLY (exit 1) if it cannot produce exactly 2 monitors — the whole
# point of the offline test is a genuine 2-monitor client, so we never
# silently proceed with 1.
#
# Defaults are the 2x1024x768 layout of xorg-dummy-2mon.conf. Override for
# the BACKLOG #45 E3 target layout (xorg-dummy-2mon-4k.conf):
#   MM_MODE0=2560x1440_60.00 MM_MODE1=3840x2400R MM_POS1=2560x0
# Every mode named here must exist in the running server's mode list — the
# config file's Modeline section is what puts it there; MM_MODELINE0/1 add
# one at runtime for a server started without it.
set -u
MODE0=${MM_MODE0:-1024x768_60}
MODE1=${MM_MODE1:-$MODE0}
POS0=${MM_POS0:-0x0}
# side by side: monitor 1 starts where monitor 0's width ends
POS1=${MM_POS1:-$(echo "$MODE0" | sed 's/x.*//')x0}
MODELINE0=${MM_MODELINE0:-63.50 1024 1072 1176 1328 768 771 775 798 -hsync +vsync}
MODELINE1=${MM_MODELINE1:-}

# Ensure the named modes exist and are attached to both outputs. --newmode is
# a no-op-with-error if it already exists (from the config Modeline), so ignore.
[ -n "$MODELINE0" ] && xrandr --newmode "$MODE0" $MODELINE0 2>/dev/null
[ -n "$MODELINE1" ] && xrandr --newmode "$MODE1" $MODELINE1 2>/dev/null
for out in DUMMY0 DUMMY1; do
    xrandr --addmode "$out" "$MODE0" 2>/dev/null || true
    [ "$MODE1" != "$MODE0" ] && xrandr --addmode "$out" "$MODE1" 2>/dev/null
done

xrandr --output DUMMY0 --mode "$MODE0" --pos "$POS0" --primary 2>&1
xrandr --output DUMMY1 --mode "$MODE1" --pos "$POS1" 2>&1

n=$(xrandr --listmonitors 2>/dev/null | awk '/^Monitors:/ {print $2}')
echo "== xrandr --listmonitors =="
xrandr --listmonitors
if [ "${n:-0}" -ne 2 ]; then
    echo "FAIL: expected 2 monitors, got ${n:-0}." >&2
    echo "  If DUMMY1 stays 'disconnected', this dummy build needs" >&2
    echo "  Option \"ConnectedMonitor\" \"DUMMY0,DUMMY1\" in the Device section" >&2
    echo "  (or a newer xf86-video-dummy). See README.md." >&2
    exit 1
fi
echo "OK: 2 monitors ($MODE0 at $POS0, $MODE1 at $POS1)"
