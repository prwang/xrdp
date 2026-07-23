#!/bin/bash
# reset_420.sh — restore the KNOWN-GOOD baseline between codec experiments.
#
# EXPERIMENT DISCIPLINE (owner-mandated 2026-07-23): a codec trial only
# counts if it is bracketed by green AVC420 baselines. Restarting xrdp does
# NOT reset the backend Xorg session; a reconnect after a codec switch can
# render black even on the known-good 420 path (observed live: a session
# cycled v2 -> 444v1 -> 420 stayed black on 420). So between EVERY run:
#   1. run this script (sets avc_mode=420, restarts xrdp, KILLS all
#      backend sessions so the next connect is a fresh login);
#   2. connect, confirm the 420 baseline RENDERS;
#   3. only then flip avc_mode to the mode under test, restart xrdp,
#      kill sessions again, reconnect fresh, observe;
#   4. finish by re-running this script and confirming 420 renders again.
# If a 420 baseline is black, the rig is contaminated or a real
# reconnect/codec-switch bug is reproducing — nothing observed in between
# is attributable to the codec under test.
set -eu
GFX_TOML="${GFX_TOML:-/etc/xrdp/gfx.toml}"

sed -i 's/^avc_mode = ".*"/avc_mode = "420"/' "$GFX_TOML"
grep "^avc_mode" "$GFX_TOML"
systemctl restart xrdp
# kill every backend session: config binds at session creation, and only a
# fresh login gives an uncontaminated rig. sesadmin's kill:<sid> is "not yet
# implemented" (verified 2026-07-23), but the Session ID IS the sesexec pid,
# and SIGTERM to sesexec tears the whole session down cleanly (verified).
xrdp-sesadmin -c=list 2>/dev/null | awk -F': ' '/Session ID:/ {print $2}' \
| while read -r sid; do
    echo "killing session $sid (TERM to sesexec)"
    kill -TERM "$sid" 2>/dev/null || true
done
sleep 3
LEFT=$(xrdp-sesadmin -c=list 2>/dev/null | grep -c "Session ID:" || true)
echo "sessions remaining: ${LEFT:-0}"
echo "RESET DONE — next connect is a fresh login on avc_mode=420."
echo "Confirm the 420 baseline RENDERS before and after every trial."
