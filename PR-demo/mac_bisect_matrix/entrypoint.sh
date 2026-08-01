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
echo "${SESSION_KIND:-banner}" > /etc/session_kind

# BACKLOG #70: the ack trace has two halves and they live in two
# processes. xrdp reads XRDP_ACK_TRACE straight out of the pod
# environment, but the session Xorg does not: sesexec CLEARS the
# environment before starting it (sesman/sesexec/env.c) and repopulates
# it from sesman.ini's [SessionVariables]. Without this the xorgxrdp
# capture legs are simply absent and the overlap cannot be computed --
# measured that way once, on arm-u, before this existed.
if [ "${XRDP_ACK_TRACE:-0}" = "1" ] \
        && ! grep -q '^XRDP_ACK_TRACE=' /etc/xrdp/sesman.ini; then
    sed -i 's/^\[SessionVariables\]$/[SessionVariables]\nXRDP_ACK_TRACE=1/' \
        /etc/xrdp/sesman.ini
    grep -q '^XRDP_ACK_TRACE=1' /etc/xrdp/sesman.ini \
        || echo "WARN: could not arm XRDP_ACK_TRACE for the session" >&2
fi

# ffmpeg runs as root under xrdp (see gfx.toml) so root reaching the render
# node is what matters; group ids for video/render differ between host and
# image, so don't rely on them.
ls -la /dev/dri/ >&2 || true

mkdir -p /var/run/xrdp /var/run/xrdp/sockdir
# BACKLOG #74: the perf sink writes <XRDP_PERF_TRACE>.<pid>; it fopen()s
# once and gives up silently if the directory is missing, so create it
# here rather than discover an empty measurement afterwards. Deliberately
# NOT /var/log/xrdp: the whole point of the sink is that stage timings do
# not land among the operator-facing log.
if [ -n "${XRDP_PERF_TRACE:-}" ]; then
    mkdir -p "$(dirname "$XRDP_PERF_TRACE")"
fi
chmod 755 /var/run/xrdp
chmod 1777 /var/run/xrdp/sockdir
rm -f /var/run/xrdp/xrdp.pid /var/run/xrdp/xrdp-sesman.pid

/usr/sbin/xrdp-sesman
exec /usr/sbin/xrdp --nodaemon
