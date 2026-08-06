#!/bin/sh
# i55_h1h2_uprobe.sh — BACKLOG #55/#64 H1-vs-H2 decisive instrument.
#
# Timestamps, on one perf clock, every stage of the m=1 AVC444 cycle so
# the ~120 ms period can be partitioned into named legs, and snapshots
# per-thread scheduler delay so "thread was busy" (H1: saturated Xorg
# main loop applies the xup ack late) and "thread was runnable but
# starved" (H2: CPU oversubscription) are distinguished by measurement,
# not argument.
#
# Probe points (deployed d77d054 xorgxrdp / 52b87988 xrdp; names verified
# with nm on the box — rdpClientConProcessMsgClientRegionEx is fully
# inlined in this build, so ack APPLY is observed at the socket-read
# function rdpClientConRecv.isra.0, the first entry after the xrdp-side
# ack emit):
#
#   Xorg   sched_ok  rdpScheduleDeferredUpdate.part.0  (schedule survived guard)
#   Xorg   cbfire    rdpDeferredUpdateCallback         (deferred timer fired)
#   Xorg   caprect   rdpCapRect.isra.0                 (capture decided/armed)
#   Xorg   pack      rdpCaptureGfxA2 (+%return)        (SIMD pack duration)
#   Xorg   xrecv     rdpClientConRecv.isra.0           (xup socket serviced = ack apply)
#   xrdp   pset      pump_set (+%return)               (worker: submit batch to encoder
#                                                        children + deadline wait — the
#                                                        encode wall-time block)
#   xrdp   collect   gfx_batch_collect_one             (worker: collect one child)
#   xrdp   pegfx     process_enc_egfx (+%return)       (main: EGFX assembly per enc_done)
#   xrdp   fstart    xrdp_egfx_frame_start
#   xrdp   fend      xrdp_egfx_frame_end
#   xrdp   wire      xrdp_egfx_send_data
#   xrdp   cack      xrdp_mm_egfx_frame_ack            (client EGFX frame ack arrived)
#   xup    aemit     lib_mod_frame_ack                 (xrdp emits msg106 ack to Xorg)
#
# v2 (2026-07-31): xrdp_ffmpeg_avc444_encode_pair recorded ZERO hits on
# the deployed 52b87988 — the #45 step-5/7 batch path calls pump_set, not
# encode_pair. Probes follow the code that runs, not the code that exists.
#
# The H1 quantity is aemit -> next xrecv (how long the written ack sits
# before Xorg's main loop services the fd). The H2 quantity is each
# thread's schedstat run_delay delta over the window.
#
# Scheduler-delay snapshots are TWO reads (before/after), never a polling
# loop — a sampler on this box once moved the measured rate from 47.6 to
# 71.3 ms (E5-2 protocol, gate G6).
#
# Usage (from the dev box, one non-interactive command):
#   ssh -n <t4> 'sudo sh /usr/local/bin/i55_h1h2_uprobe.sh 30 condB'
# Output: /tmp/i55_<tag>.tar  (events + schedstat + affinity + env proof)
set -u
SECS=${1:-30}
TAG=${2:-run}
U=${I55_USER:-ubuntu}
SO=/usr/lib/xorg/modules/libxorgxrdp.so
XRDP_BIN=/usr/sbin/xrdp
XUP_SO=/usr/lib/x86_64-linux-gnu/xrdp/libxup.so
D=/tmp/i55_${TAG}
rm -rf "$D"; mkdir -p "$D"

XORG_PID=$(pgrep -u "$U" -x Xorg | head -1)
[ -n "$XORG_PID" ] || { echo "ABORT: no session Xorg for $U"; exit 1; }
# every xrdp process (daemon + per-connection), its ffmpeg children, the
# payload — the whole pipeline's thread set for the schedstat snapshot
PIDS="$XORG_PID $(pgrep -x xrdp) $(pgrep -x ffmpeg) $(pgrep -f textflood | head -2)"

# an attached client is a precondition: no client -> no capture
ss -tn state established '( sport = :3389 )' | grep -q 3389 \
    || { echo "ABORT: no RDP client attached"; exit 1; }

# ---- proof of condition (quality gate 2: did the knob actually apply) --
{
  echo "tag=$TAG secs=$SECS date=$(date -u +%FT%TZ)"
  echo "nproc=$(nproc)"
  for p in $PIDS; do
      [ -d "/proc/$p" ] || continue
      printf 'affinity pid=%s comm=%s %s\n' "$p" "$(cat /proc/$p/comm)" \
             "$(taskset -pc "$p" 2>/dev/null)"
  done
  systemctl show xrdp -p CPUAffinity -p Environment 2>/dev/null
  grep -a "Matched" /var/log/xrdp.log 2>/dev/null | tail -1
} > "$D/env.txt"

snap() {
    for p in $PIDS; do
        [ -d "/proc/$p" ] || continue
        for t in /proc/"$p"/task/*; do
            [ -r "$t/schedstat" ] || continue
            printf '%s %s %s %s\n' "$p" "${t##*/}" \
                   "$(tr ' ' '_' < "$t/comm")" "$(cat "$t/schedstat")"
        done
    done
}

perf probe -d 'probe_libxorgxrdp:*' >/dev/null 2>&1
perf probe -d 'probe_xrdp:*'        >/dev/null 2>&1
perf probe -d 'probe_libxup:*'      >/dev/null 2>&1
add() { perf probe -x "$1" --add "$2" >/dev/null 2>&1 \
        || echo "note: could not probe $2" >> "$D/env.txt"; }
add "$SO" 'sched_ok=rdpScheduleDeferredUpdate.part.0'
add "$SO" 'cbfire=rdpDeferredUpdateCallback'
add "$SO" 'caprect=rdpCapRect.isra.0'
add "$SO" 'pack=rdpCaptureGfxA2'
add "$SO" 'pack_ret=rdpCaptureGfxA2%return'
add "$SO" 'xrecv=rdpClientConRecv.isra.0'
add "$XRDP_BIN" 'pset=pump_set'
add "$XRDP_BIN" 'pset_ret=pump_set%return'
add "$XRDP_BIN" 'collect=gfx_batch_collect_one'
add "$XRDP_BIN" 'pegfx=process_enc_egfx'
add "$XRDP_BIN" 'pegfx_ret=process_enc_egfx%return'
add "$XRDP_BIN" 'fstart=xrdp_egfx_frame_start'
add "$XRDP_BIN" 'fend=xrdp_egfx_frame_end'
add "$XRDP_BIN" 'wire=xrdp_egfx_send_data'
add "$XRDP_BIN" 'cack=xrdp_mm_egfx_frame_ack'
add "$XUP_SO" 'aemit=lib_mod_frame_ack'

snap > "$D/schedstat_before.txt"
perf record -a -q -o "$D/perf.data" \
    -e 'probe_libxorgxrdp:*' -e 'probe_xrdp:*' -e 'probe_libxup:*' \
    -- sleep "$SECS"
snap > "$D/schedstat_after.txt"

perf script -i "$D/perf.data" -F comm,pid,tid,time,event 2>/dev/null \
    > "$D/events.txt"
wc -l < "$D/events.txt" | xargs echo "events recorded:"

perf probe -d 'probe_libxorgxrdp:*' >/dev/null 2>&1
perf probe -d 'probe_xrdp:*'        >/dev/null 2>&1
perf probe -d 'probe_libxup:*'      >/dev/null 2>&1
rm -f "$D/perf.data"
tar -C /tmp -cf "/tmp/i55_${TAG}.tar" "i55_${TAG}"
echo "OK: /tmp/i55_${TAG}.tar"
