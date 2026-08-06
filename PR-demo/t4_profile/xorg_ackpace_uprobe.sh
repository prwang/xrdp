#!/bin/sh
# xorg_ackpace_uprobe.sh — BACKLOG #64c decisive probe: WHY is capture
# ack-paced when every readable gate admits a second capture?
#
# Counts and TIMESTAMPS, on the live session Xorg, the five events that
# bracket the contradiction:
#
#   rdpClientConProcessMsgClientRegionEx   ack arrival (the observed pacer)
#   rdpScheduleDeferredUpdate              every schedule attempt (damage,
#                                          ack, callback tail) — may be
#                                          inlined; its absence is noted,
#                                          not fatal
#   rdpDeferredUpdateCallback              timer actually fired
#   rdpCapRect                             callback decided to capture
#   rdpCapture                             pixels actually captured
#
# The split it decides (#64c hypotheses):
#   callback rate ~= capture rate (~8/s)  -> the timer never fires while a
#     frame is outstanding: scheduling is being swallowed (updateScheduled
#     held TRUE across the encode window) and only the ack's schedule
#     materializes. H1.
#   callback rate >> capture rate         -> the timer fires and an
#     invisible-at-INFO gate refuses: compare rdpCapRect (top/capacity
#     gate refused) vs rdpCapture (empty dirty intersect). H2.
#
# perf timestamps share one clock, so callback-vs-ack phase is measured
# directly; the full timeline is printed for offline correlation.
#
# Needs root (uprobes). The module must be unstripped (it is; the #59
# probe relied on the same). ALWAYS start after a logoff: a pid picked
# up front is the pid about to die (2026-07-30 lesson, inherited from
# xorg_capture_uprobe.sh).
#
#   ssh -n -i $KEY $T4 'sudo sh /usr/local/bin/xorg_ackpace_uprobe.sh 45'
set -u
SECS=${1:-45}
U=${E52_USER:-ubuntu}
SO=${E52_XORGXRDP_SO:-/usr/lib/xorg/modules/libxorgxrdp.so}
DATA=/tmp/ackpace.perf.data

n=0
while [ "$n" -lt 90 ]; do
    p=$(pgrep -u "$U" -x Xorg | head -1)
    [ -n "$p" ] && break
    n=$((n + 1))
    sleep 1
done
[ -n "${p:-}" ] || { echo "no session Xorg for $U after 90 s"; exit 1; }
sleep 8   # settle past login churn: steady-state pacing is the question

perf probe -x "$SO" --del 'probe_libxorgxrdp:*' >/dev/null 2>&1
# Names as they exist in the deployed d77d054 module (checked with nm):
#   rdpScheduleDeferredUpdate.part.0 — GCC split the function: the
#     `if (updateScheduled) return;` guard is INLINED at every call
#     site and .part.0 is only entered when a timer is genuinely
#     armed. So this probe counts schedules that SURVIVED the guard —
#     exactly the discriminator H1 needs, better than the full
#     function would have been.
#   rdpCapRect.isra.0 — same body, ISRA-optimised signature.
#   rdpClientConProcessMsgClientRegionEx is fully inlined; ack arrival
#     is recovered offline from the xrdp trace (last=1 + ~1 ms).
for fn in 'rdpScheduleDeferredUpdate.part.0' rdpDeferredUpdateCallback \
          'rdpCapRect.isra.0' rdpCapture; do
    perf probe -x "$SO" --add "$fn" >/dev/null 2>&1 \
        || echo "note: could not probe $fn (inlined or absent)"
done
# ACKPACE_VARS=1: also log rect_id / rect_id_ack AT CALLBACK ENTRY, so a
# refused entry shows the outstanding count that refused it (at m=1,
# outstanding == rect_id - rect_id_ack). The callback's third parameter
# is `pointer arg` (void *), so DWARF cannot deref it; the offsets are
# read from the SAME COMMIT's local build with gdb
# (offsetof(_rdpClientCon, rect_id) = 0x12a80, rect_id_ack = 0x12a84 —
# layout is source+ABI determined, flag-independent) and arg arrives in
# %dx per SysV. Recompute the offsets if the struct changes.
if [ "${ACKPACE_VARS:-0}" = 1 ]; then
    perf probe -x "$SO" --add \
        'cbvars=rdpDeferredUpdateCallback rid=+0x12a80(%dx):s32 rack=+0x12a84(%dx):s32' \
        >/dev/null 2>&1 || echo "note: could not add cbvars probe"
fi

echo "recording ${SECS}s on xorg pid=$p"
perf record -o "$DATA" -q -p "$p" -e 'probe_libxorgxrdp:*' -- sleep "$SECS"

echo "== counts =="
perf script -i "$DATA" -F event 2>/dev/null | sort | uniq -c | sort -rn
echo "== timeline (ts event trace) =="
perf script -i "$DATA" -F time,event,trace 2>/dev/null

perf probe -x "$SO" --del 'probe_libxorgxrdp:*' >/dev/null 2>&1
rm -f "$DATA"
