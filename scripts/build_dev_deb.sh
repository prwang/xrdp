#!/bin/sh
# build_dev_deb.sh — build a self-contained, commit-tagged xrdp .deb from the
# current source tree, for developer / BEFORE-AFTER testing.
#
# The package:
#   * is named "xrdp-dev" and Conflicts/Replaces/Provides "xrdp", so it cleanly
#     supersedes the Debian stock package without dpkg file-conflict errors;
#   * carries a Version and FILENAME tagged with the git commit hash (and a
#     "+dirty" suffix if the tree has uncommitted changes), so successive builds
#     NEVER overwrite each other and every .deb is traceable to a commit;
#   * is staged via "make install DESTDIR=" (no writes to the live system).
#
# This script does NOT install anything. It only produces a .deb under OUTDIR.
# See dev_config.md Part C/D for deploy/update/rollback.
#
# Usage:
#   ./scripts/build_dev_deb.sh                 # uses ./ as builddir, ./dist as OUTDIR
#   BUILDDIR=. OUTDIR=/tmp/xrdp-debs ./scripts/build_dev_deb.sh
#
# Prerequisites: the tree is already ./configure'd and `make` has succeeded.

set -eu

BUILDDIR="${BUILDDIR:-.}"
OUTDIR="${OUTDIR:-./dist}"
STAGE="${STAGE:-$(mktemp -d)}"

# --- version / commit identity ------------------------------------------------
BASE_VERSION="$(sed -n 's/.*AC_INIT(\[xrdp\], \[\([^]]*\)\].*/\1/p' \
    "${BUILDDIR}/configure.ac")"
: "${BASE_VERSION:?could not read base version from configure.ac}"

GIT_HASH="$(git -C "${BUILDDIR}" rev-parse --short=12 HEAD)"
if ! git -C "${BUILDDIR}" diff --quiet HEAD 2>/dev/null \
        || ! git -C "${BUILDDIR}" diff --cached --quiet HEAD 2>/dev/null; then
    DIRTY="+dirty"
else
    DIRTY=""
fi

# Debian version: base + git commit. '+' is a legal Debian version char and
# sorts after the plain upstream version, so newer commits upgrade cleanly.
VERSION="${BASE_VERSION}+git${GIT_HASH}${DIRTY}"
ARCH="$(dpkg --print-architecture)"
DEB_NAME="xrdp-dev_${VERSION}_${ARCH}.deb"

echo "==> base=${BASE_VERSION} commit=${GIT_HASH} dirty=${DIRTY:-no}"
echo "==> package version: ${VERSION}"

# --- stage the install tree ---------------------------------------------------
rm -rf "${STAGE}"
mkdir -p "${STAGE}"
make -C "${BUILDDIR}" install "DESTDIR=${STAGE}" >/dev/null
echo "==> staged $(find "${STAGE}" -type f | wc -l) files to ${STAGE}"

# --- DEBIAN/control + metadata ------------------------------------------------
mkdir -p "${STAGE}/DEBIAN"

# conffiles: everything we install under /etc, so dpkg preserves admin edits.
if [ -d "${STAGE}/etc" ]; then
    ( cd "${STAGE}" && find etc -type f -printf '/%p\n' ) \
        > "${STAGE}/DEBIAN/conffiles"
fi

INSTALLED_KB="$(du -sk "${STAGE}" | cut -f1)"

cat > "${STAGE}/DEBIAN/control" <<EOF
Package: xrdp-dev
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: xrdp dev build <xrdp-devel@googlegroups.com>
Installed-Size: ${INSTALLED_KB}
Depends: libc6, libssl3 | libssl1.1, libpam0g, libx11-6, libxfixes3, libxrandr2
Recommends: xorgxrdp, xserver-xorg-core
Conflicts: xrdp
Replaces: xrdp
Provides: xrdp
Section: net
Priority: optional
Description: xrdp RDP server (developer build from git ${GIT_HASH})
 Self-built xrdp from the source tree at commit ${GIT_HASH}${DIRTY}.
 Supersedes the distribution xrdp package for development / BEFORE-AFTER
 testing of in-progress changes. NOT for production use.
EOF

# --- build --------------------------------------------------------------------
mkdir -p "${OUTDIR}"
dpkg-deb --root-owner-group --build "${STAGE}" "${OUTDIR}/${DEB_NAME}" >/dev/null

echo "==> built ${OUTDIR}/${DEB_NAME}"
echo
dpkg-deb -I "${OUTDIR}/${DEB_NAME}" | sed 's/^/    /'
echo "==> install with:  sudo apt-get install -y ${OUTDIR}/${DEB_NAME}"
