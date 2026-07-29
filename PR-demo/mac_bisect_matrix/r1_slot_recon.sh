#!/bin/bash
# r1_slot_recon.sh — measure, at monitorCount = 2, which capture slot each
# monitor actually lands in (xrdp BACKLOG #45 recon gate R1).
#
# WHY THIS EXISTS. FR-CAPTURE-8 gives the capture loop a budget of two
# outstanding rects and two shmem slots per monitor, so capture of frame
# N+1 can overlap encode of frame N. The slot is chosen globally, as
# `(rect_id + 1) & 1`, and rect_id advances once per SEND — i.e. by
# monitorCount between one monitor's consecutive sends. At even m that
# parity is constant per monitor, which would pin each monitor to one
# slot forever and make the two-slot mechanism inert. That is derived
# from source; #45 R1 requires it measured before step 6c changes the
# arithmetic. This script produces the measurement.
#
# HOW. Server side is arm-q (PR-demo/mac_bisect_matrix/k8s/arm-q.yaml):
# arm-n's encoder config on an xorgxrdp carrying one recon-only INFO line
# per AVC444 send —
#   R1SLOT mon <n> shmem_offset <o> rect_id <r> rect_id_ack <a> max_outstanding <m>
# shmem_offset is the offset really put on the wire, so "did monitor n's
# offset ever change" is the answer to R1, with no derivation left.
#
# CLIENT SIDE STAYS ON THE HOST (CLAUDE.md "the container fleet is for
# the RDP SERVER side only"): a dummy 2-monitor X server plus the host's
# xfreerdp3, reaching the pod through its loopback host port. Nothing is
# installed into the pod and no host xrdp instance is touched.
#
# Credential: probe444's password lives ONLY in root-owned
# /root/.oracle_cred; it is read into a variable and handed to the client
# through the environment (/args-from:env:), never as an argument and
# never printed — same handling as verify_matrix.sh.
#
# It also answers recon gate R2 on the same session: the pod's /dev/shm
# is sampled for the whole run, and the reported floor is the capture
# arena xorgxrdp actually reserved (it logs the byte count at connect),
# against the tmpfs the pod was given.
#
# Geometry is parameterised. Default is the 2x1024x768 rig; the #45 E3
# target layout is:
#   R1_XORG_CONF=$D/../multimon_offline/xorg-dummy-2mon-4k.conf \
#   R1_DISPLAY=:94 R1_MODE0=2560x1440_60 R1_MODE1=3840x2400R \
#   R1_POS1=2560x0 R1_MODELINE0="312.25 2560 ..." \
#   R1_MODELINE1="592.25 3840 ..." r1_slot_recon.sh 60
# (or just run it via r1r2_target_geometry.sh, which pins those values).
#
# Usage: r1_slot_recon.sh [seconds]        (default 60)
set -u
D=$(cd "$(dirname "$0")" && pwd)
SECS=${1:-60}
ARM=${R1_ARM:-arm-q}
PORT=${R1_PORT:-40016}
NS=${R1_NS:-bisect-matrix}
SU=${R1_USER:-probe444}
CRED=${R1_CRED_FILE:-/root/.oracle_cred}
CLI=${R1_DISPLAY:-:95}
FRDP=${R1_XFREERDP:-xfreerdp3}
MMCONF=${R1_MULTIMON_DIR:-$D/../multimon_offline}
XCONF=${R1_XORG_CONF:-$MMCONF/xorg-dummy-2mon.conf}
STAMP=$(date +%Y%m%d_%H%M%S)
OUT=${R1_OUT:-$D/captures/r1_slot_recon_$STAMP}
mkdir -p "$OUT"

fail() { echo "ABORT: $*" >&2; exit 1; }

[ -s "$CRED" ] || fail "no probe credential at $CRED"
command -v "$FRDP" >/dev/null || fail "$FRDP not on the host"
POD=$(kubectl -n "$NS" get pod -l "arm=$ARM" -o jsonpath='{.items[0].metadata.name}') \
    || fail "cannot find the $ARM pod"
[ -n "$POD" ] || fail "no running pod for $ARM"
echo "arm=$ARM pod=$POD port=$PORT secs=$SECS out=$OUT"

# The arm must actually be the recon build, or the run measures nothing.
kubectl -n "$NS" exec "$POD" -- grep -qc R1SLOT \
    /usr/lib/xorg/modules/libxorgxrdp.so \
    || fail "$ARM's xorgxrdp has no R1SLOT instrumentation"

# --- client-side X server with TWO 1024x768 monitors ---------------------
if ! DISPLAY=$CLI xrandr --query >/dev/null 2>&1; then
    echo "starting dummy Xorg on $CLI ($(basename "$XCONF")) ..."
    setsid Xorg "$CLI" -config "$XCONF" -noreset \
        -logfile "$OUT/client-xorg.log" </dev/null >/dev/null 2>&1 &
    sleep 4
fi
DISPLAY=$CLI \
    MM_MODE0=${R1_MODE0:-1024x768_60} MM_MODE1=${R1_MODE1:-1024x768_60} \
    MM_POS1=${R1_POS1:-1024x0} \
    MM_MODELINE0=${R1_MODELINE0:-63.50 1024 1072 1176 1328 768 771 775 798 -hsync +vsync} \
    MM_MODELINE1=${R1_MODELINE1:-} \
    bash "$MMCONF/setup_monitors.sh" >"$OUT/client-monitors.txt" 2>&1 \
    || { cat "$OUT/client-monitors.txt"; fail "client did not present 2 monitors"; }
tail -1 "$OUT/client-monitors.txt"

# --- drive one multimon AVC444 login ------------------------------------
pkill -9 -f "$FRDP.*:$PORT" 2>/dev/null; sleep 1
PW=$(cat "$CRED")
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$PORT" "/u:$SU" "/p:$PW" "/multimon" \
                        "/gfx:AVC444" "/cert:ignore" "/log-level:WARN")
setsid env DISPLAY=$CLI RDPARGS="$RDPARGS" \
    "$FRDP" /args-from:env:RDPARGS </dev/null >"$OUT/client.log" 2>&1 &
unset PW RDPARGS
echo "connected; recording for ${SECS}s ..."

# R2: sample the pod's /dev/shm for the whole session. One exec, not one
# per sample. Columns: total_bytes used_bytes (df -B1).
kubectl -n "$NS" exec "$POD" -- bash -c \
    "for i in \$(seq 1 $SECS); do df -B1 --output=size,used /dev/shm \
     | tail -1; sleep 1; done" >"$OUT/shm_samples.txt" 2>/dev/null &
SAMPLER=$!
sleep "$SECS"
wait $SAMPLER 2>/dev/null
pkill -9 -f "$FRDP.*:$PORT" 2>/dev/null

# --- collect the server-side log ----------------------------------------
# sesman starts Xorg with -logfile ~/.xorgxrdp.<display>.log (sesman.ini).
XLOG=$(kubectl -n "$NS" exec "$POD" -- \
    bash -lc "ls -t /home/$SU/.xorgxrdp.*.log 2>/dev/null | head -1")
[ -n "$XLOG" ] || fail "no session Xorg log in the pod — did the login fail? \
see $OUT/client.log"
kubectl -n "$NS" exec "$POD" -- cat "$XLOG" > "$OUT/session-xorg.log" \
    || fail "could not fetch $XLOG"
kubectl -n "$NS" logs "$POD" --tail=400 > "$OUT/pod.log" 2>&1
grep -a R1SLOT "$OUT/session-xorg.log" > "$OUT/r1slot.txt"
n=$(wc -l < "$OUT/r1slot.txt")
echo "R1SLOT records: $n  ($OUT/r1slot.txt)"
[ "$n" -gt 0 ] || fail "no R1SLOT records — the session never sent an AVC444 \
frame (check $OUT/client.log and $OUT/session-xorg.log)"

python3 "$D/r1_slot_report.py" "$OUT/r1slot.txt" | tee "$OUT/VERDICT.txt"

# --- R2: the /dev/shm floor at this geometry -----------------------------
{
    echo
    echo "=== R2 — /dev/shm at this geometry ==="
    grep -a "AllocateSharedMemory" "$OUT/session-xorg.log" | tail -3
    python3 - "$OUT/shm_samples.txt" "$OUT/session-xorg.log" <<'PY'
import re, sys
size = peak = 0
try:
    for line in open(sys.argv[1]):
        f = line.split()
        if len(f) == 2 and f[0].isdigit():
            size, used = int(f[0]), int(f[1])
            peak = max(peak, used)
except FileNotFoundError:
    pass
res = [int(m) for m in re.findall(r"AllocateSharedMemory:.*bytes (\d+)",
                                  open(sys.argv[2], errors="replace").read())]
mib = 1024.0 * 1024.0
if res:
    print("capture arena reserved by xorgxrdp : %d B (%.1f MiB)"
          % (max(res), max(res) / mib))
if size:
    print("tmpfs configured                   : %d B (%.0f MiB)"
          % (size, size / mib))
    print("peak /dev/shm used during session  : %d B (%.1f MiB)"
          % (peak, peak / mib))
    if peak and peak < size:
        print("margin                             : %.1fx headroom, "
              "%.1f MiB unused at peak" % (size / float(peak),
                                           (size - peak) / mib))
    print("R2 VERDICT: %s" % ("PASS — the session ran with the tmpfs "
                              "configured above and never came close to it"
                              if peak and peak * 2 < size else
                              "REVIEW — peak is within 2x of the tmpfs; "
                              "look before raising the geometry further"))
else:
    print("R2: no /dev/shm samples collected — RED, the floor is unmeasured")
PY
} | tee -a "$OUT/VERDICT.txt"
