#!/bin/sh
# Build the oracle/VAAPI FreeRDP client into /opt/freerdp-vaapi on the DEV
# BOX (CLAUDE.md "T4 test methodology": all client-side harness lives here).
# One build serves two roles, selected at runtime:
#   - normal run: VAAPI hw H264 decode (Debian ships -DWITH_VAAPI=OFF)
#   - FREERDP_ORACLE_DUMP=1: save-only oracle — dumps encoded AVC payloads,
#     acks immediately, contributes ~0 to the frame interval (server-ceiling
#     measurements; see patch_gfx_oracle.py header)
# Idempotent; ~2 min on 32 cores. Requires deb-src entries (apt-get source).
set -e
D=$(cd "$(dirname "$0")" && pwd)
SRC=/tmp/freerdp-oracle-src
PREFIX=/opt/freerdp-vaapi

if [ -x "$PREFIX/bin/xfreerdp" ] && \
   strings "$PREFIX/lib/libfreerdp3.so.3.15.0" 2>/dev/null \
       | grep -q "FREERDP_ORACLE_DUMP"; then
    echo "oracle client already built at $PREFIX"
    exit 0
fi

apt-get build-dep -y -qq freerdp3
rm -rf "$SRC"; mkdir -p "$SRC"
( cd "$SRC" && apt-get source freerdp3 )
TREE=$(find "$SRC" -maxdepth 1 -type d -name "freerdp3-*" | head -1)
[ -n "$TREE" ] || { echo "ABORT: freerdp3 source not extracted"; exit 1; }

python3 "$D/patch_gfx_oracle.py" "$TREE"

cmake -S "$TREE" -B "$TREE/build-oracle" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" \
    -DWITH_VAAPI=ON -DWITH_FFMPEG=ON -DWITH_SWSCALE=ON -DWITH_DSP_FFMPEG=ON \
    -DWITH_SERVER=OFF -DWITH_MANPAGES=OFF -DWITH_SAMPLE=OFF -DWITH_KRB5=OFF \
    -DCHANNEL_URBDRC=OFF -DWITH_PULSE=OFF -DWITH_CUPS=OFF -DWITH_PCSC=OFF \
    -DWITH_CLIENT_SDL=OFF
cmake --build "$TREE/build-oracle" -j"$(nproc)"
cmake --install "$TREE/build-oracle"
echo "built $PREFIX/bin/xfreerdp"
