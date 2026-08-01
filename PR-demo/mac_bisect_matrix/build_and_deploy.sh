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
# Letters ran out at arm-w; later arms are numbered x001, x002, ...
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
    # arm-s/arm-t: the BACKLOG #52 E5-2 pair — SAME xorgxrdp as arm-r on
    # both, so the producer side is identical and the A/B isolates the
    # xrdp-side steps 5+7
    [arm-s]="xorgxrdp-dev_1%3a0.10.80+git20260729225933.d77d05463e52_amd64.deb"
    [arm-t]="xorgxrdp-dev_1%3a0.10.80+git20260729225933.d77d05463e52_amd64.deb"
    # arm-r: the BACKLOG #45 arm — step 6's per-monitor capture budget,
    # coverage intersect and per-monitor slot (xorgxrdp d77d05463e52),
    # paired with the xrdp deb carrying steps 0-7
    [arm-r]="xorgxrdp-dev_1%3a0.10.80+git20260729225933.d77d05463e52_amd64.deb"
    # arm-u/arm-v: the BACKLOG #70 A/B. SAME xrdp deb and SAME xorgxrdp
    # deb on both -- the only difference between the arms is one
    # gfx.toml line (eager_slot_ack), so a build difference cannot
    # confound the comparison. The xorgxrdp side carries the SLOT_ONLY
    # ack and the +1 held-region entry; the xup contract moved to
    # 20260731, so this xorgxrdp pairs ONLY with this xrdp.
    [arm-u]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [arm-v]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # arm-w (BACKLOG #70B): arm-v's config on an instrumented xrdp. The
    # xorgxrdp side is the SAME deb as arm-u/arm-v -- the xup contract
    # did not move, so the attribution is about arm-v's pipeline.
    [arm-w]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x001/x002 (BACKLOG #70B): the emit-split A/B. SAME xrdp deb and
    # SAME xorgxrdp deb on both -- the arms differ by one gfx.toml line
    # (emit_thread). The xrdp change is encoder-internal and does not
    # move the xup contract, so this is still arm-u/v/w's xorgxrdp.
    [x001]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x002]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x003/x004 (BACKLOG #61b): the x001/x002 A/B re-run under textflood.
    # Same debs on all four arms; the pairs differ only in SESSION_KIND.
    [x003]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x004]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x005/x006 (BACKLOG #61e): x003/x004's configs on the INSTRUMENTED
    # xrdp. The xorgxrdp side is untouched -- the new brackets are all
    # inside xrdp, and the producer must stay identical or the `wait`
    # bracket would be measuring a different capture path.
    [x005]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x006]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x007 (#61e CONTROL): the UNTRACED twin of x006 -- same xrdp deb,
    # same xorgxrdp deb, same gfx.toml body; XRDP_PERF_TRACE unset.
    [x007]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x013 (#61e redo): the SAME xorgxrdp as x005/x006/x007, so the
    # producer side is identical to the arms whose numbers this replaces
    [x013]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
)
declare -A ARM_TAG=(
    [arm-e]=c693eeab5ec2
    [arm-m]=39bb08a48377.xx5b9650c-xfce
    [arm-n]=34795577580b.xx5b9650c-xfce
    [arm-p]=6894d7de2202.xx5b9650c-xfce
    [arm-q]=34795577580b.xx957fa79
    [arm-r]=f7acb5979788.xxd77d054
    [arm-s]=52b8798839ad.xxd77d054
    [arm-t]=5dae11f63adb.xxd77d054
    [arm-u]=348a16dde3f3.xx10fa3aa
    [arm-v]=348a16dde3f3.xx10fa3aa
    [arm-w]=e6e1f6f5641e.xx10fa3aa
    [x001]=4bbf11814323.xx10fa3aa
    [x002]=4bbf11814323.xx10fa3aa
    # -tf = the SAME xrdp deb, image rebuilt with the textflood binary.
    # A distinct tag so x001/x002 keep the exact image they were measured on.
    [x003]=4bbf11814323.xx10fa3aa-tf
    [x004]=4bbf11814323.xx10fa3aa-tf
    # #61e: a NEW tag, never a rebuild of the -tf image -- x003/x004
    # keep the exact bytes their 1.12x was measured on.
    # #61e v2: the ring-buffer tracer (FR-TRACE-1). The v1 tag
    # 05847031a303 is the one whose SHARED FILE* measured 135 ms.
    [x005]=2781220ae747.xx10fa3aa-tf
    [x006]=2781220ae747.xx10fa3aa-tf
    # x007: the UNTRACED twin of x006 -- the SAME image, differing only
    # in that its manifest omits XRDP_PERF_TRACE. It is the only control
    # that can show whether observing the pipeline changes it.
    [x007]=2781220ae747.xx10fa3aa-tf
    # x013: the ring-traced build (#61h). Shipped source is identical to
    # the 66a60311 image already on this box, but that tag names a commit
    # the history rewrite removed, so it is rebuilt from a hash that still
    # exists rather than deployed from a package nothing can trace.
    [x013]=82babb9fe4ba.xx10fa3aa-tf
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
    # arm-r: BACKLOG #45 steps 0-7 (xrdp) paired with step 6 (xorgxrdp
    # d77d05463e52). This is the pair every #45 gate is measured on.
    [f7acb5979788.xxd77d054]="xrdp-dev_0.10.80+git20260729233553.f7acb5979788_amd64.deb"
    # arm-s (#52 E5-2): #45 steps 0-7 PLUS the step-0 log clock fix — the
    # trace timestamps this benchmark is read from have to be right
    [52b8798839ad.xxd77d054]="xrdp-dev_0.10.80+git20260730013346.52b8798839ad_amd64.deb"
    # arm-t (#52 E5-2 baseline): #45 steps 0-4 only (a0d9e773) + the same
    # log clock fix, from branch bench/e52-arm-t-baseline
    [5dae11f63adb.xxd77d054]="xrdp-dev_0.10.80+git20260730013437.5dae11f63adb_amd64.deb"
    # arm-u/arm-v (BACKLOG #70): the eager slot-release ack, off by
    # default in the binary and turned on per arm by gfx.toml
    [348a16dde3f3.xx10fa3aa]="xrdp-dev_0.10.80+git20260731212249.348a16dde3f3_amd64.deb"
    # arm-w (BACKLOG #70B): the same encoder, plus common/perf_trace and
    # the worker-stage brackets
    [e6e1f6f5641e.xx10fa3aa]="xrdp-dev_0.10.80+git20260801010944.e6e1f6f5641e_amd64.deb"
    # x001/x002 (BACKLOG #70B): the assembler thread, plus the
    # prerequisites that make its join point sound (emit no longer
    # touches the ffmpeg handle array; arm state published after the
    # join). Default off in the binary; armed per arm by gfx.toml.
    [4bbf11814323.xx10fa3aa]="xrdp-dev_0.10.80+git20260801021842.4bbf11814323_amd64.deb"
    [4bbf11814323.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260801021842.4bbf11814323_amd64.deb"
    # x005/x006 (BACKLOG #61e): the same encoder plus five perf-trace
    # brackets -- book, rel, wait, enq, take. Behaviourally a no-op with
    # the sink disarmed, which gate 4 (x005 vs x003, x006 vs x004) is
    # there to confirm rather than assume.
    [05847031a303.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260801141607.05847031a303_amd64.deb"
    [2781220ae747.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260801150407.2781220ae747_amd64.deb"
    # x013 (#61e redo): per-frame records on common/perf_trace's ring
    # instead of log.c -- the #61h fix, plus the six-field payload
    [82babb9fe4ba.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260801213501.82babb9fe4ba_amd64.deb"
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
# BACKLOG #61b: textflood is compiled in a builder stage of the image
cp "$D/../textflood/textflood.c" "$BUILD/"
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
