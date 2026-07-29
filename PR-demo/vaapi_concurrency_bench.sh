#!/bin/sh
# vaapi_concurrency_bench.sh — how much does concurrency actually buy?
#
# The question this answers, BEFORE spending money on a cloud GPU box:
# the AVC444 pair encodes main then aux SERIALLY, and multi-monitor
# encodes each monitor serially after that, so m monitors cost
# 2m*(w+e) (PRD "Concurrency state of the encode pipeline"). Task #45
# step 5 replaces that with one poll set over all children. The gain is
# bounded by how well the GPU actually runs N concurrent H.264 encodes —
# which is a property of the silicon and the driver, not of our code.
#
# So: measure N=1,2,4 concurrent encodes at a fixed resolution and
# report ms/frame per stream. N=2 models one monitor's main+aux; N=4
# models two monitors. If ms/frame stays flat as N grows, concurrency is
# free and the pump_set work pays off in full. If it scales linearly,
# the encoders are already saturating the engine and #45 step 5 buys
# nothing but latency smoothing.
#
# Deliberately mirrors the shipped child's argv (rawvideo NV12 in, NUT
# out, h264_mp4toannexb) so the numbers transfer to the runner. Input is
# a pre-generated raw file rather than a pipe: this isolates ENCODE cost
# from the vmsplice feed, which is measured separately.
#
# Usage:
#   PR-demo/vaapi_concurrency_bench.sh                    # default sweep
#   W=3840 H=2400 FRAMES=120 PR-demo/vaapi_concurrency_bench.sh
set -eu

W=${W:-1920}
H=${H:-1088}
FRAMES=${FRAMES:-180}
DEV=${DEV:-/dev/dri/renderD128}
FFMPEG=${FFMPEG:-/usr/bin/ffmpeg}
OUT=${OUT:-/tmp/vaapi_bench.$$}
CONCURRENCY=${CONCURRENCY:-"1 2 4"}

command -v "$FFMPEG" >/dev/null || { echo "ABORT: no $FFMPEG" >&2; exit 1; }
[ -e "$DEV" ] || { echo "ABORT: no render node $DEV" >&2; exit 1; }

mkdir -p "$OUT"
trap 'rm -rf "$OUT"' EXIT

RAW="$OUT/in_${W}x${H}.nv12"
echo "generating $FRAMES frames of ${W}x${H} NV12 ..."
timeout 600 "$FFMPEG" -hide_banner -loglevel error \
    -f lavfi -i "testsrc=size=${W}x${H}:rate=30" \
    -frames:v "$FRAMES" -pix_fmt nv12 -f rawvideo -y "$RAW"

# one encoder, same shape as spawn_child(): rawvideo NV12 -> h264_vaapi
# -> NUT on stdout, zero-latency knobs, one reference (the FR-H264-8
# guard requires num_ref_idx_l0_default == 0)
run_one() {
    timeout 900 "$FFMPEG" -hide_banner -nostdin -loglevel error \
        -f rawvideo -pixel_format nv12 -video_size "${W}x${H}" \
        -framerate 30 -i "$1" \
        -map 0:v:0 -an -sn -dn -fps_mode passthrough \
        -vaapi_device "$DEV" -vf 'format=nv12,hwupload' \
        -c:v h264_vaapi -rc_mode CQP -qp 20 -bf 0 -async_depth 1 \
        -g 30000 -refs 1 \
        -bsf:v h264_mp4toannexb -flush_packets 1 -write_index 0 \
        -f nut -y "$2"
}

printf '\n%-3s %-12s %-12s %-12s %s\n' \
    N "wall_ms" "ms/frame" "fps/stream" "verdict vs N=1"
base=""
for n in $CONCURRENCY; do
    i=0
    start=$(date +%s%N)
    while [ "$i" -lt "$n" ]; do
        run_one "$RAW" "$OUT/out_$i.nut" &
        i=$((i + 1))
    done
    wait
    end=$(date +%s%N)
    wall=$(( (end - start) / 1000000 ))
    per=$(awk "BEGIN{printf \"%.2f\", $wall/$FRAMES}")
    fps=$(awk "BEGIN{printf \"%.1f\", $FRAMES*1000/$wall}")
    if [ -z "$base" ]; then
        base=$per
        verdict="baseline"
    else
        verdict=$(awk "BEGIN{r=$per/$base; \
            printf \"%.2fx slower/stream (1.00 = concurrency free)\", r}")
    fi
    printf '%-3s %-12s %-12s %-12s %s\n' "$n" "$wall" "$per" "$fps" "$verdict"
done

echo
echo "Reading this: N=2 models ONE monitor's main+aux pair; N=4 models"
echo "TWO monitors. A ratio near 1.00 means the engine absorbs the extra"
echo "streams and #45 step 5 converts today's 2m*(w+e) into ~(w+e). A"
echo "ratio near N means the engine is already saturated and the win is"
echo "latency shaping only, not throughput."
