#!/bin/bash
# Connect the save-only ORACLE client to the T4 session (replacing any
# running client). With FREERDP_ORACLE_DUMP=1 the client dumps encoded AVC
# payloads to /tmp/oracle_avc_s<id>.bin and acks immediately: fps measured
# server-side (PR-demo/t4_profile/frame_accounting.sh) is then the pure
# server+WAN pipeline — the true server ceiling.
#
# TIMING INSTRUMENT ONLY: renders nothing, proves no fidelity. The distro
# xfreerdp3 remains the smoke/fidelity client; never smoke-gate against
# this. GFX=AVC444 default (GFX=AVC420 for mains-only comparisons).
set -u
T4=${T4:-$(cat /root/.t4_host 2>/dev/null)}
T4_KEY=${T4_KEY:-/root/.ssh/tmp_access_T4}
[ -z "$T4" ] && { echo "ABORT: set T4=user@host or /root/.t4_host"; exit 1; }
SU=${RDP_USER:-ubuntu}
CRED_FILE=${RDP_PASS_FILE:-/root/.ubuntu_cred}
LPORT=${TUNNEL_PORT:-33890}
CLI=${CLIENT_DISPLAY:-:99}
GFX=${GFX:-AVC444}
BIN=/opt/freerdp-vaapi/bin/xfreerdp

[ -x "$BIN" ] || { echo "ABORT: run build_oracle_client.sh first"; exit 1; }
if ! ss -tln 2>/dev/null | grep -q ":$LPORT "; then
    ssh -i "$T4_KEY" -f -N -o ExitOnForwardFailure=yes \
        -L "$LPORT:127.0.0.1:3389" "$T4"
fi

pkill -9 -x xfreerdp3 2>/dev/null
pkill -9 -x xfreerdp 2>/dev/null
sleep 2
rm -f /tmp/oracle_avc_s*.bin

PW=$(ssh -i "$T4_KEY" "$T4" "sudo cat $CRED_FILE")
RDPARGS=$(printf '%s\n' "/v:127.0.0.1:$LPORT" "/u:$SU" "/p:$PW" \
                        "/multimon" "/gfx:$GFX" "/cert:ignore" \
                        "/log-level:WARN")
setsid env DISPLAY=$CLI LD_LIBRARY_PATH=/opt/freerdp-vaapi/lib \
    FREERDP_ORACLE_DUMP=1 RDPARGS="$RDPARGS" \
    "$BIN" /args-from:env:RDPARGS </dev/null >/tmp/oracle_client.log 2>&1 &
unset PW RDPARGS
sleep 12

DISPLAY=$CLI xdotool search --name FreeRDP >/dev/null 2>&1 \
    || { echo "FAIL: oracle client login failed"; tail -5 /tmp/oracle_client.log; exit 1; }
ssh -i "$T4_KEY" "$T4" 'sudo grep -E "Matched .* mode" /var/log/xrdp.log | tail -1'
echo "oracle client up (dump: /tmp/oracle_avc_s*.bin)"
