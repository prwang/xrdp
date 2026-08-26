#!/bin/bash
# Build and deploy the final paired clean-room candidate without tracing.
set -eu

D=$(cd "$(dirname "$0")" && pwd)
DIST=${DIST:-/work/dist}
XRDP_DEB=${XRDP_DEB:-default/xrdp-dev_0.10.80+git20260826181259.f8d8d06d2ffd_amd64.deb}
XORGXRDP_DEB=${XORGXRDP_DEB:-xorgxrdp-dev_1%3a0.10.80+git20260826181120.aca3c774cb8b_amd64.deb}
IMAGE=${IMAGE:-localhost/xrdp-bisect:cleanroom-f8d8d06d-aca3c774-u2404-acceptance.p0c38c1c1}
BUILD="$D/.build-x042-cleanroom"

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
kubectl -n bisect-matrix create configmap xrdp-gfx-x042 \
    --from-file=gfx.toml="$D/gfx/x042.toml" \
    --dry-run=client -o yaml | kubectl apply -f -
kubectl apply -f "$D/k8s/x042.yaml"
image_id=$(podman image inspect --format '{{.Id}}' "$IMAGE")
kubectl -n bisect-matrix patch deployment xrdp-x042 --type merge \
    -p "{\"spec\":{\"template\":{\"metadata\":{\"annotations\":{\"acceptance-image-id\":\"$image_id\"}}}}}"
kubectl -n bisect-matrix rollout status deployment/xrdp-x042 --timeout=300s

pod=$(kubectl -n bisect-matrix get pod -l arm=x042 \
    --field-selector status.phase=Running \
    -o jsonpath='{.items[0].metadata.name}')
kubectl -n bisect-matrix cp "$D/x042_profile.sh" \
    "$pod:/home/tester/x042_profile.sh"
kubectl -n bisect-matrix exec "$pod" -- \
    chown tester:tester /home/tester/x042_profile.sh
kubectl -n bisect-matrix exec "$pod" -- \
    chmod 0755 /home/tester/x042_profile.sh
switch_hash=$(sha256sum "$D/x042_profile.sh" | cut -d' ' -f1)
remote_switch_hash=$(kubectl -n bisect-matrix exec "$pod" -- \
    sha256sum /home/tester/x042_profile.sh | cut -d' ' -f1)
remote_switch_meta=$(kubectl -n bisect-matrix exec "$pod" -- \
    stat -c '%U:%G %a' /home/tester/x042_profile.sh)
test "$switch_hash" = "$remote_switch_hash"
test "$remote_switch_meta" = "tester:tester 755"
if [ -f "$D/../../.turn_draft.md" ]
then
    draft_name=".turn_draft_$(date -u +%Y%m%dT%H%M%SZ).md"
    kubectl -n bisect-matrix cp "$D/../../.turn_draft.md" \
        "$pod:/home/tester/$draft_name"
    kubectl -n bisect-matrix exec "$pod" -- \
        chown tester:tester "/home/tester/$draft_name"
    kubectl -n bisect-matrix exec "$pod" -- \
        chmod 0644 "/home/tester/$draft_name"
    draft_hash=$(sha256sum "$D/../../.turn_draft.md" | cut -d' ' -f1)
    remote_draft_hash=$(kubectl -n bisect-matrix exec "$pod" -- \
        sha256sum "/home/tester/$draft_name" | cut -d' ' -f1)
    remote_draft_meta=$(kubectl -n bisect-matrix exec "$pod" -- \
        stat -c '%U:%G %a' "/home/tester/$draft_name")
    test "$draft_hash" = "$remote_draft_hash"
    test "$remote_draft_meta" = "tester:tester 644"
fi
