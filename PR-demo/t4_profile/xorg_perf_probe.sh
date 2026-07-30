#!/bin/sh
# xorg_perf_probe.sh — name the function that is burning the session Xorg's
# core, on the box where it matters (BACKLOG #55: the T4's E5-2 remainder is
# a single Xorg thread at 92 % of a core, and #54 asks which part of it).
#
# The xorgxrdp deb is built unstripped with debug_info, so a plain
# `perf record -g` resolves to function AND source line without shipping
# anything extra to the box.
#
# Installed onto the T4 by e52_t4_payload.sh and invoked as ONE
# non-interactive command (CLAUDE.md "T4 test methodology" — no
# bash-over-ssh heredocs):
#   ssh -n -i $KEY $T4 'sudo sh /tmp/xorg_perf_probe.sh 45'
#
# Start it just before a capture run: it waits for a session Xorg to exist
# and then records, so the window lands inside the run.
#
# Cost of the instrument: sampling at 499 Hz with call graphs is a few
# percent of one core — small, but NOT zero on a 4-vCPU box, so read the
# SHAPE of the profile from this and take absolute rates from an
# unprofiled run (a `top -d 2` sampler alone moved the E5-2 rate 50 %).
set -u
SECS=${1:-45}
U=${E52_USER:-ubuntu}
OUT=${E52_PERF_OUT:-/tmp/xorg_perf}
mkdir -p "$OUT"

n=0
while [ "$n" -lt 60 ]; do
    p=$(pgrep -u "$U" -x Xorg | head -1)
    [ -n "$p" ] && break
    n=$((n + 1))
    sleep 1
done
[ -n "${p:-}" ] || { echo "no session Xorg for $U after 60 s"; exit 1; }
echo "profiling xorg pid=$p for ${SECS}s"

perf record -F 499 -g --call-graph dwarf,4096 -p "$p" \
    -o "$OUT/perf.data" -- sleep "$SECS" >/dev/null 2>&1

echo "=== FLAT (self time, where the cycles actually are) ==="
perf report -i "$OUT/perf.data" --stdio --no-children -g none --percent-limit 0.4

echo
echo "=== CALLERS of the top self-time symbols ==="
perf report -i "$OUT/perf.data" --stdio --children -g graph,0.6,caller \
    --percent-limit 1.5 2>/dev/null | head -120

echo
echo "=== BY DSO (which library owns the time) ==="
perf report -i "$OUT/perf.data" --stdio --no-children --sort dso \
    --percent-limit 0.3
