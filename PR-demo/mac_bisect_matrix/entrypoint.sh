#!/bin/bash
# Pod entrypoint: bring up sesman + xrdp in the foreground.
# Expects (from the pod spec):
#   ARM_LABEL                  — human-readable arm id shown by banner.sh
#   /run/matrix/tester.hash    — crypt(3) hash for the tester account
#                                (hostPath, root-only on the host)
#   /dev/dri                   — GPU render node (hostPath) for VAAPI
set -e

[ -s /etc/machine-id ] || dbus-uuidgen > /etc/machine-id

if [ -s /run/matrix/tester.hash ]; then
    usermod -p "$(cat /run/matrix/tester.hash)" tester
else
    echo "WARN: no /run/matrix/tester.hash — tester login will fail" >&2
fi
# probe444: byte-verification account for the automated harness (the
# harness cannot and must not know the owner's tester password)
if [ -s /run/matrix/probe.hash ]; then
    usermod -p "$(cat /run/matrix/probe.hash)" probe444
fi

echo "${ARM_LABEL:-unlabeled-arm}" > /etc/arm_label

# ffmpeg runs as root under xrdp (see gfx.toml) so root reaching the render
# node is what matters; group ids for video/render differ between host and
# image, so don't rely on them.
ls -la /dev/dri/ >&2 || true

mkdir -p /var/run/xrdp /var/run/xrdp/sockdir
chmod 755 /var/run/xrdp
chmod 1777 /var/run/xrdp/sockdir
rm -f /var/run/xrdp/xrdp.pid /var/run/xrdp/xrdp-sesman.pid

/usr/sbin/xrdp-sesman
exec /usr/sbin/xrdp --nodaemon
