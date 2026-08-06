#!/bin/bash
# e7_drag_sweep.sh — BACKLOG #45 gate E7: the dual-monitor drag gate.
#
# WHAT IT PROVES. The 251bc4d incident froze ONE monitor while the other
# kept flowing, during a window drag that crossed the monitor boundary.
# A whole-session liveness check PASSES that bug, which is why this gate
# asserts PER-MONITOR frame progress: every monitor's own rect stream
# must advance during the sweep, and a monitor that goes quiet for longer
# than E7_STALL_MS while the other keeps sending is a FAILURE.
#
# HOW. E3's offscreen rig (host dummy X server, 2560x1440 + 3840x2400)
# with the REAL rendering client — E7 is a fidelity-and-liveness gate, so
# the oracle client is not allowed here. The load is the orbiting-window
# driver of PR-demo/t4_profile/profile_owner_load.sh reduced to what this
# gate needs: a window swept back and forth ACROSS the boundary, with
# xdotool, inside the session (server side, in the pod).
#
# AXIS. The owner's real layout is stacked, so the incident's drag was
# up/down. This rig's E3 layout is side by side, so the axis that
# actually crosses the boundary here is X. E7_AXIS=x|y overrides; the
# default is chosen from the layout, and the axis used is printed.
#
# Per-monitor progress is read from the server's own GFX_TRACE log lines
# (surface id per monitor), which needs XRDP_GFX_TRACE=1 in the arm's
# env. No client-side inference and no screenshots: the question is
# whether the SERVER kept sending for both monitors.
#
# Usage: e7_drag_sweep.sh [seconds]      (default 60)
set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-60}
ARM=${E_ARM:-arm-r}
PORT=${E_PORT:-40017}
NS=${E_NS:-bisect-matrix}
SU=${E_USER:-probe444}
CRED=${E_CRED_FILE:-/root/.oracle_cred}
CLI=${E_DISPLAY:-:94}
FRDP=${E_XFREERDP:-xfreerdp3}
MMCONF=${E_MULTIMON_DIR:-$D/../multimon_offline}
XCONF=${E_XORG_CONF:-$MMCONF/xorg-dummy-2mon-4k.conf}
STALL_MS=${E7_STALL_MS:-2000}
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${E_OUT:-$D/captures/e7_drag_$STAMP}
mkdir -p "$OUT"

fail() { echo "ABORT: $*" >&2; exit 1; }

[ -s "$CRED" ] || fail "no probe credential at $CRED"
command -v "$FRDP" >/dev/null || fail "$FRDP not on the host"
POD=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
      -o jsonpath='{.items[0].metadata.name}') || fail "no $ARM pod"
[ -n "$POD" ] || fail "no running pod for $ARM"
kubectl -n "$NS" exec "$POD" -- sh -c 'command -v xdotool' >/dev/null 2>&1 \
    || fail "the $ARM image has no xdotool — rebuild it (Containerfile)"
echo "arm=$ARM pod=$POD port=$PORT secs=$SECS out=$OUT"

# --- the E3 rig ----------------------------------------------------------
if ! DISPLAY=$CLI xrandr --query >/dev/null 2>&1; then
    setsid Xorg "$CLI" -config "$XCONF" -noreset \
        -logfile "$OUT/client-xorg.log" </dev/null >/dev/null 2>&1 &
    sleep 4
fi
DISPLAY=$CLI \
    MM_MODE0=${E_MODE0:-2560x1440_60} MM_MODE1=${E_MODE1:-3840x2400R} \
    MM_POS1=${E_POS1:-2560x0} \
    MM_MODELINE0=${E_MODELINE0:-312.25 2560 2752 3024 3488 1440 1443 1448 1493 -hsync +vsync} \
    MM_MODELINE1=${E_MODELINE1:-592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync} \
    bash "$MMCONF/setup_monitors.sh" >"$OUT/client-monitors.txt" 2>&1 \
    || { cat "$OUT/client-monitors.txt"; fail "client did not present 2 monitors"; }
tail -1 "$OUT/client-monitors.txt"

# xrdp logs to /var/log/xrdp.log INSIDE the pod (xrdp.ini LogFile), not to
# the container's stdout: kubectl logs carries only the entrypoint's own
# output, which is why a first version of this harness reported zero
# GFX_TRACE records on a session that was in fact running.
MARK_P=$(kubectl -n "$NS" exec "$POD" -- \
    bash -lc 'wc -l < /var/log/xrdp.log 2>/dev/null' | tr -d ' \r')
MARK_P=${MARK_P:-0}
# A COLD session, by default: xrdp reconnects to an EXISTING session, and
# a fleet pod that has been up for hours may have one whose scrolling
# xterm is long dead -- a 25 s control run on such a session produced 28
# pictures (1.1 sends/s) and would have made E2's >= 1000 pairs
# impossible. This is a disposable probe444 session in a test pod, never
# the owner's: log it off and let sesman build a fresh one.
if [ "${E_COLD:-1}" = 1 ]; then
    kubectl -n "$NS" exec "$POD" -- bash -lc \
        "pkill -TERM -u $SU -x xterm; pkill -TERM -u $SU Xorg" >/dev/null 2>&1
    kubectl -n "$NS" exec "$POD" -- bash -lc \
        "for i in \$(seq 1 25); do pgrep -u $SU -x Xorg >/dev/null || break; \
         sleep 1; done" >/dev/null 2>&1
fi
PW=$(cat "$CRED")
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:$SU" "/p:$PW" "/multimon" \
                        "/gfx:AVC444" "/cert:ignore" "/log-level:WARN")
setsid env DISPLAY=$CLI RDPARGS="$RDPARGS" \
    "$FRDP" /args-from:env:RDPARGS </dev/null >"$OUT/client.log" 2>&1 &
CLIENT_PGID=$!
unset PW RDPARGS
sleep 8
# The gate's subject is per-monitor progress DURING THE SWEEP. Session
# startup is a different phenomenon and must not be scored as a stall:
# measured 2026-07-29, the 3840x2400 monitor's encoder pair spawns after
# the 2560x1440 one and its second frame lands ~4.7 s after connect while
# the small monitor is already streaming. That transient is REPORTED
# separately below rather than discarded, and the assertion runs on
# sends after this mark.
SWEEP_T0=$(date -u +%H:%M:%S.%N | awk -F: '{print $1*3600 + $2*60 + $3}')

# --- the sweep, inside the session --------------------------------------
# One non-interactive exec (CLAUDE.md: no bash-over-ssh heredoc
# iteration). The sweep runs for the whole window and is killed with the
# client.
AXIS=${E7_AXIS:-x}
kubectl -n "$NS" exec "$POD" -- bash -lc "
set -e
DISP=\$(ls /tmp/.X11-unix/ | head -1 | sed 's/^X/:/')
export DISPLAY=\$DISP
XA=\$(ls /home/$SU/.Xauthority 2>/dev/null || true)
[ -n \"\$XA\" ] && export XAUTHORITY=\$XA
WID=\$(su -s /bin/bash $SU -c \"DISPLAY=\$DISP xdotool search --onlyvisible \
    --name . 2>/dev/null | tail -1\")
echo \"display=\$DISP window=\$WID\"
[ -n \"\$WID\" ] || { echo 'no window in the session'; exit 1; }
su -s /bin/bash $SU -c \"DISPLAY=\$DISP xdotool windowsize \$WID 1400 1000\"
# Cross the boundary: monitor 0 is 2560 wide, monitor 1 starts at 2560.
# Sweep the window from well inside monitor 0 to well inside monitor 1
# and back, so its area straddles the seam for most of the trace.
su -s /bin/bash $SU -c \"DISPLAY=\$DISP bash -c '
for r in \\\$(seq 1 200); do
  for x in 800 1400 2000 2400 2600 3000 3400 3000 2600 2400 2000 1400; do
    if [ \\\"$AXIS\\\" = x ]; then
      xdotool windowmove \$WID \\\$x 300
    else
      xdotool windowmove \$WID 1200 \\\$x
    fi
    sleep 0.05
  done
done'\" >/dev/null 2>&1 &
echo started
sleep $SECS
pkill -f 'xdotool windowmove' 2>/dev/null || true
" >"$OUT/sweep.log" 2>&1 &
SWEEP=$!
echo "sweeping for ${SECS}s (axis $AXIS) ..."
sleep "$SECS"
wait $SWEEP 2>/dev/null
kill -9 -- -"$CLIENT_PGID" 2>/dev/null
sleep 1

kubectl -n "$NS" exec "$POD" -- cat /var/log/xrdp.log 2>/dev/null \
    | tail -n +$((MARK_P + 1)) > "$OUT/xrdp.log"
grep -a "GFX_TRACE" "$OUT/xrdp.log" > "$OUT/gfx_trace.txt" 2>/dev/null
head -3 "$OUT/sweep.log"

python3 - "$OUT/gfx_trace.txt" "$STALL_MS" "$SWEEP_T0" <<'PY' | tee "$OUT/VERDICT.txt"
import re
import sys

TS = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)\.(\d+)")
# the trace line is "GFX_TRACE avc dmg surface=N num_rects=..." -- two
# tokens before the field, and other GFX_TRACE lines (ack, batch) carry
# no surface at all. Match the field itself, not a token count.
SURF = re.compile(r"GFX_TRACE avc dmg surface=(\d+)")


def secs(line):
    m = TS.match(line)
    if not m:
        return None
    return (int(m.group(4)) * 3600 + int(m.group(5)) * 60 + int(m.group(6))
            + float("0." + m.group(7)))


per = {}
for line in open(sys.argv[1], errors="replace"):
    m = SURF.search(line)
    t = secs(line)
    if m is None or t is None:
        continue
    per.setdefault(int(m.group(1)), []).append(t)

stall_ms = float(sys.argv[2])
t0 = float(sys.argv[3]) if len(sys.argv) > 3 else 0.0
print("=== E7 — per-monitor progress during the boundary sweep ===")
print("sweep window starts at %.3f (session startup before it is reported "
      "separately, not scored)" % t0)
startup = {}
for surf in sorted(per):
    early = [t for t in per[surf] if t < t0]
    gaps = [b - a for a, b in zip(sorted(early), sorted(early)[1:])]
    startup[surf] = (len(early), max(gaps) * 1000.0 if gaps else 0.0)
    per[surf] = [t for t in per[surf] if t >= t0]
for surf in sorted(startup):
    print("  startup: surface %d %d sends, worst gap %.0f ms"
          % (surf, startup[surf][0], startup[surf][1]))
per = dict((k, v) for k, v in per.items() if len(v) > 1)
if len(per) < 2:
    print("RED: only %d surface(s) sent anything (%s). E7 needs BOTH "
          "monitors' streams; a whole-session check would have passed "
          "this." % (len(per), sorted(per)))
    raise SystemExit(1)
bad = []
for surf in sorted(per):
    t = sorted(per[surf])
    span = t[-1] - t[0]
    gaps = [b - a for a, b in zip(t, t[1:])]
    worst = max(gaps) * 1000.0 if gaps else 0.0
    print("surface %-4d sends %-6d span %6.1f s  worst gap %7.1f ms  "
          "mean %6.1f ms" % (surf, len(t), span,
                             worst, (span / len(gaps) * 1000.0) if gaps else 0))
    if worst > stall_ms:
        bad.append((surf, worst))
print()
if bad:
    for surf, worst in bad:
        print("RED: surface %d stalled %.0f ms (> %.0f ms) while the other "
              "monitor kept sending — the 251bc4d shape" % (surf, worst,
                                                            stall_ms))
    raise SystemExit(1)
print("E7 VERDICT: PASS — both monitors' rect streams advanced throughout "
      "the sweep, no stall beyond %.0f ms" % stall_ms)
PY
echo
echo "evidence: $OUT"
