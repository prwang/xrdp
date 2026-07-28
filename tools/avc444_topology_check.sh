#!/bin/sh
# avc444_topology_check.sh — decode-topology invariance regression
# (PRD FR-H264-7, owner directive 2026-07-28).
#
# Takes an AVC444 wire capture and proves, via ffmpeg framemd5, that the
# stream decodes to bit-identical pixels under all three client decode
# topologies:
#   1. one in-order decoder consuming the full interleave
#      (Windows/mstsc/xfreerdp shape);
#   2. a decoder consuming the main view with ALL aux NALs dropped;
#   3. two independent decoders, one per view (the macOS VideoToolbox
#      shape from the 2026-07 bisect).
# Any mismatch is a RED result: fix the stream, never the check.
#
# Input formats (auto-detected):
#   - oracle probe444 dump: <u32 LE record length><RFX_AVC444_BITMAP_STREAM>...
#     (LC in bits 30-31 of the first record word splits main/aux exactly the
#     way a per-view client does);
#   - raw Annex-B: aux = non-reference type-1 slices (nal_ref_idc == 0,
#     the leaf shape), everything else = main.
#
# Usage: avc444_topology_check.sh <capture> [ffmpeg-path]
# Requires: python3, ffmpeg. Portable — no live session or GPU needed.
set -u
CAP=${1:?usage: avc444_topology_check.sh <capture> [ffmpeg-path]}
FFMPEG=${2:-ffmpeg}
[ -r "$CAP" ] || { echo "RED: cannot read $CAP" >&2; exit 2; }
command -v "$FFMPEG" >/dev/null || { echo "RED: no ffmpeg" >&2; exit 2; }
WORK=$(mktemp -d) || exit 2
trap 'rm -rf "$WORK"' EXIT

python3 - "$CAP" "$WORK" <<'PYEOF' || { echo "RED: split failed" >&2; exit 1; }
import struct
import sys

cap, work = sys.argv[1], sys.argv[2]
data = open(cap, 'rb').read()


def nals_of(buf):
    out = []
    i = buf.find(b'\x00\x00\x01')
    while i >= 0:
        j = buf.find(b'\x00\x00\x01', i + 3)
        end = j if j >= 0 else len(buf)
        raw = buf[i + 3:end]
        if raw.endswith(b'\x00'):
            raw = raw[:-1]
        if raw:
            out.append(raw)
        i = j
    return out


def record_streams(data):
    """Yield (main_annexb, aux_annexb) per record from an oracle dump."""
    pos = 0
    while pos + 4 <= len(data):
        (ln,) = struct.unpack_from('<I', data, pos)
        pos += 4
        rec = data[pos:pos + ln]
        pos += ln
        if len(rec) != ln or ln < 8:
            raise ValueError('bad record framing')
        (w,) = struct.unpack_from('<I', rec, 0)
        avc1len = w & 0x3FFFFFFF
        lc = (w >> 30) & 0x3
        (nrects,) = struct.unpack_from('<I', rec, 4)
        body = rec[8 + nrects * 10:]
        end1 = 4 + avc1len - (8 + nrects * 10)
        if lc == 1:
            yield body, b''
        elif lc == 2:
            yield b'', body
        else:                       # LC=0: main then aux in one record
            yield body[:end1], body[end1:]


is_dump = not (data.startswith(b'\x00\x00\x01')
               or data.startswith(b'\x00\x00\x00\x01'))
main_nals = []                      # list of NAL payloads, wire order
aux_nals = []
kinds = []                          # 'M'/'A' per frame-starting VCL, order
inter = bytearray()

if is_dump:
    views = list(record_streams(data))
else:
    views = None

def classify(nal):
    t = nal[0] & 0x1F
    nri = (nal[0] >> 5) & 0x3
    return t, nri

if views is not None:
    for m, a in views:
        for nal in nals_of(m):
            inter += b'\x00\x00\x00\x01' + nal
            main_nals.append(nal)
            if classify(nal)[0] in (1, 5):
                kinds.append('M')
        for nal in nals_of(a):
            inter += b'\x00\x00\x00\x01' + nal
            aux_nals.append(nal)
            if classify(nal)[0] in (1, 5):
                kinds.append('A')
else:
    for nal in nals_of(data):
        t, nri = classify(nal)
        inter += b'\x00\x00\x00\x01' + nal
        if t == 1 and nri == 0:     # non-reference leaf = aux view
            aux_nals.append(nal)
            kinds.append('A')
        else:
            main_nals.append(nal)
            if t in (1, 5):
                kinds.append('M')

if not aux_nals:
    sys.exit('no auxiliary NALs found — not an AVC444 interleave capture')

# aux decoder gets the MAIN parameter sets spliced in front (the leaf
# rewrite drops the aux child's own SPS/PPS; a per-view decoder still
# needs them once)
sps = next((n for n in main_nals if (n[0] & 0x1F) == 7), None)
pps = next((n for n in main_nals if (n[0] & 0x1F) == 8), None)
if sps is None or pps is None:
    sys.exit('no SPS/PPS in the main view')

with open(work + '/interleaved.h264', 'wb') as f:
    f.write(inter)
with open(work + '/main_only.h264', 'wb') as f:
    for nal in main_nals:
        f.write(b'\x00\x00\x00\x01' + nal)
with open(work + '/aux_only.h264', 'wb') as f:
    f.write(b'\x00\x00\x00\x01' + sps + b'\x00\x00\x00\x01' + pps)
    for nal in aux_nals:
        if (nal[0] & 0x1F) not in (7, 8):
            f.write(b'\x00\x00\x00\x01' + nal)
with open(work + '/kinds.txt', 'w') as f:
    f.write(''.join(kinds))
print('split: %d main NALs, %d aux NALs, %d frames (%d main / %d aux)'
      % (len(main_nals), len(aux_nals), len(kinds),
         kinds.count('M'), kinds.count('A')))
PYEOF

md5s()
{
    # frame hashes only; pts/pos columns legitimately differ between feeds
    "$FFMPEG" -hide_banner -loglevel error -fflags +genpts -i "$1" \
        -fps_mode passthrough -f framemd5 - 2>"$1.err" \
        | awk -F, '/^[^#]/ { gsub(/ /, "", $NF); print $NF }'
}
md5s "$WORK/interleaved.h264" > "$WORK/inter.md5"
md5s "$WORK/main_only.h264"   > "$WORK/main.md5"
md5s "$WORK/aux_only.h264"    > "$WORK/aux.md5"

python3 - "$WORK" <<'PYEOF'
import sys

work = sys.argv[1]
kinds = open(work + '/kinds.txt').read().strip()
inter = open(work + '/inter.md5').read().split()
main = open(work + '/main.md5').read().split()
aux = open(work + '/aux.md5').read().split()

fail = 0
if len(inter) != len(kinds):
    print('RED: interleaved decode emitted %d frames, wire has %d '
          'frame-starting VCLs' % (len(inter), len(kinds)))
    fail = 1
want_m = [h for h, k in zip(inter, kinds) if k == 'M']
want_a = [h for h, k in zip(inter, kinds) if k == 'A']
if main != want_m:
    n = sum(1 for x, y in zip(main, want_m) if x != y) \
        + abs(len(main) - len(want_m))
    print('RED: topology 2 (drop-all-aux) diverges: %d/%d main frames '
          'differ from the interleaved decode' % (n, len(want_m)))
    fail = 1
if aux != want_a:
    n = sum(1 for x, y in zip(aux, want_a) if x != y) \
        + abs(len(aux) - len(want_a))
    print('RED: topology 3 (per-view aux decoder) diverges: %d/%d aux '
          'frames differ from the interleaved decode' % (n, len(want_a)))
    fail = 1
if fail:
    sys.exit(1)
print('GREEN: decode-topology invariance holds — %d/%d main and %d/%d '
      'aux frames bit-identical across single-decoder, drop-aux and '
      'per-view decodes' % (len(want_m), len(want_m),
                            len(want_a), len(want_a)))
PYEOF
RC=$?
for f in interleaved main_only aux_only; do
    if [ -s "$WORK/$f.h264.err" ]; then
        echo "decoder stderr ($f):"
        head -5 "$WORK/$f.h264.err"
    fi
done
exit $RC
