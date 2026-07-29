#!/bin/bash
# make_cut_children.sh -- regenerate the child elementary streams the
# BACKLOG #45 scheduled-paired-cut vectors are spliced from.
#
# Same shape as the FR-H264-8 vector children already committed under
# ltr_vectors/run1 (64x64, libx264, refs=1, CABAC, poc_type=2, no
# B frames, log2_max_frame_num = 4) but LONGER: the cut sequence needs
# five aux pictures, and those children carry only three.
#
# The two views differ in content only -- their SPS/PPS parse fields
# must be identical or the splicer's main/aux compatibility guard
# refuses the pair (that guard is part of what the vectors pin).
#
# Usage: make_cut_children.sh [outdir]   (default ltr_vectors/cut)
set -eu
D=$(cd "$(dirname "$0")" && pwd)
OUT="${1:-$D/ltr_vectors/cut}"
FRAMES="${FRAMES:-8}"
mkdir -p "$OUT"

enc()
{
    local src="$1"
    local dst="$2"

    ffmpeg -y -hide_banner -loglevel error \
        -f lavfi -i "$src" \
        -frames:v "$FRAMES" -pix_fmt nv12 \
        -c:v libx264 -preset ultrafast -tune zerolatency \
        -profile:v high -bf 0 -refs 1 -crf 18 -g 300 \
        -x264-params "bframes=0:ref=1:cabac=1:repeat-headers=1:aud=0" \
        -f h264 "$dst"
}

enc "testsrc2=size=64x64:rate=60" "$OUT/main_child.h264"
enc "mandelbrot=size=64x64:rate=60" "$OUT/aux_child.h264"

for f in "$OUT/main_child.h264" "$OUT/aux_child.h264"; do
    python3 "$D/../../tools/avc444_ltr_wire_audit.py" --annexb "$f" \
        "$(basename "$f")" | sed -n '1,8p;/GUARD VERDICT/p'
done
