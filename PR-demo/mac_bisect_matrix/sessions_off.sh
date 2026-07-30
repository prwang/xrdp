#!/bin/bash
# sessions_off.sh — log off every fleet session and kill the client-side
# leftovers. RUN THIS AFTER A MEASUREMENT CAMPAIGN.
#
# Why it exists (owner report 2026-07-30): a fleet session keeps running its
# payload after the client goes away, so a pod that measured something hours
# ago is still scrolling a corpus at full tilt. Eight arms had accumulated
# sessions burning ~4 cores between them — two flood sessions alone were at
# 97 % and 89 % of a core, and old 10 Hz `code` sessions had been running for
# 23 h. Nothing needs them: e_gate_run.sh builds a COLD session as its first
# step (E_COLD=1), so leaving them up buys nothing and costs a core each.
#
# What it does, and does not do:
#   * logs the WHOLE session off inside each pod (the sanctioned operation —
#     CLAUDE.md GUI lifecycle: never pkill/relaunch individual GUI processes
#     in a live session);
#   * kills the dev-box client rig (the dummy Xorg and any surviving
#     xfreerdp/oracle client) BY PID, never by pattern: a `pkill -f` for a
#     client pattern matched this script's own shell on 2026-07-30 and killed
#     the cleanup mid-run;
#   * leaves the pods themselves running. They are ~0 CPU without a session
#     and their manifests are the record of what is deployed.
#
# Usage: sessions_off.sh [arm ...]      (no args = every pod in the namespace)
set -u
NS=${E_NS:-bisect-matrix}
SELF=$$

if [ "$#" -gt 0 ]; then
    PODS=""
    for arm in "$@"; do
        PODS="$PODS $(kubectl -n "$NS" get pod -l "arm=$arm" \
            -o jsonpath='{range .items[*]}{.metadata.name}{" "}{end}')"
    done
else
    PODS=$(kubectl -n "$NS" get pods \
        -o jsonpath='{range .items[*]}{.metadata.name}{" "}{end}')
fi

for p in $PODS; do
    [ -n "$p" ] || continue
    kubectl -n "$NS" exec "$p" -- bash -lc \
        'for u in probe444 tester; do
             pkill -TERM -u $u -x xterm 2>/dev/null
             pkill -TERM -u $u Xorg 2>/dev/null
         done; true' >/dev/null 2>&1
    echo "$p: session logged off"
done
sleep 4
for p in $PODS; do
    [ -n "$p" ] || continue
    n=$(kubectl -n "$NS" exec "$p" -- bash -lc \
        'pgrep -c -x Xorg 2>/dev/null || true' 2>/dev/null | tr -d ' \r')
    [ "${n:-0}" != 0 ] && echo "WARNING: $p still has ${n} session Xorg"
done

# --- dev-box client rig, by PID -------------------------------------------
for pid in $(pgrep -f "Xorg :9" || true) \
           $(pgrep -x xfreerdp3 || true) \
           $(pgrep -f "freerdp-vaapi/bin/xfreerdp" || true); do
    [ "$pid" = "$SELF" ] && continue
    kill -TERM "$pid" 2>/dev/null && echo "client rig: killed pid $pid"
done
sleep 2
echo "load: $(uptime | sed 's/.*load average/load average/')"
