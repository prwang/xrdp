#!/bin/bash
# i80_wan_pair.sh -- BACKLOG #80 step 4, first live measurement of the
# credit frontier, on the #81 netem harness.
#
# THE EXPERIMENT, AND IT IS EXACTLY TWO LEGS (owner-approved 2026-08-03,
# "stand up two more arms comparing RTT0 and RTT40ms"). Adding a leg
# makes this a different experiment that cannot be compared against the
# one that was authorised -- CLAUDE.md, "an approval covers the
# experiment that was DESCRIBED".
#
#   leg lan   arm x018, no shaping           -- the loopback baseline
#   leg wan   arm x019, 40 ms true RTT       -- netem, both directions
#
# Both arms carry the SAME image (1d5bc0960db8, the credit frontier),
# the SAME xorgxrdp (10fa3aa23033), the SAME gfx.toml body and the SAME
# payload and geometry as BACKLOG #78's x017. C = wire_window = 1.
#
# ---------------------------------------------------------------------
# PREDICTIONS, WRITTEN BEFORE THE RUN. A metric chosen after seeing the
# data proves nothing, and #80 step 4 requires these to be re-derived
# for DROP semantics rather than inherited from the stall-era sweep.
#
# The comparison target for the LAN leg is x017's `direct` leg, which is
# the same payload, geometry, monitor count, client rig and xorgxrdp,
# with fif = 1 -- and NO proxy in the path. Recorded numbers there:
# withheld p50 7.6 ms, stall fraction 29.7 %, period mean/p50/p90
# 21.5 / 17.1 / 42.7 ms, 746 cycles over 20 s.
#
# P1  THE DEFECT IS GONE ON THE LAN LEG. `withheld` -- the interval from
#     the moment the in-tree safety condition is met (the children have
#     absorbed frame k-2's input) to the moment a credit permitting
#     capture k is emitted -- collapses. Predict p50 <= 1 ms and stall
#     fraction (withheld > 10 ms) <= 5 %, against 7.6 ms and 29.7 %.
#     FALSIFIER: x018 still shows ~7.6 ms. Then the frontier did not
#     change the thing it was built to change, and no rate on this arm
#     means anything.
#
# P2  THE LONG TAIL COLLAPSES ON THE LAN LEG. x017 direct ran a period
#     p90 of 42.7 ms against a p50 of 17.1 -- a tail 2.5x the median,
#     which #79 attributed to the withheld credit. Predict x018's
#     p90/p50 below 2.0.
#     FALSIFIER: the ratio is unchanged. Then the tail was never the ack
#     window and #79's attribution was wrong.
#
# P3  AT 40 ms THE WINDOW BINDS, AND THE RESPONSE IS A DROP, NOT A HOLD.
#     With C = 1 a capture k needs the client to have acknowledged k-3,
#     so in steady state 3P >= L + RTT where L is capture-to-egress
#     latency; at L ~ 30 ms that floors the period near 23 ms. Predict
#     x019's period rises above x018's, `withheld` rises with it, and
#     BOTH are correct behaviour rather than the defect -- what says so
#     is that nothing is holding a finished frame: the transport's
#     queued bytes (egress field c, from trans::wait_bytes) stay at ~0
#     KiB rather than growing.
#     FALSIFIER: queued bytes grow with RTT. Then frames are being held
#     at egress after all and the induction in #80 is wrong.
#
# P4  THE WIRE BOUND HOLDS LIVE. On every `send` record,
#     id_server - id_client <= C + 2 = 3, on both legs. This is the
#     invariant CI asserts over the whole reachable state space; a live
#     violation is a red result against CI, not a curiosity.
#
# P5  MECHANISM CHECK, BEFORE ANY RATE (quality gate 2). Every ackslot
#     and ackregion record must read C = 1 in field e. If it reads 2,
#     gfx.toml never reached the encoder and NEITHER leg counts.
#     Separately: the RTT must be verified by measurement through each
#     arm's own RDP port, not trusted from the tc command line.
#
# WHAT THIS PAIR CANNOT SETTLE, stated here so it is not claimed later.
# There is no OLD-build leg under netem. x017's D = 40 leg was taken
# with ack_delay_proxy, which delays only client->server bytes above
# TLS; netem delays both directions below TCP. Those are different
# instruments and their numbers are not comparable (quality gate 5).
# The clean head-to-head in this pair is x018 against x017 `direct` --
# both unshaped, no proxy anywhere. A true old-vs-new comparison AT
# 40 ms would need one more leg (x017 behind netem 40, ~2 min); it is
# not run here because it is not in the approved description.
# ---------------------------------------------------------------------
#
# Usage: i80_wan_pair.sh [seconds]     (default 20 -- x017's 5 s sweep
#        failed its own control leg; 20 s is what made that series
#        readable, and this pair is read against it)

set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-20}
LAN_ARM=${I80_LAN_ARM:-x018}
LAN_PORT=${I80_LAN_PORT:-40034}
WAN_ARM=${I80_WAN_ARM:-x019}
WAN_PORT=${I80_WAN_PORT:-40035}
WAN_RTT=${I80_WAN_RTT:-40}
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${I80_OUT:-$D/captures/i80_wanpair_${STAMP}_s${SECS}}

mkdir -p "$OUT"
fail() { echo "ABORT: $*" >&2; exit 1; }

# netem is host state. It must not survive this script under any exit
# path, including a failure or a ^C -- a leftover qdisc is a WAN nobody
# declared, invisible to every capture taken afterwards.
cleanup()
{
    "$D/netem_rtt.sh" clear "$WAN_ARM" > "$OUT/netem_clear_exit.txt" 2>&1
    "$D/netem_rtt.sh" clear "$LAN_ARM" >> "$OUT/netem_clear_exit.txt" 2>&1
}
trap cleanup EXIT INT TERM

echo "=== netem harness self-check (the instrument proves itself first) ==="
"$D/netem_rtt.sh" selftest "$LAN_ARM" > "$OUT/netem_selftest.txt" 2>&1 \
    || { tail -20 "$OUT/netem_selftest.txt"; fail "netem selftest RED"; }
tail -1 "$OUT/netem_selftest.txt"

run_leg()
{
    leg=$1
    arm=$2
    port=$3
    rtt=$4
    legout=$OUT/leg_$leg
    mkdir -p "$legout"

    echo "=== leg $leg: arm $arm, port $port, requested RTT ${rtt} ms ==="
    "$D/netem_rtt.sh" apply "$arm" "$rtt" > "$legout/netem_apply.txt" 2>&1 \
        || { cat "$legout/netem_apply.txt"; fail "leg $leg: netem apply"; }
    sed 's/^/   /' "$legout/netem_apply.txt"
    measured=$(tail -1 "$legout/netem_apply.txt")

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
        echo "delay_ms $rtt"
        echo "rtt_measured_ms $measured"
        echo "arm $arm"
        echo "secs $SECS"
    } > "$legout/window.txt"

    # what the RTT actually was, measured on the RDP path, AFTER the leg
    # as well as before -- a qdisc that fell off mid-run would otherwise
    # be invisible
    "$D/netem_rtt.sh" status "$arm" > "$legout/netem_after.txt" 2>&1
    grep -E "measured rtt|qdisc" "$legout/netem_after.txt" | sed 's/^/   /'

    if [ $rc -ne 0 ]; then
        tail -15 "$legout/gate.txt"
        fail "leg $leg: gate run failed (rc=$rc)"
    fi
    grep -aE "^sends:|send-to-send" "$legout/gate.txt" | sed 's/^/   /'
}

kubectl -n bisect-matrix get pods > "$OUT/fleet_pods.txt" 2>&1

run_leg lan "$LAN_ARM" "$LAN_PORT" 0
"$D/netem_rtt.sh" clear "$LAN_ARM" > /dev/null 2>&1
run_leg wan "$WAN_ARM" "$WAN_PORT" "$WAN_RTT"

echo
echo "capture: $OUT"
