#!/bin/sh
# xorg_cpu_probe.sh — how much of one core the session Xorg is using,
# measured without perturbing the thing being measured.
#
# Why not `top` (owner-relevant, 2026-07-30): on the T4's 4 vCPUs a
# `top -b -d 2` sampler running alongside an E5-2 capture moved the batched
# arm's mean send interval from 47.6 ms to 71.3 ms — a 50 % error, larger
# than the effect under test. A polling loop is not a free observer on a
# small box. This reads /proc/<pid>/stat exactly twice, 60 s apart, and
# divides: two file reads for the whole measurement.
#
# Installed onto the T4 and run as ONE non-interactive command (CLAUDE.md
# "T4 test methodology" — no bash-over-ssh heredocs):
#   scp -i $KEY xorg_cpu_probe.sh $T4:/tmp/ && ssh -n -i $KEY $T4 'sh /tmp/xorg_cpu_probe.sh'
# Start it just before the capture: it sleeps 25 s so the settle window
# lands inside a run whose session has finished logging in.
#
# Env: E52_USER (default ubuntu), E52_SETTLE (25), E52_WINDOW (60).
set -u
U=${E52_USER:-ubuntu}
SETTLE=${E52_SETTLE:-25}
WINDOW=${E52_WINDOW:-60}

sleep "$SETTLE"
p=$(pgrep -u "$U" -x Xorg | head -1)
[ -n "$p" ] || { echo "no session Xorg for $U"; exit 1; }

ticks() { awk '{print $14 + $15}' "/proc/$1/stat" 2>/dev/null; }

a=$(ticks "$p"); ta=$(date +%s.%N)
sleep "$WINDOW"
b=$(ticks "$p"); tb=$(date +%s.%N)
[ -n "${b:-}" ] || { echo "session Xorg $p went away mid-probe"; exit 1; }

hz=$(getconf CLK_TCK)
echo "xorg_pid=$p ticks=$((b - a)) hz=$hz wall=$(echo "$tb - $ta" | bc)"
echo "xorg_cpu_frac=$(echo "scale=3; ($b - $a) / $hz / ($tb - $ta)" | bc)"
