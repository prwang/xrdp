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
# Usage: build_and_deploy.sh [arm-x ...] — no args = all arms. Images are
# only (re)built when missing (FORCE_BUILD=1 overrides): session content
# (banner.sh) and gfx.toml are ConfigMaps, so the common iteration —
# tweak content/config, roll ONE arm — never rebuilds or re-imports an
# image (the ~1.5GB import + native-snapshotter unpack is the slow path).
ARMS="${*:-arm-a arm-b arm-c arm-d arm-e arm-f arm-g arm-h arm-i arm-j arm-k arm-l}"

# arm -> xrdp-dev commit tag. xorgxrdp defaults to the Mac-good ee1ec01
# but MUST be paired per-arm when the xrdp build speaks a newer xup
# contract: arm-h (xrdp bd1ab35b, contract 20260727) requires the T4's
# xorgxrdp 251bc4d — with ee1ec01 sesman rejects logins with a contract
# version mismatch (caught live 2026-07-27, port 40007).
XORGXRDP_DEB="xorgxrdp-dev_1%3a0.10.80+gitee1ec01eed50_amd64.deb"
declare -A ARM_XORG_DEB=(
    [arm-h]="xorgxrdp-dev_1%3a0.10.80+git251bc4d3db8d_amd64.deb"
    [arm-i]="xorgxrdp-dev_1%3a0.10.80+git251bc4d3db8d_amd64.deb"
    [arm-k]="xorgxrdp-dev_1%3a0.10.80+git251bc4d3db8d_amd64.deb"
    [arm-j]="xorgxrdp-dev_1%3a0.10.80+git251bc4d3db8d_amd64.deb"
    [arm-l]="xorgxrdp-dev_1%3a0.10.80+git251bc4d3db8d_amd64.deb"
)
declare -A ARM_TAG=(
    [arm-a]=52099149 [arm-b]=52099149 [arm-c]=e96e655416dc [arm-d]=52099149
    [arm-e]=c693eeab5ec2 [arm-f]=52099149 [arm-g]=52099149-xfce
    [arm-h]=bd1ab35b791e-xfce [arm-i]=bd1ab35b791e-xfce
    [arm-j]=649b447c9f4d-xfce [arm-k]=8b8d17c2636a-xfce
    [arm-l]=459b66d5319f-xfce
)
declare -A TAG_DEB=(
    [52099149]="xrdp-dev_0.10.80+git520991491f1e_amd64.deb"
    [e96e655416dc]="xrdp-dev_0.10.80+gite96e655416dc_amd64.deb"
    [c693eeab5ec2]="xrdp-dev_0.10.80+gitc693eeab5ec2_amd64.deb"
    [bd1ab35b791e]="xrdp-dev_0.10.80+git20260727002823.bd1ab35b791e_amd64.deb"
    [649b447c9f4d]="xrdp-dev_0.10.80+git20260726151325.649b447c9f4d_amd64.deb"
    [8b8d17c2636a]="xrdp-dev_0.10.80+git20260727184103.8b8d17c2636a_amd64.deb"
    [459b66d5319f]="xrdp-dev_0.10.80+git20260727210620.459b66d5319f_amd64.deb"
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
for arm in $ARMS; do
    tag=${ARM_TAG[$arm]}
    if [ "${FORCE_BUILD:-0}" != "1" ] \
            && k3s ctr images ls -q | grep -q "xrdp-bisect:$tag"; then
        continue
    fi
    base_tag=${tag%-xfce}
    xfce=0; [ "$base_tag" != "$tag" ] && xfce=1
    deb=${TAG_DEB[$base_tag]}
    xorg_deb="${ARM_XORG_DEB[$arm]:-$XORGXRDP_DEB}"
    cp "$DIST/$deb" "$DIST/$xorg_deb" "$BUILD/"
    podman build \
        --build-arg XRDP_DEB="$deb" \
        --build-arg XORGXRDP_DEB="$xorg_deb" \
        --build-arg INSTALL_XFCE="$xfce" \
        -t "localhost/xrdp-bisect:$tag" -f "$D/Containerfile" "$BUILD"
    # k3s runs pods with the fuse-overlayfs snapshotter (see
    # /etc/rancher/k3s/config.yaml; switched from native 2026-07-27 —
    # native full-copied the rootfs per image at first create, ~20 min
    # per new arm). New images still unpack layers once at first create
    # (minutes, kubelet runtime-request-timeout=15m covers it); every
    # later pod create is an overlay mount (seconds)
    podman save "localhost/xrdp-bisect:$tag" \
        | k3s ctr images import - >/dev/null
    echo "image xrdp-bisect:$tag built + imported"
done

# --- deploy ---
kubectl apply -f "$D/k8s/namespace.yaml"
kubectl -n bisect-matrix create configmap xrdp-banner \
    --from-file=banner.sh="$D/banner.sh" \
    --dry-run=client -o yaml | kubectl apply -f -
for arm in $ARMS; do
    kubectl -n bisect-matrix create configmap "xrdp-gfx-$arm" \
        --from-file=gfx.toml="$D/gfx/$arm.toml" \
        --dry-run=client -o yaml | kubectl apply -f -
    kubectl apply -f "$D/k8s/$arm.yaml"
    # config changes must reach a FRESH pod (gfx.toml is read at connect,
    # but binaries/entrypoint state are not) — always roll
    kubectl -n bisect-matrix rollout restart "deployment/xrdp-$arm"
done
for arm in $ARMS; do
    kubectl -n bisect-matrix rollout status "deployment/xrdp-$arm" \
        --timeout=300s
done
kubectl -n bisect-matrix get pods -o wide
echo
echo "matrix up: arm-a 127.0.0.1:40000  arm-b :40001  arm-c :40002" \
     "arm-d :40003  arm-e :40004"
