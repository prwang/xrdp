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

# --- span the WHOLE root window, not one monitor --------------------------
# The fleet pods run the payload with NO window manager, so xterm
# -maximized sizes itself to the root window and one xterm inks BOTH
# monitors. The T4 runs XFCE (wm1.sh -> xfce4-session), and to xfwm4
# "maximized" means the CURRENT MONITOR. Measured on the first T4 probe
# (2026-07-30): 251 damage events on surface 1 against 44 on surface 0 —
# i.e. the second monitor was essentially idle, which is BACKLOG #53's
# one-active-one-idle regime and NOT the two-monitor batching the E5-2
# gate is about. The gate needs both monitors damaged in the same cycle.
#
# So size the window explicitly to the root geometry. This is the payload
# resizing ITS OWN window at login, inside the session — not an external
# process reaching into a live session (CLAUDE.md GUI lifecycle).
#
# Two traps, both hit on the first T4 probes (2026-07-30):
#   * `xdotool getdisplaygeometry` reports the PRIMARY MONITOR (2560x1440
#     here), not the root window. The root size comes from xwininfo -root
#     (6400x2400) — using the xdotool number sizes the window to one
#     monitor again, which is the bug being fixed.
#   * a window xfwm4 considers MAXIMIZED is re-snapped to its monitor
#     after any resize, so the maximized state has to be removed first
#     (and `-maximized` is no longer passed to xterm at all).
E52_TITLE=E52FLOOD
E52_MAIN=$$
if command -v xdotool >/dev/null 2>&1 && command -v xwininfo >/dev/null 2>&1
then
    (
        RG=$(xwininfo -root 2>/dev/null \
             | sed -n 's/^  -geometry  *\([0-9]*x[0-9]*\).*/\1/p')
        RW=${RG%x*}
        RH=${RG#*x}
        case "${RW:-0}${RH:-0}" in
        *[!0-9]* | 0*) exit 0 ;;
        esac
        # RE-ASSERT FOR THE LIFE OF THE SESSION, not once. A one-shot
        # resize was measured to hold on one run and silently lose on the
        # next (2026-07-30: a 75 s repeat came back kids_armed=2 in 96 %
        # of cycles, i.e. back to one monitor, and would have been
        # reported as a batched number for a regime the batch cannot
        # help). xfwm4 re-snaps on its own schedule; the payload is not
        # in a position to know when, so it just checks. Re-asserting a
        # geometry the window already has damages nothing, so the check
        # is free in the thing being measured.
        LOG=${XDG_RUNTIME_DIR:-/tmp}/e52_span.log
        : > "$LOG"
        # ...and stop when the payload does. `exec xterm` below keeps this
        # script's pid, so watching it is watching the flood window; without
        # this the loop outlives every logoff and the box accumulates one
        # spinning shell per login.
        while kill -0 "$E52_MAIN" 2>/dev/null; do
            wid=$(xdotool search --name "^$E52_TITLE\$" 2>/dev/null | head -1)
            if [ -n "$wid" ]; then
                set -- $(xdotool getwindowgeometry --shell "$wid" 2>/dev/null \
                         | sed -n 's/^X=\([0-9-]*\)/\1/p;s/^Y=\([0-9-]*\)/\1/p;s/^WIDTH=\([0-9]*\)/\1/p;s/^HEIGHT=\([0-9]*\)/\1/p')
                # POSITION MATTERS AS MUCH AS SIZE. A 6400x2400 window at
                # X=2570 covers monitor 1 and nothing else — measured on
                # the T4 on 2026-07-30, and it read as a perfectly normal
                # run until the coverage gate called it. xfwm4 restores a
                # window's pre-maximize POSITION when the maximized state
                # is removed, so the move has to be re-checked, not just
                # issued. The tolerance is the frame: asking for 0,0
                # lands the client at (border, titlebar).
                if [ "${1:-9999}" -gt 100 ] || [ "${2:-9999}" -gt 200 ] \
                   || [ "${3:-0}" != "$RW" ] || [ "${4:-0}" != "$RH" ]; then
                    echo "$(date -Is) fix ${3:-?}x${4:-?}+${1:-?}+${2:-?}" \
                         "-> ${RW}x${RH}+0+0" >> "$LOG"
                    xdotool windowstate --remove MAXIMIZED_VERT \
                        --remove MAXIMIZED_HORZ "$wid" 2>/dev/null
                    sleep 1
                    xdotool windowmove --sync "$wid" 0 0 2>/dev/null
                    xdotool windowsize --sync "$wid" "$RW" "$RH" 2>/dev/null
                fi
            fi
            sleep 3
        done
    ) &
fi

if [ ! -s "$CORPUS" ]; then
    exec xterm -fa 'DejaVu Sans Mono' -fs 22 -bg red -fg white -e bash -c '
        while true; do
            clear
            echo "  E5-2 PAYLOAD INVALID: no corpus at '"$CORPUS"'"
            echo "  install it with PR-demo/t4_profile/e52_t4_payload.sh"
            sleep 2
        done'
fi

exec xterm -title "$E52_TITLE" -fa 'DejaVu Sans Mono' -fs 14 \
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
