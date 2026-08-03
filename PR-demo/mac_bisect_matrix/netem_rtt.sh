#!/bin/bash
# netem_rtt.sh -- BACKLOG #81: WAN RTT simulation on a fleet pod's network
# namespace, applied from the host with `tc netem`.
#
# WHAT IT DOES, IN ONE SENTENCE. It makes bytes take longer to travel
# between the client rig on this host and one arm's xrdp pod, in BOTH
# directions, so an arm can be measured on something that behaves like a
# WAN without leaving the box.
#
# WHY BOTH DIRECTIONS. The thing being simulated is a round trip. A
# delay applied to one direction only produces a link where video arrives
# instantly and acks come back late -- that is a MECHANISM instrument (it
# isolates the ack variable), not an environment. #80's shipped default
# for the wire window C has to be derived from an environment, so the
# delay is split in half and applied to each direction separately:
#
#     host side:  tc qdisc ... dev <veth>  root netem delay RTT/2
#     pod side:   tc qdisc ... dev eth0    root netem delay RTT/2
#
# Each netem instance delays what leaves that interface, so a packet
# crosses one of them on the way out and the other on the way back, and
# the round trip picks up RTT. There is no ifb mirred redirect anywhere
# here and there does not need to be one.
#
# WHERE THE PACKETS ACTUALLY GO. The client connects to
# 127.0.0.1:<hostPort>; k3s DNATs that to the pod IP and routes it out
# cni0 into the pod's veth pair. Both halves of the pair are in the path,
# one of them in the host namespace and one inside the pod, which is
# exactly the two places the qdiscs go.
#
# STATELESSNESS, and it is the load-bearing property (owner directive,
# 2026-07-31, the client-rig statelessness rule). A leftover qdisc from a
# previous run is a WAN nobody declared, and it would be invisible in
# every capture taken afterwards. So:
#   * every qdisc this script creates carries handle 8081:, and `clear`
#     removes only qdiscs with that handle;
#   * `apply` REFUSES to touch an interface that already has a root qdisc
#     which is neither a kernel default (noqueue / pfifo_fast / mq) nor
#     ours -- it never silently replaces someone else's shaping;
#   * `apply` always verifies the resulting RTT by MEASUREMENT (ping
#     through the same veth pair) and fails if the measurement disagrees
#     with what was asked for. A knob that was set is not an RTT that
#     exists; this is the 2026-07-31 mode-name lesson applied to tc.
#
# SERVER SIDE ONLY. Nothing here touches the client rig, xfreerdp, the
# Xvfb displays or the host's own xrdp. The only interfaces named are the
# two ends of one pod's veth pair.
#
# Usage:
#   netem_rtt.sh apply  <arm> <rtt_ms> [jitter_ms] [loss_pct]
#   netem_rtt.sh verify <arm> <expect_rtt_ms>
#   netem_rtt.sh clear  <arm>
#   netem_rtt.sh status <arm>
#   netem_rtt.sh selftest <arm>      -- the <=60 s self-check #81 names
#
# rtt_ms 0 means "no shaping": apply clears any qdisc of ours and then
# MEASURES the loopback baseline, so an RTT=0 leg still records what its
# RTT actually was rather than assuming zero.

set -u

NS=${NETEM_NS:-bisect-matrix}
HANDLE=8081
# ping tolerance: netem's own timer granularity plus scheduling noise.
# Absolute floor in ms, or a fraction of the requested RTT, whichever is
# larger -- 2 ms is meaningless at RTT=150 and generous at RTT=10.
TOL_ABS_MS=${NETEM_TOL_ABS_MS:-3}
TOL_FRAC=${NETEM_TOL_FRAC:-0.15}

fail()
{
    echo "netem_rtt: ABORT: $*" >&2
    exit 1
}

need_root()
{
    [ "$(id -u)" = 0 ] || fail "must run as root (tc + nsenter)"
}

# --- resolving one arm to (pod ip, container pid, host-side veth) -------

pod_name()
{
    kubectl -n "$NS" get pod -l "arm=$1" \
        -o jsonpath='{.items[0].metadata.name}' 2>/dev/null
}

pod_ip()
{
    kubectl -n "$NS" get pod -l "arm=$1" \
        -o jsonpath='{.items[0].status.podIP}' 2>/dev/null
}

# The container's PID in the host namespace, via crictl. kubectl exec is
# deliberately NOT used: it depends on iproute2 being inside the image,
# and the image is a thing under test.
pod_pid()
{
    local pod cid
    pod=$(pod_name "$1")
    [ -n "$pod" ] || return 1
    cid=$(crictl ps --name xrdp -o json 2>/dev/null | python3 -c "
import json, sys
d = json.load(sys.stdin)
for c in d['containers']:
    if c['labels'].get('io.kubernetes.pod.name') == '$pod':
        print(c['id'])
        break
")
    [ -n "$cid" ] || return 1
    crictl inspect "$cid" 2>/dev/null | python3 -c "
import json, sys
print(json.load(sys.stdin)['info']['pid'])
"
}

# The host end of the pod's veth pair, found by IDENTITY rather than by
# name: eth0 inside the netns reports its peer's ifindex in iflink, and
# exactly one host interface has that ifindex. Six veths were present on
# this box for four pods (stale ones survive pod restarts), so guessing
# by name or by creation order is not safe.
# NOTE: read the peer index with `ip`, never with
# /sys/class/net/eth0/iflink. `nsenter -n` swaps the network namespace but
# NOT the mount namespace, so sysfs still shows the HOST's interfaces and
# that path silently returns the host's own eth0 peer -- a wrong answer
# that looks like a right one (it did, on the first run of this harness).
host_veth()
{
    local pid link
    pid=$1
    link=$(nsenter -t "$pid" -n ip -o link show eth0 2>/dev/null \
           | sed -n 's/^[0-9]*: eth0@if\([0-9]*\):.*/\1/p')
    [ -n "$link" ] || return 1
    local i
    for i in /sys/class/net/*/ifindex
    do
        if [ "$(cat "$i")" = "$link" ]
        then
            basename "$(dirname "$i")"
            return 0
        fi
    done
    return 1
}

# --- qdisc inspection ---------------------------------------------------

# prints the root qdisc line for an interface, host side or pod side
qdisc_show()
{
    local side=$1 pid=$2 dev=$3
    if [ "$side" = pod ]
    then
        nsenter -t "$pid" -n tc qdisc show dev "$dev" 2>/dev/null | head -1
    else
        tc qdisc show dev "$dev" 2>/dev/null | head -1
    fi
}

# 0 = free to use (kernel default or already ours), 1 = someone else's
qdisc_is_ours_or_default()
{
    local line=$1
    case "$line" in
        *"qdisc noqueue "*|*"qdisc pfifo_fast "*|*"qdisc mq "*) return 0 ;;
        *"qdisc netem $HANDLE:"*) return 0 ;;
        "") return 0 ;;
        *) return 1 ;;
    esac
}

qdisc_del()
{
    local side=$1 pid=$2 dev=$3
    if [ "$side" = pod ]
    then
        nsenter -t "$pid" -n tc qdisc del dev "$dev" root 2>/dev/null
    else
        tc qdisc del dev "$dev" root 2>/dev/null
    fi
    return 0
}

qdisc_add()
{
    local side=$1 pid=$2 dev=$3 us=$4 jit=$5 loss=$6
    local args="delay ${us}us"
    if [ "$jit" != 0 ]
    then
        args="$args ${jit}ms distribution normal"
    fi
    if [ "$loss" != 0 ]
    then
        args="$args loss ${loss}%"
    fi
    if [ "$side" = pod ]
    then
        # shellcheck disable=SC2086
        nsenter -t "$pid" -n tc qdisc add dev "$dev" root \
            handle "$HANDLE:" netem $args
    else
        # shellcheck disable=SC2086
        tc qdisc add dev "$dev" root handle "$HANDLE:" netem $args
    fi
}

# The loopback port the client rig dials for this arm. Read from the
# committed manifest, which is the file that decides it.
pod_host_port()
{
    local d
    d=$(cd "$(dirname "$0")" && pwd)
    sed -n 's/^ *hostPort: *\([0-9]*\).*/\1/p' "$d/k8s/$1.yaml" 2>/dev/null \
        | head -1
}

# --- measurement --------------------------------------------------------

# median TCP handshake time in ms to 127.0.0.1:<port>. One handshake is
# one round trip, so this is an RTT measurement taken on the RDP path.
tcp_connect_ms()
{
    python3 - "$1" <<'PYEOF'
import socket, statistics, sys, time
port = int(sys.argv[1])
ts = []
for _ in range(7):
    t = time.perf_counter()
    try:
        s = socket.create_connection(("127.0.0.1", port), timeout=5)
    except OSError:
        print("nan")
        sys.exit(0)
    ts.append((time.perf_counter() - t) * 1000.0)
    s.close()
    time.sleep(0.05)
print("%.2f" % statistics.median(ts))
PYEOF
}


# Measures the RTT through the SAME veth pair the RDP bytes cross, by
# pinging the pod IP from the host namespace. Prints the average in ms.
measure_rtt()
{
    local ip=$1 n=${2:-10}
    ping -n -q -c "$n" -i 0.2 -W 3 "$ip" 2>/dev/null \
        | awk -F'/' '/^(rtt|round-trip)/ {print $5}'
}

# --- commands -----------------------------------------------------------

resolve()
{
    ARM=$1
    PIP=$(pod_ip "$ARM")
    [ -n "$PIP" ] || fail "arm $ARM: no pod IP (is it deployed in ns $NS?)"
    PPID_NS=$(pod_pid "$ARM") || fail "arm $ARM: cannot find container pid"
    [ -n "$PPID_NS" ] || fail "arm $ARM: cannot find container pid"
    VETH=$(host_veth "$PPID_NS") \
        || fail "arm $ARM: cannot find host veth for pid $PPID_NS"
}

cmd_status()
{
    resolve "$1"
    echo "arm       $ARM"
    echo "pod ip    $PIP"
    echo "pod pid   $PPID_NS"
    echo "host veth $VETH"
    echo "host qdisc  $(qdisc_show host "$PPID_NS" "$VETH")"
    echo "pod  qdisc  $(qdisc_show pod "$PPID_NS" eth0)"
    echo "measured rtt $(measure_rtt "$PIP" 10) ms"
}

cmd_clear()
{
    resolve "$1"
    local h p
    h=$(qdisc_show host "$PPID_NS" "$VETH")
    p=$(qdisc_show pod "$PPID_NS" eth0)
    case "$h" in *"netem $HANDLE:"*) qdisc_del host "$PPID_NS" "$VETH" ;; esac
    case "$p" in *"netem $HANDLE:"*) qdisc_del pod "$PPID_NS" eth0 ;; esac
    echo "netem_rtt: cleared $ARM ($VETH + pod eth0)"
    echo "host qdisc  $(qdisc_show host "$PPID_NS" "$VETH")"
    echo "pod  qdisc  $(qdisc_show pod "$PPID_NS" eth0)"
}

cmd_apply()
{
    local arm=$1 rtt=$2 jit=${3:-0} loss=${4:-0}
    resolve "$arm"

    local h p
    h=$(qdisc_show host "$PPID_NS" "$VETH")
    p=$(qdisc_show pod "$PPID_NS" eth0)
    qdisc_is_ours_or_default "$h" \
        || fail "host $VETH already carries a qdisc this harness did not
        create -- refusing to replace it: $h"
    qdisc_is_ours_or_default "$p" \
        || fail "pod eth0 already carries a qdisc this harness did not
        create -- refusing to replace it: $p"

    # always start from a known state: our own leftovers are removed
    case "$h" in *"netem $HANDLE:"*) qdisc_del host "$PPID_NS" "$VETH" ;; esac
    case "$p" in *"netem $HANDLE:"*) qdisc_del pod "$PPID_NS" eth0 ;; esac

    if [ "$rtt" = 0 ]
    then
        local base
        base=$(measure_rtt "$PIP" 20)
        echo "netem_rtt: $arm RTT=0 leg -- no shaping applied"
        echo "netem_rtt: measured loopback baseline rtt ${base} ms"
        echo "$base"
        return 0
    fi

    # half the round trip on each side; microseconds so odd values are
    # not silently rounded to a different RTT than the one requested
    local half_us
    half_us=$(python3 -c "print(int(round($rtt * 1000 / 2.0)))")
    qdisc_add host "$PPID_NS" "$VETH" "$half_us" "$jit" "$loss" \
        || fail "tc failed on host $VETH"
    qdisc_add pod "$PPID_NS" eth0 "$half_us" "$jit" "$loss" \
        || { qdisc_del host "$PPID_NS" "$VETH"; fail "tc failed in pod netns"; }

    echo "netem_rtt: $arm applied ${rtt} ms RTT" \
         "(${half_us} us each way, jitter ${jit} ms, loss ${loss} %)"
    cmd_verify "$arm" "$rtt"
}

cmd_verify()
{
    local arm=$1 want=$2
    [ -n "${PIP:-}" ] || resolve "$arm"
    local got
    got=$(measure_rtt "$PIP" 20)
    [ -n "$got" ] || fail "ping to $PIP produced no rtt line"
    local ok
    ok=$(python3 -c "
want = float('$want')
got = float('$got')
tol = max(float('$TOL_ABS_MS'), want * float('$TOL_FRAC'))
print('OK' if abs(got - want) <= tol else 'BAD')
print('%.3f' % tol)
")
    local verdict tol
    verdict=$(echo "$ok" | head -1)
    tol=$(echo "$ok" | tail -1)
    echo "netem_rtt: requested ${want} ms, measured ${got} ms," \
         "tolerance ${tol} ms -> $verdict"
    [ "$verdict" = OK ] || fail "applied RTT does not match the request"
    echo "$got"
}

# The <=60 s self-check BACKLOG #81 names. It proves the three things the
# harness claims, on a pod that is not running a measurement:
#   1. a requested RTT appears as a MEASURED RTT (both directions),
#   2. clear() restores the interface to what it was,
#   3. apply() refuses an interface carrying somebody else's qdisc.
cmd_selftest()
{
    local arm=$1
    resolve "$arm"
    echo "=== netem_rtt selftest on arm $arm ($VETH, pod $PIP) ==="

    local before_h before_p
    before_h=$(qdisc_show host "$PPID_NS" "$VETH")
    before_p=$(qdisc_show pod "$PPID_NS" eth0)
    echo "before: host [$before_h]"
    echo "before: pod  [$before_p]"

    local base
    base=$(measure_rtt "$PIP" 20)
    echo "1a. baseline rtt ${base} ms"

    local m40
    cmd_apply "$arm" 40 > /dev/null || fail "selftest: apply 40 failed"
    m40=$(measure_rtt "$PIP" 20)
    echo "1b. after apply 40: measured ${m40} ms"
    python3 -c "
import sys
base, got = float('$base'), float('$m40')
if not (37.0 <= got <= 46.0):
    sys.exit('selftest RED: 40 ms requested, measured %.3f ms' % got)
if got - base < 30.0:
    sys.exit('selftest RED: delay did not take (base %.3f -> %.3f)'
             % (base, got))
" || fail "selftest step 1 RED"
    echo "1c. OK -- the requested delay is present as a measured round trip"

    cmd_clear "$arm" > /dev/null
    local after_h after_p mclr
    after_h=$(qdisc_show host "$PPID_NS" "$VETH")
    after_p=$(qdisc_show pod "$PPID_NS" eth0)
    mclr=$(measure_rtt "$PIP" 20)
    echo "2a. after clear: host [$after_h]"
    echo "2b. after clear: pod  [$after_p]"
    echo "2c. after clear: measured ${mclr} ms"
    [ "$after_h" = "$before_h" ] \
        || fail "selftest RED: host qdisc not restored"
    [ "$after_p" = "$before_p" ] \
        || fail "selftest RED: pod qdisc not restored"
    python3 -c "
import sys
base, got = float('$base'), float('$mclr')
if got - base > 5.0:
    sys.exit('selftest RED: rtt did not return to baseline '
             '(%.3f -> %.3f)' % (base, got))
" || fail "selftest step 2 RED"
    echo "2d. OK -- stateless: interfaces and rtt back to where they were"

    # 3. refuse a foreign qdisc. A plain tbf with a handle that is not
    # ours stands in for "somebody else is shaping this interface".
    tc qdisc add dev "$VETH" root handle 9999: tbf rate 100mbit \
        burst 32kbit latency 50ms 2>/dev/null \
        || fail "selftest: could not install the foreign qdisc"
    # a SUBSHELL, because the refusal path is fail() -> exit, and an
    # `exit` inside a plain function call would take the selftest with it
    # instead of being caught as a non-zero result
    if ( cmd_apply "$arm" 40 ) > /dev/null 2>&1
    then
        tc qdisc del dev "$VETH" root 2>/dev/null
        fail "selftest RED: apply() replaced a foreign qdisc"
    fi
    tc qdisc del dev "$VETH" root 2>/dev/null
    echo "3.  OK -- apply() refuses an interface it does not own"

    # 4. THE ONE THAT MATTERS. Steps 1-2 measured ICMP to the pod IP.
    # What is under test is RDP, which arrives at 127.0.0.1:<hostPort>
    # and reaches the pod through a DNAT and a route -- a different
    # address, and in principle a different path. This step times a TCP
    # handshake through the port the client rig actually dials, so the
    # claim "the arm is behind N ms of RTT" is measured where the bytes
    # go rather than inferred from where the qdiscs are.
    local hport
    hport=$(pod_host_port "$arm")
    if [ -n "$hport" ]
    then
        local t_base t_delay
        t_base=$(tcp_connect_ms "$hport")
        cmd_apply "$arm" 40 > /dev/null || fail "selftest: apply 40 failed"
        t_delay=$(tcp_connect_ms "$hport")
        cmd_clear "$arm" > /dev/null
        echo "4a. tcp connect to 127.0.0.1:$hport --" \
             "baseline ${t_base} ms, at RTT 40 ${t_delay} ms"
        python3 -c "
import sys
base, got = float('$t_base'), float('$t_delay')
if got - base < 30.0:
    sys.exit('selftest RED: the RDP port is NOT behind the delay '
             '(%.2f -> %.2f ms)' % (base, got))
if got > 60.0:
    sys.exit('selftest RED: handshake cost %.2f ms, more than one rtt'
             % got)
" || fail "selftest step 4 RED"
        echo "4b. OK -- the delay is on the path the RDP client dials"
    else
        echo "4.  SKIPPED -- no hostPort found for $arm in k8s/"
    fi

    local final_h final_p
    final_h=$(qdisc_show host "$PPID_NS" "$VETH")
    final_p=$(qdisc_show pod "$PPID_NS" eth0)
    [ "$final_h" = "$before_h" ] \
        || fail "selftest RED: host qdisc left dirty: $final_h"
    [ "$final_p" = "$before_p" ] \
        || fail "selftest RED: pod qdisc left dirty: $final_p"
    echo "=== netem_rtt selftest GREEN ==="
}

need_root
[ $# -ge 2 ] || {
    sed -n '55,64p' "$0" >&2
    exit 2
}
CMD=$1
shift
case "$CMD" in
    apply)    cmd_apply "$@" ;;
    verify)   cmd_verify "$@" ;;
    clear)    cmd_clear "$@" ;;
    status)   cmd_status "$@" ;;
    selftest) cmd_selftest "$@" ;;
    *)        fail "unknown command $CMD" ;;
esac
