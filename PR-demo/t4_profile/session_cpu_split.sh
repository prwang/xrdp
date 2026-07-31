#!/bin/sh
# session_cpu_split.sh — where the session's CPU goes, split between the
# X server, the payload, and xrdp's encoder side, measured without
# perturbing it.
#
# xorg_cpu_probe.sh answers "is Xorg saturated?". This answers the
# follow-on question the E5-2 attribution actually needs: when the worker
# is only ~50 % busy and half of every period is spent WAITING for damage,
# who is not producing it — the payload, or the X server it has to push
# its damage through?
#
# Same discipline as xorg_cpu_probe.sh and for the same measured reason:
# on 4 vCPUs a `top -b -d 2` sampler moved the batched arm's send interval
# from 47.6 ms to 71.3 ms, an error larger than the effect under test. So
# this reads each /proc/<pid>/stat exactly TWICE, WINDOW seconds apart.
# No polling, no sampling profiler.
#
# Installed onto the T4 and run as ONE non-interactive command (CLAUDE.md
# "T4 test methodology" — no bash-over-ssh heredocs):
#   ssh -n -i $KEY $T4 'sh /usr/local/bin/session_cpu_split.sh'
#
# Env: E52_USER (default ubuntu), E52_SETTLE (25), E52_WINDOW (60).
set -u
U=${E52_USER:-ubuntu}
SETTLE=${E52_SETTLE:-25}
WINDOW=${E52_WINDOW:-60}
HZ=$(getconf CLK_TCK 2>/dev/null || echo 100)

cpu_of() {   # cpu_of <pid> -> utime+stime in ticks, or empty
    [ -d "/proc/$1" ] || return 1
    sed 's/.*) //' "/proc/$1/stat" 2>/dev/null \
        | awk '{print $12 + $13}'
}

sum_of() {   # sum_of <pid list> -> total ticks
    t=0
    for p in $1; do
        v=$(cpu_of "$p" 2>/dev/null) || continue
        [ -n "$v" ] && t=$((t + v))
    done
    echo "$t"
}

sleep "$SETTLE"

XORG=$(pgrep -u "$U" -x Xorg | head -1)
if [ -z "$XORG" ]; then
    echo "session_cpu_split: no session Xorg for $U — is a session up?" >&2
    exit 1
fi
# the payload is whatever is drawing: textflood, or an xterm flood
PAYLOAD=$(pgrep -u "$U" -x textflood | head -1)
PAYNAME=textflood
if [ -z "$PAYLOAD" ]; then
    PAYLOAD=$(pgrep -u "$U" -x xterm | head -1)
    PAYNAME=xterm
fi
# xrdp's encoder side: the ffmpeg/nvenc children xrdp spawns
ENC=$(pgrep -f 'xrdp-ffmpeg-shim|ffmpeg' | tr '\n' ' ')
NCPU=$(nproc)

x0=$(cpu_of "$XORG")
p0=$(cpu_of "$PAYLOAD" 2>/dev/null || echo 0)
e0=$(sum_of "$ENC")
t0=$(date +%s.%N)
sleep "$WINDOW"
x1=$(cpu_of "$XORG")
p1=$(cpu_of "$PAYLOAD" 2>/dev/null || echo 0)
e1=$(sum_of "$ENC")
t1=$(date +%s.%N)

echo "session_cpu_split: window ${WINDOW}s, ${NCPU} vCPUs, payload=$PAYNAME"
awk -v x0="$x0" -v x1="$x1" -v p0="$p0" -v p1="$p1" \
    -v e0="$e0" -v e1="$e1" -v t0="$t0" -v t1="$t1" \
    -v hz="$HZ" -v n="$NCPU" 'BEGIN {
    w = t1 - t0;
    xs = (x1 - x0) / hz; ps = (p1 - p0) / hz; es = (e1 - e0) / hz;
    printf "  Xorg (capture + X)   %6.2f s = %5.1f %% of a core\n", xs, 100*xs/w;
    printf "  payload              %6.2f s = %5.1f %% of a core\n", ps, 100*ps/w;
    printf "  xrdp encoder side    %6.2f s = %5.1f %% of a core\n", es, 100*es/w;
    printf "  ------------------------------------------------\n";
    printf "  measured total       %6.2f s = %5.1f %% of a core"    \
           " = %.2f of %d cores\n", xs+ps+es, 100*(xs+ps+es)/w, (xs+ps+es)/w, n;
    printf "\n";
    if (100*xs/w > 85)
        printf "  -> Xorg is SATURATED: the single X thread is the ceiling.\n";
    else if (100*ps/w > 85)
        printf "  -> the PAYLOAD is saturated: it cannot generate damage any\n" \
               "     faster, so the measured interval is the rate of the\n" \
               "     PAYLOAD, not of the pipeline. Not a conclusive run.\n";
    else
        printf "  -> neither Xorg (%.1f%%) nor the payload (%.1f%%) is pinned:\n" \
               "     the limit is elsewhere (encode service, flow control, or\n" \
               "     a serialisation between them). Do NOT attribute to CPU.\n", \
               100*xs/w, 100*ps/w;
}'
