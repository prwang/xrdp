#!/bin/bash
# Configure the dummy X server into N side-by-side RandR monitors. Run with
# DISPLAY pointing at that server.
# Fails LOUDLY (exit 1) if it cannot produce exactly MM_MONITORS monitors —
# the whole point of the offline test is a genuine client of a KNOWN monitor
# count, so we never silently proceed with a different one.
#
# Defaults are the 2x1024x768 layout of xorg-dummy-2mon.conf. Override for
# the BACKLOG #45 E3 target layout (xorg-dummy-2mon-4k.conf):
#   MM_MODE0=2560x1440_60.00 MM_MODE1=3840x2400R MM_POS1=2560x0
# Every mode named here must exist in the running server's mode list — the
# config file's Modeline section is what puts it there; MM_MODELINE0/1 add
# one at runtime for a server started without it.
#
# MM_MONITORS=1 drives a SINGLE-monitor client: DUMMY1 is switched off and
# only MODE0 is presented. This is the BACKLOG #64 control — it asks whether
# the capture/encode serialisation is a property of the multimon batch or of
# the pipeline itself, so the ONLY thing that may differ from the 2-monitor
# run is the monitor count.
set -u
MONITORS=${MM_MONITORS:-2}
MODE0=${MM_MODE0:-1024x768_60}
MODE1=${MM_MODE1:-$MODE0}
POS0=${MM_POS0:-0x0}
# side by side: monitor 1 starts where monitor 0's width ends
POS1=${MM_POS1:-$(echo "$MODE0" | sed 's/x.*//')x0}
MODELINE0=${MM_MODELINE0:-63.50 1024 1072 1176 1328 768 771 775 798 -hsync +vsync}
MODELINE1=${MM_MODELINE1:-}

case "$MONITORS" in
1|2) ;;
*) echo "FAIL: MM_MONITORS must be 1 or 2, got '$MONITORS'" >&2; exit 1 ;;
esac

# Ensure the named modes exist and are attached to both outputs. --newmode is
# a no-op-with-error if it already exists (from the config Modeline), so ignore.
[ -n "$MODELINE0" ] && xrandr --newmode "$MODE0" $MODELINE0 2>/dev/null
[ -n "$MODELINE1" ] && xrandr --newmode "$MODE1" $MODELINE1 2>/dev/null
for out in DUMMY0 DUMMY1; do
    xrandr --addmode "$out" "$MODE0" 2>/dev/null || true
    [ "$MODE1" != "$MODE0" ] && xrandr --addmode "$out" "$MODE1" 2>/dev/null
done

xrandr --output DUMMY0 --mode "$MODE0" --pos "$POS0" --primary 2>&1
if [ "$MONITORS" -eq 2 ]; then
    xrandr --output DUMMY1 --mode "$MODE1" --pos "$POS1" 2>&1
else
    # --off, not merely unmapped: a monitor left in the RandR list is still
    # advertised to the server in the client's monitor layout PDU, which is
    # what the capture side counts.
    xrandr --output DUMMY1 --off 2>&1
fi

n=$(xrandr --listmonitors 2>/dev/null | awk '/^Monitors:/ {print $2}')
echo "== xrandr --listmonitors =="
xrandr --listmonitors
if [ "${n:-0}" -ne "$MONITORS" ]; then
    echo "FAIL: expected $MONITORS monitors, got ${n:-0}." >&2
    if [ "$MONITORS" -eq 2 ]; then
        echo "  If DUMMY1 stays 'disconnected', this dummy build needs" >&2
        echo "  Option \"ConnectedMonitor\" \"DUMMY0,DUMMY1\" in the Device section" >&2
        echo "  (or a newer xf86-video-dummy). See README.md." >&2
    fi
    exit 1
fi
# Verify the ACTIVE pixel geometry, not just the monitor count. A RandR
# mode is picked BY NAME: if "3840x2160R" was ever created against the
# wrong modeline (2026-07-31: MM_MODE0 overridden without MM_MODELINE0, so
# --newmode built a mode NAMED 3840x2160R with the default 2560x1440
# timings), the count check passes, this script printed the requested name
# as "OK", and the run silently measured the wrong workload. The mode
# name's WxH prefix is the contract; the active geometry must match it.
check_geo() {
    out=$1; mode=$2
    want=$(echo "$mode" | grep -oE '^[0-9]+x[0-9]+')
    got=$(xrandr --listmonitors 2>/dev/null | awk -v o="$out" \
        '$0 ~ o {for (i=1;i<=NF;i++) if ($i ~ /^[0-9]+\/[0-9]+x[0-9]+\/[0-9]+/) {
             split($i, a, "/"); split(a[2], b, "x"); split(a[3], c, "+");
             print a[1] "x" b[2]; exit}}')
    if [ -n "$want" ] && [ "$got" != "$want" ]; then
        echo "FAIL: $out active geometry is '$got' but mode '$mode'" >&2
        echo "  promises '$want' — a mode with this name exists with the" >&2
        echo "  WRONG timings. Delete it (xrandr --delmode $out $mode;" >&2
        echo "  xrandr --rmmode $mode) or pass the matching MM_MODELINE." >&2
        exit 1
    fi
}
check_geo DUMMY0 "$MODE0"
[ "$MONITORS" -eq 2 ] && check_geo DUMMY1 "$MODE1"
if [ "$MONITORS" -eq 2 ]; then
    echo "OK: 2 monitors ($MODE0 at $POS0, $MODE1 at $POS1)"
else
    echo "OK: 1 monitor ($MODE0 at $POS0; DUMMY1 off)"
fi
