#!/bin/bash
# i92_sparse_aux_ab.sh — BACKLOG #92 / PRD FR-H264-9: what does dropping
# the chroma view in motion actually buy?
#
# THE TREATMENT, in one sentence. The AVC444 aux view carries the chroma
# detail, is 44.8 % of the bytes and costs a whole second full-frame pack
# and a second encode. This build sends it only when the screen settles
# (chroma_idle_ms) and at least once every chroma_refresh_ms whatever the
# screen is doing; every other frame ships the luma view alone as an
# LC=1 PDU with no LC=2 behind it.
#
# ONE ARM, TWO CONFIGURATIONS — and that is the whole design.
# The owner directed one new arm (x030). Comparing it against archived
# numbers alone would confound the treatment with a new xrdp build, a
# different day and a drifting host: an UNCHANGED arm on this box has
# moved 36.8 -> 41.8 ms across a single day (BACKLOG #88). So the
# control is this same arm with the feature switched off in its
# ConfigMap:
#
#   OFF   chroma_refresh_ms = 0     the aux view on every frame, which
#                                   is byte-for-byte the behaviour that
#                                   shipped before #92
#   ON    chroma_refresh_ms = 1000  the owner's deployed values, with
#         chroma_idle_ms   = 100    chroma clamped to <= 10 per second
#
# Same image, same xorgxrdp, same payload, same geometry, same client
# rig, same hour. The legs INTERLEAVE off/on/off/on so each condition's
# pair brackets the other's and drift cannot be read as an effect.
# Rolling a ConfigMap is not a new arm: it is the cheap iteration
# build_and_deploy.sh is built around, no deb and no image.
#
# Every leg archives the gfx.toml that was live for it, so which body
# produced which number is never in doubt.
#
# GEOMETRY. One monitor at 3840x2400 = 9.22 Mpx, SESSION_KIND=textflood,
# oracle client — the same conditions as the archived numbers this is
# read against (x014's 18.5 ms/send, captures/i76_x015_fif1_20260802).
#
# MECHANISM CHECKS BEFORE ANY RATE (quality gate 2), printed per leg:
#   * the ON legs must show 'auxdue' records with the decision field 0
#     on a real fraction of frames. All-1 means the feature never
#     applied and the leg says nothing about it.
#   * the OFF legs must show either no 'auxdue' records at all or the
#     decision 1 on every one. Any 0 on a control leg means the wrong
#     ConfigMap was live.
#   * the gap between consecutive chroma frames must never exceed
#     chroma_refresh_ms on an ON leg. That is the GUARANTEE; a run that
#     violates it is a red result whatever the rate says.
#
# Usage: ./i92_sparse_aux_ab.sh [seconds-per-leg]   (default 20)

set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-20}
ARM=${I92_ARM:-x030}
PORT=${I92_PORT:-40046}
NS=bisect-matrix
OUT=$D/captures/i92_sparse_aux_ab_$(date +%Y%m%d_%H%M%S)_s$SECS
mkdir -p "$OUT"

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

# Build the two gfx.toml bodies ONCE, up front, from the arm's committed
# file. They differ by exactly the two chroma lines; everything else is
# byte-identical by construction rather than by inspection.
ON_TOML=$OUT/gfx_on.toml
OFF_TOML=$OUT/gfx_off.toml
cp "$D/gfx/$ARM.toml" "$ON_TOML"
sed 's/^chroma_refresh_ms = .*/chroma_refresh_ms = 0/;
     s/^chroma_idle_ms = .*/chroma_idle_ms = 0/' "$ON_TOML" > "$OFF_TOML"
grep -q '^chroma_refresh_ms = 1000' "$ON_TOML" \
    || fail "the ON body does not set chroma_refresh_ms = 1000"
grep -q '^chroma_refresh_ms = 0' "$OFF_TOML" \
    || fail "the OFF body does not set chroma_refresh_ms = 0"
echo "the two bodies differ by:"
diff "$OFF_TOML" "$ON_TOML" | sed 's/^/   /'

roll()
{
    # swap the arm's gfx.toml ConfigMap and wait for the pod to come
    # back on it. The pod must be RECREATED, not just re-read: gfx.toml
    # is loaded once at startup.
    body=$1
    kubectl -n "$NS" create configmap "xrdp-gfx-$ARM" \
        --from-file=gfx.toml="$body" --dry-run=client -o yaml \
        | kubectl apply -f - >/dev/null \
        || fail "could not update the gfx ConfigMap"
    kubectl -n "$NS" rollout restart "deployment/xrdp-$ARM" >/dev/null \
        || fail "could not restart $ARM"
    kubectl -n "$NS" rollout status "deployment/xrdp-$ARM" \
        --timeout=180s >/dev/null || fail "$ARM did not come back"
    # the deployed body, read back OUT OF THE POD -- not assumed from
    # what we just applied
    pod=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
          -o jsonpath='{.items[0].metadata.name}')
    kubectl -n "$NS" exec "$pod" -- sha256sum /etc/xrdp/gfx.toml \
        | cut -c1-16
}

run_leg()
{
    leg=$1
    body=$2
    legout=$OUT/leg_$leg
    mkdir -p "$legout"

    echo "=== leg $leg: arm $ARM, port $PORT, ${SECS}s, body $(basename "$body") ==="
    live=$(roll "$body")
    cp "$body" "$legout/gfx.toml"
    echo "$live" > "$legout/deployed_gfx_sha.txt"
    echo "   deployed gfx.toml sha256:$live"

    T0=$(date +%s)
    E_ARM=$ARM E_PORT=$PORT E_MODE=oracle E_MONITORS=1 \
        E_MODE0=3840x2400R \
        E_MODELINE0="592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync" \
        E5_BASE_MS=18.5 E_OUT="$legout" \
        bash "$D/e_gate_run.sh" "$SECS" > "$legout/gate.txt" 2>&1
    rc=$?
    T1=$(date +%s)

    {
        echo "t0 $T0"
        echo "t1 $T1"
        echo "arm $ARM"
        echo "port $PORT"
        echo "secs $SECS"
        echo "leg $leg"
        echo "body $(basename "$body")"
        echo "gfx_sha $live"
    } > "$legout/window.txt"

    if [ $rc -ne 0 ]; then
        tail -20 "$legout/gate.txt"
        fail "leg $leg: gate run failed (rc=$rc)"
    fi
    grep -aE "^sends:|send-to-send|FR-BENCH-1 margin" "$legout/gate.txt" \
        | sed 's/^/   /'
}

kubectl -n "$NS" get pods > "$OUT/fleet_pods.txt" 2>&1
cp "$D/certs/$ARM.cert" "$OUT/arm_certificate.txt" 2>/dev/null \
    || fail "$ARM has no certificate -- certify the arm before measuring it"

run_leg a1 "$OFF_TOML"
run_leg b1 "$ON_TOML"
run_leg a2 "$OFF_TOML"
run_leg b2 "$ON_TOML"

# leave the arm on its COMMITTED body, so the fleet's state matches the
# file in git rather than whatever the last leg happened to be
roll "$ON_TOML" >/dev/null

echo
echo "capture: $OUT"
echo "now run: python3 $D/i92_sparse_aux_analyze.py $OUT"
