#!/bin/sh
# e52_t4_payload.sh — install / arm / disarm the E5-2 benchmark payload on
# the T4, from the dev box. Same shape as frame_accounting.sh: the payload
# itself is a PERSISTENT on-T4 deploy, this wrapper checksum-compares the
# versioned copy against the installed one, re-installs only what changed,
# and invokes single non-interactive commands (CLAUDE.md "T4 test
# methodology" — no bash-over-ssh heredocs).
#
# Usage (from the dev box):
#   sh PR-demo/t4_profile/e52_t4_payload.sh install
#   sh PR-demo/t4_profile/e52_t4_payload.sh arm codeflood   # then log the session off
#   sh PR-demo/t4_profile/e52_t4_payload.sh arm code        # 10 Hz cadence variant
#   sh PR-demo/t4_profile/e52_t4_payload.sh arm textflood   # client-side render
#   sh PR-demo/t4_profile/e52_t4_payload.sh disarm          # then log the session off
#   sh PR-demo/t4_profile/e52_t4_payload.sh status
#
# ARMING TAKES EFFECT AT THE NEXT LOGIN, by design: the payload is an XDG
# autostart entry because the only sanctioned operations on a live remote
# GUI session are logging it off and autostarting at login (owner
# directive 2026-07-27). This wrapper therefore never touches a running
# session; it prints the reminder and stops.
set -eu
T4=${T4:-$(cat /root/.t4_host 2>/dev/null || true)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
[ -n "$T4" ] || { echo "ABORT: set T4=user@host or /root/.t4_host" >&2; exit 1; }
D=$(cd "$(dirname "$0")" && pwd)
CORPUS=$D/../mac_bisect_matrix/code_corpus.ansi
TFDIR=$D/../textflood
ACTION=${1:-status}

rsh() { ssh -n -i "$T4_KEY" "$T4" "$@"; }

push() {   # push <local file> <installed path> <mode>
    lsum=$(md5sum "$1" | cut -d' ' -f1)
    rsum=$(rsh "md5sum '$2' 2>/dev/null | cut -d' ' -f1" || true)
    if [ "$lsum" != "$rsum" ]; then
        echo "installing $(basename "$2") ($lsum)"
        scp -q -i "$T4_KEY" "$1" "$T4:/tmp/$(basename "$2")"
        rsh "sudo install -D -m $3 /tmp/$(basename "$2") '$2'"
    else
        echo "up to date: $2"
    fi
}

case "$ACTION" in
install)
    [ -s "$CORPUS" ] || { echo "ABORT: no corpus at $CORPUS" >&2; exit 1; }
    push "$D/e52_payload.sh"      /usr/local/bin/e52_payload.sh          755
    push "$D/e52-payload.desktop" /etc/xdg/autostart/e52-payload.desktop 644
    push "$CORPUS"                /usr/local/share/code_corpus.ansi      644
    # textflood is COMPILED ON THE T4, not shipped as a binary: it links
    # against the box's own cairo/Xlib, and a binary built here against
    # different sonames would either fail to start or silently pull in a
    # different glyph rasterizer, which is the one thing this payload must
    # not do. The build is checksum-gated like everything else.
    push "$TFDIR/textflood.c"     /usr/local/src/textflood.c             644
    push "$TFDIR/build.sh"        /usr/local/src/textflood_build.sh      755
    echo "building textflood on the T4"
    rsh "sudo sh -c 'command -v cc >/dev/null 2>&1 && \
         pkg-config --exists cairo x11 xext' || \
         sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q \
         build-essential libcairo2-dev libx11-dev libxext-dev >/dev/null"
    rsh "sudo sh -c 'cd /usr/local/src && CC=cc sh ./textflood_build.sh /usr/local/bin'"
    rsh "/usr/local/bin/textflood --help >/dev/null && echo 'textflood: OK'"
    echo "installed. Nothing runs until it is armed:"
    echo "  $0 arm codeflood   (then log the RDP session off and back in)"
    ;;
arm)
    KIND=${2:-codeflood}
    case "$KIND" in
    codeflood|code|gpuflood|textflood) ;;
    *) echo "ABORT: kind must be codeflood|code|gpuflood|textflood" >&2
       exit 1 ;;
    esac
    rsh "sudo sh -c 'echo $KIND > /etc/xrdp-e52-payload'"
    echo "armed: $KIND"
    echo "LOG THE RDP SESSION OFF AND BACK IN — the payload starts at login."
    echo "(e_gate_run.sh with E_COLD=1 does that for you as its first step.)"
    ;;
disarm)
    rsh "sudo rm -f /etc/xrdp-e52-payload"
    echo "disarmed. The payload stops at the next login; log the session off."
    ;;
status)
    echo "T4: $T4"
    rsh "echo -n 'marker: '; cat /etc/xrdp-e52-payload 2>/dev/null \
         || echo '(disarmed)'; \
         md5sum /usr/local/bin/e52_payload.sh /usr/local/share/code_corpus.ansi \
         /etc/xdg/autostart/e52-payload.desktop /usr/local/bin/textflood 2>&1; \
         pgrep -a -f e52_payload.sh || echo 'payload not running'"
    ;;
*)
    echo "usage: $0 {install|arm [codeflood|code]|disarm|status}" >&2
    exit 1
    ;;
esac
