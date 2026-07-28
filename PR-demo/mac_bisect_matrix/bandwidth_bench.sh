#!/bin/bash
# bandwidth_bench.sh — repeatable AVC444 wire-bandwidth A/B across arms.
#
# Two modes, both against the FIXED banner.sh content (static color
# chart + 1 Hz tick) at a fixed geometry, so byte counts are directly
# comparable between arms. Intended A/B: arm-i (pre-FR-H264-7
# single-chain build) vs arm-m (unconditional reference partitioning)
# on the identical VAAPI CQP config.
#
#   default        steady-state wire rate: attach the stock acking
#                  client (xfreerdp3), let login churn settle, then
#                  read the TCP socket rx counter over a fixed window.
#                  Measures real sustained traffic (incl. TLS framing,
#                  identical across arms). Requires TICKING content.
#   MODE=frames    per-frame payload split: oracle save-only client +
#                  bandwidth_stats.py (per-view / IDR-vs-P sizes of the
#                  H.264 GFX records). NOTE: the oracle client never
#                  acks, so xrdp's in-flight window fills after ~3
#                  frames — this mode measures the initial-paint frames
#                  only, never sustained traffic (learned 2026-07-28).
#
# Preconditions the 2026-07-28 shakedown proved load-bearing:
#   1. Arms must run SESSION_KIND=banner — the xfce arms idle static,
#      so steady-state traffic is ~0 on ANY build. Flip + revert with:
#        kubectl -n bisect-matrix set env deployment/xrdp-arm-i \
#            deployment/xrdp-arm-m SESSION_KIND=banner   # then =xfce
#   2. Each run needs a FRESH session: reconnecting to a stale probe444
#      session (dead wm, static screen) reads as zero traffic. The
#      SESSION_KIND flip above forces a pod roll, which guarantees it;
#      otherwise `kubectl -n bisect-matrix rollout restart` the arms.
#
# CLIENT SIDE STAYS ON THE HOST (owner directive). Credential comes
# from root-only /root/.oracle_cred into the RDPARGS env var, never
# argv, never printed.
#
# Usage: bandwidth_bench.sh [arm ...]          (default: arm-i arm-m)
#        WARMUP_SECS=12 WINDOW_SECS=20 SIZE=1600x900 to override.
set -u
CRED=${PROBE_CRED_FILE:-/root/.oracle_cred}
CLI=${VERIFY_DISPLAY:-:97}
SIZE=${SIZE:-1600x900}
WARMUP=${WARMUP_SECS:-12}
WINDOW=${WINDOW_SECS:-20}
MODE=${MODE:-steady}
D=$(cd "$(dirname "$0")" && pwd)
OUT=${BENCH_OUT:-/tmp/bandwidth_bench}
mkdir -p "$OUT"

[ -s "$CRED" ] || { echo "ABORT: no probe cred"; exit 1; }
if [ "$MODE" = frames ]; then
    BIN=/opt/freerdp-vaapi/bin/xfreerdp
else
    BIN=/usr/bin/xfreerdp3
fi
[ -x "$BIN" ] || { echo "ABORT: client $BIN missing"; exit 1; }

if ! DISPLAY=$CLI xdotool getdisplaygeometry >/dev/null 2>&1; then
    setsid Xvfb "$CLI" -screen 0 1600x900x24 </dev/null >/dev/null 2>&1 &
    sleep 2
fi

declare -A PORT=( [arm-a]=40000 [arm-b]=40001 [arm-c]=40002 [arm-d]=40003
                  [arm-e]=40004 [arm-f]=40005 [arm-g]=40006 [arm-h]=40007
                  [arm-i]=40008 [arm-j]=40009 [arm-k]=40010 [arm-l]=40011
                  [arm-m]=40012 )

rx_bytes()
{
    ss -tin "dport = :$1" | grep -o 'bytes_received:[0-9]*' \
        | cut -d: -f2 | sort -n | tail -1
}

fail=0
for arm in "${@:-arm-i arm-m}"; do
    port=${PORT[$arm]:?unknown arm $arm}
    echo "=== $arm (127.0.0.1:$port) $MODE @ $SIZE ==="
    pkill -9 -x "$(basename "$BIN")" 2>/dev/null; sleep 1
    rm -f /tmp/oracle_avc_s*.bin
    PW=$(cat "$CRED")
    RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$port" "/u:probe444" "/p:$PW" \
                            "/size:$SIZE" "/gfx:AVC444" "/cert:ignore" \
                            "/log-level:WARN")
    setsid env DISPLAY=$CLI LD_LIBRARY_PATH=/opt/freerdp-vaapi/lib \
        FREERDP_ORACLE_DUMP=1 RDPARGS="$RDPARGS" \
        "$BIN" /args-from:env:RDPARGS </dev/null >"$OUT/$arm.client.log" 2>&1 &
    unset PW RDPARGS

    if [ "$MODE" = frames ]; then
        sleep "$WINDOW"
        pkill -9 -x "$(basename "$BIN")" 2>/dev/null
        dump=$(ls /tmp/oracle_avc_s*.bin 2>/dev/null | head -1)
        if [ -z "$dump" ]; then
            echo "  FAIL: no oracle dump (login or GFX failed)"
            tail -3 "$OUT/$arm.client.log" | sed 's/^/  | /'
            fail=1
            continue
        fi
        cp "$dump" "$OUT/$arm.bin"
        python3 "$D/bandwidth_stats.py" "$OUT/$arm.bin" "$arm" | sed 's/^/  /'
    else
        sleep "$WARMUP"
        b0=$(rx_bytes "$port")
        sleep "$WINDOW"
        b1=$(rx_bytes "$port")
        pkill -9 -x "$(basename "$BIN")" 2>/dev/null
        if [ -z "$b0" ] || [ -z "$b1" ]; then
            echo "  FAIL: no client socket (login failed?)"
            tail -3 "$OUT/$arm.client.log" | sed 's/^/  | /'
            fail=1
            continue
        fi
        rate=$(( (b1 - b0) / WINDOW ))
        echo "  steady-state: $rate B/s over ${WINDOW}s (rx $b0 -> $b1)"
        if [ "$rate" -eq 0 ]; then
            echo "  WARNING: zero traffic — stale session or non-banner" \
                 "content (see preconditions in the script header)"
            fail=1
        fi
    fi
done
exit $fail
