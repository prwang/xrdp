#!/bin/bash
# Keystroke-driven full-screen colour test. r=red g=green b=blue w=white.
# Each keypress paints the WHOLE screen in ONE terminal write => one damage =>
# one GFX frame, tagged with a running count n. If the pipeline withholds the
# tail frame, the screen shows the PREVIOUS key's colour and count (n lags by 1)
# until you press the next key. Nothing is drawn between keypresses, so no other
# damage can flush a withheld frame -- the lag is unambiguous.
#
# e = colour-EDGE pattern (FR-PROC-7 fidelity-after-settle): the screen fills
# with alternating half-cell red/blue vertical stripes. Flat colours look the
# same at 4:2:0 and 4:4:4; narrow opposing-chroma stripes wash out at 4:2:0,
# so a screenshot taken after the pipeline settles must show full saturation
# (the deferred LC=2 aux has landed) or chroma is stuck at 4:2:0.
stty -echo 2>/dev/null; tput civis 2>/dev/null
declare -A C=( [r]=41 [g]=42 [b]=44 [w]=47 )
n=0
printf '\033[40m\033[2J\033[H\033[37m  press r / g / b / w / e  (q quits)'
while IFS= read -rsn1 k; do
    [ "$k" = "q" ] && break
    if [ "$k" = "e" ]; then
        n=$((n+1))
        echo "$(date +%H:%M:%S.%3N) key=e n=$n" >> /tmp/ab/keylog.txt
        cols=$(tput cols); rows=$(tput lines)
        line=""
        for ((i = 0; i < cols; i++)); do line+=$'▌'; done
        out=$'\033[2J\033[H\033[31;44m'
        for ((r = 0; r < rows; r++)); do out+="$line"; done
        printf '%s' "$out"
        continue
    fi
    code=${C[$k]}
    [ -z "$code" ] && continue
    n=$((n+1)); echo "$(date +%H:%M:%S.%3N) key=$k n=$n" >> /tmp/ab/keylog.txt
    printf '\033[%sm\033[2J\033[H\033[30m  key=%s  n=%s' "$code" "$k" "$n"
done
stty echo 2>/dev/null; tput cnorm 2>/dev/null
