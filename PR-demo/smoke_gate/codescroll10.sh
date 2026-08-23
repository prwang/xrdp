#!/bin/bash
# codescroll10.sh -- the fixed 10 Hz code payload, as a standalone app.
#
# WHY THIS EXISTS SEPARATELY. The same scroll is available as a session
# kind (SESSION_KIND=code), but a session kind REPLACES the desktop: the
# container execs it instead of starting XFCE. On the interactive arm the
# owner wants a real desktop AND the payload, so the payload has to be
# something you launch from a terminal inside the session.
#
# WHAT IT DOES, and why the numbers are what they are. One corpus line
# every 0.1 s -- a fixed 10 Hz metronome, not a free-running flood. That
# is a typical reading-scroll rate, about 20 px of vertical shift per
# encoded frame, which stays inside the encoder's motion-search range so
# the main chain tracks it by inter-prediction rather than re-coding
# every glyph. It is deliberately NOT a throughput benchmark: the rate is
# pinned by the sleep, so a healthy pipeline and a struggling one both
# produce 10 frames a second. What it tests is whether the scroll is
# SMOOTH and the text is CORRECT at a rate a human reads at.
#
# WHAT A FAULT LOOKS LIKE:
#   * jerky or clumped scrolling -- frames are arriving in bursts rather
#     than at the pace they were produced;
#   * a line that tears horizontally, or a half-updated screen -- a frame
#     was presented before it was complete;
#   * glyph edges that shimmer or lose their colour fringes -- the chroma
#     plane is being degraded (this payload is deliberately subpixel
#     antialiased, so the fringes are the point);
#   * the counter in the title jumping by more than one -- frames were
#     dropped or coalesced upstream of the client.
#
# The corpus is the same pre-generated file the benchmark session kinds
# use, so what is on screen is real code with real syntax colouring
# rather than synthetic text.
#
# Usage:  codescroll10.sh [lines-per-tick] [seconds-per-tick]
#         defaults 1 and 0.1, i.e. 10 Hz
# Quit:   q, or Ctrl-C

set -u
CORPUS=${CK_CORPUS:-/usr/local/share/code_corpus.ansi}
STEP=${1:-1}
DELAY=${2:-0.1}
BG=$'\033[48;2;0;43;54m'
FG=$'\033[38;2;131;148;150m'

case "$STEP" in
    ''|*[!0-9]*) STEP=1 ;;
esac
[ "$STEP" -lt 1 ] && STEP=1

if [ ! -s "$CORPUS" ]; then
    printf '\033[41m\033[2J\033[H'
    printf '  NO CODE CORPUS at %s\n\n' "$CORPUS"
    printf '  This payload scrolls a pre-generated syntax-coloured corpus.\n'
    printf '  Without it there is nothing to scroll and any judgement of\n'
    printf '  smoothness would be about an empty screen. Refusing to run.\n'
    printf '\033[0m'
    exit 1
fi

mapfile -t L < "$CORPUS"
N=${#L[@]}
i=0
n=0

tput civis 2>/dev/null
stty -echo 2>/dev/null

cleanup()
{
    stty echo 2>/dev/null
    tput cnorm 2>/dev/null
    printf '\033[0m\n'
}
trap cleanup EXIT INT TERM

printf '%s%s\033[2J\033[H' "$BG" "$FG"
printf 'codescroll10 -- %s lines every %ss (%s corpus lines). q quits.\n' \
    "$STEP" "$DELAY" "$N"
sleep 1

while true; do
    k=0
    out=''
    while [ "$k" -lt "$STEP" ]; do
        out="$out$BG${L[$((i % N))]}$BG"$'\n'
        i=$((i + 1))
        k=$((k + 1))
    done
    n=$((n + 1))
    printf '%s' "$out"
    printf '\033]0;codescroll10 frame %d\007' "$n"
    read -rsn1 -t "$DELAY" key 2>/dev/null && [ "$key" = q ] && break
done
