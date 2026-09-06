#!/bin/bash
# One immutable odd-edge comparison. Never modifies the existing controls.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
XRDP_DEB=${XRDP_DEB:?set the absolute path of the committed xrdp package}
IMAGE=localhost/xrdp-bisect:dev-odd-edge-40062
BASE=localhost/xrdp-bisect:dev-wire-655639d8-baf9658-u2404-xfce
BASE_ID=b5657410a41da495f83b7231bb3c7763229fa3708f9079bb0008d67d25061d44

test -f "$XRDP_DEB"
test "$(timeout 20s git -C /work branch --show-current)" = \
    dev/avc444_metablock_checkpoint
timeout 20s git -C /work diff --exit-code HEAD -- xrdp common libxrdp
test "$(timeout 20s podman image inspect "$BASE" --format '{{.Id}}')" = \
    "$BASE_ID"
if timeout 20s kubectl -n bisect-matrix get deployment xrdp-x046 \
    >/dev/null 2>&1; then
    echo 'Refusing to replace the existing comparison arm.' >&2
    exit 1
fi
test -s /etc/xrdp-matrix/tester.hash
test -s /etc/xrdp-matrix/probe.hash
BUILD=$(mktemp -d "$D/.build-x046-odd-edge.XXXXXX")
cp "$XRDP_DEB" "$BUILD/"
timeout 300s podman build --pull=never \
    --build-arg "XRDP_DEB=${XRDP_DEB##*/}" \
    -t "$IMAGE" -f "$D/Containerfile.odd-edge" "$BUILD"
timeout 180s podman save "$IMAGE" | timeout 180s k3s ctr images import -
timeout 20s kubectl -n bisect-matrix create configmap xrdp-gfx-x046 \
    --from-file=gfx.toml="$D/gfx/x046.toml" \
    --dry-run=client -o yaml | timeout 20s kubectl apply -f -
timeout 20s kubectl apply -f "$D/k8s/x046.yaml"
timeout 310s kubectl -n bisect-matrix rollout status \
    deployment/xrdp-x046 --timeout=300s
timeout 20s kubectl -n bisect-matrix exec deployment/xrdp-x046 -- \
    dpkg-query -W xrdp-dev xorgxrdp-dev
echo 'Port 40062 installed. Certificate and smoke are still required.'
