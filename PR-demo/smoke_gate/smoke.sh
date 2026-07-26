#!/bin/bash
# Deploy smoke gate. Run AFTER every install/restart of xrdp on this box and
# BEFORE handing the rig to a human: full keystroke-colour test against the
# LIVE binary+config via xfreerdp, asserting every keypress renders and the
# encoder logged no restarts during the run. Exit 0 = safe to hand over.
set -u
D=$(cd "$(dirname "$0")" && pwd)
mkdir -p /tmp/ab
# Full date+time mark: a time-of-day-only mark matched OLD log lines from
# prior days whose clock time was later than the mark (false FAIL observed
# 2026-07-23 against errors logged 2026-07-17).
MARK=$(date '+%Y-%m-%dT%H:%M:%S')
# Two session sizes, both mandatory: an encoder startup failure once keyed on
# resolution (ffmpeg probesize window) — green at 1920x1080, frozen at the
# mstsc default 1024x768.
# Colour-edge fidelity floor (FR-PROC-7 §8): settled chroma must be full
# 4:4:4. Provisional floor — CALIBRATE against an always-full-chroma build
# (record the observed value here) before trusting a pass near the line;
# a 4:2:0-stuck screen washes far below any calibrated floor.
EDGE_MIN=${EDGE_MIN:-0.50}
pass=1
for size in 1920x1080 1024x768; do
    KEYTEST_SIZE=$size bash "$D/keytest.sh" >/tmp/ab/smoke_$size.out 2>&1
    ok=$(grep -c "  ok$" /tmp/ab/smoke_$size.out)
    lag=$(grep -c "LAG" /tmp/ab/smoke_$size.out)
    edge=$(grep "EDGE_FIDELITY" /tmp/ab/smoke_$size.out | awk '{print $2}')
    errs=$(awk -v m="[$MARK" 'substr($1, 1, length(m)) >= m' \
               /var/log/xrdp.log 2>/dev/null \
           | grep -cE "restarting encoder|sequence mismatch")
    echo "smoke[$size]: ok=$ok lag=$lag edge=${edge:-none} encoder_errors=$errs"
    if [ "$ok" -lt 8 ] || [ "$lag" -ne 0 ] || [ "$errs" -ne 0 ]; then
        pass=0
    fi
    if [ -z "$edge" ] || awk -v e="$edge" -v m="$EDGE_MIN" \
            'BEGIN { exit !(e < m) }'; then
        echo "smoke[$size]: EDGE FAIL (${edge:-missing} < $EDGE_MIN)"
        pass=0
    fi
done
if [ "$pass" -eq 1 ]; then
    echo "SMOKE PASS — safe to hand over"
    exit 0
fi
echo "SMOKE FAIL — do NOT hand over (see /tmp/ab/smoke_*.out and xrdp.log)"
exit 1
