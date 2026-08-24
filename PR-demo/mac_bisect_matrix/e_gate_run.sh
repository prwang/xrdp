#!/bin/bash
# e_gate_run.sh — run the BACKLOG #45 acceptance gates E2/E3/E4/E5 in one
# offscreen dual-monitor session and report each one separately.
#
# WHAT IT MEASURES, and with which instrument (the distinction #45 E5 is
# built on):
#
#   E5 oracle frame interval  MODE=oracle. The oracle client acks BEFORE
#                             decode and present, so the send-to-send
#                             interval seen at the server is the SERVER
#                             ceiling with the client contributing ~0.
#                             This is the gate metric. The baseline is
#                             E5_BASE_MS (default 51.1 ms, the 2026-07-29
#                             pre-steps-5..7 run at this geometry);
#                             >= 2.0x of it passes. Set E5_BASE_MS=none
#                             for a characterization that has no
#                             apples-to-apples historical baseline; the
#                             distribution is still reported, but no E5
#                             ratio or gate verdict is fabricated.
#
#                             E5_BASE_MS EXISTS BECAUSE A BASELINE IS ONLY
#                             COMPARABLE UNDER THE SAME PAYLOAD. The 51.1
#                             default was measured with SESSION_KIND=code,
#                             a `sleep 0.1` metronome: the reassessment on
#                             2026-07-30 showed both it and the 52.5 ms
#                             measurement were readings of that 10 Hz
#                             clock, not of the server (BACKLOG #45 GATE
#                             RESULTS, #52). Under SESSION_KIND=codeflood
#                             run the BASELINE ARM first and pass its mean
#                             in as E5_BASE_MS; never compare a flood run
#                             against the cadence default.
#   end-to-end rate           MODE=render. The distro xfreerdp3 decodes
#                             and presents. Recorded beside E5 every
#                             time as CONTEXT, never as the gate: the
#                             session is client-bound by 3.29x, so this
#                             number cannot move until the client side
#                             is addressed (out of #45 scope).
#   E2 wire properties        MOVED OUT, 2026-08-01 (owner directive).
#                             These are a property of the deployed ARM --
#                             this image's ffmpeg, this host's VAAPI
#                             driver, this arm's encoder_args -- not of a
#                             measurement run, and the rewriter logic
#                             behind them is already pinned byte-exactly
#                             by CI (tests/xrdp/test_avc444_ltr.c, 26
#                             golden-vector assertions). They now run
#                             ONCE per deploy, on 3 s of payload, in
#                             arm_certify.sh, and NOWHERE ELSE. Measured
#                             cost of the old arrangement, 2026-08-01:
#                             10 min 07 s of dump-walking against 64 s of
#                             measurement, on a 7.75 GB dump, re-proving
#                             properties that had not changed. This
#                             script now writes no dump at all (E_DUMP=1
#                             overrides, for bitstream investigations
#                             only) and refuses to run against an arm
#                             with no current certificate.
#   E2 (historical)           the oracle dump was run through
#                             tools/avc444_ltr_wire_audit.py --assert
#                             --intra-refresh N (no mid-stream IDR, cuts
#                             only on scheduled ordinals, paired across
#                             views, own-slot refs, one contiguous
#                             frame_num chain, depth <= N) and through
#                             oracle_black_frame_check.py (every picture
#                             decodes, zero black frames).
#   E2 server log             zero rewrite failures, zero "unsupported",
#                             zero pair aborts, zero budget assertions.
#   E3 geometry               2560x1440 + 3840x2400 by default, offscreen
#                             on the host's dummy X server.
#   E4 set size               the per-cycle poll-set size the worker
#                             logged (needs XRDP_GFX_TRACE=1 in the pod
#                             env, which the #45 arm sets).
#
# The oracle client is a TIMING AND SYNTAX instrument only: it renders
# nothing and proves no fidelity. E1 (smoke gate) and E7 (drag sweep)
# use the real rendering client and are separate scripts.
#
# Client side stays on the HOST (CLAUDE.md: the container fleet is the
# RDP SERVER side only). Nothing is installed into the pod.
#
# Credential: probe444's password lives ONLY in root-owned
# /root/.oracle_cred, is read into a variable and handed to the client
# through the environment, never as an argument and never printed.
#
# THE ARM MUST BE SESSION_KIND=code (cadence) or =codeflood (throughput,
# BACKLOG #52 — the frame-interval gate's payload). Measured 2026-07-29 while validating
# this harness: two control runs against arm-n (SESSION_KIND=xfce, a
# static desktop) produced 28 pictures in 25 s and 10 in 30 s -- the
# session simply had nothing to damage, and E2's >= 1000 pairs would be
# unreachable no matter how long the run. The scrolling code corpus is
# what generates continuous damage on both monitors. arm-r sets it.
#
# Usage: e_gate_run.sh [seconds]     (default 120; E2 wants >= 1000 pairs)
#
#   fleet pod (default):
#     E_ARM=arm-s E_PORT=40018 E_MODE=oracle E_REFRESH=240 e_gate_run.sh 180
#
#   a real box over an ssh port-forward (the T4) — protocol, artifact list
#   and single-instance upgrade/downgrade discipline in
#   PR-demo/t4_profile/E5-2_T4_PROTOCOL.md:
#     ssh -f -N -i /root/.ssh/tmp_access_T4 -L 33389:127.0.0.1:3389 <t4>
#     E_TARGET=ssh E_PORT=33389 E_USER=ubuntu \
#       E_CRED_FILE=/root/.t4_rdp_cred E_MODE=oracle e_gate_run.sh 180
#
# Env: E_TARGET pod|ssh · E_SSH_HOST (else /root/.t4_host) · E_SSH_KEY ·
#   E_ARM/E_NS (pod only) · E_PORT · E_USER · E_CRED_FILE · E_MODE ·
#   E5_BASE_MS (milliseconds, or "none")
#   oracle|render · E_REFRESH · E5_BASE_MS · E_OUT · E_COLD 0|1 ·
#   E_MODE0/E_MODE1/E_POS1/E_MODELINE0/E_MODELINE1 (client geometry) ·
#   E_FREEZE_AT (BACKLOG #80 freeze leg, default OFF — see below) ·
#   E_STAMPS (benchmark payload's own per-frame stamps file, server side)
set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-120}
ARM=${E_ARM:-arm-r}
# BACKLOG #104: DERIVE the port from the arm's own manifest, exactly as
# arm_certify.sh does. It used to default to a hardcoded 40017, which
# was one arm's port in a fleet that has since been replaced twice --
# and on 2026-08-10 that cost a whole 20 s run: E_ARM=x033 was passed,
# E_PORT was not, the client dialled 40017 where nothing listens, and
# the gate reported "0 GFX_TRACE send records" as though the ARM were
# silent. A default that names one arm is a trap for every other arm.
PORT=${E_PORT:-$(sed -n 's/^ *hostPort: *//p' "$D/k8s/$ARM.yaml" 2>/dev/null | head -1)}
PORT=${PORT:-40017}
NS=${E_NS:-bisect-matrix}
# TARGET: pod (a bisect-fleet arm, the default) or ssh (a real box reached
# over an ssh port-forward — the T4). The client side is identical either
# way and always runs HERE (CLAUDE.md: nothing is installed client-side on
# the T4); only server-side identity, log collection and the cold-session
# reset differ, and both go through srv()/srv_cat() below.
TARGET=${E_TARGET:-pod}
SSH_HOST=${E_SSH_HOST:-$(cat /root/.t4_host 2>/dev/null || true)}
SSH_KEY=${E_SSH_KEY:-/root/.ssh/tmp_access_T4}
if [ "$TARGET" = ssh ]; then
    SU=${E_USER:-ubuntu}
    CRED=${E_CRED_FILE:-/root/.ubuntu_cred}
else
    SU=${E_USER:-probe444}
    CRED=${E_CRED_FILE:-/root/.oracle_cred}
fi
CLI=${E_DISPLAY:-:94}
FRDP=${E_XFREERDP:-xfreerdp3}
MODE=${E_MODE:-oracle}
REFRESH=${E_REFRESH:-240}
# mean ms per send the E5 ratio is taken against. Default = the 2026-07-29
# SESSION_KIND=code (10 Hz cadence) baseline. A flood run MUST pass its own
# baseline arm's mean instead — see the header.
E5_BASE_MS=${E5_BASE_MS:-51.1}
ORACLE_BIN=${E_ORACLE_BIN:-/opt/freerdp-vaapi/bin/xfreerdp}
MMCONF=${E_MULTIMON_DIR:-$D/../multimon_offline}
# How many monitors the client presents. 2 is the E3/E5-2 target layout.
# E_MONITORS=1 is the BACKLOG #64 control: same payload, same build, same
# everything, one monitor — it separates "the multimon batch serialises"
# from "the pipeline never overlaps at all".
NMON=${E_MONITORS:-2}
# Session geometry for the NMON=1 case, WxH. Defaults to E_MODE0 with any
# trailing RandR mode suffix (the "R" of 3840x2160R, "_60" of 2560x1440_60)
# stripped, so the session matches the one monitor the client presents.
E_SIZE=${E_SIZE:-$(echo "${E_MODE0:-3840x2160R}" \
    | sed 's/^\([0-9]\{1,\}x[0-9]\{1,\}\).*$/\1/')}
XCONF=${E_XORG_CONF:-$MMCONF/xorg-dummy-2mon-4k.conf}
# --- BACKLOG #80 freeze leg (default OFF) ---------------------------------
# E_FREEZE_AT=<seconds into the measurement leg> stops the client process
# group with SIGSTOP that far into the run and resumes it with SIGCONT at
# teardown. Unset -- the default -- is the ordinary leg: no signal is
# sent, no trap is installed, and the script takes exactly the path it
# took before this existed.
#
# WHAT THIS FREEZES, AND WHAT IT DOES NOT. Read this before quoting any
# number from a freeze leg. SIGSTOP stops the whole client PROCESS, so it
# stops the client READING its TCP socket as well as acknowledging
# frames. The receive buffer fills, the client advertises a zero TCP
# window, and the server's writes then block in the kernel. That is a
# HARSHER condition for the server's queue than an ack-only freeze, not a
# weaker one: an ack-only freeze would keep draining the socket and would
# exercise only the EGFX frame-acknowledge window, whereas this exercises
# the acknowledge window AND transport backpressure at once, and from the
# server side afterwards the two cannot be told apart. A question that
# needs the acknowledge window in isolation needs a client that keeps
# reading and withholds only RDPGFX_FRAME_ACKNOWLEDGE; this harness
# cannot produce that condition and does not claim to.
FREEZE_AT=${E_FREEZE_AT:-}
# Set as soon as the client is launched, so the freeze trap knows whether
# there is a group to resume. Declared here for `set -u`.
CLIENT_PGID=
HOLD_PID=
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${E_OUT:-$D/captures/e_gate_${MODE}_$STAMP}
mkdir -p "$OUT"

fail() { echo "ABORT: $*" >&2; exit 1; }

# Resume-and-kill for the freeze leg. SIGCONT FIRST, then SIGKILL: a
# process group left in T (stopped) state is invisible to the "did the
# client survive" check below in the way that matters -- it is neither
# doing anything nor gone, it still holds the RDP socket open, and it
# outlives this script. SIGKILL does remove a stopped process on its
# own, but sending SIGCONT first is what lets the group run its own exit
# and be reaped normally. Both kills are no-ops (ESRCH, swallowed) once
# the group is already dead, which is why this is safe to call from an
# EXIT trap that fires after the ordinary teardown has run.
freeze_release()
{
    [ -n "$HOLD_PID" ] && kill "$HOLD_PID" 2>/dev/null
    [ -n "$CLIENT_PGID" ] || return 0
    kill -CONT -- -"$CLIENT_PGID" 2>/dev/null
    kill -9 -- -"$CLIENT_PGID" 2>/dev/null
    return 0
}

# A mistyped freeze aborts the run rather than quietly producing an
# ORDINARY leg wearing a freeze label. That is the whole hazard here: a
# freeze leg and a normal leg differ only in what happened during the
# sleep, and a silently-ignored E_FREEZE_AT is a run nobody can tell
# apart from the control afterwards.
if [ -n "$FREEZE_AT" ]; then
    case $FREEZE_AT in
        *[!0-9]*|'') fail "E_FREEZE_AT must be a whole number of seconds \
into the measurement leg; got '$FREEZE_AT'" ;;
    esac
    [ "$FREEZE_AT" -ge 1 ] || fail "E_FREEZE_AT=$FREEZE_AT would freeze the \
client before the leg starts; it must be >= 1"
    [ "$FREEZE_AT" -lt "$SECS" ] || fail "E_FREEZE_AT=$FREEZE_AT is not \
inside a ${SECS}s leg — the client would never be stopped and the run \
would be an ordinary leg labelled as a freeze leg"
fi

# --- server side: one command, one place -----------------------------------
# srv <shell command>   run it on the server under test
# srv_cat <path>        stream a server-side file to stdout
if [ "$TARGET" = ssh ]; then
    [ -n "$SSH_HOST" ] || fail "E_TARGET=ssh needs E_SSH_HOST or /root/.t4_host"
    srv() { ssh -n -i "$SSH_KEY" -o BatchMode=yes "$SSH_HOST" "$1"; }
    srv_cat() { ssh -n -i "$SSH_KEY" -o BatchMode=yes "$SSH_HOST" \
                    "sudo cat '$1' 2>/dev/null || cat '$1'"; }
    SRV_NAME=$SSH_HOST
else
    POD=$(kubectl -n "$NS" get pod -l "arm=$ARM" \
          -o jsonpath='{.items[0].metadata.name}') || fail "no $ARM pod"
    [ -n "$POD" ] || fail "no running pod for $ARM"
    srv() { kubectl -n "$NS" exec "$POD" -- bash -lc "$1"; }
    srv_cat() { kubectl -n "$NS" exec "$POD" -- cat "$1"; }
    SRV_NAME=$POD
fi

if [ "$TARGET" = ssh ]; then
    CRED_META=$(srv "sudo stat -c '%a %U:%G' '$CRED' 2>/dev/null" \
                | tr -d '\r')
    [ "$CRED_META" = "600 root:root" ] || fail "the remote RDP credential \
$CRED must exist as mode 600 root:root; got '${CRED_META:-missing}'"
else
    [ -s "$CRED" ] || fail "no RDP credential at $CRED"
fi
echo "target=$TARGET server=$SRV_NAME arm=$ARM port=$PORT mode=$MODE" \
     "user=$SU secs=$SECS out=$OUT"
[ -n "$FREEZE_AT" ] && echo "LEG: FREEZE LEG — the client will be STOPPED" \
    "${FREEZE_AT}s into the ${SECS}s leg"

# Record WHAT is deployed before measuring it: a gate result against an
# unknown build is not a gate result.
srv 'dpkg -l | grep -E "xrdp-dev|xorgxrdp-dev"' \
    > "$OUT/deployed_packages.txt" 2>&1
grep -qE '^ii +xorgxrdp-dev' "$OUT/deployed_packages.txt" \
    || fail "xorgxrdp-dev is NOT installed on $SRV_NAME — an xrdp-dev deb
Breaks: old xorgxrdp and may have removed it (DEPLOY_RUNBOOK 2b); no
session will start and any number from this run is meaningless"
if [ "$TARGET" = pod ]; then
    kubectl -n "$NS" get pod "$POD" \
        -o jsonpath='{.spec.containers[0].image}{"\n"}' \
        > "$OUT/deployed_image.txt" 2>&1
    # Resolve the endpoint the client will actually dial independently of
    # the pod selected for log collection. An E_PORT override used to let
    # those name different arms while the resulting empty trace was reported
    # as an idle session.
    POD_ENDPOINTS=$OUT/pod_endpoints.tsv
    kubectl -n "$NS" get pods -o \
        jsonpath='{range .items[*]}{.metadata.name}{"\t"}{.metadata.labels.arm}{"\t"}{range .spec.containers[*].ports[*]}{.hostPort}{","}{end}{"\n"}{end}' \
        > "$POD_ENDPOINTS"
    DIAL_ID=$(awk -F '\t' -v port="$PORT" '
        {
            n = split($3, ports, ",")
            for (i = 1; i <= n; i++)
                if (ports[i] == port) print $1 "\t" $2
        }' "$POD_ENDPOINTS")
    [ "$(printf '%s\n' "$DIAL_ID" | sed '/^$/d' | wc -l)" -eq 1 ] \
        || fail "port $PORT resolves to zero or multiple running pods; see \
$POD_ENDPOINTS"
    DIAL_POD=$(printf '%s\n' "$DIAL_ID" | cut -f1)
    DIAL_ARM=$(printf '%s\n' "$DIAL_ID" | cut -f2)
    SELECTED_ARM=$(kubectl -n "$NS" get pod "$POD" \
                   -o jsonpath='{.metadata.labels.arm}')
    python3 "$D/gate_evidence_check.py" identity \
        --requested-arm "$ARM" --selected-pod "$POD" \
        --selected-arm "$SELECTED_ARM" --dialed-pod "$DIAL_POD" \
        --dialed-arm "$DIAL_ARM" | tee "$OUT/target_identity.txt" \
        || fail "dialled endpoint and collected pod identity disagree; see \
$OUT/target_identity.txt"
else
    { echo "host install (no image): $SSH_HOST"
      srv 'uname -srm; nvidia-smi --query-gpu=name --format=csv,noheader \
           2>/dev/null || ls /dev/dri'; } > "$OUT/deployed_image.txt" 2>&1
fi
srv_cat /etc/xrdp/gfx.toml > "$OUT/gfx.toml" 2>&1
# WHICH PAYLOAD produced the damage is part of the measurement: a frame
# interval read under the 10 Hz `code` metronome and one read under
# `codeflood` are different quantities (BACKLOG #52). On the fleet the
# payload is /etc/session_kind; on a real box it is the armed marker of
# PR-demo/t4_profile/e52_payload.sh.
srv 'cat /etc/session_kind 2>/dev/null || cat /etc/xrdp-e52-payload \
     2>/dev/null || echo "UNKNOWN (no session-kind marker on this server)"' \
    > "$OUT/deployed_session_kind.txt" 2>&1
grep -q UNKNOWN "$OUT/deployed_session_kind.txt" && echo \
    "WARNING: the payload is not declared on $SRV_NAME — record what was" \
    "on screen by hand, or the interval cannot be compared to anything" >&2
srv 'cat /etc/textflood_monitor 2>/dev/null || echo all' \
    > "$OUT/deployed_textflood_monitor.txt" 2>&1
# the recon build must be GONE (its gate is answered); if it is still
# there the arm is the wrong one
if srv 'grep -qc R1SLOT /usr/lib/xorg/modules/libxorgxrdp.so' >/dev/null 2>&1
then
    fail "$SRV_NAME still carries the R1 recon xorgxrdp — wrong build for a gate run"
fi

# --- the arm's bytes must have been certified (owner directive, 2026-08-01)
# E2's wire audit and black-frame decode moved out of this script and into
# arm_certify.sh, which runs 3 s of payload once per deploy. That only
# stays honest if a measurement REFUSES to run against an arm nobody
# certified -- otherwise the checks quietly stop happening at all, which
# is strictly worse than the 10 min they used to cost.
#
# The certificate is keyed to image + gfx.toml. A redeploy that changes
# either invalidates it, exactly like the ARM_TAG / manifest pin guard:
# a stale certificate covering a different pair is the same class of lie
# as a manifest pinning another arm's binary.
CERTFILE=$D/certs/$ARM.cert
if [ "$TARGET" = pod ]; then
    [ -f "$CERTFILE" ] || fail "$ARM has no certificate at $CERTFILE — its \
bytes have never been checked. Run: $D/arm_certify.sh $ARM"
    WANT_IMAGE=$(kubectl -n "$NS" get pod "$POD" \
                 -o jsonpath='{.spec.containers[0].image}')
    WANT_GFX=$(sha256sum "$D/gfx/$ARM.toml" | cut -c1-16)
    GOT_KEY=$(sed -n 's/^key: *//p' "$CERTFILE")
    if [ "$GOT_KEY" != "$WANT_IMAGE|$WANT_GFX" ]; then
        fail "$ARM's certificate is STALE — it certifies
  $GOT_KEY
but the running pod is
  $WANT_IMAGE|$WANT_GFX
Re-certify: $D/arm_certify.sh $ARM"
    fi
fi

# --- client-side X server at the E3 target geometry ----------------------
# STATELESS, always (owner directive 2026-07-31): never adopt a dummy X
# server another run left behind. An adopted server carries the previous
# run's RandR state — on 2026-07-31 a leftover :94 held a mode NAMED
# 3840x2160R with 2560x1440 timings, setup_monitors "passed", and two T4
# runs measured a 3.69 Mpx workload labelled 4K. Kill whatever answers on
# $CLI, start fresh from the config file, and tear it down at the end.
CLI_XPID=$(cat "/tmp/.X${CLI#:}-lock" 2>/dev/null | tr -d ' ')
if [ -n "$CLI_XPID" ] && kill -0 "$CLI_XPID" 2>/dev/null; then
    echo "killing leftover X server on $CLI (pid $CLI_XPID)"
    kill -TERM "$CLI_XPID" 2>/dev/null
    for i in 1 2 3 4 5; do
        kill -0 "$CLI_XPID" 2>/dev/null || break
        sleep 1
    done
fi
echo "starting dummy Xorg on $CLI ($(basename "$XCONF")) ..."
setsid Xorg "$CLI" -config "$XCONF" -noreset \
    -logfile "$OUT/client-xorg.log" </dev/null >/dev/null 2>&1 &
CLI_XPID=$!
sleep 4
DISPLAY=$CLI \
    MM_MONITORS=$NMON \
    MM_MODE0=${E_MODE0:-2560x1440_60} MM_MODE1=${E_MODE1:-3840x2400R} \
    MM_POS1=${E_POS1:-2560x0} \
    MM_MODELINE0=${E_MODELINE0:-312.25 2560 2752 3024 3488 1440 1443 1448 1493 -hsync +vsync} \
    MM_MODELINE1=${E_MODELINE1:-592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync} \
    bash "$MMCONF/setup_monitors.sh" >"$OUT/client-monitors.txt" 2>&1 \
    || { cat "$OUT/client-monitors.txt"; \
         fail "client did not present $NMON monitor(s)"; }
tail -1 "$OUT/client-monitors.txt"

# --- mark both logs, then drive ONE multimon AVC444 login -----------------
# The session Xorg log outlives a client, and the pod log is the whole
# container's life: mark both and read only this run's window. Skipping
# this turned a 60 s measurement into 2.2 h of accumulated lines on
# 2026-07-29.
# Mark the FILE, not just a line count: a run that creates a new session
# gets a new .xorgxrdp.<display>.log, and a count taken from the previous
# run's file then skips the whole of the new one. Measured 2026-07-31:
# a #70 A/B re-run collected a ZERO-BYTE session log and the capture legs
# silently vanished from the analysis.
MARK_XLOG=$(srv "ls -t /home/$SU/.xorgxrdp.*.log 2>/dev/null | head -1" \
    | tr -d ' \r')
MARK_X=0
if [ -n "$MARK_XLOG" ]; then
    MARK_X=$(srv "wc -l < '$MARK_XLOG' 2>/dev/null" | tr -d ' \r')
fi
MARK_X=${MARK_X:-0}
# xrdp logs to /var/log/xrdp.log INSIDE the pod (xrdp.ini LogFile), not to
# the container's stdout: kubectl logs carries only the entrypoint's own
# output, which is why a first version of this harness reported zero
# GFX_TRACE records on a session that was in fact running.
MARK_P=$(srv 'sudo wc -l < /var/log/xrdp.log 2>/dev/null \
    || wc -l < /var/log/xrdp.log 2>/dev/null' | tr -d ' \r')
MARK_P=${MARK_P:-0}
echo "log marks: session-xorg $MARK_X lines, xrdp.log $MARK_P lines"

# --- ENCODER INPUT PIPE GUARD (BACKLOG #103) -----------------------------
# xrdp requires an encoder input pipe of at least 64 KiB, which is a
# measured knee and not the 1 MiB it asks for (PRD FR-PROC-6 clause 4):
# below it the pipe cannot hold enough for xrdp and the encoder to run at
# the same time, so they take turns and each turn costs a pair of context
# switches. A uid over the HOST's fs/pipe-user-pages-soft that is not
# CAP_SYS_RESOURCE-capable in the initial user namespace is refused every
# resize and the pipe stays at two pages. That is not a small effect and
# it is not visible in any rate: measured 2026-08-08 in this fleet, an
# 8192-byte pipe carried each 13.8 MB picture in 1688 turns instead of 14
# and took 5.84 ms in the standalone #103 reproducer.
#
# xrdp does not and must not change a system setting to fix this (owner
# directive, 2026-08-09). It logs PIPE_TOO_SMALL instead, and THIS is the
# harness half of that rule: a run in the clamped state is not a valid
# measurement, so the gate refuses it and asks the owner to act.
#
# Read over the WHOLE log, not this run's window: the children are
# spawned when the SESSION starts, which on a warm pod happened during an
# earlier leg and would be behind the mark.
pipe_warn_count()
{
    srv 'sudo grep -ac PIPE_TOO_SMALL /var/log/xrdp.log 2>/dev/null \
        || grep -ac PIPE_TOO_SMALL /var/log/xrdp.log 2>/dev/null \
        || true' 2>/dev/null | tr -d ' \r' | tail -1
}
PIPE_N=$(pipe_warn_count)
PIPE_N=${PIPE_N:-0}
if [ "${PIPE_N:-0}" -gt 0 ] 2>/dev/null; then
    srv 'sudo grep -a PIPE_TOO_SMALL /var/log/xrdp.log 2>/dev/null \
        || grep -a PIPE_TOO_SMALL /var/log/xrdp.log 2>/dev/null \
        || true' > "$OUT/pipe_too_small.txt" 2>/dev/null
    echo "*** $SRV_NAME logged PIPE_TOO_SMALL $PIPE_N times BEFORE this" \
         "run started ***"
    sed -n '1,2p' "$OUT/pipe_too_small.txt"
    if [ "${E_ALLOW_TINY_PIPE:-0}" != 1 ]; then
        fail "the encoder input pipe on $SRV_NAME is smaller than xrdp
asked for, so this box is not in a state where a timing number means
anything (BACKLOG #103). OWNER ACTION: raise fs/pipe-user-pages-soft on
the HOST, or give the server CAP_SYS_RESOURCE in the initial user
namespace, then re-run. The full lines are in $OUT/pipe_too_small.txt.
E_ALLOW_TINY_PIPE=1 runs anyway and stamps every result INVALID -- use it
only to reproduce an old clamped capture on purpose."
    fi
    echo "E_ALLOW_TINY_PIPE=1: running anyway; the VERDICT will be" \
         "stamped INVALID"
fi

# A COLD session, by default: xrdp reconnects to an EXISTING session, and
# a fleet pod that has been up for hours may have one whose scrolling
# xterm is long dead -- a 25 s control run on such a session produced 28
# pictures (1.1 sends/s) and would have made E2's >= 1000 pairs
# impossible. This is a disposable probe444 session in a test pod, never
# the owner's: log it off and let sesman build a fresh one.
#
# On a real box (E_TARGET=ssh) it matters for a DIFFERENT reason (CLAUDE.md,
# T4 deployments): a surviving session keeps the PREVIOUS xorgxrdp module
# loaded, so after a deb swap the measurement can silently be of the old
# code. Logging the WHOLE session off is the sanctioned operation — never
# pkill/relaunch individual GUI processes inside a live session. E_COLD=0
# measures an existing session on purpose.
if [ "${E_COLD:-1}" = 1 ]; then
    if [ "$TARGET" = ssh ]; then
        srv "for session in \$(loginctl list-sessions --no-legend | \
             awk '{print \$1}'); do \
             [ \"\$(loginctl show-session \"\$session\" -p Name \
                    --value)\" = '$SU' ] || continue; \
             [ \"\$(loginctl show-session \"\$session\" -p Type \
                    --value)\" = x11 ] || continue; \
             sudo loginctl terminate-session \"\$session\"; done" \
            >/dev/null 2>&1
        srv "for i in \$(seq 1 25); do \
             found=0; \
             for session in \$(loginctl list-sessions --no-legend | \
                  awk '{print \$1}'); do \
                 [ \"\$(loginctl show-session \"\$session\" -p Name \
                        --value)\" = '$SU' ] || continue; \
                 [ \"\$(loginctl show-session \"\$session\" -p Type \
                        --value)\" = x11 ] && found=1; \
             done; \
             [ \"\$found\" = 0 ] && break; sleep 1; done" \
            >/dev/null 2>&1
    else
        srv "xpid=\$(pgrep -u '$SU' -x Xorg | head -1); \
             [ -n \"\$xpid\" ] || exit 0; \
             command -v xfce4-session-logout >/dev/null || exit 4; \
             args=\$(tr '\\0' ' ' < /proc/\$xpid/cmdline); \
             display=\$(printf '%s\\n' \"\$args\" | \
                 grep -oE ' :[0-9]+ ' | head -1 | tr -d ' '); \
             auth=\$(printf '%s\\n' \"\$args\" | \
                 grep -oE -- '-auth [^ ]+' | head -1 | cut -d' ' -f2); \
             su -s /bin/sh '$SU' -c \"DISPLAY=\$display \
                 XAUTHORITY=\$auth xfce4-session-logout --logout\"" \
            >/dev/null 2>&1 || fail "$ARM cannot log its whole session off"
    fi
    # ...AND THEN WAIT FOR SESMAN TO FINISH THE TEARDOWN. The Xorg
    # process disappearing is not the end of the session: sesman still
    # has to reap the window manager and the channel server and retire
    # the session record. Measured on the T4 on 2026-07-31, that tail is
    # ~600 ms, and a client that connects inside it is accepted into a
    # dying session and dropped:
    #
    #   02:29:38.725 WARN  Window manager exited with non-zero exit code 1
    #   02:29:38.756 INFO  Session on display X11-10 has finished
    #   02:29:39.365 WARN  xrdp process exited after 608 ms
    #   02:29:39.164 ERROR freerdp_post_connect failed (broken pipe)
    #
    # which produced a 0-byte gfx_trace.txt and a run that had to be
    # thrown away. It is a race, so the baseline arm won it and the
    # batched arm lost it — exactly the kind of flake that would have
    # been read as "the batched deb cannot start a session".
    srv "for i in \$(seq 1 20); do pgrep -u $SU -f sesexec >/dev/null \
         || break; sleep 1; done" >/dev/null 2>&1
    sleep 3
fi
if [ "$TARGET" = ssh ]; then
    PW=$(srv "sudo cat '$CRED'")
else
    PW=$(cat "$CRED")
fi
[ -n "$PW" ] || fail "RDP credential is empty"
if [ "$NMON" -eq 2 ]; then
    RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:$SU" "/p:$PW" \
                            "/multimon" \
                            "/gfx:AVC444" "/cert:ignore" "/log-level:WARN")
else
    # no /multimon: one monitor, so the client must not ask the server to
    # build a multi-surface layout it would then have to leave idle.
    #
    # /size is MANDATORY here and is not a cosmetic default. With /multimon
    # the session geometry comes from the monitor layout PDU; WITHOUT it
    # xfreerdp asks for its own default 1024x768 no matter what the client's
    # X server presents. The first E_MONITORS=1 run (2026-07-31) was thrown
    # away for exactly this: the client showed 3840x2160, the session ran at
    # 1024x768, and all 6685 damage records read bbox=(0,0)-(1024,768) — a
    # 0.79 Mpx run that would have been read as a 4K one.
    RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:$SU" "/p:$PW" \
                            "/size:$E_SIZE" \
                            "/gfx:AVC444" "/cert:ignore" "/log-level:WARN")
fi
if [ "$MODE" = oracle ]; then
    [ -x "$ORACLE_BIN" ] || fail "oracle client missing at $ORACLE_BIN"
    echo "client: ORACLE (save-only, acks before decode) — server ceiling"
    # FREERDP_ORACLE_DUMP IS NOT OPTIONAL IN ORACLE MODE. It does not
    # merely "also save the bytes" -- it is what makes this client an
    # ORACLE: save-only, acking BEFORE decode and present. Without it the
    # client decodes and presents, becomes the bottleneck, and the server
    # stalls waiting for acks, so the send-to-send interval stops being
    # the server ceiling and becomes a measurement of xfreerdp.
    #
    # MEASURED 2026-08-01, same pod, back to back, 20 s each, nothing but
    # this variable different:
    #     dump ON   654 sends  mean  25.7 ms  p50 25  p90  29  p99  32
    #     dump OFF   73 sends  mean 230.0 ms  p50 28  p90 668  p99 764
    # An attempt to drop the dump "because the E2 walks were expensive"
    # therefore SWAPPED THE INSTRUMENT (honesty rule: never swap the
    # component under test). The 10 min tax was never the writing -- it
    # was WALKING the dump twice, and that is what moved to
    # arm_certify.sh. The write itself goes to tmpfs and is what keeps
    # the client fast.
    DUMPDIR=$OUT/oracle
    mkdir -p "$DUMPDIR"
    rm -f /tmp/oracle_avc_s*.bin
    setsid env DISPLAY=$CLI LD_LIBRARY_PATH=/opt/freerdp-vaapi/lib \
        FREERDP_ORACLE_DUMP=1 RDPARGS="$RDPARGS" \
        "$ORACLE_BIN" /args-from:env:RDPARGS </dev/null \
        >"$OUT/client.log" 2>&1 &
else
    command -v "$FRDP" >/dev/null || fail "$FRDP not on the host"
    echo "client: RENDERING $FRDP — end-to-end context run"
    setsid env DISPLAY=$CLI RDPARGS="$RDPARGS" \
        "$FRDP" /args-from:env:RDPARGS </dev/null >"$OUT/client.log" 2>&1 &
fi
# setsid: the child is a session leader so the whole group dies by pgid.
# Never go back to pkill -f "<client>.*:<port>" — the port travels in the
# environment and appears in no argv, so that pattern silently matches
# nothing (it left a client alive for 2.2 h on 2026-07-29).
CLIENT_PGID=$!
unset PW RDPARGS
# A freeze leg -- and ONLY a freeze leg -- installs a trap, because a
# STOPPED group is the one thing this harness can leave behind that a
# later run cannot see and cannot recover from: it holds the RDP socket,
# it never dies on its own, and the next run's "client survived" warning
# would be the first anyone hears of it. The trap resumes and kills on
# every exit path, including an error abort inside the analysis below and
# a Ctrl-C. It is conditional so that a run with E_FREEZE_AT unset takes
# exactly the path it took before the freeze leg was added: no trap, and
# teardown by the same single kill it always used.
#
# One limitation, stated rather than hidden: a script started as a
# BACKGROUND job of a non-interactive shell (`e_gate_run.sh ... &` from a
# wrapper) has SIGINT ignored on entry and POSIX forbids trapping it
# again, so the INT line below is inert in that case -- verified on this
# box 2026-08-06. The EXIT and TERM lines still fire there, and a
# foreground run (the normal way this is driven) traps all three. Nothing
# traps SIGKILL: if this harness is `kill -9`ed during a freeze leg the
# client stays stopped, and `kill -CONT -- -<pgid>` is the manual repair.
if [ -n "$FREEZE_AT" ]; then
    trap 'freeze_release' EXIT
    trap 'freeze_release; exit 130' INT
    trap 'freeze_release; exit 143' TERM
fi
echo "connected; recording for ${SECS}s ..."
# The run window, for cutting the perf ring down to THIS run. The ring
# file is the xrdp process's whole life -- a pod that has served three
# runs has all three in it -- so it needs the same windowing xrdp.log
# already gets from MARK_P. Without it a 60 s run on a 45-minute-old pod
# reported "2629 sends over 2713.5 s" (2026-08-01).
RUN_T0_NS=$(date +%s%N)
RUN_T0=$((RUN_T0_NS / 1000000000))
if [ -n "$FREEZE_AT" ]; then
    # `sleep N & wait` and not a plain `sleep N`. While bash is waiting on
    # a FOREGROUND child it defers a trapped signal until that child
    # returns, so with a plain sleep a SIGTERM to this script would sit
    # unhandled -- and the client would stay FROZEN -- for the whole
    # remainder of the hold. `wait` is interruptible and the handler runs
    # at once. Measured on this box 2026-08-06 with a 20 s sleep and a
    # SIGTERM 1 s in: plain sleep, the handler had still not run when the
    # test gave up 2 s later; sleep + wait, the handler ran in under a
    # second. (Ctrl-C at a terminal happens to be prompt either way --
    # SIGINT reaches the sleep too, because it goes to the whole
    # foreground group -- but `kill -TERM` from a wrapper script does not,
    # and that is how this harness is usually stopped.)
    sleep "$FREEZE_AT" &
    HOLD_PID=$!
    wait "$HOLD_PID"
    HOLD_PID=
    kill -STOP -- -"$CLIENT_PGID" 2>/dev/null \
        || echo "WARNING: SIGSTOP to client group $CLIENT_PGID failed —" \
                "this is NOT a freeze leg, do not read it as one" >&2
    # The instant, in BOTH clocks, so the analysis can align the freeze
    # against either instrument without re-deriving an offset:
    # CLOCK_REALTIME is what the server's GFX_TRACE records are stamped
    # in, CLOCK_MONOTONIC is what the benchmark payload's own stamps file
    # (loop_start_ms) is in. On the fleet the pod shares this box's
    # kernel, so the two CLOCK_MONOTONIC readings are the same clock;
    # over an ssh port-forward (E_TARGET=ssh) the server is a different
    # box and only the wall stamp is comparable, and only to within skew.
    python3 -c "
import time
print('freeze_signal  SIGSTOP (whole client process group)')
print('freeze_at_s    $FREEZE_AT')
print('leg_secs       $SECS')
print('mono_ms        %.3f' % (time.clock_gettime(time.CLOCK_MONOTONIC)
                               * 1000.0))
print('epoch_ms       %.3f' % (time.time() * 1000.0))
print('wall_utc       %s' % time.strftime('%Y-%m-%dT%H:%M:%S',
                                          time.gmtime()))
print('clock_note     mono_ms is the CLIENT box monotonic clock. It is the')
print('               same clock as the payload stamps only when the')
print('               server is a local pod (E_TARGET=pod).')
" > "$OUT/freeze_instant.txt"
    cat "$OUT/freeze_instant.txt"
    echo "client group $CLIENT_PGID FROZEN at +${FREEZE_AT}s;" \
         "holding for $((SECS - FREEZE_AT))s"
    sleep $((SECS - FREEZE_AT)) &
    HOLD_PID=$!
    wait "$HOLD_PID"
    HOLD_PID=
else
    sleep "$SECS"
fi
RUN_T1_NS=$(date +%s%N)
RUN_T1=$((RUN_T1_NS / 1000000000))
{
    echo "start_epoch $RUN_T0"
    echo "end_epoch $RUN_T1"
    echo "start_epoch_ns $RUN_T0_NS"
    echo "end_epoch_ns $RUN_T1_NS"
} > "$OUT/measurement_window.txt"
# SIGCONT before the teardown kill so a frozen group is running when it
# is reaped; a no-op on an ordinary leg, where nothing was stopped.
[ -n "$FREEZE_AT" ] && kill -CONT -- -"$CLIENT_PGID" 2>/dev/null
kill -9 -- -"$CLIENT_PGID" 2>/dev/null
sleep 1
if pgrep -g "$CLIENT_PGID" >/dev/null 2>&1; then
    echo "WARNING: client process group $CLIENT_PGID survived teardown —" \
         "the next run's window will be contaminated" >&2
fi
# stateless client rig: the dummy X server this run started dies with it
kill -TERM "$CLI_XPID" 2>/dev/null
if [ "$MODE" = oracle ]; then
    mv /tmp/oracle_avc_s*.bin "$DUMPDIR"/ 2>/dev/null
    du -cb "$DUMPDIR"/*.bin 2>/dev/null | tail -1 \
        > "$OUT/oracle_dump_bytes.txt"
    # The dump had to be WRITTEN (it is what makes the client an oracle)
    # but nothing here reads it any more: the wire audit and black-frame
    # decode moved to arm_certify.sh. At 3840x2400 it is 7.75 GB per
    # 60 s, so it is deleted unless someone actually wants the bytes.
    # E_KEEP_DUMP=1 keeps them, for a bitstream investigation.
    if [ "${E_KEEP_DUMP:-0}" != 1 ]; then
        rm -f "$DUMPDIR"/*.bin
        rmdir "$DUMPDIR" 2>/dev/null
        echo "oracle dump discarded after the run (E_KEEP_DUMP=1 to keep);" \
             "size was $(awk '{print $1}' "$OUT/oracle_dump_bytes.txt") bytes"
    fi
fi

# --- collect both server-side logs, windowed ------------------------------
XLOG=$(srv "ls -t /home/$SU/.xorgxrdp.*.log 2>/dev/null | head -1")
[ -n "$XLOG" ] || fail "no session Xorg log on $SRV_NAME — did the login fail? \
see $OUT/client.log"
XLINES=$(srv "wc -l < '$XLOG' 2>/dev/null" | tr -d ' \r')
XLINES=${XLINES:-0}
if [ "$XLOG" != "$MARK_XLOG" ] || [ "$XLINES" -lt "$MARK_X" ]; then
    # Either a different file, or the SAME name reopened by a new
    # session (a cold run truncates .xorgxrdp.<display>.log, so the
    # mark is past the end and the window comes out empty). Both mean
    # every line in it belongs to this run.
    echo "session Xorg log is this run's own ($XLOG, $XLINES lines," \
         "mark was $MARK_X); taking it whole"
    srv_cat "$XLOG" > "$OUT/session-xorg.log"
else
    srv_cat "$XLOG" | tail -n +$((MARK_X + 1)) > "$OUT/session-xorg.log"
fi
srv_cat /var/log/xrdp.log | tail -n +$((MARK_P + 1)) > "$OUT/xrdp.log"
# Ask again now the run is over: on a COLD pod the session this run
# created is the FIRST to spawn encoder children, so the pre-run check
# above had nothing to find. Same count, same command, after the fact.
PIPE_N=$(pipe_warn_count)
PIPE_N=${PIPE_N:-0}
if [ "${PIPE_N:-0}" -gt 0 ] 2>/dev/null && [ ! -s "$OUT/pipe_too_small.txt" ]
then
    srv 'sudo grep -a PIPE_TOO_SMALL /var/log/xrdp.log 2>/dev/null \
        || grep -a PIPE_TOO_SMALL /var/log/xrdp.log 2>/dev/null \
        || true' > "$OUT/pipe_too_small.txt" 2>/dev/null
fi
# --- the per-frame trace, from the ring the sink writes ------------------
# BACKLOG #61h: GFX_TRACE / ACK_TRACE records are no longer log.c lines --
# they were ~12 unbuffered writes per frame on the xrdp main thread, on the
# path #61f exists to make faster, and the "send window" every #61f number
# was quoted against was the interval between two of them. They go to
# common/perf_trace's ring now. Pull the ring file and render it back into
# the line shapes every analysis here already reads.
PERF_DIR=/var/log/xrdp-perf
EVIDENCE_ERROR=
mkdir -p "$OUT/perf"
PERF_FILES=$(srv "ls -t $PERF_DIR/enc.* 2>/dev/null | head -4" | tr -d '\r')
if [ -n "$PERF_FILES" ]; then
    for f in $PERF_FILES; do
        srv_cat "$f" > "$OUT/perf/$(basename "$f")" 2>/dev/null
    done
    # --since/--until: cut the ring down to THIS run. A margin of 5 s on
    # each side absorbs clock skew between the pod's CLOCK_REALTIME and
    # the host's without letting a neighbouring run's records in.
    python3 "$D/perf_trace_lines.py" \
        --since $((RUN_T0 - 5)) --until $((RUN_T1 + 5)) "$OUT"/perf/enc.* \
        > "$OUT/perf_trace_lines.txt" 2>"$OUT/perf_trace_lines.log"
    cat "$OUT/perf_trace_lines.log"
    # the ACK_TRACE analyses read xrdp.log; the E5 parser reads
    # gfx_trace.txt. Both get the rendered records.
    grep -a "ACK_TRACE" "$OUT/perf_trace_lines.txt" >> "$OUT/xrdp.log"
    grep -a "GFX_TRACE" "$OUT/perf_trace_lines.txt" > "$OUT/gfx_trace.txt"
    TRACE_RECORDS=$(wc -l < "$OUT/perf_trace_lines.txt" | tr -d ' ')
    if ! python3 "$D/gate_evidence_check.py" records \
             --seconds "$SECS" --records "$TRACE_RECORDS" \
             > "$OUT/trace_presence.txt" 2>&1
    then
        cat "$OUT/trace_presence.txt" >&2
        EVIDENCE_ERROR="trace record-presence gate failed"
    else
        cat "$OUT/trace_presence.txt"
    fi
    # SPAN GUARD. The rendered trace must cover roughly the run and not
    # much more. This is what catches a window that failed to apply --
    # the failure mode is silent and severe: on 2026-08-01 an unwindowed
    # ring made a 60 s run report "2629 sends over 2713.5 s", mean
    # 1032.5 ms against a p50 of 25.0 ms, because two idle gaps between
    # runs of the same pod were inside the "run". A mean over a span the
    # session did not occupy is not a rate.
    SPAN=$(awk -F'[][]' '/GFX_TRACE/ {print $2}' "$OUT/gfx_trace.txt" \
        | sed 's/+0000//' \
        | awk 'NR==1{a=$0} {b=$0} END{if(NR>1) print a" "b}' \
        | while read -r s e; do
              python3 -c "
import sys,datetime as dt
f='%Y-%m-%dT%H:%M:%S.%f'
print(int((dt.datetime.strptime('$e',f)-dt.datetime.strptime('$s',f)).total_seconds()))"
          done)
    if [ -n "$SPAN" ] && [ "$SPAN" -gt $((SECS * 2 + 30)) ]; then
        fail "the rendered trace spans ${SPAN}s for a ${SECS}s run — the \
ring window did not apply and every rate below would be an average over \
time the session did not occupy. This is a harness fault; do not read the \
numbers."
    fi
    echo "trace span: ${SPAN:-?}s for a ${SECS}s run"
else
    echo "RED: no perf ring file under $PERF_DIR on $SRV_NAME —" \
         "is XRDP_PERF_TRACE set for this arm? The per-frame" \
         "trace lives ONLY there, so E5/E4 will read an empty trace" >&2
    EVIDENCE_ERROR="no perf ring file was collected"
    : > "$OUT/gfx_trace.txt"
fi

# --- the producer's OWN frame timestamps (evidence saturation gate) ------
# "Saturation is verified per run, never assumed. A gate run
# is valid only if BOTH hold, and the harness VERDICT must print both."
# This is evidence methodology, not a product requirement; its live status is
# in docs/pr_evidence_matrix.md. NO CODE HERE IMPLEMENTED IT -- the 1.09x
# margin that arm x014's record turns on was
# computed by hand from an archived stamps file after that run, which is
# exactly the "verify per run" the FR exists to make automatic.
#
# The benchmark payload (PR-demo/textflood) writes one line per frame to
# /tmp/e52_textflood_stamps.tsv INSIDE THE SESSION -- banner.sh passes
# that path on the fleet, e52_payload.sh takes the same compiled-in
# default on a real box. It is opened "w" at session start, so the file
# belongs to the session this run created. The other payloads (code,
# codeflood, gpuflood) write no stamps at all and the margin below is
# then reported NOT MEASURED, never as a pass.
STAMPS=${E_STAMPS:-/tmp/e52_textflood_stamps.tsv}
PRODSTAMPS=$OUT/textflood_stamps.tsv
srv_cat "$STAMPS" > "$PRODSTAMPS" 2>/dev/null
if [ ! -s "$PRODSTAMPS" ]; then
    rm -f "$PRODSTAMPS"
    PRODSTAMPS=
    echo "no producer stamps at $STAMPS on $SRV_NAME — the producer" \
         "margin will read NOT MEASURED (expected unless the payload is" \
         "textflood)"
fi

# --- the report ----------------------------------------------------------
{
    echo "=== performance gate run: $ARM, $MODE client, ${SECS}s\
${FREEZE_AT:+, FREEZE LEG at +${FREEZE_AT}s} ==="
    echo "image:    $(cat "$OUT/deployed_image.txt")"
    grep -E "xrdp-dev|xorgxrdp-dev" "$OUT/deployed_packages.txt" \
        | awk '{print "package: " $2 " " $3}'
    echo "monitors: $(tail -1 "$OUT/client-monitors.txt")"
    echo "payload:  SESSION_KIND = $(cat "$OUT/deployed_session_kind.txt")"
    echo "selector: TEXTFLOOD_MONITOR = \
$(cat "$OUT/deployed_textflood_monitor.txt")"
    echo "refresh:  intra_refresh_frames = \
$(sed -n 's/^ *intra_refresh_frames *= *\([0-9][0-9]*\).*/\1/p' \
  "$OUT/gfx.toml" | head -1)"
    # Same rule as the freeze banner below: a run whose encoder input
    # pipe was clamped must never be readable as an ordinary one, so it
    # is stated here, above every number it contaminates.
    if [ "${PIPE_N:-0}" -gt 0 ] 2>/dev/null; then
        echo "pipe:     *** ENCODER INPUT PIPE TOO SMALL — THIS RUN IS"
        echo "          NOT A VALID MEASUREMENT (input pipe too small) ***"
        echo "          An encoder child got an input pipe below the"
        echo "          64 KiB minimum xrdp requires, $PIPE_N times."
        echo "          The pipe cannot then hold enough for xrdp and"
        echo "          the encoder to run at the same time, so they"
        echo "          take turns; the standalone pipe reproducer measured"
        echo "          5.84 ms per 13.8 MB picture. Every rate is then"
        echo "          depressed by an amount that has nothing to do"
        echo "          with the build or the config under test."
        echo "          OWNER ACTION: raise fs/pipe-user-pages-soft on"
        echo "          the HOST, or give the server CAP_SYS_RESOURCE in"
        echo "          the initial user namespace. xrdp will not change"
        echo "          a system setting itself (owner directive,"
        echo "          2026-08-09). Lines: pipe_too_small.txt"
    else
        echo "pipe:     encoder input pipe at or above the 64 KiB"
        echo "          minimum (no PIPE_TOO_SMALL in $SRV_NAME's log)"
    fi
    # A freeze leg must never be readable as an ordinary one. Say so here,
    # in the VERDICT, above every number it contaminates.
    if [ -n "$FREEZE_AT" ]; then
        echo "leg:      *** CLIENT-FREEZE DIAGNOSTIC ***  the client"
        echo "          process group was SIGSTOPped ${FREEZE_AT}s into"
        echo "          this ${SECS}s leg and resumed only at teardown."
        echo "          A stopped client stops READING the socket as well"
        echo "          as acknowledging frames, so its receive buffer"
        echo "          fills and it advertises a zero TCP window: the"
        echo "          rates below cover a leg that was deliberately"
        echo "          stalled and are NOT comparable to a normal run."
        echo "          Freeze instant (both clocks): freeze_instant.txt"
    fi
    echo

    echo "=== E5 / rate — send interval from the server's own log ==="
    python3 - "$OUT/gfx_trace.txt" "$MODE" "$E5_BASE_MS" "$NMON" \
             "$PRODSTAMPS" "$RUN_T0" "$RUN_T1" <<'PY'
import re
import sys

TS = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)\.(\d+)")
DMG = re.compile(r"GFX_TRACE dmg surface=(\d+)")
BBOX = re.compile(r"GFX_TRACE dmg surface=\d+ num_rects=\d+ "
                  r"x1=(\d+) y1=(\d+) x2=(\d+) y2=(\d+)")
BATCH = re.compile(r"GFX_TRACE batch cycle=(\d+) set_n=(\d+) "
                   r"monitors_armed=(\d+) kids_armed=(\d+)")
SEND = re.compile(r"GFX_TRACE send bytes=(\d+) last=(\d+) frame_id=(\d+)")


def secs(line):
    m = TS.match(line)
    if not m:
        return None
    h, mi, s, frac = int(m.group(4)), int(m.group(5)), int(m.group(6)), \
        m.group(7)
    return h * 3600 + mi * 60 + s + float("0." + frac)


t = []
events = []
bboxes = []
for line in open(sys.argv[1], errors="replace"):
    v = secs(line)
    if v is None:
        continue
    if "GFX_TRACE enc submitted_seq" in line:
        t.append(v)
        continue
    m = DMG.search(line)
    if m:
        events.append((v, "dmg", int(m.group(1))))
        b = BBOX.search(line)
        if b:
            bboxes.append((int(b.group(3)), int(b.group(4))))
        continue
    m = BATCH.search(line)
    if m:
        events.append((v, "batch", int(m.group(4))))
        continue
    m = SEND.search(line)
    if m and m.group(2) == "1":
        events.append((v, "last", int(m.group(3))))
if len(t) < 3:
    print("RED: only %d GFX_TRACE send records — is XRDP_GFX_TRACE=1 set "
          "in the arm's env?" % len(t))
    raise SystemExit(0)
# File order is write order. Out-of-order stamps mean the deployed xrdp
# still carries the pre-#52 log clock (common/log.c printed microseconds
# as the millisecond field), and every percentile below would need
# offline repair — say so instead of quietly sorting it away.
inversions = sum(1 for a, b in zip(t, t[1:]) if b < a)
if inversions:
    print("WARNING: %d of %d send stamps go BACKWARDS in file order — this "
          "build predates the log timestamp fix; percentiles below "
          "are unreliable (means are not)" % (inversions, len(t) - 1))
    t.sort()
span = t[-1] - t[0]
gaps = sorted(b - a for a, b in zip(t, t[1:]))
n = len(gaps)


def pct(p):
    return gaps[min(n - 1, int(p * n))] * 1000.0


mean = sum(gaps) / n * 1000.0
print("sends: %d over %.1f s" % (len(t), span))
print("send-to-send gap: mean %.1f ms  p50 %.0f ms  p90 %.0f ms  "
      "p99 %.0f ms" % (mean, pct(0.5), pct(0.9), pct(0.99)))
nmon = int(sys.argv[4]) if len(sys.argv) > 4 else 2

# --- FR-BENCH-1: what the BENCHMARK PAYLOAD itself managed ---------------
# The payload writes one tab-separated line per rendered frame; the
# second field, loop_start_ms, is CLOCK_MONOTONIC milliseconds taken at
# the top of its render loop (PR-demo/textflood/textflood.c, now_ms()).
# Its frame interval is the difference between consecutive loop_start_ms.
# The comment header carries an epoch/monotonic anchor pair so those
# stamps can be laid on the wall clock the server's records use.
prod_path = sys.argv[5] if len(sys.argv) > 5 else ""
run_t0 = float(sys.argv[6]) if len(sys.argv) > 6 else 0.0
run_t1 = float(sys.argv[7]) if len(sys.argv) > 7 else 0.0
prod = []
anchor = None
prod_note = ""
if prod_path:
    try:
        fh = open(prod_path, errors="replace")
    except OSError:
        fh = None
    if fh is not None:
        for line in fh:
            if line.startswith("#"):
                me = re.search(r"epoch_ms=([0-9.]+)", line)
                mm = re.search(r"mono_ms=([0-9.]+)", line)
                if me and mm:
                    anchor = (float(me.group(1)), float(mm.group(1)))
                continue
            f = line.split("\t")
            if len(f) < 2:
                continue
            try:
                prod.append(float(f[1]))
            except ValueError:
                continue
        fh.close()
prod_total = len(prod)
prod_scope = "the whole session, NOT cut to this leg"
# Cut the payload's frames down to THIS measurement leg, for the same
# reason the perf ring is cut: the stamps file spans the whole SESSION,
# which outlives the leg on both sides. Arm x014's archived file holds
# 101.5 s of frames for a 60 s run, and its interval over the whole file
# is 15.94 ms against 16.90 ms over the leg -- a 6 % error, in the
# direction that flatters the margin.
if prod and anchor and run_t1 > run_t0:
    epoch_ms, mono_ms = anchor
    win = [v for v in prod
           if run_t0 <= (epoch_ms + (v - mono_ms)) / 1000.0 <= run_t1]
    if len(win) >= 30:
        prod = win
        prod_scope = "inside this leg"
    else:
        prod_note = ("only %d of %d payload frames landed inside the run "
                     "window, so the WHOLE file is used below. Either the "
                     "server's wall clock disagrees with this box's, or "
                     "the payload restarted mid-run -- check before "
                     "quoting the margin" % (len(win), prod_total))
elif prod and not anchor:
    prod_note = ("the stamps file has no epoch/mono anchor header, so the "
                 "interval below is over the WHOLE session and not just "
                 "this run's leg")

if len(prod) >= 3:
    pgaps = sorted(b - a for a, b in zip(prod, prod[1:]))
    pn = len(pgaps)
    pmean = sum(pgaps) / pn
    print("producer frame interval (the benchmark payload's own per-frame "
          "stamps, %d of %d session frames — %s): mean %.1f ms  p50 %.1f "
          "ms  p90 %.1f ms"
          % (len(prod), prod_total, prod_scope, pmean, pgaps[pn // 2],
             pgaps[min(pn - 1, int(0.9 * pn))]))
    if prod_note:
        print("         NOTE: %s" % prod_note)
    margin = mean / pmean
    print("producer margin = pipeline mean %.1f ms / producer mean "
          "%.1f ms = %.2fx" % (mean, pmean, margin))
    # THREE STATES, and never a run-failing one (owner directive,
    # 2026-08-06). The 2.0x floor stays a hard gate for claims that two
    # pipeline STAGES OVERLAP -- below it, a second frame may simply not
    # have existed, and "the stages did not overlap" cannot be told from
    # "there was nothing to overlap with". Throughput and regression
    # comparisons do not need the floor, provided both arms share the
    # payload and the margin is printed beside every number, which is
    # what this line is for.
    if margin >= 2.0:
        print("         OK (>= 2.0x floor): the payload kept damage "
              "pending at every pipeline completion, so throughput, "
              "regression AND stage-overlap claims from this run are all "
              "valid.")
    else:
        print("         PRODUCER-LIMITED at %.2fx, under the 2.0x floor. "
              "Any claim from this run about two pipeline stages "
              "OVERLAPPING is VOID: the payload is close enough to the "
              "pipeline that a missing second frame cannot be told from a "
              "stage that failed to overlap. Throughput and regression "
              "comparisons against an arm sharing this payload, geometry "
              "and client REMAIN VALID -- quote this %.2fx beside every "
              "number taken from them (owner directive, 2026-08-06)."
              % (margin, margin))
else:
    print("producer frame interval: NOT MEASURED — no benchmark payload "
          "stamps were collected, so the producer margin is unknown for "
          "this run.")
    print("         This is neither a pass nor a fail. Without it nothing "
          "here can say whether the pipeline or the payload was the "
          "limit, so treat every number below as unattributed. Expected "
          "when the payload is not textflood; if it IS textflood, the "
          "stamps file was missing on the server (see the collection "
          "warning above the report).")

# --- what geometry did the SESSION actually run at? ----------------------
# The client's monitor list is what we asked for; the damage bboxes are what
# we got. They diverged silently on 2026-07-31 (client 3840x2160, session
# 1024x768 — a 16x pixel difference) and the run had to be thrown away, so
# state the served geometry next to every number taken from it.
if bboxes:
    mw = max(b[0] for b in bboxes)
    mh = max(b[1] for b in bboxes)
    print("session geometry (max damage extent): %dx%d = %.2f Mpx over "
          "%d damage records" % (mw, mh, mw * mh / 1e6, len(bboxes)))
    if mw * mh < 2e6:
        print("         GEOMETRY WARNING: the session served under 2 Mpx. "
              "If a 4K run was intended, the client fell back to its "
              "default size (/size is mandatory without /multimon) and "
              "this is NOT a 4K measurement.")

print("%.2f sends/s = %.2f pairs/s per monitor (%d monitor%s)"
      % (len(t) / span, len(t) / span / nmon, nmon, "" if nmon == 1 else "s"))

# --- saturation evidence (BACKLOG #52: is the pipeline actually full?) ---
per_surf = {}
for v, kind, arg in events:
    if kind == "dmg":
        per_surf.setdefault(arg, []).append(v)
for s in sorted(per_surf):
    p = sorted(b - a for a, b in zip(per_surf[s], per_surf[s][1:]))
    if p:
        print("surface %d own period: n=%d mean %.1f ms  p50 %.1f ms"
              % (s, len(p) + 1, sum(p) / len(p) * 1000.0,
                 p[len(p) // 2] * 1000.0))

# --- G5: was BOTH monitors' ink actually in this run? --------------------
# The batch can only overlap monitors that have something to send, so a
# payload that inks one monitor measures BACKLOG #53's one-active-one-idle
# regime and says nothing about the batching this gate is for. It has
# happened twice: a 27-column corpus line in a 6400 px xterm (2026-07-30,
# 0.91x RED), and xfwm4 re-snapping the flood window onto one monitor on
# the T4 (2026-07-30, kids_armed=2 in 96 % of cycles). Both looked like
# ordinary runs. State the coverage so no number is read as the wrong
# regime's.
if per_surf:
    counts = {s: len(v) for s, v in per_surf.items()}
    lo, hi = min(counts.values()), max(counts.values())
    print("damage coverage: %s"
          % "  ".join("surface %d: %d" % (s, c)
                      for s, c in sorted(counts.items())))
    if nmon == 1:
        if len(counts) != 1:
            print("         COVERAGE WARNING: E_MONITORS=1 but %d surfaces "
                  "were damaged — the client presented more than one "
                  "monitor and this is not a single-monitor run"
                  % len(counts))
        else:
            print("         one monitor, one surface — this is the "
                  "single-monitor control")
    elif len(counts) < 2 or hi > 3 * max(1, lo):
        print("         COVERAGE WARNING: one monitor carried the run "
              "(%d vs %d). This is the one-active-one-idle regime "
              "not two-monitor batching — an E5 ratio from "
              "it is not an E5-2 result." % (hi, lo))
    else:
        print("         both monitors inked (worst imbalance %.2fx) — "
              "this is the two-monitor regime" % (hi / max(1.0, lo)))
busy = 0.0
armed = 0.0
cycles = 0
open_at = None
kids = {}
for v, kind, arg in events:
    if kind == "batch":
        cycles += 1
        kids[arg] = kids.get(arg, 0) + 1
        if open_at is None:
            open_at = v
    elif kind == "last" and open_at is not None:
        busy += v - open_at
        open_at = None
if cycles:
    print("cycles: %d   kids_armed: %s"
          % (cycles, "  ".join("%d:%d (%.0f%%)"
                               % (k, c, 100.0 * c / cycles)
                               for k, c in sorted(kids.items()))))
if span > 0 and busy > 0:
    print("worker busy (batch arm -> last=1): %.1f s of %.1f s = %.0f%%   "
          "mean service %.1f ms"
          % (busy, span, 100.0 * busy / span, busy / max(1, cycles) * 1000.0))
    print("             (100% busy = the pipeline is the limit; a low "
          "number with a long interval = the payload or capture is)")
print()

if sys.argv[2] == "oracle":
    if sys.argv[3].lower() in ("none", "na", "n/a"):
        print("E5 GATE: NOT REQUESTED — this characterization has no "
              "same-payload serialized baseline")
    else:
        base = float(sys.argv[3])
        print("E5 GATE: baseline %.1f ms mean per send (SAME payload — see "
              "E5_BASE_MS)" % base)
        print("         measured %.1f ms  ->  %.2fx" % (mean, base / mean))
        if mean <= base / 2.0:
            print("         >= 2.0x: PASS")
        elif mean <= base / 1.5:
            print("         between 1.5x and 2.0x: SHORT OF THE PREDICTION -- "
                  "record it as such, do not re-tune until it looks better")
        else:
            print("         under 1.5x: RED. The stop rule applies: attribute "
                  "the remainder (capture, vmsplice feed, NUT demux, LTR "
                  "rewrite, EGFX assembly) before anything ships")
else:
    print()
    print("CONTEXT ONLY (rendering client): this is the end-to-end rate, "
          "not the E5 gate. It is client-bound by ~3.3x.")
PY
    echo

    echo "=== E4 — poll-set size the worker actually armed ==="
    # gfx_trace.txt, not xrdp.log: since #61h the per-frame records come
    # from the perf ring, and only the ACK_TRACE ones are appended back
    # into xrdp.log for the delivery-chain analyses
    if grep -aq "kids_armed\|set_size\|batched" "$OUT/gfx_trace.txt"; then
        grep -ao "kids_armed=[0-9]*\|set_size=[0-9]*\|batched=[0-9]*" \
            "$OUT/gfx_trace.txt" | sort | uniq -c | sort -rn | head -5
    else
        echo "no set-size records in this window (step 7 logs them; with"
        echo "one monitor damaged per cycle the set is 2 children, not 4)"
    fi
    echo

    echo "=== E2 — server log, four things that must be zero ==="
    for pat in "rewrite failed" "unsupported" "did not return" \
               "budget exceeded" "third capture" "fifo_to_proc_depth"; do
        printf '%-24s %s\n' "$pat" \
            "$(grep -aci "$pat" "$OUT/xrdp.log" "$OUT/session-xorg.log" \
               2>/dev/null | awk -F: '{s+=$2} END{print s+0}')"
    done
    echo
} | tee "$OUT/VERDICT.txt"

# E2's two dump walks do NOT run here (owner directive, 2026-08-01).
# They are a property of the deployed ARM -- this image's ffmpeg, this
# host's VAAPI driver, this arm's encoder_args -- not of the run, so they
# are proven once by arm_certify.sh on 3 s of payload at deploy time. On
# 2026-08-01 running them here cost 10 min 07 s against 64 s of
# measurement, re-proving unchanged properties on a 7.75 GB dump.
# What is reproduced below is the certificate, so every measurement still
# STATES what was certified and against which image.
{
    echo "=== arm certification (arm_certify.sh, at deploy time) ==="
    if [ -f "$CERTFILE" ]; then
        sed -n '1,6p' "$CERTFILE"
        grep -E "ASSERT VERDICT|VERDICT: PASS|black frames" "$CERTFILE" \
            | sed 's/^/  /'
        echo "  full certificate: $CERTFILE"
    else
        echo "  NONE -- see the abort above; this line should be unreachable"
    fi
} | tee -a "$OUT/VERDICT.txt"

# The payload's stamps are archived BESIDE the capture, compressed, in
# the same shape the x014 record already carries them
# (textflood_stamps.tsv.gz). They are the evidence behind the margin line
# printed above, and the margin is now a load-bearing caveat on every
# throughput number rather than a formality, so it has to be auditable
# from the capture alone.
if [ -n "$PRODSTAMPS" ] && [ -f "$PRODSTAMPS" ]; then
    gzip -f "$PRODSTAMPS" \
        && echo "producer stamps archived: $PRODSTAMPS.gz"
fi

# --- LOG THE SESSION OFF. Not optional, and not a courtesy ------------
# The payload keeps running after the client goes away: the session has
# no idea a measurement ended. Left up, a textflood arm scrolls a corpus
# at full tilt forever -- 85 % of a core plus 15 % of another for Xorg,
# measured on x014 on 2026-08-02, 2 h 33 min after its run finished, with
# no consumer of a single frame. It also silently taxes whatever is
# measured NEXT on this box, which is worse than the wasted core.
#
# This is the same failure sessions_off.sh was written for on 2026-07-30
# (eight arms, ~4 cores). Writing a cleanup script did not stop it
# recurring, because nothing CALLED it. So the gate that creates the
# session now ends it, and sessions_off.sh goes back to being what it
# should be: a sweep for sessions nobody owns, not the routine path.
#
# Logging the WHOLE session off is the sanctioned operation (CLAUDE.md
# GUI lifecycle) -- never pkill/relaunch individual GUI processes inside
# a live session. E_KEEP_SESSION=1 keeps it, for when the next step is
# eyeballing the same session onscreen.
if [ "${E_KEEP_SESSION:-0}" != 1 ]; then
    if [ "$TARGET" = ssh ]; then
        srv "for session in \$(loginctl list-sessions --no-legend | \
             awk '{print \$1}'); do \
             [ \"\$(loginctl show-session \"\$session\" -p Name \
                    --value)\" = '$SU' ] || continue; \
             [ \"\$(loginctl show-session \"\$session\" -p Type \
                    --value)\" = x11 ] || continue; \
             sudo loginctl terminate-session \"\$session\"; done" \
            >/dev/null 2>&1
    else
        srv "xpid=\$(pgrep -u '$SU' -x Xorg | head -1); \
             [ -n \"\$xpid\" ] || exit 0; \
             command -v xfce4-session-logout >/dev/null || exit 4; \
             args=\$(tr '\\0' ' ' < /proc/\$xpid/cmdline); \
             display=\$(printf '%s\\n' \"\$args\" | \
                 grep -oE ' :[0-9]+ ' | head -1 | tr -d ' '); \
             auth=\$(printf '%s\\n' \"\$args\" | \
                 grep -oE -- '-auth [^ ]+' | head -1 | cut -d' ' -f2); \
             su -s /bin/sh '$SU' -c \"DISPLAY=\$display \
                 XAUTHORITY=\$auth xfce4-session-logout --logout\"" \
            >/dev/null 2>&1 || fail "$ARM cannot log its whole session off"
    fi
    srv "for i in \$(seq 1 20); do pgrep -u $SU -f sesexec >/dev/null \
         || break; sleep 1; done" >/dev/null 2>&1
    left=$(srv "pgrep -c -u $SU -x Xorg 2>/dev/null || true" \
           2>/dev/null | tr -d ' \r')
    if [ "${left:-0}" != 0 ]; then
        echo "WARNING: $ARM still has ${left} session Xorg after log off"
    else
        echo "session logged off (E_KEEP_SESSION=1 to keep it)"
    fi
fi

echo
echo "evidence: $OUT"

if [ -n "$EVIDENCE_ERROR" ]; then
    fail "$EVIDENCE_ERROR; the session was logged off and no result from \
this run is admissible"
fi

# A clamped pipe found only AFTER the run (cold pod: this run's session
# was the first to spawn children) still invalidates it, and a caller
# that chains legs must not read this leg as good. The evidence is kept
# and the banner is in the VERDICT -- the exit code is what stops the
# next leg. A red result stays red.
if [ "${PIPE_N:-0}" -gt 0 ] 2>/dev/null; then
    echo "EXIT NONZERO: the encoder input pipe was clamped; this run is" \
         "not a valid measurement. See the pipe-size banner" \
         "in $OUT/VERDICT.txt for the owner action." >&2
    exit 3
fi
