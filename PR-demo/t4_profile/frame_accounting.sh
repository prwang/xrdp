#!/bin/sh
# T4 end-to-end FRAME accounting: measures delivered fps and partitions
# the per-frame cycle across pipeline stages using uprobes on the
# DEPLOYED binaries (no restart, no redeploy, no config change).
# Complements profile_owner_load.sh (cpu%), which is a supply-side
# metric and cannot explain a delivered-fps ceiling.
#
# Probes (installed idempotently; passive when not recording):
#   Xorg  cap_a2    rdpCaptureGfxA2            capture entry
#   Xorg  ack_rx    rdpClientConProcessMsgClientRegionEx  xup ack receipt
#   xrdp  enc_pair  xrdp_ffmpeg_avc444_encode_pair        encode entry
#   xrdp  enc_ret   ...%return                            encode return
#   xrdp  wire_send xrdp_egfx_send_data                   frame on wire
#   xrdp  egfx_ack  xrdp_mm_egfx_frame_ack q/f/decoded    client GFX ack
# Also samples the RDP TCP socket (and any :22 tunnel) via ss -ti to
# rule the network in or out: sustained Send-Q / rising rtt = network;
# Send-Q ~0 at low utilization = server-paced.
#
# Requires: owner RDP client ATTACHED, visible Thunar, linux-tools,
# binaries built with debug info (our dev debs are).
#
# Reference result (2026-07-26, xrdp 52099149 + xorgxrdp ee1ec01, nvenc,
# owner load on bottom 4K monitor): 20.1 fps at EVERY stage, zero drops,
# client queue_depth=0 on all acks, wire ~8 Mbit/s of ~98 Mbit/s
# available, Send-Q ~0.  Cycle p50 ~52ms = 5.6 cap->enc + 30.1 ENCODE
# (main+aux 4K nvenc, synchronous) + <1 send + ~5 ack->next-cap.
# The synchronous dual-4K encode is the fps ceiling; capture cannot
# overlap it (single shmem buffer, borrowed by vmsplice during encode).
T4=${T4:-ubuntu@3.86.96.223}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
DISP=${DISP:-:10}
XAUTH=${XAUTH:-/var/run/xrdp/1000/Xauthority}
# session user driving the orbit (the offscreen rig runs as tester)
SESS_USER=${SESS_USER:-ubuntu}
SECS=${SECS:-12}
ORBIT_X=${ORBIT_X:-670}
ORBIT_Y=${ORBIT_Y:-1740}
ORBIT_R=${ORBIT_R:-280}
REVS=${REVS:-20}

set -e
ssh -i "$T4_KEY" "$T4" DISP="$DISP" XAUTH="$XAUTH" SECS="$SECS" \
    SESS_USER="$SESS_USER" \
    ORBIT_X="$ORBIT_X" ORBIT_Y="$ORBIT_Y" ORBIT_R="$ORBIT_R" \
    REVS="$REVS" 'bash -s' <<'REMOTE'
set -e
export DISPLAY=$DISP XAUTHORITY=$XAUTH
if ! ss -tn state established '( sport = :3389 )' | grep -q 3389; then
    echo "ABORT: no RDP client attached — nothing will be encoded"
    exit 1
fi
XRDP_BIN=/usr/sbin/xrdp
XORGXRDP_SO=/usr/lib/xorg/modules/libxorgxrdp.so
# delete-then-add: a probe installed against a PREVIOUS binary keeps its
# stale file offsets and silently records nothing after a redeploy
probe_add() { b=${1##*/}; b=${b%%.*}
              sudo perf probe -d "probe_${b}:${2%%=*}*" >/dev/null 2>&1
              sudo perf probe -x "$1" "$2" >/dev/null 2>&1 || true; }
probe_add "$XRDP_BIN" "enc_pair=xrdp_ffmpeg_avc444_encode_pair"
probe_add "$XRDP_BIN" "enc_ret=xrdp_ffmpeg_avc444_encode_pair%return"
probe_add "$XRDP_BIN" "wire_send=xrdp_egfx_send_data"
probe_add "$XRDP_BIN" \
    "egfx_ack=xrdp_mm_egfx_frame_ack queue_depth frame_id frames_decoded"
probe_add "$XORGXRDP_SO" "cap_a2=rdpCaptureGfxA2"
probe_add "$XORGXRDP_SO" "ack_rx=rdpClientConProcessMsgClientRegionEx"
XD="sudo -u $SESS_USER env DISPLAY=$DISPLAY XAUTHORITY=$XAUTHORITY"
WID=$($XD xdotool search --onlyvisible --class thunar | head -1)
if [ -z "$WID" ]; then
    echo "no visible Thunar; launching one"
    $XD nohup thunar >/dev/null 2>&1 &
    sleep 4
    WID=$($XD xdotool search --onlyvisible --class thunar | head -1)
fi
$XD xdotool windowactivate --sync $WID
$XD bash -c '
python3 -c "
import math
for rev in range('"$REVS"'):
    for s in range(40):
        t = 2*math.pi*s/40
        print(int('"$ORBIT_X"'+'"$ORBIT_R"'*math.cos(t)),
              int('"$ORBIT_Y"'+'"$ORBIT_R"'*math.sin(t)))
" | while read x y; do xdotool windowmove '"$WID"' $x $y; sleep 0.02; done' \
    2>/dev/null &
DRAG=$!
sleep 3
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
kill $DRAG 2>/dev/null || true
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
function P(a, n, name) { asort(a);
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
REMOTE
