# i80_wanpair_20260803_125816_s20 — the credit frontier, first live run

BACKLOG #80 step 4, on the #81 netem harness. Two legs, 20 s each,
sequential, the arm count the owner approved on 2026-08-03.

| leg | arm | port | RTT requested | RTT measured (RDP path) |
|-----|-----|------|---------------|-------------------------|
| lan | x018 | 40034 | none (loopback baseline) | 0.078 ms |
| wan | x019 | 40035 | 40 ms, netem both directions | 40.490 ms before, 40.428 ms after |

Deployed on both: image `localhost/xrdp-bisect:1d5bc0960db8.xx10fa3aa-tf`,
`xrdp-dev 0.10.80+git20260803024109.1d5bc0960db8` (the credit frontier),
`xorgxrdp-dev 1:0.10.80+git20260731212221.10fa3aa23033` (unchanged from
x014/x015/x017 — #80 needed no producer change). `eager_slot_ack = true`,
`wire_window = 1`. Single monitor 3840×2400, `SESSION_KIND=textflood`,
oracle client on the host. Both arms certified at deploy
(`certs/x018.cert`, `certs/x019.cert`, 7/7 asserts, 0 black frames).

Reproduce: `./i80_wan_pair.sh 20`
Analyse:   `./i80_wan_pair_analyze.py x018-lan=leg_lan x019-wan40=leg_wan \
             x017-direct=../i79_x017_ackdelay_20260802_s20/leg_direct`

## The head-to-head, LAN against LAN

x017 `direct` is the same payload, geometry, monitor count, client rig
and xorgxrdp on the OLD build at `fif = 1`, with nothing in the network
path. It is the only apples-to-apples comparison in this capture.

| | x017 direct (old) | x018 (frontier, C=1) | |
|---|---|---|---|
| withheld p90 | 35.3 ms | **10.6 ms** | −70 % |
| withheld mean | 8.45 ms | **3.46 ms** | −59 % |
| stalls (withheld > 10 ms) | 29.7 % | **18.2 %** | −39 % |
| period mean | 21.5 ms | **18.5 ms** | −14 % |
| period p90 | 42.7 ms | **25.9 ms** | −39 % |
| period p99 | 52.2 ms | **30.4 ms** | −42 % |
| period p90/p50 | 2.51 | **1.49** | |
| throughput | 46.5 /s | **54.1 /s** | +16 % |

`withheld` p50 is 0.03 ms on BOTH builds — it was never the discriminator
and the prediction should not have used it. The tail is where the old
build's cross-layer gate lived and the tail is what moved.

## Where the numbers close (quality gate 1)

* 3386 KiB on the wire per frame at 3840×2400 AVC444 — the "~3.4 MB per
  4K frame" #80's filing predicted, measured.
* wan leg: 4.9 MB mean queued behind egress, drained at
  278 × 3.49 MB / 16.6 s = 58 MB/s ⇒ **84 ms**. Measured
  egress→client-ack is 122.9 ms against a 40.4 ms link. 40 + 84 = 124.
* wan leg period model: capture k needs the client to have acked k−3, so
  3P = L + ack latency. 3 × 58.6 = 175.8 against 43.7 + 122.9 = 166.6 —
  closes within 6 %.
* No derived interval came out negative on any leg (gate 2c).

## Uncontrolled variables, stated rather than assumed away

* **Pod identity is not held fixed across the pair.** x018 and x019 are
  separate pods with identical images and identical gfx.toml bodies
  (diff-verified). Two pods were used so neither leg has to add and
  remove a qdisc between measurements; the cost is this variable.
* **There is no old-build leg under netem.** x017's D = 40 leg used
  `ack_delay_proxy`, which delayed client→server bytes only, above TLS;
  netem delays both directions below TCP. Different instruments, not
  comparable (gate 5). It appears in the analysis output labelled
  `x017-d40prox` as a reference, never as this pair's control.
* **`trans::wait_bytes` does not exist on the old build**, so the queued
  column is blank for x017 rather than zero. Nothing here says whether
  the old build queued more or less.
* **The residual 18.2 % of stalled cycles on the LAN leg is not
  attributed.** The ack record carries the frontier's three inputs at
  the instant of emission, and at that instant all three are equal
  (157/157 ties), so the record cannot say which term had been holding.
  See the experiments file for the arithmetic that makes `client + C`
  the plausible candidate and for what would settle it.
