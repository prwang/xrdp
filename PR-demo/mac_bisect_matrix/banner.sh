#!/bin/bash
# Full-screen arm banner, color-diagnostic edition: every colour patch is
# LABELED with its name, so a chroma/packing fault is self-evident to the
# tester ("the bar that says RED is blue") without knowing what the
# desktop should look like. The tick line keeps frames flowing (a stalled
# tick = pipeline stall, black = ack starvation).
ARM=$(cat /etc/arm_label 2>/dev/null || echo "unknown arm")

xsetroot -solid '#204060' || true

exec xterm -maximized -fa 'DejaVu Sans Mono' -fs 22 \
    -bg black -fg white -e bash -c '
    ARM="'"$ARM"'"
    i=0
    tput civis 2>/dev/null
    while true; do
        clear
        echo
        echo "  ================================================="
        echo "   BISECT ARM: $ARM"
        echo "  ================================================="
        echo
        printf "   %-9s \e[41m%*s\e[0m\n"  "RED"     36 ""
        printf "   %-9s \e[42m%*s\e[0m\n"  "GREEN"   36 ""
        printf "   %-9s \e[44m%*s\e[0m\n"  "BLUE"    36 ""
        printf "   %-9s \e[43m%*s\e[0m\n"  "YELLOW"  36 ""
        printf "   %-9s \e[46m%*s\e[0m\n"  "CYAN"    36 ""
        printf "   %-9s \e[45m%*s\e[0m\n"  "MAGENTA" 36 ""
        printf "   %-9s \e[47m%*s\e[0m\n"  "WHITE"   36 ""
        echo
        printf "   fine red/blue stripes: "
        for s in $(seq 1 18); do printf "\e[41m \e[44m "; done
        printf "\e[0m\n"
        echo
        echo "   tick $i"
        i=$((i + 1))
        sleep 1
    done'
