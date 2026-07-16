#!/bin/bash
# paint 4 distinct full-screen colors, RED last, then idle (no prompt after).
# a withheld tail frame => the client shows the SECOND-to-last color, not red.
tput civis 2>/dev/null
sleep 0.4
printf '\033[44m\033[2J\033[H'; sleep 0.4   # blue
printf '\033[46m\033[2J\033[H'; sleep 0.4   # cyan
printf '\033[42m\033[2J\033[H'; sleep 0.4   # green
printf '\033[41m\033[2J\033[H'              # red  (FINAL single damage)
sleep 12
