#!/bin/bash
# ack_delay_sweep.sh — BACKLOG #79 test layer 1: confirm (or falsify) the
# withheld-slot-ack mechanism by INTERVENTION on the unmodified build.
#
# THE QUESTION. #78 concluded that fif = 1's throughput tail is xrdp
# withholding the producer's slot-recycle ack until the CLIENT's frame
# ack re-opens xrdp_gfx_ack_window_open(). That is an inference from
# traces. This sweep makes it a prediction and tests it: if the client's
# acks are made to arrive D ms later, and nothing else changes, then
#
#     withheld p50   ->  ~D          (the credit waits on that ack)
#     stall fraction ->  ~100 %      (the race is now always lost)
#     send period    ->  rises with D
#
# and if the mechanism is NOT what #78 says, the response will not have
# that shape. Nothing here is modified except the arrival time of the
# client's bytes: same server deb, same gfx.toml, same client binary,
# same payload, same geometry.
#
# THE LEGS (this is the whole experiment; adding one makes it a
# different experiment — CLAUDE.md, approval covers the arm count):
#   direct  no proxy at all — the baseline every other leg is read against
#   D=0     THROUGH the proxy, zero delay — the control that says the
#           proxy itself is not the confound. If this does not reproduce
#           `direct` within noise, nothing downstream counts (gate 5).
#   D=10 / D=20 / D=40   the dose-response.
#
# Each leg is a cold-session gate run of E_SECS seconds against arm x017
# (the deployed HEAD build, fif = 1, the arm #78 measured). Sequential:
# a fleet with two live sessions is a different machine.
#
# Usage: ack_delay_sweep.sh [seconds]      (default 5 — this is a
#        mechanism check, not a rate; see "never spend a long run on a
#        binary check")
set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-5}
ARM=${A_ARM:-x017}
POD_PORT=${A_POD_PORT:-40033}
PXY_PORT=${A_PXY_PORT:-41079}
DELAYS=${A_DELAYS:-"0 10 20 40"}
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${A_OUT:-$D/captures/i79_${ARM}_ackdelay_$STAMP}
PROXY=$D/ack_delay_proxy

mkdir -p "$OUT"
fail() { echo "ABORT: $*" >&2; exit 1; }

[ -x "$PROXY" ] || fail "build the proxy first: gcc -O2 -Wall -o $PROXY $PROXY.c"

# the harness component proves itself before it is trusted (layer 1
# step 0). A red self-test stops the sweep.
echo "=== proxy self-test ==="
python3 "$D/ack_delay_proxy_selftest.py" "$PROXY" \
    > "$OUT/proxy_selftest_$STAMP.txt" 2>&1 \
    || { tail -20 "$OUT/proxy_selftest_$STAMP.txt"; fail "proxy self-test RED"; }
tail -1 "$OUT/proxy_selftest_$STAMP.txt"

# geometry and payload of BACKLOG #78 Run A, byte for byte: this sweep is
# only readable against that capture if the workload is the same one
# (gate 5, apples-to-apples).
run_leg() {
    leg=$1
    delay=$2
    legout=$OUT/leg_$leg
    mkdir -p "$legout"
    if [ "$delay" = none ]; then
        port=$POD_PORT
        echo "=== leg $leg: NO proxy, port $port ==="
    else
        port=$PXY_PORT
        echo "=== leg $leg: proxy 127.0.0.1:$port -> $POD_PORT, delay ${delay}ms ==="
        setsid "$PROXY" -l "$PXY_PORT" -r "$POD_PORT" -d "$delay" \
            </dev/null >/dev/null 2>"$legout/proxy.log" &
        PXY_PGID=$!
        sleep 1
        kill -0 "$PXY_PGID" 2>/dev/null || { cat "$legout/proxy.log"; \
            fail "proxy did not start for leg $leg"; }
    fi
    T0=$(date +%s)
    E_ARM=$ARM E_PORT=$port E_MODE=oracle E_MONITORS=1 \
        E_MODE0=3840x2400R \
        E_MODELINE0="592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync" \
        E5_BASE_MS=22.6 E_OUT="$legout" \
        bash "$D/e_gate_run.sh" "$SECS" > "$legout/gate.txt" 2>&1
    rc=$?
    T1=$(date +%s)
    echo "t0 $T0" > "$legout/window.txt"
    echo "t1 $T1" >> "$legout/window.txt"
    echo "delay_ms $delay" >> "$legout/window.txt"
    echo "secs $SECS" >> "$legout/window.txt"
    if [ "$delay" != none ]; then
        kill -TERM -- -"$PXY_PGID" 2>/dev/null
        sleep 1
        pkill -f "ack_delay_proxy -l $PXY_PORT" 2>/dev/null
        grep -E "applied delay|close after" "$legout/proxy.log" | sed 's/^/   /'
    fi
    if [ $rc -ne 0 ]; then
        tail -15 "$legout/gate.txt"
        fail "leg $leg: gate run failed (rc=$rc)"
    fi
    grep -aE "^sends:|send-to-send" "$legout/gate.txt" | sed 's/^/   /'
}

# fleet must be otherwise idle: another arm's live session shares the GPU
kubectl -n bisect-matrix get pods > "$OUT/fleet_pods.txt" 2>&1
for a in x013 x014 x015 x017; do
    p=$(kubectl -n bisect-matrix get pod -l "arm=$a" \
        -o jsonpath='{.items[0].metadata.name}' 2>/dev/null) || continue
    [ -n "$p" ] || continue
    echo "== $a" >> "$OUT/fleet_sessions_before.txt"
    kubectl -n bisect-matrix exec "$p" -- bash -lc \
        'pgrep -a Xorg | head -5' >> "$OUT/fleet_sessions_before.txt" 2>&1
done

# A_LEGS selects which legs THIS invocation runs (the capture dir is
# A_OUT, so a sweep can be split across invocations without becoming two
# experiments). Default: all of them, in order.
for leg in ${A_LEGS:-direct $DELAYS}; do
    if [ "$leg" = direct ]; then
        run_leg direct none
    else
        run_leg "d$leg" "$leg"
    fi
done

echo
echo "=== analysis (of the legs present in $OUT) ==="
python3 "$D/i79_ack_delay_analyze.py" "$OUT" | tee "$OUT/ANALYSIS.txt"
echo
echo "capture: $OUT"
