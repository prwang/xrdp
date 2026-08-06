#!/bin/bash
# i87_eager_ab.sh — the merged eager-ack A/B (owner-approved 2026-08-06).
#
# ONE experiment answering three asks that each wanted the same two arms:
#   * BACKLOG #80 item 2 — attribute the residual LAN stalls at a wire
#     window of 2, which no run has ever used;
#   * BACKLOG #80 step 5 — the fleet A/B that must exist before
#     eager_slot_ack could ever be considered for default-on;
#   * BACKLOG #87 — re-measure the eager ack's ratio under textflood on
#     a ring-traced build (the 1.11x on record is codeflood-era and was
#     taken with the per-frame trace on log.c).
#
# THE ARMS. Same image, same payload, same geometry, same client rig;
# their gfx.toml bodies differ by exactly one line.
#   x020 (:40036) CONTROL   — eager_slot_ack = false. Today's shipped
#                             behaviour: the legacy gated ack, whose
#                             window comes from XRDP_GFX_FRAMES_IN_FLIGHT,
#                             pinned to 2 in the manifest.
#   x021 (:40037) TREATMENT — eager_slot_ack = true, wire_window = 2.
#                             The credit frontier at the value the code
#                             SHIPS as its default and which has never
#                             encoded a frame in this tree (every
#                             wire_window on record is 1).
# Both arms therefore run a window of 2, reached by DIFFERENT mechanisms.
# Holding the number equal is what makes this an A/B about the ack
# mechanism rather than about the window size.
#
# WHY INTERLEAVED (A B A B) rather than one leg each: an unchanged arm
# has drifted 36.8 -> 41.8 ms across a single day on this host (BACKLOG
# #88's caution). One leg each cannot be read against that drift; two
# legs each, alternating, lets each arm's pair bracket the other's.
#
# WHAT THIS RUN CANNOT SAY. On loopback the client's acknowledgement
# round trip is microseconds, so the ack is rarely what limits the
# pipeline here. The honest question this answers is "does turning the
# eager ack on COST anything on a fast link" — a regression check ahead
# of any default flip. What it BUYS was measured elsewhere: #79's
# ack-delay sweep confirmed the mechanism causally and #80's head-to-head
# measured the wait between the encoder finishing with a frame's pixels
# and the producer being told it may capture again falling 35.3 -> 10.6 ms
# at p90.
#
# Usage: ./i87_eager_ab.sh [seconds-per-leg]   (default 20)

set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-20}
OUT=$D/captures/i87_eager_ab_$(date +%Y%m%d_%H%M%S)_s$SECS
mkdir -p "$OUT"

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

run_leg()
{
    leg=$1
    arm=$2
    port=$3
    legout=$OUT/leg_$leg
    mkdir -p "$legout"

    echo "=== leg $leg: arm $arm, port $port, ${SECS}s ==="
    T0=$(date +%s)
    E_ARM=$arm E_PORT=$port E_MODE=oracle E_MONITORS=1 \
        E_MODE0=3840x2400R \
        E_MODELINE0="592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync" \
        E5_BASE_MS=22.6 E_OUT="$legout" \
        bash "$D/e_gate_run.sh" "$SECS" > "$legout/gate.txt" 2>&1
    rc=$?
    T1=$(date +%s)

    {
        echo "t0 $T0"
        echo "t1 $T1"
        echo "arm $arm"
        echo "port $port"
        echo "secs $SECS"
        echo "leg $leg"
    } > "$legout/window.txt"

    if [ $rc -ne 0 ]; then
        tail -15 "$legout/gate.txt"
        fail "leg $leg: gate run failed (rc=$rc)"
    fi
    grep -aE "^sends:|send-to-send|FR-BENCH-1 margin" "$legout/gate.txt" \
        | sed 's/^/   /'
}

kubectl -n bisect-matrix get pods > "$OUT/fleet_pods.txt" 2>&1

run_leg a1 x020 40036
run_leg b1 x021 40037
run_leg a2 x020 40036
run_leg b2 x021 40037

echo
echo "capture: $OUT"
