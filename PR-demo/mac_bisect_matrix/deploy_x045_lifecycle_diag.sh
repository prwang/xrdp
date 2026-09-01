#!/bin/bash
# Deploy a fault-preserving clean-room wire-diagnostic comparison arm.
set -eu

D=$(cd "$(dirname "$0")" && pwd)
DIST=${DIST:-/work/dist}
XRDP_DEB=${XRDP_DEB:-wire-142c/xrdp-dev_0.10.80+git20260901120335.50a974b6e56c_amd64.deb}
XORGXRDP_DEB=${XORGXRDP_DEB:-xorgxrdp-dev_1%3a0.10.80+git20260826181120.aca3c774cb8b_amd64.deb}
IMAGE=${IMAGE:-localhost/xrdp-bisect:cleanroom-wire-50a974b6-aca3c774-u2404-xfce}
BUILD="$D/.build-x045-lifecycle-diag"

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
cp "$D/chromium-container" "$D/chromium-container.desktop" "$BUILD/"
cp "$D/../textflood/textflood.c" "$BUILD/"
cp "$D/../smoke_gate/colorkey_x11.c" "$BUILD/"
cp "$D/../win2022_ground_truth/chroma_strip_anim.c" "$BUILD/"
cp "$DIST/$XRDP_DEB" "$DIST/$XORGXRDP_DEB" "$BUILD/"

podman build \
    --build-arg "XRDP_DEB=${XRDP_DEB##*/}" \
    --build-arg "XORGXRDP_DEB=${XORGXRDP_DEB##*/}" \
    --build-arg INSTALL_XFCE=1 \
    --build-arg INSTALL_ACCEPTANCE_DESKTOP=1 \
    -t "$IMAGE" -f "$D/Containerfile.ubuntu2404" "$BUILD"
podman save "$IMAGE" | k3s ctr images import -

kubectl apply -f "$D/k8s/namespace.yaml"
kubectl -n bisect-matrix create configmap xrdp-banner \
    --from-file=banner.sh="$D/banner.sh" \
    --from-file=codescroll10.sh="$D/../smoke_gate/codescroll10.sh" \
    --from-file=code_corpus.ansi="$D/code_corpus.ansi" \
    --dry-run=client -o yaml \
    | kubectl apply --server-side --force-conflicts -f -
kubectl -n bisect-matrix create configmap xrdp-gfx-x045 \
    --from-file=gfx.toml="$D/gfx/x042.toml" \
    --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f "$D/k8s/x045.yaml"
image_id=$(podman image inspect --format '{{.Id}}' "$IMAGE")
kubectl -n bisect-matrix patch deployment xrdp-x045 --type merge \
    -p "{\"spec\":{\"template\":{\"metadata\":{\"annotations\":{\"diagnostic-image-id\":\"$image_id\"}}}}}"
kubectl -n bisect-matrix rollout status deployment/xrdp-x045 --timeout=300s

pod=$(kubectl -n bisect-matrix get pod -l arm=x045 \
    --field-selector status.phase=Running \
    -o jsonpath='{.items[0].metadata.name}')
kubectl -n bisect-matrix exec "$pod" -- \
    test -x /usr/sbin/xrdp
kubectl -n bisect-matrix exec "$pod" -- \
    grep -q 'avc_mode = "auto"' /etc/xrdp/gfx.toml
kubectl -n bisect-matrix exec "$pod" -- \
    sh -c 'grep -aq "event=wire_tx" /usr/sbin/xrdp'

echo "x045 clean-room wire diagnostic is ready on 127.0.0.1:40061"
