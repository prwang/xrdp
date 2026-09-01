#!/bin/bash
# Build and deploy the canonical development wire-diagnostic arm.
set -eu

D=$(cd "$(dirname "$0")" && pwd)
DIST=${DIST:-/work/dist/wire-142c}
XRDP_DEB=${XRDP_DEB:-xrdp-dev_0.10.80+git20260901120335.655639d8270c_amd64.deb}
XORGXRDP_DEB=${XORGXRDP_DEB:-xorgxrdp-dev_1%3a0.10.80+git20260828123216.baf9658c397d_amd64.deb}
IMAGE=${IMAGE:-localhost/xrdp-bisect:dev-wire-655639d8-baf9658-u2404-xfce}
BUILD="$D/.build-x044-resize-fixed"

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

podman build --pull \
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
kubectl -n bisect-matrix create configmap xrdp-gfx-x044 \
    --from-file=gfx.toml="$D/gfx/x044.toml" \
    --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f "$D/k8s/x044.yaml"
image_id=$(podman image inspect --format '{{.Id}}' "$IMAGE")
kubectl -n bisect-matrix patch deployment xrdp-x044 --type merge \
    -p "{\"spec\":{\"template\":{\"metadata\":{\"annotations\":{\"diagnostic-image-id\":\"$image_id\"}}}}}"
kubectl -n bisect-matrix rollout status deployment/xrdp-x044 --timeout=300s

pod=$(kubectl -n bisect-matrix get pod -l arm=x044 \
    --field-selector status.phase=Running \
    -o jsonpath='{.items[0].metadata.name}')
kubectl -n bisect-matrix exec "$pod" -- \
    test -x /usr/sbin/xrdp
kubectl -n bisect-matrix exec "$pod" -- \
    grep -q 'avc_mode = "auto"' /etc/xrdp/gfx.toml
kubectl -n bisect-matrix exec "$pod" -- \
    sh -c 'grep -aq "event=wire_tx" /usr/sbin/xrdp'

echo "x044 canonical development wire diagnostic is ready on 127.0.0.1:40060"
