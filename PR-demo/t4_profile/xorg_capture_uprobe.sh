#!/bin/sh
# xorg_capture_uprobe.sh — count the calls the capture path actually makes,
# per second, on the running session Xorg.
#
# Why counts and not just a profile (BACKLOG #55/#59): the perf profile of
# the T4's saturated Xorg showed ~19 % of its cycles in pixman_blt reached
# through the WRAPPED GC op rdpCopyArea from a timer, which is what
# rdpCopyBoxList() looks like — but Xorg on Ubuntu ships with **no symtab**
# (`nm /usr/lib/xorg/Xorg` = 0), so the six frames between WaitForSomething
# and rdpCopyArea are bare addresses and the attribution is inference. The
# xorgxrdp module IS unstripped, so a uprobe on the two capture functions
# settles it by counting: if rdpCopyBoxList fires about as often as
# a8r8g8b8_to_avc444_box, the hw->sw staging copy is in the hot path.
#
# Needs root (uprobes). Run it while a client is connected and capturing:
#   ssh -n -i $KEY $T4 'sudo sh /tmp/xorg_capture_uprobe.sh 30'
set -u
SECS=${1:-30}
U=${E52_USER:-ubuntu}
SO=${E52_XORGXRDP_SO:-/usr/lib/xorg/modules/libxorgxrdp.so}

# Wait for the session the measurement run is about to create. The harness
# logs the previous session OFF first (E_COLD=1), so a pid picked up front
# is the pid that is about to die: a first attempt attached to it and
# counted a flat zero on every probe, which reads exactly like "the copy
# path is not taken" (2026-07-30). Always start this AFTER a logoff.
n=0
while [ "$n" -lt 90 ]; do
    p=$(pgrep -u "$U" -x Xorg | head -1)
    [ -n "$p" ] && break
    n=$((n + 1))
    sleep 1
done
[ -n "${p:-}" ] || { echo "no session Xorg for $U after 90 s"; exit 1; }
sleep 8   # let the session settle so the count is steady-state capture

perf probe -x "$SO" --del 'probe_libxorgxrdp:*' >/dev/null 2>&1
for fn in rdpCopyBoxList a8r8g8b8_to_avc444_box rdpCapture; do
    perf probe -x "$SO" --add "$fn" >/dev/null 2>&1 \
        || echo "note: could not probe $fn (inlined or absent)"
done

echo "counting for ${SECS}s on xorg pid=$p"
perf stat -e 'probe_libxorgxrdp:*' -p "$p" -- sleep "$SECS" 2>&1 \
    | grep -E "probe_libxorgxrdp|seconds time elapsed"

perf probe -x "$SO" --del 'probe_libxorgxrdp:*' >/dev/null 2>&1
