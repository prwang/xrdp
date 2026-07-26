#!/bin/bash
# Byte-verify every arm of the running matrix BEFORE handing it to the
# human tester (strict honesty rule: an arm whose bitstream was never
# verified proves nothing — a mislabelled arm invalidates the whole
# bisect). For each arm: log in with the oracle save-only client as
# probe444, dump the encoded stream, and report the two facts the bisect
# turns on:
#   1. nal_hrd_parameters_present_flag in the SPS VUI  (HRD VUI half)
#   2. presence/absence of SEI NALs (type 6) in the frame records
# CLIENT SIDE STAYS ON THE HOST — this uses the existing oracle client
# binary, never a client inside the fleet.
set -u
BIN=/opt/freerdp-vaapi/bin/xfreerdp
CRED=${PROBE_CRED_FILE:-/root/.oracle_cred}
CLI=${VERIFY_DISPLAY:-:97}
GFX=${GFX:-AVC420}
D=$(cd "$(dirname "$0")" && pwd)
OUT=/tmp/matrix_verify
mkdir -p "$OUT"

[ -x "$BIN" ] || { echo "ABORT: oracle client missing"; exit 1; }
[ -s "$CRED" ] || { echo "ABORT: no probe cred"; exit 1; }

if ! DISPLAY=$CLI xdotool getdisplaygeometry >/dev/null 2>&1; then
    setsid Xvfb "$CLI" -screen 0 1600x900x24 </dev/null >/dev/null 2>&1 &
    sleep 2
fi

declare -A PORT=( [arm-a]=40000 [arm-b]=40001 [arm-c]=40002 [arm-d]=40003 )
fail=0
for arm in arm-a arm-b arm-c arm-d; do
    port=${PORT[$arm]}
    echo "=== $arm (127.0.0.1:$port) ==="
    pkill -9 -x xfreerdp 2>/dev/null; sleep 1
    rm -f /tmp/oracle_avc_s*.bin
    PW=$(cat "$CRED")
    RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$port" "/u:probe444" "/p:$PW" \
                            "/size:1280x720" "/gfx:$GFX" "/cert:ignore" \
                            "/log-level:WARN")
    setsid env DISPLAY=$CLI LD_LIBRARY_PATH=/opt/freerdp-vaapi/lib \
        FREERDP_ORACLE_DUMP=1 RDPARGS="$RDPARGS" \
        "$BIN" /args-from:env:RDPARGS </dev/null >"$OUT/$arm.client.log" 2>&1 &
    unset PW RDPARGS
    sleep 14
    pkill -9 -x xfreerdp 2>/dev/null

    dump=$(ls /tmp/oracle_avc_s*.bin 2>/dev/null | head -1)
    if [ -z "$dump" ]; then
        echo "  FAIL: no oracle dump (login or GFX failed)"
        tail -3 "$OUT/$arm.client.log" | sed 's/^/  | /'
        fail=1
        continue
    fi
    cp "$dump" "$OUT/$arm.bin"
    python3 "$D/../oracle_client/split_oracle_dump.py" "$OUT/$arm.bin" "$GFX" \
        >/dev/null 2>&1
    es="$OUT/${arm}_main.h264"
    [ -s "$es" ] || { echo "  FAIL: empty elementary stream"; fail=1; continue; }

    hrd=$(ffmpeg -hide_banner -i "$es" -c copy -bsf:v trace_headers \
              -f null - 2>&1 \
          | grep -m1 -oE 'nal_hrd_parameters_present_flag[^0-9]*[01]' \
          | grep -oE '[01]$')
    python3 - "$OUT/$arm.bin" "$GFX" "$arm" "${hrd:-?}" <<'EOF'
import struct, sys
path, mode, arm, hrd = sys.argv[1:5]
data = open(path, 'rb').read()
pos = 0
recs = []
while pos + 4 <= len(data):
    (ln,) = struct.unpack_from('<I', data, pos)
    pos += 4
    rec = data[pos:pos + ln]
    pos += ln
    if len(rec) != ln:
        break
    if mode != 'AVC420':
        (w,) = struct.unpack_from('<I', rec, 0)
        rec = rec[4:4 + (w & 0x3FFFFFFF)]
    (nr,) = struct.unpack_from('<I', rec, 0)
    buf = rec[4 + nr * 10:]
    nals, i = [], 0
    while True:
        i = buf.find(b'\x00\x00\x01', i)
        if i < 0:
            break
        nals.append(buf[i + 3] & 0x1F)
        i += 3
    recs.append(nals)
sei_frames = sum(1 for n in recs if 6 in n)
print('  records=%d  first=%s  second=%s' % (
    len(recs), recs[0] if recs else '-', recs[1] if len(recs) > 1 else '-'))
print('  VERDICT %s: nal_hrd_vui=%s  sei_frames=%d/%d' % (
    arm, hrd, sei_frames, len(recs)))
EOF
done
exit $fail
