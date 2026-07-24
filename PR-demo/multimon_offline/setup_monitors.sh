#!/bin/bash
# Configure the dummy X server (see xorg-dummy-2mon.conf) into TWO side-by-side
# 1024x768 RandR monitors. Run with DISPLAY pointing at that server.
# Fails LOUDLY (exit 1) if it cannot produce exactly 2 monitors — the whole
# point of the offline test is a genuine 2-monitor client, so we never
# silently proceed with 1.
set -u
MODE=1024x768_60

# Ensure the named mode exists and is attached to both outputs. --newmode is
# a no-op-with-error if it already exists (from the config Modeline), so ignore.
xrandr --newmode "$MODE" 63.50 1024 1072 1176 1328 768 771 775 798 -hsync +vsync 2>/dev/null || true
for out in DUMMY0 DUMMY1; do
    xrandr --addmode "$out" "$MODE" 2>/dev/null || true
done

xrandr --output DUMMY0 --mode "$MODE" --pos 0x0 --primary 2>&1
xrandr --output DUMMY1 --mode "$MODE" --pos 1024x0 2>&1

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
echo "OK: 2 x 1024x768 monitors (virtual 2048x768)"
