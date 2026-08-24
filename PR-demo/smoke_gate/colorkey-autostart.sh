#!/bin/sh

# XDG-autostart wrapper for the deploy smoke gate. The root-owned marker makes
# the persistent desktop entry inert during ordinary interactive sessions.

MARKER=/etc/xrdp-smoke-colorkey

[ -r "$MARKER" ] || exit 0
size=$(sed -n '1p' "$MARKER")
case "$size" in
    *[!0-9x]*|*x*x*|x*|*x) exit 1 ;;
esac
width=${size%x*}
height=${size#*x}
[ "$width" -ge 640 ] && [ "$width" -le 7680 ] || exit 1
[ "$height" -ge 480 ] && [ "$height" -le 4320 ] || exit 1

cols=$((width / 6 + 10))
rows=$((height / 13 + 10))
xterm -geometry "${cols}x${rows}+0+0" \
    -e /usr/local/bin/colorkey.sh </dev/null >/dev/null 2>&1 &
child=$!

# The wrapper is part of the program's login-time lifecycle. It positions and
# focuses only the xterm it created; no SSH-side process supervisor is used.
window=
for unused in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
do
    window=$(xdotool search --pid "$child" 2>/dev/null | tail -1)
    [ -n "$window" ] && break
    sleep 1
done
if [ -n "$window" ]
then
    xdotool windowmove --sync "$window" 0 0 >/dev/null 2>&1 || true
    xdotool windowactivate --sync "$window" >/dev/null 2>&1 || true
fi
wait "$child"
