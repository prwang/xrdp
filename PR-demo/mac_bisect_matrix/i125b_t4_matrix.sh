#!/bin/bash

# Repeated dense/sparse by one-/two-frame wire-window characterization on the
# T4. The client and oracle stay on this box; the server is reached only
# through an SSH tunnel. Each leg starts a fresh whole desktop session.

set -eu

D=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$D/../.." && pwd)
HOST=${I125B_HOST:-ubuntu@3.235.10.89}
KEY=${I125B_KEY:-/root/.ssh/tmp_access_T4}
PORT=${I125B_PORT:-33389}
SECS=${1:-20}
STAMP=$(date -u +%Y%m%dT%H%M%SZ)
OUT=${I125B_OUT:-$D/captures/i125b_t4_matrix_${STAMP}}
BASE=$ROOT/PR-demo/t4_profile/gfx-t4-nvenc-ltr.toml
SELECTOR=$ROOT/PR-demo/lib/t4/xrdp-benchmark-profile
DISABLE_CHROMA=$ROOT/PR-demo/t4_profile/chroma-probe-disabled.desktop
TRACE_DROPIN=$ROOT/PR-demo/lib/t4/xrdp-perf-trace.conf
TUNNEL_PID=

fail()
{
    echo "FAIL: $*" >&2
    exit 1
}

remote()
{
    ssh -n -i "$KEY" -o BatchMode=yes -o ConnectTimeout=10 \
        -o ServerAliveInterval=15 -o ServerAliveCountMax=3 "$HOST" "$1"
}

cleanup()
{
    if [ -n "$TUNNEL_PID" ]
    then
        kill "$TUNNEL_PID" 2>/dev/null || true
        wait "$TUNNEL_PID" 2>/dev/null || true
    fi
    remote 'sudo rm -f /etc/xrdp-e52-payload' >/dev/null 2>&1 || true
}

trap cleanup EXIT INT TERM

[ "$SECS" = 20 ] || fail "the committed matrix is exactly 20 seconds per leg"
[ -f "$BASE" ] || fail "missing T4 profile $BASE"
[ -x "$SELECTOR" ] || fail "selector is not executable: $SELECTOR"
[ -f "$DISABLE_CHROMA" ] || fail "missing chroma-probe override"
[ -f "$TRACE_DROPIN" ] || fail "missing perf-trace service drop-in"
mkdir -p "$OUT/profiles"

make_profile()
{
    name=$1
    window=$2
    refresh=$3
    idle=$4
    sed \
        -e "s/^wire_window = .*/wire_window = $window/" \
        -e "s/^chroma_refresh_ms = .*/chroma_refresh_ms = $refresh/" \
        -e "s/^chroma_idle_ms = .*/chroma_idle_ms = $idle/" \
        "$BASE" > "$OUT/profiles/$name.toml"
    [ "$(grep -c '^wire_window = ' "$OUT/profiles/$name.toml")" = 1 ] ||
        fail "$name does not contain exactly one wire_window"
    [ "$(grep -c '^chroma_refresh_ms = ' "$OUT/profiles/$name.toml")" = 1 ] ||
        fail "$name does not contain exactly one chroma_refresh_ms"
    [ "$(grep -c '^chroma_idle_ms = ' "$OUT/profiles/$name.toml")" = 1 ] ||
        fail "$name does not contain exactly one chroma_idle_ms"
}

make_profile dense-w1 1 0 0
make_profile sparse-w1 1 1000 100
make_profile dense-w2 2 0 0
make_profile sparse-w2 2 1000 100

for profile in "$OUT"/profiles/*.toml
do
    sed -e '/^wire_window = /d' \
        -e '/^chroma_refresh_ms = /d' \
        -e '/^chroma_idle_ms = /d' "$profile" | sha256sum
done > "$OUT/profile-common-sha256.txt"
[ "$(awk '{print $1}' "$OUT/profile-common-sha256.txt" | sort -u | wc -l)" = 1 ] ||
    fail "the matrix profiles differ outside the three treatment settings"

scp -q -i "$KEY" "$SELECTOR" "$DISABLE_CHROMA" "$TRACE_DROPIN" \
    "$OUT"/profiles/*.toml "$HOST:/tmp/"
remote 'sudo install -d -m 0700 /root/xrdp-benchmark-profiles && \
        sudo install -m 0755 /tmp/xrdp-benchmark-profile \
            /usr/local/libexec/xrdp-benchmark-profile && \
        sudo install -m 0644 /tmp/dense-w1.toml \
            /root/xrdp-benchmark-profiles/dense-w1.toml && \
        sudo install -m 0644 /tmp/sparse-w1.toml \
            /root/xrdp-benchmark-profiles/sparse-w1.toml && \
        sudo install -m 0644 /tmp/dense-w2.toml \
            /root/xrdp-benchmark-profiles/dense-w2.toml && \
        sudo install -m 0644 /tmp/sparse-w2.toml \
            /root/xrdp-benchmark-profiles/sparse-w2.toml && \
        sudo install -d -m 0755 /etc/systemd/system/xrdp.service.d && \
        sudo install -m 0644 /tmp/xrdp-perf-trace.conf \
            /etc/systemd/system/xrdp.service.d/frontier-qa.conf && \
        sudo systemctl daemon-reload && \
        install -d -m 0700 /home/ubuntu/.config/autostart && \
        install -m 0644 /tmp/chroma-probe-disabled.desktop \
            /home/ubuntu/.config/autostart/chroma-probe.desktop && \
        printf "%s\\n" textflood | \
            sudo tee /etc/xrdp-e52-payload >/dev/null'

ssh -n -i "$KEY" -o BatchMode=yes -o ConnectTimeout=10 \
    -o ExitOnForwardFailure=yes -o ServerAliveInterval=15 \
    -o ServerAliveCountMax=3 -N -L "$PORT:127.0.0.1:3389" "$HOST" &
TUNNEL_PID=$!
for unused in $(seq 1 20)
do
    ss -tln | grep -q "127.0.0.1:$PORT" && break
    kill -0 "$TUNNEL_PID" 2>/dev/null || fail "SSH tunnel exited"
    sleep 1
done
ss -tln | grep -q "127.0.0.1:$PORT" || fail "SSH tunnel did not listen"

printf '%s\n' \
    'A1 dense-w1' 'B1 sparse-w1' 'C1 dense-w2' 'D1 sparse-w2' \
    'D2 sparse-w2' 'C2 dense-w2' 'B2 sparse-w1' 'A2 dense-w1' \
    > "$OUT/order.txt"

while read -r leg profile
do
    leg_out=$OUT/leg_$leg
    mkdir -p "$leg_out"
    echo "=== $leg: $profile, ${SECS}s ==="
    remote "sudo /usr/local/libexec/xrdp-benchmark-profile '$profile'" \
        > "$leg_out/profile-status.txt"
    remote 'sudo sha256sum /etc/xrdp/gfx.toml' \
        > "$leg_out/deployed-gfx-sha256.txt"
    remote 'sudo cat /etc/xrdp/gfx.toml' > "$leg_out/deployed-gfx.toml"
    cmp "$OUT/profiles/$profile.toml" "$leg_out/deployed-gfx.toml" ||
        fail "$leg deployed a different gfx.toml"
    {
        echo "condition $leg"
        echo "profile $profile"
        echo "seconds $SECS"
    } > "$leg_out/condition.txt"
    E_TARGET=ssh E_SSH_HOST="$HOST" E_SSH_KEY="$KEY" \
        E_PORT="$PORT" E_USER=ubuntu E_CRED_FILE=/root/.ubuntu_cred \
        E_MODE=oracle E_MONITORS=1 E_MODE0=3840x2400R \
        E_MODELINE0='592.25 3840 3888 3920 4000 2400 2403 2409 2469 +hsync -vsync' \
        E5_BASE_MS=none E_OUT="$leg_out" \
        bash "$D/e_gate_run.sh" "$SECS" > "$leg_out/gate.txt" 2>&1 ||
        {
            tail -n 60 "$leg_out/gate.txt" >&2
            fail "$leg gate run failed"
        }
    grep -aE '^sends:|send-to-send|producer margin|session logged off' \
        "$leg_out/gate.txt" | sed 's/^/  /'
done < "$OUT/order.txt"

analysis_rc=0
python3 "$D/i125b_analyze.py" "$OUT" > "$OUT/analysis.txt" || analysis_rc=$?
cat "$OUT/analysis.txt"

# Leave the exact interactive treatment requested by the owner. The benchmark
# payload is disarmed and the chroma probe is restored for the next fresh login.
remote 'sudo rm -f /etc/xrdp-e52-payload && \
        rm -f /home/ubuntu/.config/autostart/chroma-probe.desktop && \
        sudo /usr/local/libexec/xrdp-benchmark-profile sparse-w2'

trap - EXIT INT TERM
cleanup
echo "capture: $OUT"
exit "$analysis_rc"
