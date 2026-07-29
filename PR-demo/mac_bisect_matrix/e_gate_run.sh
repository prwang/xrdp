#!/bin/bash
# e_gate_run.sh — run the BACKLOG #45 acceptance gates E2/E3/E4/E5 in one
# offscreen dual-monitor session and report each one separately.
#
# WHAT IT MEASURES, and with which instrument (the distinction #45 E5 is
# built on):
#
#   E5 oracle frame interval  MODE=oracle. The oracle client acks BEFORE
#                             decode and present, so the send-to-send
#                             interval seen at the server is the SERVER
#                             ceiling with the client contributing ~0.
#                             This is the gate metric. Baseline
#                             2026-07-29 (pre-steps-5..7): 51.1 ms mean
#                             per send at this geometry; >= 2.0x means
#                             <= 25.6 ms mean.
#   end-to-end rate           MODE=render. The distro xfreerdp3 decodes
#                             and presents. Recorded beside E5 every
#                             time as CONTEXT, never as the gate: the
#                             session is client-bound by 3.29x, so this
#                             number cannot move until the client side
#                             is addressed (out of #45 scope).
#   E2 wire properties        the oracle dump is run through
#                             tools/avc444_ltr_wire_audit.py --assert
#                             --intra-refresh N (no mid-stream IDR, cuts
#                             only on scheduled ordinals, paired across
#                             views, own-slot refs, one contiguous
#                             frame_num chain, depth <= N) and through
#                             oracle_black_frame_check.py (every picture
#                             decodes, zero black frames).
#   E2 server log             zero rewrite failures, zero "unsupported",
#                             zero pair aborts, zero budget assertions.
#   E3 geometry               2560x1440 + 3840x2400 by default, offscreen
#                             on the host's dummy X server.
#   E4 set size               the per-cycle poll-set size the worker
#                             logged (needs XRDP_GFX_TRACE=1 in the pod
#                             env, which the #45 arm sets).
#
# The oracle client is a TIMING AND SYNTAX instrument only: it renders
# nothing and proves no fidelity. E1 (smoke gate) and E7 (drag sweep)
# use the real rendering client and are separate scripts.
#
# Client side stays on the HOST (CLAUDE.md: the container fleet is the
# RDP SERVER side only). Nothing is installed into the pod.
#
# Credential: probe444's password lives ONLY in root-owned
# /root/.oracle_cred, is read into a variable and handed to the client
# through the environment, never as an argument and never printed.
#
# Usage: e_gate_run.sh [seconds]     (default 120; E2 wants >= 1000 pairs)
#   E_ARM=arm-r E_PORT=40017 E_MODE=oracle E_REFRESH=240 e_gate_run.sh 120
set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-120}
ARM=${E_ARM:-arm-r}
PORT=${E_PORT:-40017}
NS=${E_NS:-bisect-matrix}
SU=${E_USER:-probe444}
CRED=${E_CRED_FILE:-/root/.oracle_cred}
CLI=${E_DISPLAY:-:94}
FRDP=${E_XFREERDP:-xfreerdp3}
MODE=${E_MODE:-oracle}
REFRESH=${E_REFRESH:-240}
ORACLE_BIN=${E_ORACLE_BIN:-/opt/freerdp-vaapi/bin/xfreerdp}
MMCONF=${E_MULTIMON_DIR:-$D/../multimon_offline}
XCONF=${E_XORG_CONF:-$MMCONF/xorg-dummy-2mon-4k.conf}
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${E_OUT:-$D/captures/e_gate_${MODE}_$STAMP}
mkdir -p "$OUT"

fail() { echo "ABORT: $*" >&2; exit 1; }

[ -s "$CRED" ] || fail "no probe credential at $CRED"
POD=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
      -o jsonpath='{.items[0].metadata.name}') || fail "no $ARM pod"
[ -n "$POD" ] || fail "no running pod for $ARM"
echo "arm=$ARM pod=$POD port=$PORT mode=$MODE secs=$SECS out=$OUT"

# Record WHAT is deployed before measuring it: a gate result against an
# unknown build is not a gate result.
kubectl -n "$NS" exec "$POD" -- bash -lc \
    'dpkg -l | grep -E "xrdp-dev|xorgxrdp-dev"' \
    > "$OUT/deployed_packages.txt" 2>&1
kubectl -n "$NS" get pod "$POD" \
    -o jsonpath='{.spec.containers[0].image}{"\n"}' \
    > "$OUT/deployed_image.txt" 2>&1
kubectl -n "$NS" exec "$POD" -- cat /etc/xrdp/gfx.toml \
    > "$OUT/gfx.toml" 2>&1
# the recon build must be GONE (its gate is answered); if it is still
# there the arm is the wrong one
if kubectl -n "$NS" exec "$POD" -- \
        grep -qc R1SLOT /usr/lib/xorg/modules/libxorgxrdp.so 2>/dev/null
then
    fail "$ARM still carries the R1 recon xorgxrdp — wrong arm for a gate run"
fi

# --- client-side X server at the E3 target geometry ----------------------
if ! DISPLAY=$CLI xrandr --query >/dev/null 2>&1; then
    echo "starting dummy Xorg on $CLI ($(basename "$XCONF")) ..."
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

# --- mark both logs, then drive ONE multimon AVC444 login -----------------
# The session Xorg log outlives a client, and the pod log is the whole
# container's life: mark both and read only this run's window. Skipping
# this turned a 60 s measurement into 2.2 h of accumulated lines on
# 2026-07-29.
MARK_X=$(kubectl -n "$NS" exec "$POD" -- \
    bash -lc "wc -l < /home/$SU/.xorgxrdp.*.log 2>/dev/null | head -1" \
    | tr -d ' \r')
MARK_X=${MARK_X:-0}
MARK_P=$(kubectl -n "$NS" logs "$POD" 2>/dev/null | wc -l | tr -d ' ')
echo "log marks: session-xorg $MARK_X lines, pod $MARK_P lines"

PW=$(cat "$CRED")
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:$SU" "/p:$PW" "/multimon" \
                        "/gfx:AVC444" "/cert:ignore" "/log-level:WARN")
DUMPDIR=$OUT/oracle
if [ "$MODE" = oracle ]; then
    [ -x "$ORACLE_BIN" ] || fail "oracle client missing at $ORACLE_BIN"
    mkdir -p "$DUMPDIR"
    rm -f /tmp/oracle_avc_s*.bin
    echo "client: ORACLE (save-only, acks before decode) — server ceiling"
    setsid env DISPLAY=$CLI LD_LIBRARY_PATH=/opt/freerdp-vaapi/lib \
        FREERDP_ORACLE_DUMP=1 RDPARGS="$RDPARGS" \
        "$ORACLE_BIN" /args-from:env:RDPARGS </dev/null \
        >"$OUT/client.log" 2>&1 &
else
    command -v "$FRDP" >/dev/null || fail "$FRDP not on the host"
    echo "client: RENDERING $FRDP — end-to-end context run"
    setsid env DISPLAY=$CLI RDPARGS="$RDPARGS" \
        "$FRDP" /args-from:env:RDPARGS </dev/null >"$OUT/client.log" 2>&1 &
fi
# setsid: the child is a session leader so the whole group dies by pgid.
# Never go back to pkill -f "<client>.*:<port>" — the port travels in the
# environment and appears in no argv, so that pattern silently matches
# nothing (it left a client alive for 2.2 h on 2026-07-29).
CLIENT_PGID=$!
unset PW RDPARGS
echo "connected; recording for ${SECS}s ..."
sleep "$SECS"
kill -9 -- -"$CLIENT_PGID" 2>/dev/null
sleep 1
if pgrep -g "$CLIENT_PGID" >/dev/null 2>&1; then
    echo "WARNING: client process group $CLIENT_PGID survived teardown —" \
         "the next run's window will be contaminated" >&2
fi
if [ "$MODE" = oracle ]; then
    mv /tmp/oracle_avc_s*.bin "$DUMPDIR"/ 2>/dev/null
    du -cb "$DUMPDIR"/*.bin 2>/dev/null | tail -1 \
        > "$OUT/oracle_dump_bytes.txt"
fi

# --- collect both server-side logs, windowed ------------------------------
XLOG=$(kubectl -n "$NS" exec "$POD" -- \
    bash -lc "ls -t /home/$SU/.xorgxrdp.*.log 2>/dev/null | head -1")
[ -n "$XLOG" ] || fail "no session Xorg log in the pod — did the login fail? \
see $OUT/client.log"
kubectl -n "$NS" exec "$POD" -- cat "$XLOG" \
    | tail -n +$((MARK_X + 1)) > "$OUT/session-xorg.log"
kubectl -n "$NS" logs "$POD" 2>/dev/null \
    | tail -n +$((MARK_P + 1)) > "$OUT/xrdp.log"
grep -a "GFX_TRACE" "$OUT/xrdp.log" > "$OUT/gfx_trace.txt" 2>/dev/null

# --- the report ----------------------------------------------------------
{
    echo "=== #45 gate run: $ARM, $MODE client, ${SECS}s ==="
    echo "image:    $(cat "$OUT/deployed_image.txt")"
    grep -E "xrdp-dev|xorgxrdp-dev" "$OUT/deployed_packages.txt" \
        | awk '{print "package: " $2 " " $3}'
    echo "monitors: $(tail -1 "$OUT/client-monitors.txt")"
    echo "refresh:  intra_refresh_frames = \
$(grep -a intra_refresh_frames "$OUT/gfx.toml" | tr -d ' ' | cut -d= -f2)"
    echo

    echo "=== E5 / rate — send interval from the server's own log ==="
    python3 - "$OUT/gfx_trace.txt" "$MODE" <<'PY'
import re
import sys

TS = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)\.(\d+)")


def secs(line):
    m = TS.match(line)
    if not m:
        return None
    h, mi, s, frac = int(m.group(4)), int(m.group(5)), int(m.group(6)), \
        m.group(7)
    return h * 3600 + mi * 60 + s + float("0." + frac)


t = []
for line in open(sys.argv[1], errors="replace"):
    if "GFX_TRACE enc submitted_seq" not in line:
        continue
    v = secs(line)
    if v is not None:
        t.append(v)
if len(t) < 3:
    print("RED: only %d GFX_TRACE send records — is XRDP_GFX_TRACE=1 set "
          "in the arm's env?" % len(t))
    raise SystemExit(0)
t.sort()
span = t[-1] - t[0]
gaps = sorted(b - a for a, b in zip(t, t[1:]))
n = len(gaps)


def pct(p):
    return gaps[min(n - 1, int(p * n))] * 1000.0


mean = sum(gaps) / n * 1000.0
print("sends: %d over %.1f s" % (len(t), span))
print("send-to-send gap: mean %.1f ms  p50 %.0f ms  p90 %.0f ms  "
      "p99 %.0f ms" % (mean, pct(0.5), pct(0.9), pct(0.99)))
print("%.2f sends/s = %.2f pairs/s per monitor (2 monitors)"
      % (len(t) / span, len(t) / span / 2.0))
if sys.argv[2] == "oracle":
    base = 51.1
    print()
    print("E5 GATE: baseline %.1f ms mean per send (2026-07-29, "
          "pre-steps-5..7)" % base)
    print("         measured %.1f ms  ->  %.2fx" % (mean, base / mean))
    if mean <= base / 2.0:
        print("         >= 2.0x: PASS")
    elif mean <= base / 1.5:
        print("         between 1.5x and 2.0x: SHORT OF THE PREDICTION -- "
              "record it as such, do not re-tune until it looks better")
    else:
        print("         under 1.5x: RED. The stop rule applies: attribute "
              "the remainder (capture, vmsplice feed, NUT demux, LTR "
              "rewrite, EGFX assembly) before anything ships")
else:
    print()
    print("CONTEXT ONLY (rendering client): this is the end-to-end rate, "
          "not the E5 gate. It is client-bound by ~3.3x.")
PY
    echo

    echo "=== E4 — poll-set size the worker actually armed ==="
    if grep -aq "kids_armed\|set_size\|batched" "$OUT/xrdp.log"; then
        grep -ao "kids_armed=[0-9]*\|set_size=[0-9]*\|batched=[0-9]*" \
            "$OUT/xrdp.log" | sort | uniq -c | sort -rn | head -5
    else
        echo "no set-size records in this window (step 7 logs them; with"
        echo "one monitor damaged per cycle the set is 2 children, not 4)"
    fi
    echo

    echo "=== E2 — server log, four things that must be zero ==="
    for pat in "rewrite failed" "unsupported" "did not return" \
               "budget exceeded" "third capture" "fifo_to_proc_depth"; do
        printf '%-24s %s\n' "$pat" \
            "$(grep -aci "$pat" "$OUT/xrdp.log" "$OUT/session-xorg.log" \
               2>/dev/null | awk -F: '{s+=$2} END{print s+0}')"
    done
    echo
} | tee "$OUT/VERDICT.txt"

if [ "$MODE" = oracle ]; then
    DUMP=$(ls -S "$DUMPDIR"/*.bin 2>/dev/null | head -1)
    if [ -n "$DUMP" ]; then
        {
            echo "=== E2 — wire audit (--assert) on $(basename "$DUMP") ==="
            python3 "$D/../../tools/avc444_ltr_wire_audit.py" --assert \
                --intra-refresh "$REFRESH" "$DUMP" "$ARM gate run" \
                2>&1 | tail -25
            echo "wire audit exit: $?"
            echo
            echo "=== E2 — black-frame check ==="
            python3 "$D/oracle_black_frame_check.py" "$DUMP" 2>&1 | tail -15
        } | tee -a "$OUT/VERDICT.txt"
    else
        echo "RED: the oracle client wrote no dump — E2 cannot be judged" \
            | tee -a "$OUT/VERDICT.txt"
    fi
fi
echo
echo "evidence: $OUT"
