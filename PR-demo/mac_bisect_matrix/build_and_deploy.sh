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
# arm-u / arm-v are the BACKLOG #70 A/B pair: the SAME two debs on
# both, differing only in gfx.toml's eager_slot_ack. They are deployed
# together or not at all -- an eager measurement with no control arm
# has no baseline:
#   build_and_deploy.sh arm-u arm-v
# arm-s / arm-t are the BACKLOG #52 (E5-2) PAIR: the same xorgxrdp and
# the same encoder config, xrdp with (arm-s) and without (arm-t) #45
# steps 5+7, both running SESSION_KIND=codeflood so the payload does not
# clock the measurement. They are deployed together or not at all --
# a flood measurement of one arm alone has no baseline:
#   build_and_deploy.sh arm-s arm-t
# x001 / x002 are the BACKLOG #70B A/B: the same two debs on both,
# differing only in gfx.toml's emit_thread. Deployed together or not at
# all -- a split measurement with no same-binary control has no
# baseline (arm-w is an older build, so it is not one):
#   build_and_deploy.sh x001 x002
# x003 / x004 are the SAME A/B under SESSION_KIND=textflood -- the #61b
# payload whose X-side cost is a memcpy. Deployed together:
#   build_and_deploy.sh x003 x004
# x005 / x006 are x003 / x004 on the #61e INSTRUMENTED build (the wait
# and residency brackets). Deployed together:
#   build_and_deploy.sh x005 x006
# x006 / x007 are the INSTRUMENT's own control: the same image and the
# same gfx.toml body, trace ARMED vs DISARMED. Deployed together, or
# the traced number has nothing to be compared against:
#   build_and_deploy.sh x006 x007
# x013 is a SOLO arm and deliberately has no twin: it re-measures the
# eager-ack frame-interval DECOMPOSITION (a within-run accounting, not a
# ratio) on the ring-traced build, after #61h voided every timing taken
# with the trace on log.c. A control arm would buy nothing an attribution
# that closes against its own period does not already have:
#   build_and_deploy.sh x013
# x014 is x013 with BACKLOG #75's rewrite optimisation and NOTHING else --
# same xorgxrdp, same gfx.toml body, same payload. It is the one arm the
# owner approved for #75, and its comparison target is x013's recorded
# 25.5 ms:
#   build_and_deploy.sh x014
# x018 / x019 are the BACKLOG #80 step 4 / #81 WAN PAIR: the same image,
# the same xorgxrdp and the same gfx.toml body, measured at two round
# trip times (loopback baseline and 40 ms) applied from the host with
# netem_rtt.sh. Deployed together or not at all -- a WAN measurement
# with no same-build LAN leg has no baseline:
#   build_and_deploy.sh x018 x019
# x020 / x021 are the MERGED EAGER-ACK A/B (owner-approved 2026-08-06):
# the same image as x018/x019, the same xorgxrdp, the same payload, and
# gfx.toml bodies differing by ONE line -- eager_slot_ack, false on x020
# (the shipped gated ack) and true on x021 (the credit frontier). Both
# arms run a client window of 2, reached by DIFFERENT mechanisms: x020
# through XRDP_GFX_FRAMES_IN_FLIGHT=2 in its manifest, x021 through
# gfx.toml wire_window = 2, the value the code ships as its default and
# which no arm in this tree has ever run (every other wire_window here
# is 1). Holding the number equal is what makes the A/B about the ack
# mechanism instead of window size. Neither arm builds anything -- both
# are config-only on a cached image. Deployed together or not at all:
#   build_and_deploy.sh x020 x021
# Letters ran out at arm-w; later arms are numbered x001, x002, ...
ARMS="${*:-x031 x032 x033 x034 x035}"

# arm -> xrdp-dev commit tag. xorgxrdp defaults to the Mac-good ee1ec01
# but MUST be paired per-arm when the xrdp build speaks a newer xup
# contract: with a mismatched pair sesman rejects logins with a contract
# version complaint (caught live 2026-07-27).
XORGXRDP_DEB="xorgxrdp-dev_1%3a0.10.80+gitee1ec01eed50_amd64.deb"
declare -A ARM_XORG_DEB=(
    # BACKLOG #104: the whole matrix is ONE xrdp build on ONE
    # xorgxrdp. Every arm names it explicitly rather than taking a
    # default, so a future arm cannot silently pair differently.
    [x031]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x032]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x033]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x034]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x035]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x036]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033+dirty_amd64.deb"
    [x037]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033+dirty_amd64.deb"
)
declare -A ARM_TAG=(
    # ONE TAG FOR THE WHOLE MATRIX. If these five ever differ, the
    # matrix has stopped being a matrix: the arms would no longer
    # be one variable apart. That is exactly what happened to the
    # fleet this replaced -- 17 pods across five images.
    [x031]=3ca17beaa84d.xx10fa3aa-tf.p2fde5531
    [x032]=3ca17beaa84d.xx10fa3aa-tf.p2fde5531
    [x033]=3ca17beaa84d.xx10fa3aa-tf.p2fde5531
    [x034]=3ca17beaa84d.xx10fa3aa-tf.p2fde5531
    [x035]=3ca17beaa84d.xx10fa3aa-tf.p2fde5531
    [x036]=edfd0e5c80a6d.xx10fa3aa23033d-tf.r2.p11d2c5a7
    [x037]=edfd0e5c80a6d.xx10fa3aa23033d-tf.r2.p11d2c5a7
)
declare -A TAG_DEB=(
    # BACKLOG #104: xrdp at 3ca17bea -- the encoder-input-pipe
    # requirement (#103) and the sparse-aux cadence (#92) both in.
    [3ca17beaa84d.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260810000106.3ca17beaa84d_amd64.deb"
    [edfd0e5c80a6d.xx10fa3aa23033d-tf]="xrdp-dev_0.10.80+git20260816151351.edfd0e5c80a6+dirty_amd64.deb"
    [edfd0e5c80a6d.xx10fa3aa23033d-tf.r2]="xrdp-dev_0.10.80+git20260816151351.edfd0e5c80a6+dirty2_amd64.deb"
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

# --- payload identity: hash the benchmark payload into the tag --------
# (2026-08-06) A tag used to name TWO things: the xrdp build (<commit>)
# and the xorgxrdp build (.xx<hash>). It did NOT name the benchmark
# payload, even though the payload is compiled INTO the image (the
# textflood builder stage in the Containerfile) and is what generates
# every frame the measurement is about. So a change to
# PR-demo/textflood/*.c had two possible outcomes, both wrong: the tag
# already existed, the build was skipped, and the arm quietly ran the
# OLD payload; or FORCE_BUILD=1 overwrote the image in place, and every
# result previously measured on that tag now pointed at bytes that no
# longer existed. The arm certificate cannot catch either one -- its key
# is image tag plus config hash, and the image tag did not move.
#
# THE SCHEME. New tags carry a third component:
#
#     <xrdp commit>.xx<xorgxrdp hash>[-tf][-xfce].p<payload hash>
#
# where <payload hash> is the first 8 hex digits of a SHA-256 over every
# PR-demo/textflood/*.c file, in LC_ALL=C name order, each preceded by
# its basename (so a rename moves the hash too). Change the payload and
# the tag necessarily changes; the old image keeps its own name and the
# results measured on it stay meaningful.
#
# EXISTING TAGS ARE LEFT ALONE. Every tag registered above predates this
# scheme and has no .p component. Those are grandfathered: they deploy
# from cache exactly as before, are never renamed, and are never
# rebuilt -- rebuilding one today would compile whatever payload is
# checked out into an image whose name promises the payload of the day
# it was first built. If a legacy image is genuinely missing and must be
# rebuilt, that is a decision with consequences for every past capture
# that cites it, so it is refused here and re-enabled deliberately with
# ALLOW_LEGACY_PAYLOAD_REBUILD=1.
#
# CONSERVATIVE ON PURPOSE. The hash covers every .c in the payload
# directory plus the three onscreen probes, which are also things a
# human sees and therefore part of what an arm presents. The
# error direction is "a new tag when the image would have been
# identical", never "the same tag for a different image".
PAYLOAD_SRC=()
while IFS= read -r f; do
    # an unmatched glob comes back as itself; drop it rather than hash it
    [ -f "$f" ] && PAYLOAD_SRC+=("$f")
done < <({ printf '%s\n' "$D"/../textflood/*.c
           # the onscreen probes are part of what a session presents, so a
           # change to one must mint a new tag exactly as a payload change does
           printf '%s\n' "$D/chroma_probe.py"
           printf '%s\n' "$D/../smoke_gate/colorkey_x11.c"
           printf '%s\n' "$D/../win2022_ground_truth/chroma_strip_anim.c"
         } | LC_ALL=C sort)
[ "${#PAYLOAD_SRC[@]}" -gt 0 ] || {
    echo "ABORT: no payload sources in PR-demo/textflood"; exit 1; }
PAYLOAD_HASH=$(
    for f in "${PAYLOAD_SRC[@]}"; do
        printf '%s\n' "${f##*/}"
        cat "$f"
    done | sha256sum | cut -c1-8)
echo "payload identity: .p$PAYLOAD_HASH over" \
     "$(printf '%s ' "${PAYLOAD_SRC[@]##*/}")"
echo "  (a NEW arm's tag should end .p$PAYLOAD_HASH)"

# --- images: one per distinct xrdp-dev commit + payload ---
BUILD="$D/.build"
rm -rf "$BUILD" && mkdir -p "$BUILD"
cp "$D/entrypoint.sh" "$D/startwm.sh" "$D/banner.sh" "$BUILD/"
# BACKLOG #61b: textflood is compiled in a builder stage of the image
cp "$D/../textflood/textflood.c" "$BUILD/"
# The onscreen probes ship IN THE IMAGE. Copying them into a running pod
# by hand (2026-08-08) meant they died with it and an arm could not be
# handed over reproducibly; chroma-probe's python3-tk was missing for the
# same reason. Every one of these must exist -- a silently absent probe
# is an arm that cannot be judged.
for f in "$D/chroma_probe.py" \
         "$D/../smoke_gate/colorkey_x11.c" \
         "$D/../win2022_ground_truth/chroma_strip_anim.c"; do
    [ -f "$f" ] || { echo "ABORT: onscreen probe missing: $f"; exit 1; }
    cp "$f" "$BUILD/"
done
for arm in $ARMS; do
    tag=${ARM_TAG[$arm]}
    # split the payload component off the tag, if it has one
    case $tag in
        *.p[0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f][0-9a-f])
            tag_payload=${tag##*.p}
            core_tag=${tag%.p*}
            ;;
        *)
            tag_payload=""
            core_tag=$tag
            ;;
    esac
    # The grep is anchored: without the $ a legacy tag would also match
    # the payload-suffixed image built from it, and the build would be
    # skipped because a DIFFERENT image happened to share a prefix.
    if [ "${FORCE_BUILD:-0}" != "1" ] \
            && k3s ctr images ls -q | grep -q "xrdp-bisect:$tag\$"; then
        if [ -n "$tag_payload" ] && [ "$tag_payload" != "$PAYLOAD_HASH" ]; then
            echo "note: $arm deploys cached image payload .p$tag_payload" \
                 "while the tree holds .p$PAYLOAD_HASH — right when" \
                 "reproducing an earlier result, wrong otherwise"
        fi
        continue
    fi
    # From here the image WILL be built, so its name has to be honest
    # about the payload going into it.
    if [ -n "$tag_payload" ]; then
        if [ "$tag_payload" != "$PAYLOAD_HASH" ]; then
            echo "ABORT: $arm's tag names payload .p$tag_payload but" \
                 "PR-demo/textflood/*.c hashes to .p$PAYLOAD_HASH." >&2
            echo "Building would put today's payload under a name that" >&2
            echo "promises a different one. Either check out the payload" >&2
            echo "that tag was built from, or register the arm with a" >&2
            echo "tag ending .p$PAYLOAD_HASH." >&2
            exit 1
        fi
    elif [ "${ALLOW_LEGACY_PAYLOAD_REBUILD:-0}" != "1" ]; then
        echo "ABORT: $arm's tag '$tag' predates the payload component" >&2
        echo "and its image is not in the store, so this run would" >&2
        echo "rebuild it from whatever payload is checked out now —" >&2
        echo "silently replacing the bytes earlier captures cite." >&2
        echo "Register the arm as '$tag.p$PAYLOAD_HASH' for a fresh" >&2
        echo "image, or set ALLOW_LEGACY_PAYLOAD_REBUILD=1 if you have" >&2
        echo "restored the payload that tag was built from." >&2
        exit 1
    else
        echo "WARNING: rebuilding legacy tag '$tag' with payload" \
             ".p$PAYLOAD_HASH (ALLOW_LEGACY_PAYLOAD_REBUILD=1). The" \
             "tag does not record which payload is inside it."
    fi
    base_tag=${core_tag%-xfce}
    xfce=0; [ "$base_tag" != "$core_tag" ] && xfce=1
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

# --- certify each arm's BYTES, once, here (owner directive, 2026-08-01) ---
# The wire audit and black-frame decode are a property of the deployed
# pair -- this image's ffmpeg, this host's VAAPI driver, this arm's
# encoder_args -- not of a measurement run. They used to run inside every
# e_gate_run.sh, where on 2026-08-01 they cost 10 min 07 s against 64 s of
# measurement on a 7.75 GB dump. THIS is their one place: 3 s of payload,
# once, at deploy. e_gate_run.sh now refuses to measure an arm that has no
# current certificate, so moving them here does not quietly delete them.
CERT_FAIL=0
for arm in $ARMS; do
    "$D/arm_certify.sh" "$arm" || CERT_FAIL=1
done
if [ "$CERT_FAIL" != 0 ]; then
    echo >&2
    echo "ABORT: at least one arm failed certification — it encodes" >&2
    echo "non-conforming or undecodable bytes. Do not measure it." >&2
    exit 1
fi
echo
echo "matrix up: arm-a 127.0.0.1:40000  arm-b :40001  arm-c :40002" \
     "arm-d :40003  arm-e :40004"
