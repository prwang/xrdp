#!/bin/bash
# capture_arm_boundary.sh — drive a LOCAL fleet arm and capture both wire
# streams needed by rekey_boundary_audit.py (BACKLOG #48 acceptance).
#
# The T4 runner (PR-demo/oracle_client/run_oracle_client.sh) cannot be
# reused: it ssh-tunnels to a cloud box and reads /root/.ubuntu_cred from
# it. Fleet arms are local (127.0.0.1:4000x) and authenticate the
# probe444 account, whose credential is the root-only /root/.oracle_cred
# — the harness must never know the owner's tester password.
#
# Captures, for the SAME connection:
#   /tmp/oracle_avc_s0.bin  — every AVC444 payload, u32-length-prefixed
#   <out>/transport_egfx.dump — post-TLS byte stream (/dump:record), which
#                               preserves EGFX PDU ORDER (surface
#                               delete/create/map vs wire-to-surface)
#
#
# TWO MODES, because one client cannot answer both questions:
#
#   MODE=oracle (default)  FREERDP_ORACLE_DUMP=1 + /dump:record. The
#       oracle patch (PR-demo/oracle_client/patch_gfx_oracle.py) returns
#       CHANNEL_RC_OK *before decode/present*, so this client records
#       every AVC444 payload and renders NOTHING. Its window is black by
#       construction — sampling pixels here proves nothing about a
#       decoder. This mode answers "what did the server emit?".
#
#   MODE=render            a stock client run: no oracle env, no dump.
#       It really decodes AVC444 (VAAPI) and paints, so its window is
#       sampled every SHOT_EVERY seconds and the run ABORTS after two
#       consecutive black/frozen frames. This mode answers "did a real
#       client survive the boundaries?".
#
# Run both against the same arm to close BACKLOG #48: the byte half and
# the pixel half are separate claims and neither implies the other.
#
# Usage:
#   capture_arm_boundary.sh <port> <out_dir> [seconds]
#   MODE=render capture_arm_boundary.sh 40014 captures/x_render 200
set -u

PORT=${1:?usage: capture_arm_boundary.sh <port> <out_dir> [seconds]}
OUTDIR=${2:?usage: capture_arm_boundary.sh <port> <out_dir> [seconds]}
SECS=${3:-90}
CLI=${CLIENT_DISPLAY:-:98}
D_SELF=$(cd "$(dirname "$0")" && pwd)
BIN=/opt/freerdp-vaapi/bin/xfreerdp
CRED=${CRED:-/root/.oracle_cred}

[ -x "$BIN" ] || { echo "ABORT: no oracle client at $BIN" >&2; exit 1; }
[ -s "$CRED" ] || { echo "ABORT: no credential at $CRED" >&2; exit 1; }
mkdir -p "$OUTDIR"

# never pkill -f: it matches this script's own command line (a real
# incident on this box, CLAUDE.md). Match the executable name only.
pkill -9 -x xfreerdp >/dev/null 2>&1
pkill -9 -x xfreerdp3 >/dev/null 2>&1
sleep 2
rm -f /tmp/oracle_avc_s*.bin

# credential: read into the environment at point of use, passed via
# /args-from:env so it never becomes a process argument, never printed.
MODE=${MODE:-oracle}
PW=$(cat "$CRED")
if [ "$MODE" = "render" ]; then
    RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:probe444" "/p:$PW" \
                            "/gfx:AVC444" "/cert:ignore" "/log-level:WARN")
    ORACLE_ENV=FREERDP_ORACLE_DUMP=0
else
    RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:probe444" "/p:$PW" \
                            "/gfx:AVC444" "/cert:ignore" "/log-level:WARN" \
                            "/dump:record,file:$OUTDIR/transport_egfx.dump")
    ORACLE_ENV=FREERDP_ORACLE_DUMP=1
fi
setsid env DISPLAY="$CLI" LD_LIBRARY_PATH=/opt/freerdp-vaapi/lib \
    "$ORACLE_ENV" RDPARGS="$RDPARGS" \
    "$BIN" /args-from:env:RDPARGS </dev/null \
    >"$OUTDIR/oracle_client.log" 2>&1 &
unset PW RDPARGS
sleep 15

if ! DISPLAY="$CLI" xdotool search --name FreeRDP >/dev/null 2>&1; then
    echo "FAIL: oracle client did not connect to :$PORT" >&2
    tail -8 "$OUTDIR/oracle_client.log" >&2
    exit 1
fi
echo "connected to 127.0.0.1:$PORT; capturing ${SECS}s of damage ..."

# Sample the CLIENT's rendered output alongside the wire. The wire dump
# is the server's output, so it looks identical whether the client
# decoded the re-key or wedged on it; only the client's own framebuffer
# distinguishes those. xwd_render_check.py turns these into a verdict.
# Grab the FreeRDP WINDOW by id, not -root: on this Xvfb a root grab
# came back all-zero while the window itself held live content (measured
# 2026-07-29, mean 90.2 vs 0.00), so a root-based check would have
# reported a client failure that did not exist.
if [ "$MODE" != "render" ]; then
    # oracle mode: the patched client never decodes, so there is nothing
    # to sample. Just hold the connection open for the capture window.
    sleep "$SECS"
    verdict="n/a (oracle mode records bytes, does not render)"
fi
if [ "$MODE" = "render" ]; then
SHOT_EVERY=${SHOT_EVERY:-10}
mkdir -p "$OUTDIR/shots"
WINID=$(DISPLAY="$CLI" xdotool search --name '^FreeRDP' | head -1)
[ -n "$WINID" ] || { echo "FAIL: no FreeRDP window to sample" >&2; exit 1; }
# Each sample is judged as it is taken and the run ABORTS after two
# consecutive bad frames. A wedged decoder is the answer; collecting
# another three minutes of it wastes the operator's time and proves
# nothing further. Two, not one, so a single sample racing a repaint
# cannot fail the run.
elapsed=0
n=0
bad=0
prev_sig=""
verdict=PASS
while [ "$elapsed" -lt "$SECS" ]; do
    sleep "$SHOT_EVERY"
    elapsed=$((elapsed + SHOT_EVERY))
    n=$((n + 1))
    shot="$OUTDIR/shots/$(printf 'shot_%03d' "$n").xwd"
    DISPLAY="$CLI" xwd -id "$WINID" -silent > "$shot" 2>/dev/null
    read -r mean sig black <<EOF
$("$D_SELF/xwd_render_check.py" --single "$shot")
EOF
    if [ "$black" = "1" ]; then
        why="black (mean $mean)"
    elif [ -n "$prev_sig" ] && [ "$sig" = "$prev_sig" ]; then
        why="frozen (identical to previous sample)"
    else
        why=""
    fi
    prev_sig=$sig
    if [ -n "$why" ]; then
        bad=$((bad + 1))
        echo "  shot $n: $why  [$bad consecutive]"
        if [ "$bad" -ge 2 ]; then
            echo "ABORT at ${elapsed}s: client stopped rendering — $why" >&2
            verdict=FAIL
            break
        fi
    else
        bad=0
    fi
done
echo "render sampling verdict: $verdict ($n samples over ${elapsed}s)"
echo "$verdict $n samples over ${elapsed}s" > "$OUTDIR/render_verdict.txt"
fi

pkill -9 -x xfreerdp >/dev/null 2>&1
sleep 2
for f in /tmp/oracle_avc_s*.bin; do
    [ -e "$f" ] && cp "$f" "$OUTDIR/" && echo "captured $(basename "$f")" \
        "($(stat -c%s "$f") bytes)"
done
ls -l "$OUTDIR/transport_egfx.dump" 2>/dev/null
