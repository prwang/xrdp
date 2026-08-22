#!/bin/sh
# Build textflood and ring_recon. Deliberately a plain compile rather
# than an automake target: this is benchmark scaffolding under PR-demo/,
# not part of the shipped server, and it must build on a bare test box
# with nothing but cairo and Xlib headers.
#
#   ./build.sh [outdir]      default: alongside this script
#
# textflood needs a live X session to run as a payload, but its two
# offline modes do not:
#
#   ./textflood --verify 16 --geometry 3840x2400
#       renders 16 frames down both scroll paths and compares them byte
#       for byte; non-zero exit on the first differing pixel.
#   ./textflood --selftest --scroll strip --geometry 3840x2400
#       the producer's own standalone frame rate (PRD FR-BENCH-1).
#
# ring_recon needs no X server at all: it prices one frame of each
# candidate producer design at the same scroll distance.
set -eu
D=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$D}

: "${CC:=cc}"
CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter"

PKGS="cairo x11 xext xrandr"
if ! pkg-config --exists $PKGS; then
    echo "build.sh: missing dev packages. On Debian/Ubuntu:" >&2
    echo "  sudo apt-get install -y libcairo2-dev libx11-dev libxext-dev \\" >&2
    echo "      libxrandr-dev" >&2
    exit 1
fi

set -x
$CC $CFLAGS -o "$OUT/textflood" "$D/textflood.c" \
    $(pkg-config --cflags --libs $PKGS)
set +x
# ring_recon is optional on purpose: PR-demo/t4_profile/e52_t4_payload.sh
# copies textflood.c and this script to a bare box and nothing else, and
# a hard failure there would break a deploy over a bench that box does
# not need.
if [ -f "$D/ring_recon.c" ]; then
    set -x
    $CC $CFLAGS -o "$OUT/ring_recon" "$D/ring_recon.c" \
        $(pkg-config --cflags --libs cairo) -lpthread
    set +x
else
    echo "build.sh: no ring_recon.c beside this script, skipping it"
fi
