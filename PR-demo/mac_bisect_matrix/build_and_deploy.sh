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
# arm-p (the frame_num-wrap re-key, PRD FR-H264-8) is registered below but
# deliberately NOT in the default list: it runs a lowered re-key threshold
# (ltr_rekey_frame_num = 536) so a boundary arrives every ~268 encoded
# frames instead of every ~18 min. It is a boundary-exercising test arm,
# not a member of the steady-state matrix. Deploy it by name:
#   build_and_deploy.sh arm-p
# arm-q is likewise off the default list: it is the RECON arm for xrdp
# BACKLOG #45 recon gate R1 (arm-n's encoder config on an xorgxrdp that
# logs the capture slot actually used, per monitor, per frame). It exists
# to answer that one gate and is retired with it:
#   build_and_deploy.sh arm-q
ARMS="${*:-arm-e arm-m arm-n}"

# arm -> xrdp-dev commit tag. xorgxrdp defaults to the Mac-good ee1ec01
# but MUST be paired per-arm when the xrdp build speaks a newer xup
# contract: with a mismatched pair sesman rejects logins with a contract
# version complaint (caught live 2026-07-27).
XORGXRDP_DEB="xorgxrdp-dev_1%3a0.10.80+gitee1ec01eed50_amd64.deb"
declare -A ARM_XORG_DEB=(
    # arm-m/arm-n: 251bc4d + shmem up-front reservation (SIGBUS ->
    # loud connect-time refusal on undersized /dev/shm, 2026-07-28)
    [arm-m]="xorgxrdp-dev_1%3a0.10.80+git5b9650cafbc3_amd64.deb"
    [arm-n]="xorgxrdp-dev_1%3a0.10.80+git5b9650cafbc3_amd64.deb"
    # arm-p: same xorgxrdp as arm-n; only the xrdp side carries the
    # frame_num-wrap re-key knobs (PRD FR-H264-8)
    [arm-p]="xorgxrdp-dev_1%3a0.10.80+git5b9650cafbc3_amd64.deb"
    # arm-q: arm-n's xrdp deb with the RECON xorgxrdp (5b9650c + the
    # R1SLOT log line) — xrdp BACKLOG #45 recon gate R1. RETIRED with the
    # gate (2026-07-29): R1 and R2 are answered, step 6c landed, and the
    # recon instrumentation is reverted. Kept registered only so an old
    # capture can be reproduced; not in the default arm list.
    [arm-q]="xorgxrdp-dev_1%3a0.10.80+git20260729190443.957fa794ebdc_amd64.deb"
    # arm-r: the BACKLOG #45 arm — step 6's per-monitor capture budget,
    # coverage intersect and per-monitor slot (xorgxrdp d77d05463e52),
    # paired with the xrdp deb carrying steps 0-7
    [arm-r]="xorgxrdp-dev_1%3a0.10.80+git20260729225933.d77d05463e52_amd64.deb"
)
declare -A ARM_TAG=(
    [arm-e]=c693eeab5ec2
    [arm-m]=39bb08a48377.xx5b9650c-xfce
    [arm-n]=34795577580b.xx5b9650c-xfce
    [arm-p]=6894d7de2202.xx5b9650c-xfce
    [arm-q]=34795577580b.xx957fa79
)
declare -A TAG_DEB=(
    [c693eeab5ec2]="xrdp-dev_0.10.80+gitc693eeab5ec2_amd64.deb"
    # .xx<hash> = same xrdp deb, rebuilt image embedding xorgxrdp <hash>
    [39bb08a48377.xx5b9650c]="xrdp-dev_0.10.80+git20260728011331.39bb08a48377_amd64.deb"
    [34795577580b.xx5b9650c]="xrdp-dev_0.10.80+git20260728163625.34795577580b_amd64.deb"
    # BACKLOG #48: ltr_rekey_surface_reset — churn masked from the client
    [6894d7de2202.xx5b9650c]="xrdp-dev_0.10.80+git20260729030225.6894d7de2202_amd64.deb"
    # arm-q: arm-n's xrdp deb, image rebuilt on the recon xorgxrdp
    [34795577580b.xx957fa79]="xrdp-dev_0.10.80+git20260728163625.34795577580b_amd64.deb"
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
# server-side apply: the code corpus (~300KB) exceeds the client-side
# last-applied-configuration annotation limit (256KB)
kubectl -n bisect-matrix create configmap xrdp-banner \
    --from-file=banner.sh="$D/banner.sh" \
    --from-file=code_corpus.ansi="$D/code_corpus.ansi" \
    --dry-run=client -o yaml \
    | kubectl apply --server-side --force-conflicts -f -
# The manifest pins the image tag independently of ARM_TAG above, so a
# copy-pasted k8s/*.yaml silently runs ANOTHER arm's binary and every
# measurement taken from it is about that other arm. Caught live on
# 2026-07-29: k8s/arm-o.yaml still carried arm-n's tag. Fail loudly.
for arm in $ARMS; do
    want="localhost/xrdp-bisect:${ARM_TAG[$arm]}"
    got=$(sed -n 's/^ *image: *//p' "$D/k8s/$arm.yaml" | head -1)
    if [ "$want" != "$got" ]; then
        echo "ABORT: k8s/$arm.yaml pins image '$got' but ARM_TAG says" \
             "'$want' — the arm would run the wrong binary" >&2
        exit 1
    fi
done
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
