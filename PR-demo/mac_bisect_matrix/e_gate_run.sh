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
#                             >= 2.0x of it passes.
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
#   E2 wire properties        the oracle dump is run through
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
#   E_ARM/E_NS (pod only) · E_PORT · E_USER · E_CRED_FILE · E_MODE
#   oracle|render · E_REFRESH · E5_BASE_MS · E_OUT · E_COLD 0|1 ·
#   E_MODE0/E_MODE1/E_POS1/E_MODELINE0/E_MODELINE1 (client geometry)
set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-120}
ARM=${E_ARM:-arm-r}
PORT=${E_PORT:-40017}
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
    CRED=${E_CRED_FILE:-/root/.t4_rdp_cred}
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
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${E_OUT:-$D/captures/e_gate_${MODE}_$STAMP}
mkdir -p "$OUT"

fail() { echo "ABORT: $*" >&2; exit 1; }

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

[ -s "$CRED" ] || fail "no RDP credential at $CRED"
echo "target=$TARGET server=$SRV_NAME arm=$ARM port=$PORT mode=$MODE" \
     "user=$SU secs=$SECS out=$OUT"

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
# the recon build must be GONE (its gate is answered); if it is still
# there the arm is the wrong one
if srv 'grep -qc R1SLOT /usr/lib/xorg/modules/libxorgxrdp.so' >/dev/null 2>&1
then
    fail "$SRV_NAME still carries the R1 recon xorgxrdp — wrong build for a gate run"
fi

# --- client-side X server at the E3 target geometry ----------------------
if ! DISPLAY=$CLI xrandr --query >/dev/null 2>&1; then
    echo "starting dummy Xorg on $CLI ($(basename "$XCONF")) ..."
    setsid Xorg "$CLI" -config "$XCONF" -noreset \
        -logfile "$OUT/client-xorg.log" </dev/null >/dev/null 2>&1 &
    sleep 4
fi
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
MARK_X=$(srv "wc -l < /home/$SU/.xorgxrdp.*.log 2>/dev/null | head -1" \
    | tr -d ' \r')
MARK_X=${MARK_X:-0}
# xrdp logs to /var/log/xrdp.log INSIDE the pod (xrdp.ini LogFile), not to
# the container's stdout: kubectl logs carries only the entrypoint's own
# output, which is why a first version of this harness reported zero
# GFX_TRACE records on a session that was in fact running.
MARK_P=$(srv 'sudo wc -l < /var/log/xrdp.log 2>/dev/null \
    || wc -l < /var/log/xrdp.log 2>/dev/null' | tr -d ' \r')
MARK_P=${MARK_P:-0}
echo "log marks: session-xorg $MARK_X lines, xrdp.log $MARK_P lines"

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
    srv "pkill -TERM -u $SU -x xterm; pkill -TERM -u $SU Xorg" \
        >/dev/null 2>&1
    srv "for i in \$(seq 1 25); do pgrep -u $SU -x Xorg >/dev/null \
         || break; sleep 1; done" >/dev/null 2>&1
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
PW=$(cat "$CRED")
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
DUMPDIR=$OUT/oracle
if [ "$MODE" = oracle ]; then
    [ -x "$ORACLE_BIN" ] || fail "oracle client missing at $ORACLE_BIN"
    mkdir -p "$DUMPDIR"
    rm -f /tmp/oracle_avc_s*.bin
    echo "client: ORACLE (save-only, acks before decode) — server ceiling"
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
echo "connected; recording for ${SECS}s ..."
sleep "$SECS"
kill -9 -- -"$CLIENT_PGID" 2>/dev/null
sleep 1
if pgrep -g "$CLIENT_PGID" >/dev/null 2>&1; then
    echo "WARNING: client process group $CLIENT_PGID survived teardown —" \
         "the next run's window will be contaminated" >&2
fi
if [ "$MODE" = oracle ]; then
    mv /tmp/oracle_avc_s*.bin "$DUMPDIR"/ 2>/dev/null
    du -cb "$DUMPDIR"/*.bin 2>/dev/null | tail -1 \
        > "$OUT/oracle_dump_bytes.txt"
fi

# --- collect both server-side logs, windowed ------------------------------
XLOG=$(srv "ls -t /home/$SU/.xorgxrdp.*.log 2>/dev/null | head -1")
[ -n "$XLOG" ] || fail "no session Xorg log on $SRV_NAME — did the login fail? \
see $OUT/client.log"
srv_cat "$XLOG" | tail -n +$((MARK_X + 1)) > "$OUT/session-xorg.log"
srv_cat /var/log/xrdp.log | tail -n +$((MARK_P + 1)) > "$OUT/xrdp.log"
grep -a "GFX_TRACE" "$OUT/xrdp.log" > "$OUT/gfx_trace.txt" 2>/dev/null

# --- the report ----------------------------------------------------------
{
    echo "=== #45 gate run: $ARM, $MODE client, ${SECS}s ==="
    echo "image:    $(cat "$OUT/deployed_image.txt")"
    grep -E "xrdp-dev|xorgxrdp-dev" "$OUT/deployed_packages.txt" \
        | awk '{print "package: " $2 " " $3}'
    echo "monitors: $(tail -1 "$OUT/client-monitors.txt")"
    echo "payload:  SESSION_KIND = $(cat "$OUT/deployed_session_kind.txt")"
    echo "refresh:  intra_refresh_frames = \
$(grep -a intra_refresh_frames "$OUT/gfx.toml" | tr -d ' ' | cut -d= -f2)"
    echo

    echo "=== E5 / rate — send interval from the server's own log ==="
    python3 - "$OUT/gfx_trace.txt" "$MODE" "$E5_BASE_MS" "$NMON" <<'PY'
import re
import sys

TS = re.compile(r"^\[(\d{4})-(\d\d)-(\d\d)T(\d\d):(\d\d):(\d\d)\.(\d+)")
DMG = re.compile(r"GFX_TRACE avc dmg surface=(\d+)")
BBOX = re.compile(r"GFX_TRACE avc dmg surface=\d+ num_rects=\d+ "
                  r"bbox=\((\d+),(\d+)\)-\((\d+),(\d+)\)")
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
          "build predates the #52 step-0 log clock fix; percentiles below "
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
                  "single-monitor control (BACKLOG #64)")
    elif len(counts) < 2 or hi > 3 * max(1, lo):
        print("         COVERAGE WARNING: one monitor carried the run "
              "(%d vs %d). This is the one-active-one-idle regime "
              "(BACKLOG #53), NOT two-monitor batching — an E5 ratio from "
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
    if grep -aq "kids_armed\|set_size\|batched" "$OUT/xrdp.log"; then
        grep -ao "kids_armed=[0-9]*\|set_size=[0-9]*\|batched=[0-9]*" \
            "$OUT/xrdp.log" | sort | uniq -c | sort -rn | head -5
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

if [ "$MODE" = oracle ]; then
    DUMP=$(ls -S "$DUMPDIR"/*.bin 2>/dev/null | head -1)
    if [ -n "$DUMP" ]; then
        {
            echo "=== E2 — wire audit (--assert) on $(basename "$DUMP") ==="
            python3 "$D/../../tools/avc444_ltr_wire_audit.py" --assert \
                --intra-refresh "$REFRESH" "$DUMP" "$ARM gate run" \
                2>&1 | tail -25
            echo "wire audit exit: $?"
            echo
            echo "=== E2 — black-frame check ==="
            python3 "$D/oracle_black_frame_check.py" "$DUMP" 2>&1 | tail -15
        } | tee -a "$OUT/VERDICT.txt"
    else
        echo "RED: the oracle client wrote no dump — E2 cannot be judged" \
            | tee -a "$OUT/VERDICT.txt"
    fi
fi
echo
echo "evidence: $OUT"
