#!/bin/bash
# Deploy smoke gate. Run AFTER every install/restart of xrdp on the T4 and
# BEFORE handing the rig to a human: full keystroke-colour test against the
# LIVE binary+config via xfreerdp, asserting every keypress renders and the
# encoder logged no restarts during the run. Exit 0 = safe to hand over.
#
# Runs FROM the dev box (CLAUDE.md "T4 test methodology"): the client side
# lives here, the T4 is reached over an ssh -L forward, and the encoder log
# is checked over ssh.
#
# TARGET (added 2026-07-29): SMOKE_TARGET=t4 (default) or =pod, which runs
# the same gate against a bisect-fleet arm on its host loopback port. E1
# needs the gate to run against whatever pair is actually deployed, and
# the T4 is not always up. A pod pass is NOT a T4 pass -- the T4 is the
# representative old-CPU box -- so the target is printed with the result.
set -u
TARGET=${SMOKE_TARGET:-t4}
NS=${SMOKE_NS:-bisect-matrix}
ARM=${SMOKE_ARM:-arm-r}
T4=${T4:-$(cat /root/.t4_host 2>/dev/null)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
if [ "$TARGET" = pod ]; then
    POD=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
          -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)
    [ -z "$POD" ] && { echo "ABORT: no running pod for $ARM"; exit 1; }
    LOGMARK=$(kubectl -n "$NS" logs "$POD" 2>/dev/null | wc -l | tr -d ' ')
else
    [ -z "$T4" ] && { echo "ABORT: set T4=user@host or /root/.t4_host"; exit 1; }
fi
D=$(cd "$(dirname "$0")" && pwd)
mkdir -p /tmp/ab
# Full date+time mark: a time-of-day-only mark matched OLD log lines from
# prior days whose clock time was later than the mark (false FAIL observed
# 2026-07-23 against errors logged 2026-07-17).
MARK=$(date '+%Y-%m-%dT%H:%M:%S')
# Colour-edge fidelity floor (FR-PROC-7 §8): settled chroma must be full
# 4:4:4. Calibrated 2026-07-26 on the T4 offscreen rig (xterm bitmap
# font, half-cell red/blue stripes) against the always-full-chroma 4B
# build 4932908b: observed 1.000 at both 1920x1080 and 1024x768; a
# 4:2:0-stuck screen washes far below this floor.
EDGE_MIN=${EDGE_MIN:-0.50}
pass=1
# Two session sizes, both mandatory: an encoder startup failure once keyed on
# resolution (ffmpeg probesize window) — green at 1920x1080, frozen at the
# mstsc default 1024x768.
for size in 1920x1080 1024x768; do
    KEYTEST_SIZE=$size bash "$D/keytest.sh" >/tmp/ab/smoke_$size.out 2>&1
    ok=$(grep -c "  ok$" /tmp/ab/smoke_$size.out)
    lag=$(grep -c "LAG" /tmp/ab/smoke_$size.out)
    edge=$(grep "EDGE_FIDELITY" /tmp/ab/smoke_$size.out | awk '{print $2}')
    if [ "$TARGET" = pod ]; then
        errs=$(kubectl -n "$NS" logs "$POD" 2>/dev/null \
               | tail -n +$((LOGMARK + 1)) \
               | grep -cE "restarting encoder|sequence mismatch")
    else
        errs=$(ssh -i "$T4_KEY" "$T4" \
                   "awk -v m='[$MARK' 'substr(\$1, 1, length(m)) >= m' \
                        /var/log/xrdp.log 2>/dev/null" \
               | grep -cE "restarting encoder|sequence mismatch")
    fi
    echo "smoke[$size]: target=$TARGET ok=$ok lag=$lag \
edge=${edge:-none} encoder_errors=$errs"
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
