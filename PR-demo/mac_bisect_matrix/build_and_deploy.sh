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
    # x015 (#76): the SAME xorgxrdp and the SAME IMAGE as x014. This arm
    # builds nothing: it is x014 with XRDP_GFX_FRAMES_IN_FLIGHT=1 set in
    # k8s/x015.yaml, so the tag below is x014's and the image cache hits.
    [x015]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x014 (#75): the SAME xorgxrdp as x013 -- the only thing that differs
    # between the two arms is the xrdp build
    [x014]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x017 (#78): the SAME xorgxrdp as x014/x015 -- the pump split is
    # xrdp-internal and the producer must stay identical
    [x017]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x018/x019 (#80 step 4 / #81): the credit frontier. The SAME
    # xorgxrdp as x014/x015/x017 -- #80 changed only the arithmetic that
    # produces the credit, not the wire's credit semantics, so the
    # producer side is byte-identical to the arms this pair is read
    # against. If this deb ever differs from x017's, the head-to-head
    # against x017 is void.
    [x018]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x019]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x020/x021 (the merged eager-ack A/B): the SAME xorgxrdp as
    # x017/x018/x019 and the SAME image on both halves. The producer is
    # not part of this experiment -- the treatment is one gfx.toml line
    # in xrdp -- so any difference here would void the pair.
    [x020]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x021]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x023/x024 (owner-directed 2026-08-07): the non-regression pair.
    # x023 = credit frontier at wire_window 1, the LEGACY-EQUIVALENT
    # window (same credit ceiling and same client+3 bound as the
    # shipped frames_in_flight=2 path); x024 = x023 with emit_thread
    # off. So x020 vs x023 isolates the ack mechanism and x023 vs x024
    # isolates the emit thread.
    # x025 (owner-directed 2026-08-07): the LEGACY path with its window
    # widened to 3 via XRDP_GFX_FRAMES_IN_FLIGHT. Answers the reviewer
    # objection "why not just raise the old knob?" -- its gfx.toml body
    # is byte-identical to x020, so the env var is the only difference.
    # x026: the LEGACY ack path carrying the strip payload, so the
    # multi-monitor comparison against x022 (frontier + strip) is not
    # producer-limited the way the textflood pair was (margin 0.5x).
    # x027: THE INTERACTIVE ARM -- XFCE desktop, shipped defaults, built
    # from HEAD so what the owner looks at is what actually ships.
    # x028: frontier at wire_window 4 -- the leg that decides whether the
    # session-wide window is what limits a monitor at M = 2 (owner,
    # 2026-08-07). Same image and payload as x022; one config line apart.
    # x029 (#91): the three attribution fields. Shipped defaults, two
    # monitors, strip payload -- the arm the two approved legs run on.
    # x030 (#92): the SAME xorgxrdp as every textflood arm from x014 on.
    # The sparse-aux cadence is entirely inside xrdp -- the producer still
    # captures and packs both views on every frame; what changes is
    # whether xrdp feeds the aux one to an encoder. A different producer
    # here would void the comparison against the archived numbers.
    [x030]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x029]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x028]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x027]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x026]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x025]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x023]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    [x024]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
    # x022 (BACKLOG #83): same server as x021, different PAYLOAD.
    [x022]="xorgxrdp-dev_1%3a0.10.80+git20260731212221.10fa3aa23033_amd64.deb"
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
    # x014: x013 plus BACKLOG #75 -- the LTR rewrite copies the child's
    # already-escaped payload instead of unescaping and re-escaping the
    # whole picture around a 30-byte header edit. gfx.toml body is
    # x013's byte for byte, so the arms differ only in the xrdp deb.
    [x014]=73e4cb76d483.xx10fa3aa-tf
    # x015: BACKLOG #76 -- the SAME TAG as x014 on purpose. This arm ships
    # no new code; it is x014 with XRDP_GFX_FRAMES_IN_FLIGHT=1 in its
    # manifest, so the image cache hits and nothing is built. Deploying an
    # env-only arm this way is the cheap shape: no deb, no podman build,
    # no k3s import.
    [x015]=73e4cb76d483.xx10fa3aa-tf
    # x017 (#78): x015's config on the pump-split instrumented xrdp
    # (feedend/outfirst on the existing perf ring, nothing else)
    [x017]=661ff5fc64fa.xx10fa3aa-tf
    # x018/x019 (#80 step 4 / #81): the SAME TAG on both. The pair is a
    # WAN comparison, so a build difference between its two halves would
    # be the one thing that ruins it; the RTT is applied from the host by
    # netem_rtt.sh and lives in neither image nor manifest.
    [x018]=1d5bc0960db8.xx10fa3aa-tf
    [x019]=1d5bc0960db8.xx10fa3aa-tf
    # x020/x021 (the merged eager-ack A/B): x018/x019's TAG on both, on
    # purpose. These arms ship no new code -- they are gfx.toml and one
    # env var -- so the image cache hits, nothing is built, and the two
    # halves of the A/B are the same bytes by construction. This tag
    # predates the payload-identity suffix below; it is grandfathered
    # and must not be rebuilt (see PAYLOAD_HASH).
    [x020]=1d5bc0960db8.xx10fa3aa-tf
    [x021]=1d5bc0960db8.xx10fa3aa-tf
    [x023]=1d5bc0960db8.xx10fa3aa-tf
    [x025]=1d5bc0960db8.xx10fa3aa-tf
    [x026]=1d5bc0960db8.xx10fa3aa-tf.pc0097388
    [x028]=1d5bc0960db8.xx10fa3aa-tf.pc0097388
    [x029]=1fed64c16a89.xx10fa3aa-tf.pc0097388
    # x030 (#92): a NEW image -- this is the first arm carrying the
    # sparse-aux implementation. The payload suffix moves too, because
    # the onscreen probes joined the payload set on 2026-08-08.
    [x030]=7b550f6ae87f.xx10fa3aa-tf.p2fde5531
    [x027]=821218e54c24.xx10fa3aa-xfce.pc0097388
    [x024]=1d5bc0960db8.xx10fa3aa-tf
    # x022 carries the STRIP-RENDER payload, so its tag must name the
    # payload: same server build, different producer. Comparing it with
    # x021 is a comparison of two payloads on one server, which is
    # exactly what BACKLOG #83 asks and what the .p suffix now records.
    [x022]=1d5bc0960db8.xx10fa3aa-tf.pc0097388
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
    # x014 (#75): the rewrite optimisation, output byte-identical to the
    # x013 build (CI golden vectors + an FNV-1a digest over 120 whole 4K
    # pictures)
    [73e4cb76d483.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260801232618.73e4cb76d483_amd64.deb"
    # x017 (#78): the pump-split instrument -- feedend + outfirst per
    # child per cycle on the existing ring; behaviourally a no-op with
    # the sink disarmed
    [661ff5fc64fa.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260802030326.661ff5fc64fa_amd64.deb"
    # x018/x019 (#80): the credit frontier. Supersedes x017's build and
    # keeps its instrumentation -- feedend/outfirst are still there, plus
    # ackslot/ackregion carrying the client frontier and C, and egress
    # carrying the transport's queued KiB from trans::wait_bytes.
    [1d5bc0960db8.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260803024109.1d5bc0960db8_amd64.deb"
    # x027 (the interactive arm): built from HEAD on 2026-08-07, so it
    # carries the SHIPPED defaults -- the credit frontier on by default
    # at wire_window 2, and no emit thread in the binary at all.
    [1fed64c16a89.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260807211642.1fed64c16a89_amd64.deb"
    [821218e54c24.xx10fa3aa]="xrdp-dev_0.10.80+git20260807153324.821218e54c24_amd64.deb"
    # x030 (#92): the sparse-aux implementation -- the aux view skipped
    # in submit/pump/collect, the luma-only LC=1 framing, and the aux
    # view's own intra refresh interval. Built from HEAD on 2026-08-08.
    [7b550f6ae87f.xx10fa3aa-tf]="xrdp-dev_0.10.80+git20260808210040.7b550f6ae87f_amd64.deb"
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
