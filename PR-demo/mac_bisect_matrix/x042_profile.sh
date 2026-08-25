#!/bin/bash
# Select and wire-certify one exact x042 interactive compatibility profile.
set -eu

D=$(cd "$(dirname "$0")" && pwd)
DRAFT="$D/../../.turn_draft.md"
NS=bisect-matrix
ARM=x042
PORT=40058
MODE=${1:-}

case "$MODE" in
    auto) PROFILE="$D/gfx/x042.toml" ;;
    444) PROFILE="$D/gfx/x042-444.toml" ;;
    444v1) PROFILE="$D/gfx/x042-444v1.toml" ;;
    420) PROFILE="$D/gfx/x042-420.toml" ;;
    sparse) PROFILE="$D/gfx/x042-sparse.toml" ;;
    *)
        echo "usage: $0 auto|444|444v1|420|sparse" >&2
        exit 2
        ;;
esac

pod=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
    --field-selector status.phase=Running \
    -o jsonpath='{.items[0].metadata.name}' 2>/dev/null)
if [ -n "$pod" ] && kubectl -n "$NS" exec "$pod" -- \
    pgrep -u tester -x Xorg >/dev/null 2>&1
then
    echo "x042 has a live tester desktop; log off the whole session first" >&2
    exit 1
fi

profile_hash=$(sha256sum "$PROFILE" | cut -d' ' -f1)
kubectl -n "$NS" create configmap xrdp-gfx-x042 \
    --from-file=gfx.toml="$PROFILE" \
    --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f "$D/k8s/x042.yaml"
kubectl -n "$NS" patch deployment xrdp-x042 --type merge \
    -p "{\"spec\":{\"template\":{\"metadata\":{\"annotations\":{\"interactive-profile-sha\":\"$profile_hash\"}}}}}"
kubectl -n "$NS" rollout status deployment/xrdp-x042 --timeout=300s

if [ -f "$DRAFT" ]
then
    pod=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
        --field-selector status.phase=Running \
        -o jsonpath='{.items[0].metadata.name}')
    draft_name=".turn_draft_$(date -u +%Y%m%dT%H%M%SZ).md"
    kubectl -n "$NS" cp "$DRAFT" "$pod:/home/tester/$draft_name"
    kubectl -n "$NS" exec "$pod" -- \
        chown tester:tester "/home/tester/$draft_name"
    kubectl -n "$NS" exec "$pod" -- chmod 0644 "/home/tester/$draft_name"
fi

cert="$D/certs/x042-$MODE.cert"
E_GFX_FILE="$PROFILE" E_CERTFILE="$cert" CERT_SECS=3 \
    timeout 180s bash "$D/arm_certify.sh" "$ARM" "$PORT"
cp "$cert" "$D/certs/x042.cert"

echo "x042 profile '$MODE' is active and certified on 127.0.0.1:$PORT"
echo "profile sha256: $profile_hash"
echo "certificate: $cert"
