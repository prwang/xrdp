#!/bin/bash
# M1/M2/M3: codec linkage, encoder reach, and software-vs-hardware CPU.
set -u
XRDP_BIN="${XRDP_BIN:-/usr/sbin/xrdp}"
VAAPI_DEV="${VAAPI_DEV:-/dev/dri/renderD128}"

echo "== M1: codec libraries linked into xrdp (expect none) =="
ldd "$XRDP_BIN" 2>/dev/null | grep -iE "x264|avcodec|openh264|x265|vpx" \
    || echo "  none (codec runs as a subprocess)"
echo "  xrdp binary: $(stat -c%s "$XRDP_BIN") bytes"

echo "== M2: H.264/HEVC encoders reachable via encoder_args (no xrdp change) =="
ffmpeg -hide_banner -encoders 2>/dev/null \
    | grep -iE "\b(h264|hevc)_|libx264|libx265|libopenh264" | sed 's/^/  /'

echo "== M3: software libx264 vs hardware h264_vaapi (1080p60 x10s) =="
for enc in x264 vaapi; do
    if [ "$enc" = x264 ]; then
        args=(-pix_fmt nv12 -c:v libx264 -preset ultrafast -tune zerolatency
              -crf 18 -bf 0)
    else
        args=(-vaapi_device "$VAAPI_DEV" -vf format=nv12,hwupload
              -c:v h264_vaapi -rc_mode CQP -qp 20 -bf 0)
        # -vaapi_device is an input option; place source after it
    fi
    if [ "$enc" = vaapi ]; then
        out=$(ffmpeg -y -hide_banner -benchmark -vaapi_device "$VAAPI_DEV" \
            -f lavfi -i testsrc2=s=1920x1080:r=60:d=10 \
            -vf format=nv12,hwupload -c:v h264_vaapi -rc_mode CQP -qp 20 -bf 0 \
            -f mp4 "/tmp/enc_$enc.mp4" 2>&1)
    else
        out=$(ffmpeg -y -hide_banner -benchmark \
            -f lavfi -i testsrc2=s=1920x1080:r=60:d=10 \
            "${args[@]}" -f mp4 "/tmp/enc_$enc.mp4" 2>&1)
    fi
    printf "  %-6s %s  size=%s\n" "$enc" \
        "$(echo "$out" | grep -m1 '^bench: utime')" \
        "$(stat -c%s "/tmp/enc_$enc.mp4")"
done
