#!/bin/sh
# T4 /etc/xrdp/wm1.sh — session startup policy for the test box.
# Deployed copy of record (PR-demo policy: box files that tests depend
# on are committed so they survive container restarts). scp to
# /etc/xrdp/wm1.sh (root:root 755) when changed.
#
# Policy:
#   ubuntu — the owner's real xfce session.
#   tester — ALSO a full xfce session (2026-07-26): the offscreen
#            perf/demo rig (Xvfb + xfreerdp3 /multimon as tester) must
#            reproduce the owner environment — xfwm4 compositor, thunar
#            — or its fps numbers measure a different workload. The
#            former bare ~/.xsession xterm profile made encoder tests
#            unrepresentative of the owner's drag load.
#   other users with an executable ~/.xsession — run it (profiling
#            harness accounts); everyone else is denied.

LOG="$HOME/.cache/xrdp-startwm-xfce.log"
mkdir -p "$HOME/.cache"

exec >>"$LOG" 2>&1
set -x

echo "===== XRDP XFCE startup: $(date -Is) ====="
echo "USER=$USER"
echo "LOGNAME=$LOGNAME"
echo "HOME=$HOME"
echo "SHELL=$SHELL"
echo "PWD=$PWD"
id
env | sort

case "${USER}:${LOGNAME}" in
    ubuntu:*|*:ubuntu|tester:*|*:tester)
        ;;
    *)
        if [ -x "$HOME/.xsession" ]; then
            exec "$HOME/.xsession"
        fi
        echo "Denied: USER=$USER LOGNAME=$LOGNAME"
        exit 1
        ;;
esac

unset DBUS_SESSION_BUS_ADDRESS
unset SESSION_MANAGER

export XDG_SESSION_DESKTOP=xfce
export DESKTOP_SESSION=xfce
export XDG_CURRENT_DESKTOP=XFCE

echo "Checking commands..."
command -v xfce4-session || exit 20
command -v dbus-run-session || true

echo "Starting XFCE..."

if command -v dbus-run-session >/dev/null 2>&1; then
    exec dbus-run-session -- xfce4-session
else
    exec xfce4-session
fi
