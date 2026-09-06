#!/bin/bash
# Replace the reproduced red comparison with its forward development fix.
set -euo pipefail
D=$(cd "$(dirname "$0")" && pwd)
XRDP_DEB=${XRDP_DEB:?set the committed xrdp package path}
EVIDENCE=${EVIDENCE:?set the capture directory under /work}
BASE=localhost/xrdp-bisect:dev-wire-655639d8-baf9658-u2404-xfce
BASE_ID=b5657410a41da495f83b7231bb3c7763229fa3708f9079bb0008d67d25061d44
RED=localhost/xrdp-bisect:dev-odd-edge-40062
RED_ID=78abda7880067cf8816104077c28512c3dc504e549acf39131a480d1ec45b31d

test -f "$XRDP_DEB"
test -d "$EVIDENCE"
test "$(timeout 10s git -C /work branch --show-current)" = \
    dev/avc444_metablock_checkpoint
test "$(timeout 10s git -C /workUpdateXorgXrdp branch --show-current)" = \
    dev/avc444_metablock_checkpoint
timeout 10s git -C /work diff --exit-code HEAD
timeout 10s git -C /work merge-base --is-ancestor c729a50889a2 HEAD
SOURCE=$(timeout 10s git -C /work rev-parse --short=12 HEAD)
VERSION=$(timeout 10s dpkg-deb -f "$XRDP_DEB" Version)
case "$VERSION" in
    *."$SOURCE") ;;
    *) echo 'Package must identify the clean current commit.' >&2; exit 1 ;;
esac
IMAGE=localhost/xrdp-bisect:dev-visible-clip-$SOURCE
test "$(timeout 20s podman image inspect "$BASE" --format '{{.Id}}')" = \
    "$BASE_ID"
test "$(timeout 20s podman image inspect "$RED" --format '{{.Id}}')" = \
    "$RED_ID"
CURRENT=$(timeout 20s kubectl -n bisect-matrix get deployment xrdp-x046 \
    -o jsonpath='{.spec.template.spec.containers[0].image}')
test "$CURRENT" = "$RED"
if timeout 20s podman image exists "$IMAGE"; then
    echo 'Refusing to overwrite an existing fixed image.' >&2
    exit 1
fi

# Archive all current evidence before the pod replacement logs off its session.
timeout 20s kubectl -n bisect-matrix get deployment xrdp-x046 -o json \
    > "$EVIDENCE/red-deployment.json"
timeout 20s kubectl -n bisect-matrix get pod -l arm=x046 -o json \
    > "$EVIDENCE/red-pod.json"
timeout 30s kubectl -n bisect-matrix exec deployment/xrdp-x046 -- \
    tar -cf - /var/log/xrdp.log /var/log/xrdp-sesman.log \
    /var/log/xrdp-perf /etc/xrdp/gfx.toml > "$EVIDENCE/red-final-logs.tar"

mkdir -p /work/dist
BUILD=$(mktemp -d /work/dist/visible-clip-image.XXXXXX)
cp "$XRDP_DEB" "$BUILD/"
timeout 300s podman build --pull=never \
    --build-arg "XRDP_DEB=${XRDP_DEB##*/}" \
    -t "$IMAGE" -f "$D/Containerfile.odd-edge" "$BUILD"
timeout 180s podman save "$IMAGE" | timeout 180s k3s ctr images import -
timeout 20s podman image inspect "$IMAGE" > "$EVIDENCE/fixed-image.json"

python3 - "$IMAGE" "$BUILD/patch.json" <<'PY'
import json
import sys
patch = {"spec": {"template": {"spec": {
    "containers": [{"name": "xrdp", "image": sys.argv[1], "env": [
        {"name": "ARM_LABEL", "value": "Visible-edge correction (port 40062)"}
    ]}],
    "volumes": [{"name": "wire-trace", "hostPath": {
        "path": "/var/lib/xrdp-matrix/x046-visible-clip",
        "type": "DirectoryOrCreate"
    }}]
}}}}
with open(sys.argv[2], "w") as output:
    json.dump(patch, output)
PY
cp "$BUILD/patch.json" "$EVIDENCE/deployment-patch.json"
timeout 20s kubectl -n bisect-matrix patch deployment xrdp-x046 \
    --type=strategic --patch-file "$BUILD/patch.json"
timeout 310s kubectl -n bisect-matrix rollout status deployment/xrdp-x046 \
    --timeout=300s
timeout 20s kubectl -n bisect-matrix exec deployment/xrdp-x046 -- \
    dpkg-query -W xrdp-dev xorgxrdp-dev
echo '40062 replaced. Certificate and final rendered smoke remain required.'
