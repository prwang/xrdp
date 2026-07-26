#!/bin/bash
# Full-screen arm banner: big label naming the arm + a colour field that
# steps every second. The motion guarantees a continuous frame stream (an
# ack-starvation blackout freezes/blanks it immediately); the label tells
# the tester which arm they are looking at without checking port numbers.
ARM=$(cat /etc/arm_label 2>/dev/null || echo "unknown arm")

xsetroot -solid '#204060' || true

run_banner() {
    exec xterm -maximized -fa 'DejaVu Sans Mono' -fs 26 \
        -bg black -fg white -e bash -c '
        ARM="'"$ARM"'"
        colors=(41 42 44 45 46 43)
        i=0
        tput civis 2>/dev/null
        while true; do
            c=${colors[$((i % 6))]}
            clear
            echo
            echo "  ================================================="
            echo "   BISECT ARM: $ARM"
            echo "  ================================================="
            echo
            for row in 1 2 3 4 5 6 7 8; do
                printf "   \e[%sm%*s\e[0m\n" "$c" 44 ""
            done
            echo
            echo "   tick $i"
            i=$((i + 1))
            sleep 1
        done'
}

run_banner
