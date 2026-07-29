#!/bin/bash
# r1r2_target_geometry.sh — run recon gates R1 and R2 (BACKLOG #45) at the
# E3 TARGET layout: 2560x1440 + 3840x2400, offscreen.
#
# This is r1_slot_recon.sh with the target geometry pinned, so the two
# gates are answered at the size the acceptance gate is actually measured
# at rather than at the 2x1024x768 development layout:
#
#   R1  which capture slot each monitor lands in, per send
#   R2  the /dev/shm floor — what xorgxrdp reserves for the capture arena
#       at this geometry, and the peak tmpfs use across the session
#
# The arena is exactly w*h*1.5 * 2 views * 2 slots summed over monitors:
#   2560x1440 -> 22,118,400 B, 3840x2400 -> 55,296,000 B, total 77,414,400 B
#   (73.8 MiB). Measured against the pod's 1Gi tmpfs (k8s/arm-q.yaml).
#
# Client stays on the host (dummy X server on its own display); nothing is
# installed into the pod. Usage: r1r2_target_geometry.sh [seconds]
set -u
D=$(cd "$(dirname "$0")" && pwd)
exec env \
    R1_XORG_CONF="$D/../multimon_offline/xorg-dummy-2mon-4k.conf" \
    R1_DISPLAY="${R1_DISPLAY:-:94}" \
    R1_MODE0=2560x1440_60 \
    R1_MODE1=3840x2400R \
    R1_POS1=2560x0 \
    R1_MODELINE0="312.25 2560 2752 3024 3488 1440 1443 1448 1493 -hsync +vsync" \
    R1_MODELINE1="592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync" \
    R1_OUT="${R1_OUT:-$D/captures/r1r2_target_$(date +%Y%m%d_%H%M%S)}" \
    bash "$D/r1_slot_recon.sh" "${1:-60}"
