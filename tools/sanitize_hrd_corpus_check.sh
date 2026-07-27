#!/bin/bash
# sanitize_hrd_corpus_check.sh — offline closure test for "hypothesis B"
# (2026-07-27 Mac 444 color bisect): prove xrdp_h264_sanitize_hrd() is
# byte-perfect on a corpus of real encoder SPS shapes, not just the two
# golden vectors in tests/xrdp.
#
# For every generated stream (VAAPI CBR across a resolution sweep that
# shifts the VUI-tail bit phase, plus libx264 configured nvenc-shaped:
# nal-hrd=cbr, level 5.2, ref 3), three assertions:
#   1. FIELD EXACTNESS: trace_headers SPS field list after sanitize ==
#      field list before, minus exactly the HRD block (nal_hrd flag 1->0,
#      hrd structure, low_delay_hrd_flag). Catches any bit-shift bleeding
#      into bitstream_restriction / max_dec_frame_buffering etc.
#   2. PIXEL EXACTNESS: ffmpeg framemd5 of the sanitized stream ==
#      framemd5 of the original (slices untouched).
#   3. IDEMPOTENCY: second sanitize pass is byte-identical.
# Dependency-light: ffmpeg + cc + the in-tree annexb module. VAAPI legs
# skip (loudly) when no render node is present.
set -u
SRC=${SRC:-/work}
OUT=/tmp/sanhrd_corpus
mkdir -p "$OUT"
cd "$OUT"

cat > driver.c <<'EOF'
#include <stdio.h>
#include <stdlib.h>
#include "xrdp_h264_annexb.h"
int main(int argc, char **argv)
{
    FILE *f = fopen(argv[1], "rb");
    unsigned char *buf;
    int len;
    fseek(f, 0, SEEK_END); len = ftell(f); fseek(f, 0, SEEK_SET);
    buf = malloc(len);
    if (fread(buf, 1, len, f) != (size_t)len) { return 2; }
    fclose(f);
    if (xrdp_h264_sanitize_hrd(buf, &len) != 0)
    {
        fprintf(stderr, "sanitize FAILED\n");
        return 1;
    }
    f = fopen(argv[2], "wb");
    fwrite(buf, 1, len, f);
    fclose(f);
    return 0;
}
EOF
cc -O2 -I"$SRC/xrdp" driver.c "$SRC/xrdp/xrdp_h264_annexb.c" -o sanhrd \
    || { echo "ABORT: driver build failed"; exit 1; }

# First SPS block only, complete, padding lines excluded (the number of
# rbsp_alignment_zero_bits legitimately changes when the SPS shrinks).
sps_fields() {
    ffmpeg -hide_banner -i "$1" -c copy -bsf:v trace_headers -f null - 2>&1 \
        | python3 -c '
import sys, re
in_sps = False
for line in sys.stdin:
    if "Sequence Parameter Set" in line:
        if in_sps:
            break
        in_sps = True
        continue
    if in_sps:
        m = re.search(r"\d+\s+([a-z_0-9\[\]]+)\s+[01]+ = (\d+)", line)
        if m:
            n = m.group(1)
            if n not in ("rbsp_stop_one_bit", "rbsp_alignment_zero_bit"):
                print(n, "=", m.group(2))
        else:
            break'
}

HRD_FIELDS='nal_hrd_parameters_present_flag|vcl_hrd_parameters_present_flag|low_delay_hrd_flag|cpb_cnt_minus1|bit_rate_scale|cpb_size_scale|bit_rate_value_minus1|cpb_size_value_minus1|cbr_flag|initial_cpb_removal_delay_length_minus1|cpb_removal_delay_length_minus1|dpb_output_delay_length_minus1|time_offset_length'

pass=0; fail=0; skip=0
check_one() {
    local name=$1
    local in=$2
    ./sanhrd "$in" "$name.san.h264" || { echo "FAIL $name: sanitize error"; fail=$((fail+1)); return; }
    # 1. field exactness
    sps_fields "$in" > "$name.before"
    sps_fields "$name.san.h264" > "$name.after"
    grep -vE "$HRD_FIELDS" "$name.before" \
        | sed 's/nal_hrd_parameters_present_flag = 1/nal_hrd_parameters_present_flag = 0/' \
        > "$name.expect"
    # after must equal before-minus-hrd, with both hrd flags now 0
    { grep -vE "$HRD_FIELDS" "$name.after"; } > "$name.after_nohrd"
    if ! diff -q "$name.expect" "$name.after_nohrd" >/dev/null 2>&1; then
        echo "FAIL $name: SPS fields shifted beyond the HRD block"
        diff "$name.expect" "$name.after_nohrd" | head -6
        fail=$((fail+1)); return
    fi
    for flag in nal_hrd_parameters_present_flag vcl_hrd_parameters_present_flag; do
        v=$(grep -m1 "$flag" "$name.after" | awk '{print $NF}')
        [ "${v:-0}" = "0" ] || { echo "FAIL $name: $flag=$v after sanitize"; fail=$((fail+1)); return; }
    done
    # 2. pixel exactness
    ffmpeg -hide_banner -loglevel error -i "$in" -f framemd5 - > "$name.md5.in" 2>/dev/null
    ffmpeg -hide_banner -loglevel error -i "$name.san.h264" -f framemd5 - > "$name.md5.out" 2>/dev/null
    if ! diff -q <(grep -v '^#' "$name.md5.in") <(grep -v '^#' "$name.md5.out") >/dev/null; then
        echo "FAIL $name: decoded pixels differ"; fail=$((fail+1)); return
    fi
    # 3. idempotency
    ./sanhrd "$name.san.h264" "$name.san2.h264" || { echo "FAIL $name: 2nd pass error"; fail=$((fail+1)); return; }
    cmp -s "$name.san.h264" "$name.san2.h264" \
        || { echo "FAIL $name: not idempotent"; fail=$((fail+1)); return; }
    echo "PASS $name"
    pass=$((pass+1))
}

# --- corpus: VAAPI CBR resolution sweep (bit-phase coverage) ---
if [ -e /dev/dri/renderD128 ]; then
    for res in 1280x720 1366x768 1920x1080 2048x1152 2560x1440 \
               2880x1800 3440x1440 3840x2160 3840x2400 1936x1056; do
        n="vaapi_cbr_$res"
        ffmpeg -hide_banner -loglevel error -vaapi_device /dev/dri/renderD128 \
            -f lavfi -i "testsrc2=size=$res:rate=30" -frames:v 12 \
            -vf format=nv12,hwupload -c:v h264_vaapi \
            -rc_mode CBR -b:v 20M -sei +timing -bf 0 -async_depth 1 \
            -f h264 "$n.h264" 2>/dev/null
        [ -s "$n.h264" ] && check_one "$n" "$n.h264" \
            || { echo "SKIP $n (encode failed)"; skip=$((skip+1)); }
    done
else
    echo "SKIP all VAAPI legs (no render node)"; skip=$((skip+1))
fi

# --- corpus: libx264 nvenc-shaped (HRD + SEI + level 5.2 + refs 3) ---
for cfg in "nvencish:nal-hrd=cbr:vbv-maxrate=20000:vbv-bufsize=20000:ref=3:level=5.2:bframes=0" \
           "pic_struct:nal-hrd=cbr:vbv-maxrate=8000:vbv-bufsize=8000:pic-struct=1:bframes=0" \
           "hrd_vbr:nal-hrd=vbr:vbv-maxrate=15000:vbv-bufsize=15000:bframes=0"; do
    name="x264_${cfg%%:*}"; params="${cfg#*:}"
    for res in 1920x1080 2560x1600 3840x2160; do
        n="${name}_$res"
        ffmpeg -hide_banner -loglevel error \
            -f lavfi -i "testsrc2=size=$res:rate=30" -frames:v 12 \
            -c:v libx264 -x264-params "$params" -f h264 "$n.h264" 2>/dev/null
        [ -s "$n.h264" ] && check_one "$n" "$n.h264" \
            || { echo "SKIP $n"; skip=$((skip+1)); }
    done
done

echo "=== corpus result: PASS=$pass FAIL=$fail SKIP=$skip ==="
[ "$fail" = 0 ] && [ "$pass" -ge 10 ]
