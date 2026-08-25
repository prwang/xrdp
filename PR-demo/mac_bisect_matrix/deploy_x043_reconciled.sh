#!/bin/bash
# Build and deploy the trace-disabled reconciled development red arm.
set -eu

D=$(cd "$(dirname "$0")" && pwd)
DIST=${DIST:-/work/dist/reconciled-142c}
XRDP_DEB=${XRDP_DEB:-xrdp-dev_0.10.80+git20260825151057.f550e9151f70_amd64.deb}
XORGXRDP_DEB=${XORGXRDP_DEB:-xorgxrdp-dev_1%3a0.10.80+git20260825152758.25a273a17510_amd64.deb}
IMAGE=${IMAGE:-localhost/xrdp-bisect:reconciled-f550e915-25a273a-u2404-xfce-notrace}
BUILD="$D/.build-x043-reconciled"

test -f "$DIST/$XRDP_DEB"
test -f "$DIST/$XORGXRDP_DEB"
mkdir -p /etc/xrdp-matrix
chmod 700 /etc/xrdp-matrix
awk -F: '$1 == "tester" { print $2 }' /etc/shadow \
    > /etc/xrdp-matrix/tester.hash
chmod 600 /etc/xrdp-matrix/tester.hash
if [ -s /root/.oracle_cred ]; then
    openssl passwd -6 -stdin < /root/.oracle_cred \
        > /etc/xrdp-matrix/probe.hash
    chmod 600 /etc/xrdp-matrix/probe.hash
fi

rm -rf "$BUILD"
mkdir -p "$BUILD"
cp "$D/entrypoint.sh" "$D/startwm.sh" "$D/banner.sh" "$BUILD/"
cp "$D/chroma_probe.py" "$D/chroma-probe.desktop" "$BUILD/"
cp "$D/../textflood/textflood.c" "$BUILD/"
cp "$D/../smoke_gate/colorkey_x11.c" "$BUILD/"
cp "$D/../win2022_ground_truth/chroma_strip_anim.c" "$BUILD/"
cp "$DIST/$XRDP_DEB" "$DIST/$XORGXRDP_DEB" "$BUILD/"

podman build --pull \
    --build-arg "XRDP_DEB=${XRDP_DEB##*/}" \
    --build-arg "XORGXRDP_DEB=${XORGXRDP_DEB##*/}" \
    --build-arg INSTALL_XFCE=1 \
    -t "$IMAGE" -f "$D/Containerfile.ubuntu2404" "$BUILD"
podman save "$IMAGE" | k3s ctr images import -

kubectl apply -f "$D/k8s/namespace.yaml"
kubectl -n bisect-matrix create configmap xrdp-banner \
    --from-file=banner.sh="$D/banner.sh" \
    --from-file=codescroll10.sh="$D/../smoke_gate/codescroll10.sh" \
    --from-file=code_corpus.ansi="$D/code_corpus.ansi" \
    --dry-run=client -o yaml \
    | kubectl apply --server-side --force-conflicts -f -
kubectl -n bisect-matrix create configmap xrdp-gfx-x043 \
    --from-file=gfx.toml="$D/gfx/x043.toml" \
    --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f "$D/k8s/x043.yaml"
kubectl -n bisect-matrix rollout status deployment/xrdp-x043 --timeout=300s
