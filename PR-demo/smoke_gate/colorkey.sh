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
#
# c = EIGHT-COLOUR CYCLE and s = SLIDING BLOCK are the two INTERACTIVE modes
# (added 2026-08-07): unlike r/g/b/w/e they emit frames continuously, without
# a keypress per frame, so a human watching from a real Windows (UWP) or macOS
# client can judge ORDER, COMPLETENESS and MOTION by eye. Both run until any
# key is pressed, and that key is then acted on as the next command -- s while
# c is running starts the block, q quits the app, r/g/b/w/e paint. Both
# keep the same running count n -- in these modes one animation step = one
# full-screen write = one damage = one frame = n+1 -- and both append one line
# per frame to /tmp/ab/keylog.txt, so the log says what the screen SHOULD be
# showing at any instant.
#
# c: cycles the full screen through the eight RGB corners in this FIXED order
#    black red green blue yellow magenta cyan white, one colour per frame, at
#    CK_CYCLE_MS ms per frame (default 250 ms = 4 frames/s). The colour name,
#    the step index within the cycle (1..8), the cycle number and n are drawn
#    on every frame, so a fault can be reported precisely ("stopped on frame
#    37 showing magenta when it should be cyan"). Reading the failures:
#      - a colour OUT OF ORDER (e.g. cyan before magenta), or the step index
#        jumping by more than 1 => frames are being reordered or dropped;
#      - a colour that NEVER APPEARS, or n on screen lagging the last line in
#        keylog.txt => a frame was withheld by flow control;
#      - a WRONG HUE with the order intact (yellow looking green, magenta
#        looking blue, i.e. one of R/G/B missing from a mixed corner) => a
#        chroma-plane problem in the encoder/decoder, NOT flow control. The
#        eight corners are chosen so a swapped or dropped chroma plane is a
#        different colour NAME, not a subtle tint.
#
# s: a solid white block on a blue background, stepping CK_SLIDE_STEP columns
#    to the right per frame (default 4) at CK_SLIDE_MS ms per frame (default
#    40 ms = 25 frames/s), wrapping to the left edge, with n drawn on the
#    block. The block defaults to a tenth of the screen, capped at 40x20
#    cells and floored at 8x3 (CK_BLOCK_W and CK_BLOCK_H force a fixed size
#    in cells instead), so it is legible at 4K as well as at 1024x768; its
#    actual cell size is printed on every frame.
#    LIMITATION, stated on screen rather than glossed: a terminal can
#    only address CHARACTER CELLS, so this steps by whole cells, not pixels --
#    with the 6x13 xterm font the gate's geometry assumes (keytest.sh COLS/
#    ROWS math) 4 columns is about 24 px, so the block advances in ~24 px
#    hops, roughly 600 px/s. It is a fixed-size hop per frame, which is what
#    makes stutter and drops legible; it is not sub-pixel-smooth motion and
#    must not be described as such. Reading the failures:
#      - EVEN hops, identical spacing every frame => healthy;
#      - STUTTER (the block pauses, then resumes at the next position) => a
#        frame was withheld/late; the position sequence is still complete;
#      - a JUMP (the block reappears several hops further right, having never
#        been drawn in between, n skipping by more than 1) => frames were
#        dropped or damage was coalesced;
#      - TEARING (top and bottom halves of the block at different x in ONE
#        screen) => a partial/torn frame reached the client, i.e. the frame
#        was presented before it was complete -- a different fault from both
#        of the above.
#
# Knobs (env): CK_CYCLE_MS, CK_SLIDE_MS, CK_SLIDE_STEP, CK_BLOCK_W,
# CK_BLOCK_H. All are optional; the defaults are what the on-screen legend
# states.
#
# The two rates are a FLOOR on the frame period, not a guarantee: the pacing
# is the timeout on the same read that watches for a keypress, so each step
# also costs the shell's own work (building the string, one date, one append
# to the log). Measured locally in a plain 80x24 pty on this dev box,
# 2026-08-07: c ran at a median 253-255 ms against the 250 ms asked for, s at
# a median 42 ms against 40 ms. A step SLOWER than that by a visible margin
# is the pipeline, not the script.
mkdir -p /tmp/ab 2>/dev/null
stty -echo 2>/dev/null; tput civis 2>/dev/null
declare -A C=( [r]=41 [g]=42 [b]=44 [w]=47 )
# the eight RGB corners, in the order the c mode walks them
CYC_NAME=( black red green blue yellow magenta cyan white )
CYC_BG=( 40 41 42 44 43 45 46 47 )
CYC_FG=( 37 37 30 37 30 37 30 30 )
CYC_MS=${CK_CYCLE_MS:-250}
SLIDE_MS=${CK_SLIDE_MS:-40}
SLIDE_STEP=${CK_SLIDE_STEP:-4}
BLOCK_W=${CK_BLOCK_W:-0}
BLOCK_H=${CK_BLOCK_H:-0}
# reject anything non-numeric or zero: these become read -t timeouts and an
# unusable value would spin the loop with no pacing at all
[[ $CYC_MS =~ ^[0-9]+$ ]] && [ "$CYC_MS" -ge 1 ] || CYC_MS=250
[[ $SLIDE_MS =~ ^[0-9]+$ ]] && [ "$SLIDE_MS" -ge 1 ] || SLIDE_MS=40
[[ $SLIDE_STEP =~ ^[0-9]+$ ]] && [ "$SLIDE_STEP" -ge 1 ] || SLIDE_STEP=4
[[ $BLOCK_W =~ ^[0-9]+$ ]] || BLOCK_W=0
[[ $BLOCK_H =~ ^[0-9]+$ ]] || BLOCK_H=0
printf -v CYC_S '%d.%03d' $((CYC_MS / 1000)) $((CYC_MS % 1000))
printf -v SLIDE_S '%d.%03d' $((SLIDE_MS / 1000)) $((SLIDE_MS % 1000))
n=0
# lg: append one legend line to $out at row/col, CLIPPED to the terminal
# width in $cols. A line longer than the screen wraps onto the next row,
# where the following cursor-addressed line then covers only part of it and
# the legend turns to mush (seen at 20 columns before this existed). Every
# legend line in the animated modes goes through here, so the text degrades
# by truncation at any width instead.
lg()
{
    local row=$1
    local col=$2
    local txt=$3
    local max=$((cols - col + 1))
    [ "$max" -lt 1 ] && return
    out+=$'\033['"$row"';'"$col"'H'"${txt:0:max}"
}
# The key that stops an animated mode is not thrown away: it is handed back
# to the dispatch loop as $pending and acted on, so s during c goes straight
# to the sliding block and q during either quits. Keys arriving while nothing
# is animating are read exactly as before (one blocking read per keypress).
# Enter is the one exception: a bare newline reads as an EMPTY key, so it
# stops the mode and nothing is dispatched.
pending=""
# opening legend. It stays in the TOP-LEFT corner on a black screen: the
# gate synchronises on the centre of this screen classifying as black
# (keytest.sh "colorkey screen never displayed"), so nothing may be drawn
# near the middle before the first keypress.
# Every legend line is also kept under ~72 characters so it fits an ordinary
# 80-column terminal without needing the clip.
cols=$(tput cols)
out=$'\033[40m\033[2J\033[H\033[37m'
lg 1 3 "press r / g / b / w / e / c / s   (q quits)"
lg 2 3 "r g b w = flat red / green / blue / white screen"
lg 3 3 "e = red-blue chroma stripes (4:2:0 washes them out)"
lg 4 3 "c = 8-colour cycle, $CYC_MS ms per colour, order:"
lg 5 5 "black red green blue yellow magenta cyan white"
lg 6 3 "s = sliding block, $SLIDE_STEP cols and $SLIDE_MS ms per frame"
lg 7 3 "c and s run until any key is pressed; that key is"
lg 8 5 "then acted on (s during c starts the block, q quits)"
printf '%s' "$out"
while :; do
    if [ -n "$pending" ]; then
        k=$pending
        pending=""
    else
        IFS= read -rsn1 k || break
    fi
    [ "$k" = "q" ] && break
    if [ "$k" = "c" ]; then
        # eight-colour cycle: one corner colour per frame, fixed order,
        # looping until a key arrives (the read timeout IS the frame pacing)
        i=0
        cycle=1
        cols=$(tput cols); rows=$(tput lines)
        mid=$((rows / 2))
        [ "$mid" -lt 5 ] && mid=5
        while :; do
            n=$((n+1))
            name=${CYC_NAME[$i]}
            echo "$(date +%H:%M:%S.%3N) key=c n=$n step=$((i+1))/8 $name" \
                >> /tmp/ab/keylog.txt
            out=$'\033['"${CYC_BG[$i]}"$'m\033[2J\033[H\033['
            out+="${CYC_FG[$i]}"$'m'
            lg 1 3 "c: 8-colour cycle, order: black red green blue \
yellow magenta cyan white"
            lg 2 3 "step $((i+1))/8  cycle $cycle  n=$n  $CYC_MS ms per colour"
            lg 3 3 "(any key stops and is acted on, q quits)"
            lg "$mid" 3 "${name^^}"
            lg $((mid + 2)) 3 "frame n=$n  step $((i+1)) of 8"
            printf '%s' "$out"
            i=$((i+1))
            if [ "$i" -ge 8 ]; then
                i=0
                cycle=$((cycle+1))
            fi
            if IFS= read -rsn1 -t "$CYC_S" k2; then
                pending=$k2
                break
            fi
        done
        continue
    fi
    if [ "$k" = "s" ]; then
        # sliding block: solid white block on blue, stepping whole CHARACTER
        # CELLS (a terminal cannot address pixels -- see the header), one
        # full-screen write per step, until a key arrives
        cols=$(tput cols); rows=$(tput lines)
        # block size defaults to a tenth of the screen (capped at 40x20
        # cells, floored at 8x3) so it is legible at 4K as well as at
        # 1024x768; CK_BLOCK_W/H override with a fixed size in CELLS.
        # Both are then clamped so the block always fits below the six
        # legend lines and inside the screen width.
        bw=$BLOCK_W
        if [ "$bw" -eq 0 ]; then
            bw=$((cols / 10))
            [ "$bw" -lt 8 ] && bw=8
            [ "$bw" -gt 40 ] && bw=40
        fi
        [ "$bw" -gt "$cols" ] && bw=$cols
        bh=$BLOCK_H
        if [ "$bh" -eq 0 ]; then
            bh=$((rows / 8))
            [ "$bh" -lt 3 ] && bh=3
            [ "$bh" -gt 20 ] && bh=20
        fi
        [ "$bh" -gt $((rows - 8)) ] && bh=1
        pad=""
        for ((i = 0; i < bw; i++)); do pad+=" "; done
        top=$(((rows - bh) / 2))
        [ "$top" -lt 8 ] && top=8
        x=0
        while :; do
            n=$((n+1))
            echo "$(date +%H:%M:%S.%3N) key=s n=$n col=$x" \
                >> /tmp/ab/keylog.txt
            printf -v lbl '%-*s' "$bw" "n=$n"
            lbl=${lbl:0:bw}
            out=$'\033[44m\033[2J\033[H\033[37m'
            lg 1 3 "s: sliding block  n=$n  col=$x of $cols"
            lg 2 3 "block ${bw}x${bh} cells, $SLIDE_STEP cols and \
$SLIDE_MS ms per frame"
            lg 3 3 "steps whole character cells (~6 px each), NOT pixels:"
            lg 4 5 "even hops = ok    pause = frame withheld"
            lg 5 5 "jump = frames dropped    torn block = partial frame"
            lg 6 3 "(any key stops and is acted on, q quits)"
            out+=$'\033[47;30m'
            out+=$'\033['"$top"';'"$((x+1))"'H'"$lbl"
            for ((r = 1; r < bh; r++)); do
                out+=$'\033['"$((top+r))"';'"$((x+1))"'H'"$pad"
            done
            printf '%s' "$out"
            x=$((x + SLIDE_STEP))
            [ $((x + bw)) -gt "$cols" ] && x=0
            if IFS= read -rsn1 -t "$SLIDE_S" k2; then
                pending=$k2
                break
            fi
        done
        continue
    fi
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
