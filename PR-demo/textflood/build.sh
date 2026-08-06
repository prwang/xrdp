#!/bin/sh
# Build textflood. Deliberately a plain compile rather than an automake
# target: this is benchmark scaffolding under PR-demo/, not part of the
# shipped server, and it must build on a bare test box with nothing but
# cairo and Xlib headers.
#
#   ./build.sh [outdir]      default: alongside this script
set -eu
D=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-$D}

: "${CC:=cc}"
CFLAGS="-O2 -Wall -Wextra -Wno-unused-parameter"

PKGS="cairo x11 xext"
if ! pkg-config --exists $PKGS; then
    echo "build.sh: missing dev packages. On Debian/Ubuntu:" >&2
    echo "  sudo apt-get install -y libcairo2-dev libx11-dev libxext-dev" >&2
    exit 1
fi

set -x
$CC $CFLAGS -o "$OUT/textflood" "$D/textflood.c" \
    $(pkg-config --cflags --libs $PKGS)
