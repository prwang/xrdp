#!/bin/sh
# start_test_env.sh - bring the xrdp GUI test container back up after a restart.
#
# Restores the SSH firewall rules and starts xrdp-sesman + xrdp. Idempotent:
# safe to re-run; it won't double-start daemons or duplicate firewall rules.
#
# Persists nothing itself - the package install, iptables rules, and running
# daemons do not survive a container restart, so run this after each start.
#
# Usage: sudo sh /work/PR-demo/lib/incus_user_container/start_test_env.sh
#        (must be root: needs NET_ADMIN + daemons)

set -e

RULES=/work/PR-demo/lib/incus_user_container/iptables.rules
GW=10.177.0.1

if [ "$(id -u)" -ne 0 ]; then
    echo "[!] must run as root (needs NET_ADMIN and to start daemons)" >&2
    exit 1
fi

# --- 1. Firewall: restrict new SSH to the gateway -------------------------------
if command -v iptables >/dev/null 2>&1; then
    if [ -f "$RULES" ]; then
        iptables-restore < "$RULES"
        echo "[+] firewall restored from $RULES"
    else
        # No snapshot present - re-apply the scoped port-22 rules from scratch.
        iptables -A INPUT -i lo -p tcp --dport 22 -j ACCEPT
        iptables -A INPUT -p tcp --dport 22 -m conntrack --ctstate ESTABLISHED,RELATED -j ACCEPT
        iptables -A INPUT -p tcp --dport 22 -s "$GW" -j ACCEPT
        iptables -A INPUT -p tcp --dport 22 -j DROP
        iptables-save > "$RULES"
        echo "[+] firewall rules applied (port 22 -> gateway $GW only) and saved to $RULES"
    fi
else
    echo "[!] iptables not installed - SSH is NOT restricted to the gateway." >&2
    echo "    install with: apt-get install -y iptables, then re-run this script." >&2
fi

# --- 2. Daemons: xrdp-sesman then xrdp ------------------------------------------
if pgrep -x xrdp-sesman >/dev/null 2>&1; then
    echo "[=] xrdp-sesman already running"
else
    xrdp-sesman
    echo "[+] xrdp-sesman started"
fi
sleep 1

if pgrep -x xrdp >/dev/null 2>&1; then
    echo "[=] xrdp already running"
else
    xrdp
    echo "[+] xrdp started"
fi
sleep 1

# --- 3. Verify ------------------------------------------------------------------
echo
echo "=== listeners ==="
if ss -ltn 2>/dev/null | grep -qE '127\.0\.0\.1:3389'; then
    echo "[+] RDP listening on 127.0.0.1:3389 (localhost-only)"
else
    echo "[!] RDP NOT listening on 127.0.0.1:3389 - check /var/log/xrdp.log" >&2
fi
if ss -ltn 2>/dev/null | grep -qE '0\.0\.0\.0:3389|\*:3389|\[::\]:3389'; then
    echo "[!] WARNING: 3389 is bound to a non-loopback address - check xrdp.ini [Globals] port=" >&2
fi

echo
echo "Connect:  ssh -N -L 127.0.0.1:3389:127.0.0.1:3389 tester@<host>"
echo "          then RDP to 127.0.0.1:3389  (user: tester, blank password, session Xorg)"
