#!/bin/bash
# Build the per-arm images, import them into k3s, and (re)deploy the whole
# bisect matrix. Idempotent: rerun after editing any gfx/*.toml or manifest.
#
# Host prerequisites (dev box, documented in CLAUDE.md "Bisect/diagnosis
# sessions"): k3s (LXC config in /etc/rancher/k3s/config.yaml, /dev/kmsg
# symlink), podman, the xrdp-dev/xorgxrdp-dev debs in /work/dist.
#
# Secret handling: the container tester account reuses the HOST tester
# password by copying its crypt hash from /etc/shadow into root-only
# /etc/xrdp-matrix/tester.hash, mounted read-only into the pods. The hash
# never enters the image, git, or a process argument.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
DIST=${DIST:-/work/dist}

# arm -> xrdp-dev commit tag (xorgxrdp is the Mac-good ee1ec01 everywhere)
XORGXRDP_DEB="xorgxrdp-dev_1%3a0.10.80+gitee1ec01eed50_amd64.deb"
declare -A ARM_TAG=(
    [arm-a]=52099149 [arm-b]=52099149 [arm-c]=e96e655416dc [arm-d]=52099149
    [arm-e]=c693eeab5ec2 [arm-f]=52099149
)
declare -A TAG_DEB=(
    [52099149]="xrdp-dev_0.10.80+git520991491f1e_amd64.deb"
    [e96e655416dc]="xrdp-dev_0.10.80+gite96e655416dc_amd64.deb"
    [c693eeab5ec2]="xrdp-dev_0.10.80+gitc693eeab5ec2_amd64.deb"
)

# --- tester credential hash (root-only, host -> pods) ---
mkdir -p /etc/xrdp-matrix
chmod 700 /etc/xrdp-matrix
awk -F: '$1 == "tester" { print $2 }' /etc/shadow > /etc/xrdp-matrix/tester.hash
chmod 600 /etc/xrdp-matrix/tester.hash
[ -s /etc/xrdp-matrix/tester.hash ] || {
    echo "ABORT: no tester hash on this host"; exit 1; }
# probe444 (automated byte-verification) reuses the oracle probe credential
if [ -s /root/.oracle_cred ]; then
    openssl passwd -6 -stdin < /root/.oracle_cred \
        > /etc/xrdp-matrix/probe.hash
    chmod 600 /etc/xrdp-matrix/probe.hash
fi

# --- images: one per distinct xrdp-dev commit ---
BUILD="$D/.build"
rm -rf "$BUILD" && mkdir -p "$BUILD"
cp "$D/entrypoint.sh" "$D/startwm.sh" "$D/banner.sh" "$BUILD/"
for tag in $(printf '%s\n' "${ARM_TAG[@]}" | sort -u); do
    deb=${TAG_DEB[$tag]}
    cp "$DIST/$deb" "$DIST/$XORGXRDP_DEB" "$BUILD/"
    podman build \
        --build-arg XRDP_DEB="$deb" \
        --build-arg XORGXRDP_DEB="$XORGXRDP_DEB" \
        -t "localhost/xrdp-bisect:$tag" -f "$D/Containerfile" "$BUILD"
    # k3s runs pods with the native snapshotter (see /etc/rancher/k3s/
    # config.yaml); ctr import can't target it in this containerd build,
    # so the first container create unpacks the image natively — that
    # full-copy is why kubelet needs runtime-request-timeout=15m
    podman save "localhost/xrdp-bisect:$tag" \
        | k3s ctr images import - >/dev/null
    echo "image xrdp-bisect:$tag built + imported"
done

# --- deploy ---
kubectl apply -f "$D/k8s/namespace.yaml"
for arm in arm-a arm-b arm-c arm-d arm-e arm-f; do
    kubectl -n bisect-matrix create configmap "xrdp-gfx-$arm" \
        --from-file=gfx.toml="$D/gfx/$arm.toml" \
        --dry-run=client -o yaml | kubectl apply -f -
    kubectl apply -f "$D/k8s/$arm.yaml"
    # config changes must reach a FRESH pod (gfx.toml is read at connect,
    # but binaries/entrypoint state are not) — always roll
    kubectl -n bisect-matrix rollout restart "deployment/xrdp-$arm"
done
kubectl -n bisect-matrix rollout status deployment --timeout=180s
kubectl -n bisect-matrix get pods -o wide
echo
echo "matrix up: arm-a 127.0.0.1:40000  arm-b :40001  arm-c :40002" \
     "arm-d :40003  arm-e :40004"
