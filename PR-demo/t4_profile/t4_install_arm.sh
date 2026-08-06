#!/bin/sh
# t4_install_arm.sh — install one A/B arm's xrdp-dev deb on the T4 and
# prove which arm is actually deployed.
#
# Every hazard this guards against has already bitten this project once:
#
#   * VERSION-SORT TRAP. The deb version is base+git<COMMIT TIMESTAMP>.<hash>,
#     which is the COMMIT time, not the build time. The #45 baseline
#     (…013437.5dae11f6) sorts NEWER than the batched build
#     (…013346.52b87988), so going baseline -> batched is a DOWNGRADE to
#     apt and is refused without --allow-downgrades. Install order tells
#     you nothing; only the installed hash does, so this script prints it
#     and fails if it is not what was asked for.
#   * xrdp-dev Breaks: xorgxrdp (<< 1:0.10.80~). Installing an xrdp-dev
#     deb has silently REMOVED xorgxrdp-dev and deleted
#     /etc/X11/xrdp/xorg.conf, breaking all session creation. Verified
#     after every install here.
#   * CONFFILE PROMPT. /etc/xrdp/cert.pem and rsakeys.ini are modified on
#     any box that generated its own key, so a plain apt-get install stops
#     at a prompt and leaves xrdp-dev half-configured (BACKLOG #57).
#     --force-confold, always.
#
# Usage (from the dev box):
#   sh t4_install_arm.sh <hash-fragment>      e.g. 52b87988 or 5dae11f6
#   sh t4_install_arm.sh --show               just report what is deployed
set -eu
T4=${T4:-$(cat /root/.t4_host 2>/dev/null || true)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
DEBDIR=${DEBDIR:-\$HOME/e52_debs}
[ -n "$T4" ] || { echo "ABORT: set T4=user@host or /root/.t4_host" >&2; exit 1; }

rsh() { ssh -n -i "$T4_KEY" -o BatchMode=yes "$T4" "$@"; }

show() {
    echo "--- deployed ---"
    rsh "dpkg -l xrdp-dev xorgxrdp-dev 2>/dev/null | awk '/^ii/{print \"  \", \$2, \$3}'"
}

WANT=${1:-}
if [ -z "$WANT" ] || [ "$WANT" = "--show" ]; then
    show
    exit 0
fi

DEB=$(rsh "ls $DEBDIR/xrdp-dev_*${WANT}*_amd64.deb 2>/dev/null | head -1")
[ -n "$DEB" ] || { echo "ABORT: no deb matching '$WANT' in $DEBDIR" >&2; exit 1; }
echo "installing: $(basename "$DEB")"

rsh "sudo DEBIAN_FRONTEND=noninteractive apt-get install -y -q \
     -o Dpkg::Options::=--force-confold --allow-downgrades '$DEB'" >/dev/null

# xorgxrdp-dev must have survived the Breaks: relation
if ! rsh "dpkg -l xorgxrdp-dev 2>/dev/null | grep -q '^ii'"; then
    echo "ABORT: xrdp-dev install REMOVED xorgxrdp-dev — reinstall it before" >&2
    echo "       measuring anything; session creation is broken right now." >&2
    exit 1
fi
if ! rsh "test -f /etc/X11/xrdp/xorg.conf"; then
    echo "ABORT: /etc/X11/xrdp/xorg.conf is gone (the Breaks: hazard)" >&2
    exit 1
fi

# the installed hash is the ONLY proof of which arm this is
GOT=$(rsh "dpkg -l xrdp-dev | awk '/^ii/{print \$3}'")
case "$GOT" in
*"$WANT"*) ;;
*) echo "ABORT: asked for '$WANT' but deployed version is '$GOT'" >&2; exit 1 ;;
esac

# DAEMON-RELOAD BEFORE RESTART, ALWAYS. The deb ships xrdp.service, so
# installing it invalidates systemd's cached unit *and its drop-ins*.
# Restarting without a reload runs the stale unit, which silently drops
# /etc/systemd/system/xrdp.service.d/gfxtrace.conf — and with it
# XRDP_GFX_TRACE=1, so the run produces ZERO trace records and the E5
# rate cannot be computed at all. Cost one 180 s batched run on
# 2026-07-31 before it was spotted.
rsh "sudo systemctl daemon-reload"
rsh "sudo systemctl restart xrdp"
rsh "systemctl show xrdp -p Environment | grep -q XRDP_GFX_TRACE \
     && echo '  XRDP_GFX_TRACE: applied' \
     || echo '  WARNING: XRDP_GFX_TRACE not in the unit environment — E5 will be empty'"
sleep 2
# A surviving session keeps the PREVIOUS xorgxrdp module loaded, so the
# owner could unknowingly test the old code (T4 directive 2026-07-26).
# e_gate_run.sh with E_COLD=1 logs the session off as its first step; this
# is the belt to that braces.
rsh "pgrep -a -f 'Xorg :1' >/dev/null 2>&1 && echo '  NOTE: a session Xorg is still running — run with E_COLD=1' || true"
show
echo "OK: arm '$WANT' deployed and xrdp restarted."
echo "NOW SMOKE-GATE IT before measuring (BACKLOG #61 precondition)."
