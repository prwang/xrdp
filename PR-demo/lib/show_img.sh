#!/bin/bash
# show_img.sh <image> -- display an image full-screen 1:1 on the xrdp session
# display as the session user. ffplay loops the still so a (re)connecting GFX
# client always receives a full repaint. Box-specific: see PR-demo/README.md.
set -u
IMG="$1"
: "${SESSION_DISPLAY:=:10}"
: "${SESSION_XAUTH:=/var/run/xrdp/1000/Xauthority}"
: "${SESSION_USER:=tester}"

run() { sudo -u "$SESSION_USER" env DISPLAY="$SESSION_DISPLAY" \
        XAUTHORITY="$SESSION_XAUTH" "$@"; }

run pkill -u "$SESSION_USER" ffplay  >/dev/null 2>&1
run pkill -u "$SESSION_USER" qterminal >/dev/null 2>&1
sleep 1
run setsid ffplay -fs -an -loglevel quiet -loop 0 -i "$IMG" \
    </dev/null >/dev/null 2>&1 &
sleep 3
echo "show_img: ffplay pid(s): $(pgrep -x ffplay | tr '\n' ' ')"
