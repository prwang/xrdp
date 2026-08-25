#!/bin/bash

# Run the clean-room dense/sparse by wire-window replay on one disposable
# local arm. Interactive arms x040 and x042 are never changed.

set -eu

D=$(cd "$(dirname "$0")" && pwd)
ARM=x041
NS=bisect-matrix
PORT=40057
SECS=${1:-20}
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
OUT=${I142_OUT:-$D/captures/i142_local_matrix_$STAMP}
BASE=$D/gfx/x041.toml
MANIFEST=$D/k8s/x041.yaml

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

[ "$SECS" = 20 ] || fail "the retained replay is exactly 20 seconds per leg"
[ -f "$BASE" ] || fail "missing baseline profile $BASE"
[ -f "$MANIFEST" ] || fail "missing arm manifest $MANIFEST"
mkdir -p "$OUT/profiles" "$OUT/certs"

make_profile()
{
    name=$1
    window=$2
    refresh=$3
    idle=$4
    sed \
        -e "s/^wire_window = .*/wire_window = $window/" \
        -e "s/^chroma_refresh_ms = .*/chroma_refresh_ms = $refresh/" \
        -e "s/^chroma_idle_ms = .*/chroma_idle_ms = $idle/" \
        "$BASE" > "$OUT/profiles/$name.toml"
    [ "$(grep -c '^wire_window = ' "$OUT/profiles/$name.toml")" = 1 ] ||
        fail "$name does not contain exactly one wire_window"
    [ "$(grep -c '^chroma_refresh_ms = ' "$OUT/profiles/$name.toml")" = 1 ] ||
        fail "$name does not contain exactly one chroma_refresh_ms"
    [ "$(grep -c '^chroma_idle_ms = ' "$OUT/profiles/$name.toml")" = 1 ] ||
        fail "$name does not contain exactly one chroma_idle_ms"
}

make_profile dense-w1 1 0 0
make_profile sparse-w1 1 1000 100
make_profile dense-w2 2 0 0
make_profile sparse-w2 2 1000 100

for profile in "$OUT"/profiles/*.toml
do
    sed -e '/^wire_window = /d' \
        -e '/^chroma_refresh_ms = /d' \
        -e '/^chroma_idle_ms = /d' "$profile" | sha256sum
done > "$OUT/profile-common-sha256.txt"
[ "$(awk '{print $1}' "$OUT/profile-common-sha256.txt" | sort -u | wc -l)" = 1 ] ||
    fail "profiles differ outside the three treatment settings"

kubectl apply -f "$D/k8s/namespace.yaml"
kubectl -n "$NS" create configmap xrdp-banner \
    --from-file=banner.sh="$D/banner.sh" \
    --from-file=codescroll10.sh="$D/../smoke_gate/codescroll10.sh" \
    --from-file=code_corpus.ansi="$D/code_corpus.ansi" \
    --dry-run=client -o yaml \
    | kubectl apply --server-side --force-conflicts -f -
kubectl -n "$NS" create configmap xrdp-benchmark-x041 \
    --from-file=startwm.sh="$D/startwm.sh" \
    --from-file=textflood.desktop="$D/textflood-autostart.desktop" \
    --dry-run=client -o yaml \
    | kubectl apply --server-side --force-conflicts -f -

deploy_profile()
{
    profile=$1
    profile_hash=$(sha256sum "$profile" | cut -d' ' -f1)
    kubectl -n "$NS" create configmap xrdp-gfx-x041 \
        --from-file=gfx.toml="$profile" \
        --dry-run=client -o yaml | kubectl apply -f -
    kubectl apply -f "$MANIFEST"
    kubectl -n "$NS" patch deployment xrdp-x041 --type merge \
        -p "{\"spec\":{\"template\":{\"metadata\":{\"annotations\":{\"matrix-profile-sha\":\"$profile_hash\"}}}}}"
    kubectl -n "$NS" rollout status deployment/xrdp-x041 --timeout=300s
    pod=$(kubectl -n "$NS" get pod -l arm=x041 \
        --field-selector status.phase=Running \
        -o jsonpath='{.items[0].metadata.name}')
    [ -n "$pod" ] || fail "rollout has no running $ARM pod"
    kubectl -n "$NS" exec "$pod" -- cat /etc/xrdp/gfx.toml \
        > "$OUT/deployed-gfx.toml"
    cmp "$profile" "$OUT/deployed-gfx.toml" ||
        fail "deployed config differs from $profile"
}

for profile_name in dense-w1 sparse-w1 dense-w2 sparse-w2
do
    profile=$OUT/profiles/$profile_name.toml
    cert=$OUT/certs/$profile_name.cert
    echo "certifying $profile_name"
    deploy_profile "$profile"
    E_GFX_FILE="$profile" E_CERTFILE="$cert" CERT_SECS=3 \
        timeout 180s bash "$D/arm_certify.sh" "$ARM" "$PORT" ||
        fail "$profile_name certification failed"
done

printf '%s\n' \
    'A1 dense-w1' 'B1 sparse-w1' 'C1 dense-w2' 'D1 sparse-w2' \
    'D2 sparse-w2' 'C2 dense-w2' 'B2 sparse-w1' 'A2 dense-w1' \
    > "$OUT/order.txt"

while read -r leg profile_name
do
    profile=$OUT/profiles/$profile_name.toml
    cert=$OUT/certs/$profile_name.cert
    leg_out=$OUT/leg_$leg
    mkdir -p "$leg_out"
    echo "running $leg: $profile_name, ${SECS}s"
    deploy_profile "$profile"
    {
        echo "condition $leg"
        echo "profile $profile_name"
        echo "seconds $SECS"
        echo "ready_stamp_lines 60"
    } > "$leg_out/condition.txt"
    E_TARGET=pod E_ARM="$ARM" E_PORT="$PORT" E_MODE=oracle \
        E_MONITORS=1 E_MODE0=3840x2400R \
        E_MODELINE0='592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync' \
        E5_BASE_MS=none E_GFX_FILE="$profile" E_CERTFILE="$cert" \
        E_READY_STAMP_LINES=60 E_OUT="$leg_out" \
        timeout 150s bash "$D/e_gate_run.sh" "$SECS" \
        > "$leg_out/gate.txt" 2>&1 ||
        {
            tail -n 80 "$leg_out/gate.txt" >&2
            fail "$leg gate failed"
        }
    grep -aE '^sends:|send-to-send|producer margin|session logged off' \
        "$leg_out/gate.txt" | sed 's/^/  /'
done < "$OUT/order.txt"

python3 "$D/i125b_analyze.py" "$OUT" > "$OUT/analysis.txt" ||
{
    cat "$OUT/analysis.txt" >&2
    fail "matrix analysis failed"
}
cat "$OUT/analysis.txt"

kubectl -n "$NS" scale deployment/xrdp-x041 --replicas=0
echo "capture: $OUT"
