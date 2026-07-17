#!/bin/bash
# Deploy smoke gate. Run AFTER every install/restart of xrdp on this box and
# BEFORE handing the rig to a human: full keystroke-colour test against the
# LIVE binary+config via xfreerdp, asserting every keypress renders and the
# encoder logged no restarts during the run. Exit 0 = safe to hand over.
set -u
MARK=$(date '+%H:%M:%S')
bash /tmp/ab/keytest.sh >/tmp/ab/smoke_last.out 2>&1
ok=$(grep -c "  ok$" /tmp/ab/smoke_last.out)
lag=$(grep -c "LAG" /tmp/ab/smoke_last.out)
errs=$(awk -F'T' -v m="$MARK" '$2 >= m' /var/log/xrdp.log 2>/dev/null \
       | grep -cE "restarting encoder|sequence mismatch")
echo "smoke: ok=$ok lag=$lag encoder_errors=$errs"
if [ "$ok" -ge 8 ] && [ "$lag" -eq 0 ] && [ "$errs" -eq 0 ]; then
    echo "SMOKE PASS — safe to hand over"
    exit 0
fi
echo "SMOKE FAIL — do NOT hand over (see /tmp/ab/smoke_last.out and xrdp.log)"
exit 1
