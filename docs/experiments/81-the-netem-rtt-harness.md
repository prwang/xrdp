# #81 — the netem RTT harness, and the retirement of the ack-delay proxy

**Status:** harness built and self-checked GREEN 2026-08-03
(`PR-demo/mac_bisect_matrix/netem_rtt.sh`). First use:
`docs/experiments/80-the-credit-frontier.md`, "Step 4".

Records in this directory are kept verbatim. Supersede with a dated
note; do not tidy.

---

## What it does, and why both directions

`netem_rtt.sh` puts a `tc netem` delay on a fleet pod's veth pair, from
the host, so an arm can be measured on something that behaves like a WAN
without leaving the box.

The delay is split in half and applied twice — once on the host end of
the veth pair, once on `eth0` inside the pod's network namespace. Each
netem instance delays what *leaves* its interface, so a packet crosses
one on the way out and the other on the way back and the round trip
picks up the whole RTT. No `ifb` mirred redirect is involved and none is
needed.

Both directions is the point. The instrument it replaces delayed one
direction, which produces a link where video arrives instantly and acks
come back late. That isolates the ack variable — useful for a mechanism
test — but it is not an environment, and PRD FR-FLOW-1 clause 4 requires
the shipped window C to be chosen against an environment.

## The two things that went wrong while building it, both worth keeping

**1. `nsenter -n` does not change the mount namespace.** The first
version read the pod's veth peer index from
`/sys/class/net/eth0/iflink` inside `nsenter -t PID -n`. That returns a
number — the wrong one. sysfs is still the host's, so the value read was
the *host's own* eth0 peer (ifindex 87), and the harness aborted with
"cannot find host veth". It failed loudly, which was luck: had the host
happened to have an interface at that index, the harness would have
shaped an unrelated link and every capture taken afterwards would have
been measuring an RTT it did not have. The fix is to ask `ip` inside the
namespace (`ip -o link show eth0` → `eth0@if5`) rather than a filesystem
that did not come with it.

There were **six veths on this box for four pods** — stale ones survive
pod restarts — so resolving by name or by creation order was never an
option either. Identity, via the peer ifindex, is the only safe route.

**2. ICMP to the pod IP is not the path under test.** Steps 1 and 2 of
the self-check measure `ping` to the pod's address. RDP arrives at
`127.0.0.1:<hostPort>`, is DNAT'd by k3s and routed to the pod — a
different address, and in principle a different path. Step 4 of the
self-check therefore times a TCP handshake through the port the client
rig actually dials:

```
4a. tcp connect to 127.0.0.1:40030 -- baseline 0.31 ms, at RTT 40 40.37 ms
4b. OK -- the delay is on the path the RDP client dials
```

That is the 2026-07-31 mode-name lesson applied to `tc`: a knob that was
set is not an RTT that exists, and an RTT that exists on one path is not
an RTT on another.

## The self-check

`netem_rtt.sh selftest <arm>` — 22 s, GREEN, run on an idle arm before
every campaign by `i80_wan_pair.sh`:

1. a requested 40 ms appears as a measured 40.4 ms round trip, and the
   baseline it started from was 0.07 ms;
2. `clear` restores both interfaces to the exact qdisc lines they had
   and the RTT to baseline;
3. `apply` **refuses** an interface carrying a qdisc the harness did not
   create (tested with a foreign `tbf`);
4. the delay is present on the RDP port, not merely on ICMP.

Statelessness is enforced rather than intended: every qdisc the harness
creates carries handle `8081:`, `clear` removes only that handle, and
`i80_wan_pair.sh` clears on EXIT/INT/TERM. A leftover qdisc is a WAN
nobody declared, invisible in every capture taken afterwards.

## The ack-delay proxy is retired (owner directive, 2026-08-03)

`ack_delay_proxy.c`, `ack_delay_proxy_selftest.py` and
`ack_delay_sweep.sh` are deleted. Two reasons, the owner's: it burns
100 % of a core when idle, and it overlaps with what netem now does
properly. `i79_ack_delay_analyze.py` is **kept** — it is the measurement
layer (`load`, `summarize`, `discriminate`) that the #80 head-to-head
imports, and the head-to-head is only meaningful if both builds' numbers
come out of the same code.

### Are the i79 results void?

**No, and here is the reasoning, because the rule that would void them
is severe.** CLAUDE.md says a result whose INSTRUMENT was on the
measured path is deleted rather than superseded. A proxy burning a core
on the same box as the measurement is exactly that shape, so this had to
be settled before the #80 head-to-head could quote x017's numbers.

What settles it is the control leg the sweep was built with. `d0` runs
*through* the proxy at zero delay; `direct` has no proxy in the path at
all. If the proxy were burning a core during a leg, `d0` would diverge
from `direct`. Recorded 2026-08-02, 20 s each: period mean **22.5 ms vs
21.5 ms, 4.3 % apart** — the control passed. A spinning core would not
have left a 4.3 % gap.

**What I could not do is reproduce the 100 % CPU.** Three idle states
were probed on 2026-08-03 (listening with no connection; one connection
open and silent; the remote closing while the client stayed open) and
utime+stime stayed at 0 ticks over 5 s in all three. So the state that
burns the core is not identified here. If the owner observed it *during*
a leg rather than between legs, the control-leg argument above is the
thing to re-examine — and the #80 head-to-head quotes only the `direct`
leg, which had no proxy in the path under any hypothesis.

The #80 golden replay in `tests/xrdp/test_avc444_credit_frontier.c` uses
the event ORDER from `leg_d40`. That ordering is a sequence of xrdp's own
internal events; its expected values are hand-derived from FR-FLOW-1, not
read off the capture. CPU contention could change the intervals between
those events; it cannot invent an ordering the pipeline does not produce.
