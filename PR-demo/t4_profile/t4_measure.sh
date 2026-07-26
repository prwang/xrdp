#!/bin/bash
# t4_measure.sh — PERSISTENT on-T4 frame accounting (methodology rules in
# CLAUDE.md "T4 test methodology"). Deployed to /usr/local/bin/t4_measure.sh
# by PR-demo/t4_profile/frame_accounting.sh (checksum-gated scp); invoked as
# ONE non-interactive command:
#
#   ssh <t4> t4_measure.sh            # orbit drag + 12s accounting
#   ssh <t4> NO_DRAG=1 t4_measure.sh  # passive accounting of live traffic
#
# Measures delivered fps and partitions the per-frame cycle across pipeline
# stages using uprobes on the DEPLOYED binaries (no restart, no redeploy).
# Probes are (re)installed delete-then-add on every run: a probe installed
# against a previous binary keeps stale file offsets and silently records
# nothing after a redeploy.
#
# Assumptions: run as ubuntu (passwordless sudo); the session under test is
# ubuntu's own xrdp session (owner directive: no special test users); an RDP
# client is attached (no client -> no capture -> nothing to measure).
#
# Env knobs: SECS (12) recording window; ORBIT_X/Y/R (670/1740/280, the
# bottom-4K orbit of the owner's stacked dual-monitor layout) and REVS (40)
# for the thunar drag; NO_DRAG=1 to skip driving the session.
set -e
SECS=${SECS:-12}
ORBIT_X=${ORBIT_X:-670}
ORBIT_Y=${ORBIT_Y:-1740}
ORBIT_R=${ORBIT_R:-280}
REVS=${REVS:-40}
NO_DRAG=${NO_DRAG:-0}

XRDP_BIN=/usr/sbin/xrdp
XORGXRDP_SO=/usr/lib/xorg/modules/libxorgxrdp.so

if ! ss -tn state established '( sport = :3389 )' | grep -q 3389; then
    echo "ABORT: no RDP client attached — nothing will be encoded"
    exit 1
fi

# session display: the non-console Xorg this user owns
DISP=$(pgrep -a -u "$(id -u)" -x Xorg | grep -oE ' :[0-9]+ ' | head -1 \
       | tr -d ' ')
if [ -z "$DISP" ]; then
    echo "ABORT: no xrdp session Xorg for user $(whoami)"
    exit 1
fi
XAUTH=/var/run/xrdp/$(id -u)/Xauthority
export DISPLAY=$DISP XAUTHORITY=$XAUTH
echo "session display=$DISP user=$(whoami)"

# Quiet gate: fresh xfce logins AND app launches (thunar) fire multi-
# second all-core CPU storms of sandboxed glycin-svg icon loaders
# (measured 2026-07-26: ~10 loaders, ~50 CPU-s, idle pinned to 0%; a
# single loader still burns ~65% of a core) which would contaminate any
# measurement on this 4-core box. Called at start AND again right
# before recording (after thunar launch — which spawns its own storm).
# Additionally refuses while any glycin loader is alive, storming or
# not. Aborts loudly rather than measure through noise; does not alter
# the session environment.
quiet_wait() {
    local quiet=0 idle i
    for i in $(seq 1 60); do
        idle=$(top -b -n1 | grep '%Cpu' | head -1 | grep -oE '[0-9.]+ id' \
               | cut -d' ' -f1 | cut -d. -f1)
        if [ "${idle:-0}" -ge 85 ] && ! pgrep -f glycin-loaders >/dev/null
        then
            quiet=$((quiet + 1))
            [ "$quiet" -ge 3 ] && { echo "quiet gate ($1): idle=${idle}%"
                                    return 0; }
        else
            quiet=0
        fi
        sleep 2
    done
    echo "ABORT: CPU never went quiet at $1 (login storm or foreign load)"
    exit 1
}
quiet_wait "session"

probe_add() { b=${1##*/}; b=${b%%.*}
              sudo perf probe -d "probe_${b}:${2%%=*}*" >/dev/null 2>&1 || true
              sudo perf probe -x "$1" "$2" >/dev/null 2>&1 || true; }
probe_add "$XRDP_BIN" "enc_pair=xrdp_ffmpeg_avc444_encode_pair"
probe_add "$XRDP_BIN" "enc_ret=xrdp_ffmpeg_avc444_encode_pair%return"
probe_add "$XRDP_BIN" "wire_send=xrdp_egfx_send_data"
probe_add "$XRDP_BIN" \
    "egfx_ack=xrdp_mm_egfx_frame_ack queue_depth frame_id frames_decoded"
probe_add "$XORGXRDP_SO" "cap_a2=rdpCaptureGfxA2"
probe_add "$XORGXRDP_SO" "ack_rx=rdpClientConProcessMsgClientRegionEx"

if [ "$NO_DRAG" != "1" ]; then
    # thunar maps several tiny helper windows (10x10, 2x2) that match
    # --class thunar; moving one of those generates no damage (measured
    # 0.4 fps, 2026-07-26). Require real geometry.
    pick_thunar() {
        for w in $(xdotool search --onlyvisible --class thunar 2>/dev/null)
        do
            eval "$(xdotool getwindowgeometry --shell "$w" 2>/dev/null)"
            if [ "${WIDTH:-0}" -ge 300 ]; then echo "$w"; return; fi
        done
    }
    WID=$(pick_thunar)
    if [ -z "$WID" ]; then
        echo "no visible Thunar; launching one"
        setsid thunar </dev/null >/dev/null 2>&1 &
        sleep 4
        WID=$(pick_thunar)
    fi
    [ -z "$WID" ] && { echo "ABORT: no real-size Thunar window"; exit 1; }
    echo "orbiting thunar WID=$WID"
    # thunar's own icon loading spawns another glycin storm; re-gate
    # before the recorded window
    quiet_wait "pre-record"
    xdotool windowactivate --sync "$WID" >/dev/null 2>&1 || true
    # ONE xdotool process with the whole orbit as chained argv commands:
    # spawning xdotool per move costs ~100ms under load and caps the
    # OFFERED move rate below the pipeline ceiling (measured 2026-07-26:
    # 9 moves/s offered -> 9.3 fps delivered, pipeline idle).
    # --sync is REQUIRED: without it this xdotool build leaves every
    # windowmove in Xlib's output buffer until process exit, so the
    # sleeps pace nothing and all moves land as one burst after the
    # recording window (measured 0.3 fps, 2026-07-26 on the new T4);
    # --sync round-trips per move, flushing each one on schedule.
    CHAIN=$(python3 -c "
import math
out = []
for rev in range($REVS):
    for s in range(40):
        t = 2*math.pi*s/40
        out.append('windowmove --sync $WID %d %d sleep 0.016' % (
                   int($ORBIT_X+$ORBIT_R*math.cos(t)),
                   int($ORBIT_Y+$ORBIT_R*math.sin(t))))
print(' '.join(out))")
    xdotool $CHAIN 2>/dev/null &
    DRAG=$!
    sleep 3
fi

(for i in $(seq 0 "$SECS"); do
    echo "T=$i"
    ss -tin state established '( sport = :3389 )' | tail -1
    sleep 1
 done) > /tmp/ss_frame_acct.log 2>&1 &
SS=$!
sudo perf record -a -q -o /tmp/frame_acct.perf \
    -e probe_xrdp:egfx_ack -e probe_xrdp:enc_pair \
    -e probe_xrdp:enc_ret__return -e probe_xrdp:wire_send \
    -e probe_libxorgxrdp:cap_a2 -e probe_libxorgxrdp:ack_rx \
    -- sleep "$SECS"
wait $SS
[ "$NO_DRAG" != "1" ] && kill $DRAG 2>/dev/null || true

sudo perf script -i /tmp/frame_acct.perf | awk '
{
    for (i = 1; i <= NF; i++)
    {
        if ($i ~ /^[0-9]+\.[0-9]+:$/) { t = $i; sub(/:$/, "", t) }
        if ($i ~ /^probe_/) { e = $i; sub(/:$/, "", e) }
    }
    printf "%.6f %s\n", t, e
}' | sort -n > /tmp/frame_acct.txt
echo "=== per-stage event rate (frames/s over ${SECS}s) ==="
awk -v s="$SECS" '{c[$2]++} END {for (k in c) printf "%-34s %d (%.1f/s)\n", k, c[k], c[k]/s}' \
    /tmp/frame_acct.txt | sort
echo "=== cycle partition (ms) ==="
gawk '
function S(a, n,   i, s) { s = 0; for (i = 1; i <= n; i++) { s += a[i] } return s / n }
function P(a, n, name) { if (n < 1) { printf "%-22s n=0\n", name; return }
    asort(a);
    printf "%-22s n=%d mean=%.1f p50=%.1f p90=%.1f\n", \
        name, n, S(a, n), a[int(n / 2)], a[int(n * 0.9)] }
$2 == "probe_libxorgxrdp:cap_a2" { cap = $1 }
$2 == "probe_xrdp:enc_pair" { enc = $1; if (cap) { d1[++n1] = ($1 - cap) * 1000 } }
$2 ~ /enc_ret/ { if (enc) { d2[++n2] = ($1 - enc) * 1000 } er = $1 }
$2 == "probe_xrdp:wire_send" { if (er) { ws = $1 } }
$2 == "probe_xrdp:egfx_ack" { if (ws) { d3[++n3] = ($1 - ws) * 1000 } }
$2 == "probe_libxorgxrdp:ack_rx" {
    if (ws) { d4[++n4] = ($1 - ws) * 1000 }
    if (ar) { pd[++n5] = ($1 - ar) * 1000 } ar = $1 }
END { P(d1, n1, "cap->enc_entry"); P(d2, n2, "ENCODE duration");
      P(d4, n4, "wire_send->ack_rx"); P(d3, n3, "wire_send->client_ack");
      P(pd, n5, "frame period") }' /tmp/frame_acct.txt
echo "=== client decode backlog (queue_depth histogram) ==="
sudo perf script -i /tmp/frame_acct.perf | grep -o "queue_depth=[0-9a-fx]*" \
    | sort | uniq -c | sort -rn | head -5
echo "=== wire Mbit/s (per ~1s) ==="
grep -o "bytes_sent:[0-9]*" /tmp/ss_frame_acct.log \
    | awk -F: 'NR > 1 { printf "%.1f ", ($2 - p) * 8 / 1e6 } { p = $2 } END { print "" }'
