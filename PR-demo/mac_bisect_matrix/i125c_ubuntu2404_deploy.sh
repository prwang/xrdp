#!/bin/bash
# Build and deploy the one Ubuntu 24.04 / FFmpeg 6 CPU arm for #125C.
# XRDP_DEB may name a repaired package; the default is the unmodified
# frontier package used for the required first reproduction.
set -eu

D=$(cd "$(dirname "$0")" && pwd)
DIST=${DIST:-/work/dist}
XRDP_DEB=${XRDP_DEB:-xrdp-dev_0.10.80+git20260824141733.4cf5063e05d3_amd64.deb}
XORGXRDP_DEB=${XORGXRDP_DEB:-frontier-20260822/xorgxrdp-dev_1%3a0.10.80+git20260822114312.c190343ff28a_amd64.deb}
IMAGE=${IMAGE:-localhost/xrdp-bisect:i125c-u2404-frontier}
BUILD="$D/.build-i125c-u2404"

test -f "$DIST/$XRDP_DEB"
test -f "$DIST/$XORGXRDP_DEB"
rm -rf "$BUILD"
mkdir -p "$BUILD"
cp "$D/entrypoint.sh" "$D/startwm.sh" "$D/banner.sh" "$BUILD/"
cp "$DIST/$XRDP_DEB" "$BUILD/"
cp "$DIST/$XORGXRDP_DEB" "$BUILD/"

podman build --pull \
    --build-arg "XRDP_DEB=${XRDP_DEB##*/}" \
    --build-arg "XORGXRDP_DEB=${XORGXRDP_DEB##*/}" \
    -t "$IMAGE" -f "$D/Containerfile.ubuntu2404" "$BUILD"
podman save "$IMAGE" | k3s ctr images import -

kubectl -n bisect-matrix create configmap xrdp-gfx-x039 \
    --from-file=gfx.toml="$D/gfx/x039.toml" \
    --dry-run=client -o yaml | kubectl apply -f -
kubectl -n bisect-matrix create configmap xrdp-banner \
    --from-file=banner.sh="$D/banner.sh" \
    --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f "$D/k8s/x039.yaml"
kubectl -n bisect-matrix set image deployment/xrdp-x039 xrdp="$IMAGE"
kubectl -n bisect-matrix rollout status deployment/xrdp-x039 --timeout=120s
