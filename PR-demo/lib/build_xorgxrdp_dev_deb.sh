#!/bin/sh
# Build a commit-identified xorgxrdp package from an already-built tree.
# This stages the installation and never changes the live host installation.
set -eu

BUILDDIR=${BUILDDIR:-.}
OUTDIR=${OUTDIR:-./dist}
STAGE=${STAGE:-$(mktemp -d)}

BASE_VERSION=$(sed -n \
    's/.*AC_INIT(\[xorgxrdp\], \[\([^]]*\)\].*/\1/p' \
    "${BUILDDIR}/configure.ac")
: "${BASE_VERSION:?could not read version from configure.ac}"
GIT_HASH=$(git -C "${BUILDDIR}" rev-parse --short=12 HEAD)
GIT_TIME=$(git -C "${BUILDDIR}" show -s --format=%cd \
    --date=format:%Y%m%d%H%M%S HEAD)
DIRTY=
if ! git -C "${BUILDDIR}" diff --quiet HEAD 2>/dev/null ||
        ! git -C "${BUILDDIR}" diff --cached --quiet HEAD 2>/dev/null
then
    DIRTY=+dirty
fi
VERSION="1:${BASE_VERSION}+git${GIT_TIME}.${GIT_HASH}${DIRTY}"
ARCH=$(dpkg --print-architecture)
FILE_VERSION=$(printf '%s' "${VERSION}" | sed 's/:/%3a/g')
DEB_NAME="xorgxrdp-dev_${FILE_VERSION}_${ARCH}.deb"

rm -rf "${STAGE}"
mkdir -p "${STAGE}" "${OUTDIR}"
make -C "${BUILDDIR}" install "DESTDIR=${STAGE}" >/dev/null
mkdir -p "${STAGE}/DEBIAN"
if [ -d "${STAGE}/etc" ]
then
    (cd "${STAGE}" && find etc -type f -printf '/%p\n') \
        > "${STAGE}/DEBIAN/conffiles"
fi
INSTALLED_KB=$(du -sk "${STAGE}" | cut -f1)

cat > "${STAGE}/DEBIAN/control" <<EOF
Package: xorgxrdp-dev
Version: ${VERSION}
Architecture: ${ARCH}
Maintainer: xrdp dev build <xrdp-devel@googlegroups.com>
Installed-Size: ${INSTALLED_KB}
Depends: xserver-xorg-core, libgbm1, libegl1, libepoxy0
Conflicts: xorgxrdp
Replaces: xorgxrdp
Provides: xorgxrdp
Section: net
Priority: optional
Description: xorgxrdp development build from git ${GIT_HASH}
 GLAMOR-enabled xorgxrdp producer built from commit ${GIT_HASH}${DIRTY}.
EOF

dpkg-deb --root-owner-group --build "${STAGE}" \
    "${OUTDIR}/${DEB_NAME}" >/dev/null
echo "${OUTDIR}/${DEB_NAME}"
