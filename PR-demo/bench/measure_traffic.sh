#!/bin/bash
# Measure AVC444 (main+aux) vs AVC420 (main-only) intra-frame encoded bytes,
# using xrdp's real converter (via /tmp/conv_measure) + the default libx264
# encoder_args, for one or more source PNGs.
set -eu
ENC=(-c:v libx264 -bf 0 -preset ultrafast -tune zerolatency -crf 18 -g 240
     -x264-params repeat-headers=1)

measure() {
    local png="$1" label="$2"
    local W H
    W=$(python3 -c "from PIL import Image;print(Image.open('$png').size[0])")
    H=$(python3 -c "from PIL import Image;print(Image.open('$png').size[1])")
    # PNG -> raw BGRA (converter reads a8r8g8b8 as native LE = B,G,R,A bytes)
    ffmpeg -y -hide_banner -loglevel error -i "$png" -pix_fmt bgra \
        -f rawvideo /tmp/in.bgra
    local info; info=$(/tmp/conv_measure /tmp/in.bgra "$W" "$H" \
        /tmp/main.nv12 /tmp/aux.nv12)
    local CW CH
    CW=$(echo "$info" | sed -n 's/coded \([0-9]*\)x.*/\1/p')
    CH=$(echo "$info" | sed -n 's/coded [0-9]*x\([0-9]*\).*/\1/p')
    # encode each NV12 view as one intra picture, Annex-B
    for v in main aux; do
        ffmpeg -y -hide_banner -loglevel error -f rawvideo -pixel_format nv12 \
            -video_size "${CW}x${CH}" -i /tmp/$v.nv12 -frames:v 1 "${ENC[@]}" \
            -bsf:v h264_mp4toannexb -f h264 /tmp/$v.264
    done
    local m a
    m=$(stat -c%s /tmp/main.264); a=$(stat -c%s /tmp/aux.264)
    printf "%-22s %5dx%-4d  main(420)=%7d  aux=%7d  444=main+aux=%7d  444/420=%.2fx  aux/main=%.0f%%\n" \
        "$label" "$W" "$H" "$m" "$a" "$((m + a))" \
        "$(python3 -c "print(($m+$a)/$m)")" \
        "$(python3 -c "print(100*$a/$m)")"
}

for arg in "$@"; do
    measure "${arg%%:*}" "${arg##*:}"
done
