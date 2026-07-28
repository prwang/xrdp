#!/bin/bash
# bandwidth_bench.sh — repeatable AVC444 wire-bandwidth A/B across arms
# and workload classes.
#
# For every workload it flips the arms' SESSION_KIND (banner.sh
# dispatch), force-rolls the pods (fresh session — reconnecting to a
# stale session reads as zero traffic), then per arm attaches a client
# and measures. Workloads (deterministic, see banner.sh):
#   tick    sparse small UI update (1 Hz tick line)
#   scroll  colored text, LINE-BY-LINE scroll (1 line/0.1 s) — typical
#           scroll baseline, inside encoder motion-search range
#   gray    full-screen luma motion, CONSTANT chroma (5 fps) — the
#           FR-H264-8 discriminator: aux-refs-aux P is all-skip here,
#           the FR-H264-7 all-intra leaf re-encodes every damaged MB
#   chroma  full-screen chroma motion (5 fps) — flat-band adversarial
#           bound (intra-friendly), not typical chroma-rich content
#   code    syntax-highlighted repo C (Solarized, subpixel AA),
#           LINE-BY-LINE scroll — realistic developer payload
#   scrollfast/codefast  10 lines/0.1 s ME-defeating stress bounds
#
# FR-H264-8 BANDWIDTH GATE (owner directive 2026-07-28): this bench,
# MODE=frames on the line-scroll baselines (scroll, code), is the
# benchmark harness for the aux-refs-aux optimization — its measured
# per-view KB/frame vs the leaf arm GATES that feature's acceptance
# (PRD FR-H264-8, gate item 5).
#
# Modes:
#   default        steady-state wire rate: stock acking client
#                  (xfreerdp3), warmup past login churn, then read the
#                  TCP socket rx counter over a fixed window (real
#                  sustained traffic; TLS framing identical across
#                  arms).
#   MODE=frames    per-frame payload split: oracle save-only client +
#                  bandwidth_stats.py — reports delivered pairs/s and
#                  steady KB/frame per view. This is the PRIMARY unit
#                  (owner directive 2026-07-28): B/s conflates frame
#                  cost with achieved delivery rate, which is CLIENT-
#                  dependent (oracle ~8.3 pairs/s vs xfreerdp3 ~10 on
#                  the same 10 Hz content). CORRECTION of an earlier
#                  claim: the oracle client DOES ack and sustains
#                  delivery; the 3-frame captures that suggested
#                  otherwise were static xfce sessions with nothing to
#                  encode. Cross-check: KB/frame x stock-client fps
#                  reproduces the MODE=steady TCP rates within ~3%.
#
# Intended A/B: arm-i (pre-FR-H264-7 single-chain build) vs arm-m
# (unconditional reference partitioning) on the identical VAAPI CQP
# config; reuse unchanged for the FR-H264-8 aux-refs-aux arm later.
#
# CLIENT SIDE STAYS ON THE HOST (owner directive). Credential comes
# from root-only /root/.oracle_cred into the RDPARGS env var, never
# argv, never printed. Arms are reverted to RESTORE_KIND (default
# xfce, the git-declared fleet state) when the run ends.
#
# Usage: bandwidth_bench.sh [arm ...]          (default: arm-i arm-m)
#        WORKLOADS="tick scroll gray chroma" WARMUP_SECS=12
#        WINDOW_SECS=20 SIZE=1600x900 RESTORE_KIND=xfce to override.
set -u
CRED=${PROBE_CRED_FILE:-/root/.oracle_cred}
CLI=${VERIFY_DISPLAY:-:97}
SIZE=${SIZE:-1600x900}
WARMUP=${WARMUP_SECS:-12}
WINDOW=${WINDOW_SECS:-20}
MODE=${MODE:-steady}
WORKLOADS=${WORKLOADS:-tick scroll gray chroma code}
RESTORE_KIND=${RESTORE_KIND:-xfce}
NS=bisect-matrix
D=$(cd "$(dirname "$0")" && pwd)
OUT=${BENCH_OUT:-/tmp/bandwidth_bench}
mkdir -p "$OUT"

ARMS=("${@:-arm-i}") ; [ $# -eq 0 ] && ARMS=(arm-i arm-m)

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

DEPLOYS=()
for arm in "${ARMS[@]}"; do
    : "${PORT[$arm]:?unknown arm $arm}"
    DEPLOYS+=("deployment/xrdp-$arm")
done

set_kind()
{
    # set env AND force-roll: an unchanged env value would otherwise
    # skip the roll and leave a stale (dead-wm, static) session behind
    kubectl -n $NS set env "${DEPLOYS[@]}" "SESSION_KIND=$1" >/dev/null
    kubectl -n $NS rollout restart "${DEPLOYS[@]}" >/dev/null
    for d in "${DEPLOYS[@]}"; do
        kubectl -n $NS rollout status "$d" --timeout=120s >/dev/null || return 1
    done
    sleep 3
}

rx_bytes()
{
    ss -tin "dport = :$1" | grep -o 'bytes_received:[0-9]*' \
        | cut -d: -f2 | sort -n | tail -1
}

fail=0
for wl in $WORKLOADS; do
    kind=$wl ; [ "$wl" = tick ] && kind=banner
    set_kind "$kind" || { echo "ABORT: rollout failed for $kind"; exit 1; }
    for arm in "${ARMS[@]}"; do
        port=${PORT[$arm]}
        echo "=== $wl / $arm (127.0.0.1:$port) $MODE @ $SIZE ==="
        pkill -9 -x "$(basename "$BIN")" 2>/dev/null; sleep 1
        rm -f /tmp/oracle_avc_s*.bin
        PW=$(cat "$CRED")
        RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$port" "/u:probe444" \
                                "/p:$PW" "/size:$SIZE" "/gfx:AVC444" \
                                "/cert:ignore" "/log-level:WARN")
        setsid env DISPLAY=$CLI LD_LIBRARY_PATH=/opt/freerdp-vaapi/lib \
            FREERDP_ORACLE_DUMP=1 RDPARGS="$RDPARGS" \
            "$BIN" /args-from:env:RDPARGS </dev/null \
            >"$OUT/$wl.$arm.client.log" 2>&1 &
        unset PW RDPARGS

        if [ "$MODE" = frames ]; then
            sleep "$WINDOW"
            pkill -9 -x "$(basename "$BIN")" 2>/dev/null
            dump=$(ls /tmp/oracle_avc_s*.bin 2>/dev/null | head -1)
            if [ -z "$dump" ]; then
                echo "  FAIL: no oracle dump (login or GFX failed)"
                tail -3 "$OUT/$wl.$arm.client.log" | sed 's/^/  | /'
                fail=1
                continue
            fi
            cp "$dump" "$OUT/$wl.$arm.bin"
            python3 "$D/bandwidth_stats.py" "$OUT/$wl.$arm.bin" \
                "$wl/$arm" "$WINDOW" | sed 's/^/  /'
        else
            sleep "$WARMUP"
            b0=$(rx_bytes "$port")
            sleep "$WINDOW"
            b1=$(rx_bytes "$port")
            pkill -9 -x "$(basename "$BIN")" 2>/dev/null
            if [ -z "$b0" ] || [ -z "$b1" ]; then
                echo "  FAIL: no client socket (login failed?)"
                tail -3 "$OUT/$wl.$arm.client.log" | sed 's/^/  | /'
                fail=1
                continue
            fi
            rate=$(( (b1 - b0) / WINDOW ))
            echo "  steady-state: $rate B/s over ${WINDOW}s (rx $b0 -> $b1)"
            if [ "$rate" -eq 0 ]; then
                echo "  WARNING: zero traffic — dead session content?"
                fail=1
            fi
        fi
    done
done

set_kind "$RESTORE_KIND" || fail=1
echo "arms reverted to SESSION_KIND=$RESTORE_KIND"
exit $fail
