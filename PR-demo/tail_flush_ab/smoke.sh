#!/bin/bash
# Deploy smoke gate. Run AFTER every install/restart of xrdp on this box and
# BEFORE handing the rig to a human: full keystroke-colour test against the
# LIVE binary+config via xfreerdp, asserting every keypress renders and the
# encoder logged no restarts during the run. Exit 0 = safe to hand over.
set -u
D=$(cd "$(dirname "$0")" && pwd)
mkdir -p /tmp/ab
MARK=$(date '+%H:%M:%S')
# Two session sizes, both mandatory: an encoder startup failure once keyed on
# resolution (ffmpeg probesize window) — green at 1920x1080, frozen at the
# mstsc default 1024x768.
pass=1
for size in 1920x1080 1024x768; do
    KEYTEST_SIZE=$size bash "$D/keytest.sh" >/tmp/ab/smoke_$size.out 2>&1
    ok=$(grep -c "  ok$" /tmp/ab/smoke_$size.out)
    lag=$(grep -c "LAG" /tmp/ab/smoke_$size.out)
    errs=$(awk -F'T' -v m="$MARK" '$2 >= m' /var/log/xrdp.log 2>/dev/null \
           | grep -cE "restarting encoder|sequence mismatch")
    echo "smoke[$size]: ok=$ok lag=$lag encoder_errors=$errs"
    if [ "$ok" -lt 8 ] || [ "$lag" -ne 0 ] || [ "$errs" -ne 0 ]; then
        pass=0
    fi
done
if [ "$pass" -eq 1 ]; then
    echo "SMOKE PASS — safe to hand over"
    exit 0
fi
echo "SMOKE FAIL — do NOT hand over (see /tmp/ab/smoke_*.out and xrdp.log)"
exit 1
