#!/bin/bash
# e52_payload.sh — PERSISTENT on-T4 session payload for the E5-2 frame
# interval benchmark (BACKLOG #52). Installed to /usr/local/bin by
# PR-demo/t4_profile/e52_t4_payload.sh and started by
# /etc/xdg/autostart/e52-payload.desktop at LOGIN.
#
# WHY IT IS AN AUTOSTART ENTRY AND NOT AN SSH LAUNCH (CLAUDE.md, owner
# directive 2026-07-27): the only two sanctioned operations on a live
# remote GUI session are (1) log the whole session off, (2) have the
# program autostart at login. Never pkill/relaunch a GUI process inside a
# live session over ssh. So the payload is armed by writing a marker file
# and takes effect at the NEXT login; disarming is the same in reverse.
#
#   sudo sh -c 'echo codeflood > /etc/xrdp-e52-payload'   # arm, then log off
#   sudo rm -f /etc/xrdp-e52-payload                      # disarm, then log off
#
# Kinds (must stay in sync with PR-demo/mac_bisect_matrix/banner.sh, which
# is the fleet's copy of the same payloads — the two are compared to each
# other, so a divergence silently changes what "the same benchmark" means):
#   codeflood  corpus scroll, 25 lines per write, NO sleep, each line
#              repeated 32x so every row wraps past the right edge of the
#              widest monitor. The throughput payload: the producer is
#              limited by the consumer, so the send interval is the
#              server's own rather than a metronome reading.
#   code       the same corpus, 1 line per 0.1 s — the 10 Hz CADENCE
#              payload, kept for bandwidth comparisons with the fleet.
#   none/absent  do nothing at all (normal desktop).
#
# Fails LOUD, on screen, if the corpus is missing: a benchmark that
# silently scrolls something else is worse than one that does not run.
set -u
MARKER=${E52_MARKER:-/etc/xrdp-e52-payload}
CORPUS=${E52_CORPUS:-/usr/local/share/code_corpus.ansi}
KIND=$(cat "$MARKER" 2>/dev/null | tr -d ' \r\n')

case "${KIND:-none}" in
codeflood|code) ;;
*) exit 0 ;;
esac

# The owner's session is the one under test (no special test users), so
# never assume a display: take the one this session actually has.
[ -n "${DISPLAY:-}" ] || exit 0

if [ ! -s "$CORPUS" ]; then
    exec xterm -fa 'DejaVu Sans Mono' -fs 22 -bg red -fg white -e bash -c '
        while true; do
            clear
            echo "  E5-2 PAYLOAD INVALID: no corpus at '"$CORPUS"'"
            echo "  install it with PR-demo/t4_profile/e52_t4_payload.sh"
            sleep 2
        done'
fi

exec xterm -maximized -fa 'DejaVu Sans Mono' -fs 14 \
    -bg '#002b36' -fg '#839496' -e bash -c '
    CORPUS="'"$CORPUS"'"
    KIND="'"$KIND"'"
    mapfile -t L < "$CORPUS"
    STEP=1
    DELAY=0.1
    REPEAT=1
    [ "$KIND" = codeflood ] && { STEP=25; DELAY=; REPEAT=32; }
    i=0
    tput civis 2>/dev/null
    while true; do
        n=1
        while [ "$n" -le "$STEP" ]; do
            txt="${L[$(((i + n) % ${#L[@]}))]}"
            if [ "$REPEAT" -gt 1 ]; then
                r=1
                while [ "$r" -lt "$REPEAT" ]; do
                    txt="$txt $txt"
                    r=$((r * 2))
                done
            fi
            printf "%4d  %s\n" $(((i + n) % ${#L[@]})) "$txt"
            n=$((n + 1))
        done
        i=$((i + STEP))
        [ -n "$DELAY" ] && sleep "$DELAY"
    done'
