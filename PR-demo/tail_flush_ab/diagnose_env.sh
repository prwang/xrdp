#!/bin/bash
# Run ON THE DEPLOYMENT BOX (the one that shows the withhold) to pin down WHY
# async_depth=1 is or isn't enough there. Prints the GPU/VAAPI identity and
# measures the encoder's pipeline depth directly (independent of xrdp), so we
# can tell "encoder holds a frame even at async_depth 1" from "something in xrdp
# holds it". No live session needed.
set -u
RENDER=${1:-/dev/dri/renderD128}
echo "=================== GPU / VAAPI identity ==================="
for n in /sys/class/drm/renderD*/device/uevent; do
    [ -f "$n" ] && { echo "-- $n --"; grep -E 'DRIVER|PCI_ID|PCI_SLOT' "$n"; }
done
echo "-- vainfo ($RENDER) --"
vainfo --display drm --device "$RENDER" 2>&1 | grep -iE "Driver version|VAProfileH264(Main|High|ConstrainedBaseline) *: *VAEntrypointEncSlice" | sort -u
echo "-- ffmpeg vaapi encoders --"
ffmpeg -hide_banner -encoders 2>/dev/null | grep -iE "vaapi|nvenc|qsv" | grep -i 264
echo
echo "============ encoder pipeline depth (the key measurement) ============"
echo "withheld-while-idle frames == pipeline depth. 0 => async_depth 1 is enough"
echo "here; >0 => this driver keeps frames in flight and you need tail_flush=true"
echo "(or a software libx264 -tune zerolatency encoder)."
echo
XRDP_PROBE_RENDER="$RENDER" python3 "$(dirname "$0")/ffmpeg_pipeline_depth_probe.py"
