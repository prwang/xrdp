# i79_x017_ackdelay_20260802_s20 — #79 layer 1: the mechanism, tested by intervention

**What this run decides.** #78 concluded from traces that fif = 1's
throughput tail is xrdp withholding the producer's slot-recycle credit
until the CLIENT's frame ack re-opens `xrdp_gfx_ack_window_open()`.
That was an inference. This sweep delays the client's acks on purpose,
on the **unmodified deployed build**, and asks whether the machine
responds the way the theory demands.

**Verdict: the causal chain is confirmed. Two of the four predictions
were quantitatively wrong, in an informative direction — the effect is
not a random tail but a deterministic period-3 limit cycle.**

## Conditions

* arm **x017**, `localhost/xrdp-bisect:661ff5fc64fa.xx10fa3aa-tf`,
  `XRDP_GFX_FRAMES_IN_FLIGHT=1`, xorgxrdp `10fa3aa23033`. Certificate
  `certs/x017.cert` current (ASSERT VERDICT PASS 7/7); E2 counters zero
  in every leg.
* one monitor 3840×2400, `SESSION_KIND=textflood`, oracle client, cold
  session per leg, **20 s per leg** (see "sample size" below), fleet
  otherwise idle (`fleet_sessions_before.txt`: no Xorg in any pod).
* **Host DVFS pinned** (owner-set, 2026-08-02): GPU
  `power_dpm_force_performance_level=high`, SCLK active level 2900 MHz;
  CPU `performance`. A pinned run is its own condition — do not compare
  these numbers with pre-pin captures except where noted.
* The only thing that differs between legs is when the client's bytes
  reach the server. Same server deb, same `gfx.toml`, same client
  binary, same payload, same geometry.

## The instrument

`ack_delay_proxy.c` — a loopback TCP proxy that forwards
server→client immediately and releases each client→server read D ms
after it arrived. It does not parse RDP (the connection is TLS); it
delays the whole client→server direction, which on an oracle session
carries frame acks and nothing else. Measured: **775 client→server
chunks for 751 frames** at D = 0, ~70 B each — one ack PDU per frame,
so what was delayed is the acks.

Three checks before it was trusted, all in `proxy_selftest_*.txt`:
applied delay matches the knob to ±0.2 ms, D = 0 adds 0.037 ms of RTT,
and the server→client direction sustains ≥ 3.3 GB/s (the session pushes
153 MB/s). Its own per-leg telemetry is in `leg_*/proxy.log`; the
in-server check that the delay applied is P0 below, which does not
trust the proxy at all.

## Sample size — why 20 s and not the 5 s the item specified

A first sweep at 5 s/leg is kept in `../i79_x017_ackdelay_20260802_s5`.
It is unreadable and says so: the gate spends most of a 5 s window on
login, so each leg produced **45–51 usable cycles**, and the control leg
came out 18.5 % apart with `egress→cliack` 7 ms *lower* through the
proxy — a difference no proxy can cause. A distribution whose signal is
a 31 % tail cannot be compared at n = 45. 20 s/leg gives 391–746 cycles
and the control closes to 4.3 %. This is a sample-size correction to the
same five legs, not an added arm or condition.

## Result

| leg | D | cycles | egress→cliack p50 | withheld p50 / p90 | withheld > 10 ms | period mean / p50 / p90 | worker wait mean |
|---|---|---|---|---|---|---|---|
| direct | — | 746 | 7.6 | 0.03 / 35.3 | 29.7 % | 21.5 / 17.1 / 42.7 | 4.9 |
| d0 | 0 | 714 | 10.1 | 0.04 / 35.4 | 36.9 % | 22.5 / 17.1 / 43.6 | 5.9 |
| d10 | 10 | 569 | 17.1 | 24.10 / 43.3 | 58.0 % | 27.9 / 18.1 / 52.4 | 11.4 |
| d20 | 20 | 480 | 27.8 | 35.99 / 54.7 | 66.7 % | 33.1 / 18.4 / 63.8 | 16.4 |
| d40 | 40 | 391 | 48.1 | 56.96 / 77.1 | 66.8 % | 40.1 / 18.8 / 85.6 | 23.2 |

`withheld` = credit emission − `absorb(k−2)`, the interval between the
in-tree safety condition being met and the credit reaching the wire.
Zero negative values in every leg (2c). Full output: `ANALYSIS.txt`.

**CONTROL.** Proxy at D = 0 vs no proxy: period 22.5 vs 21.5 ms (4.3 %),
withheld p90 35.4 vs 35.3, stall fraction 36.9 vs 29.7 %. The proxy is
not the confound, and both legs reproduce #78 Run A (22.6 ms, 31.1 %,
p90 36.4) taken at 60 s two hours earlier.

**P0 — the delay applied, measured inside the server.** `egress→cliack`
p50 rose +7.0 / +17.8 / +38.0 ms for D = 10 / 20 / 40. Not the proxy's
own claim: the server's own trace.

**P1 — "withheld p50 ≈ D": FALSIFIED, and the error is informative.**
Measured 24.1 / 36.0 / 57.0 against a predicted 10 / 20 / 40. The
*slope* is right (Δwithheld/ΔD = 1.10 between D = 10 and D = 40) but
there is a ~13 ms offset. The prediction was written as if only the ack
moved; in a closed loop the rate also drops, so the ack that opens the
window is itself emitted later. The right prediction was about the
stalled subpopulation, not the median.

**P2 — "stall fraction → ~100 %": FALSIFIED, and this is the finding.**
It saturates at exactly 2/3. The reason is visible in the per-capture
pattern: at D ≥ 20 the system locks into a **period-3 limit cycle**,
`SS.SS.SS.` — two captures whose credit is withheld, then one whose
credit is prompt, with **zero exceptions** (D = 20: 160 runs of `SS`
and 160 singleton `.`; D = 40: 131 and 130). At D = 0 the same motif is
there but intermittent (115 `SS` runs, 33 singletons, irregular gaps) —
that intermittency is the "31 % tail" #76 was filed on. **The tail is
not a random race; it is a deterministic cycle that the ack race
modulates.**

**P3 — the period rises with D: CONFIRMED.** 22.5 → 27.9 → 33.1 →
40.1 ms, i.e. 0.44 ms of frame period per ms of ack delay.

## The discriminator: the slot credit, not the send path

Delaying acks would also slow the server if **egress** were gated by the
same window. That rival predicts the same period curve. Two measurements
separate them:

| leg | sends by (id_server − id_client) at send time | period of cycles whose credit was withheld | period of cycles whose credit was prompt |
|---|---|---|---|
| direct | 0:2223 1:933 2:4 | 32.7 (n=221) | 16.8 (n=524) |
| d0 | 0:1884 1:1116 2:4 | 31.9 (n=263) | 16.9 (n=450) |
| d10 | 0:1001 1:1395 2:4 | 36.1 (n=329) | 16.6 (n=239) |
| d20 | 0:676 1:975 2:373 | 41.5 (n=320) | 16.3 (n=159) |
| d40 | 0:556 1:552 2:552 | 52.0 (n=260) | 16.4 (n=130) |

1. **Egress is not ack-gated, and less so as D grows.** At D = 40 exactly
   one third of sends go out with 2 frames unacked and another third
   with 1. The send path never waits for the client.
2. **All of the added period lands in the gated cycles.** Cycles whose
   credit was prompt run at **16.3–16.9 ms in every leg** — flat under a
   40 ms ack delay — while gated cycles go 31.9 → 52.0. `pump` (encode)
   is 15.2–15.4 ms everywhere. The machine is not slower; only the gate
   moves.
3. The decomposition closes: ⅔·52.0 + ⅓·16.4 = 40.1 = the measured mean
   at D = 40 (D = 20: 41.5·320/479 + 16.3·159/479 = 33.1 = measured).

## One stalled capture, in plain words (D = 40 leg, capture 76)

```
  -27.56 ms  absorb  73     children absorbed frame 73's input
  -27.50 ms  ackslot 73     ...credit for its slot went out at once
  -18.26 ms  egress  73
  -17.46 ms  msgin   74
   -8.30 ms  msgin   75
   +0.00 ms  absorb  74     frame 74's slot is now SAFE to recycle
   +8.88 ms  egress  74     ...but no credit: the window is closed
  +16.14 ms  absorb  75
  +24.78 ms  egress  75     server sends on, 2 frames unacked
  +31.91 ms  cliack  73     73+1 > 75 is false — window stays closed
  +61.98 ms  cliack  74     74+1 > 75 is false — still closed
  +79.59 ms  (credit for 74 emitted)                withheld 79.59 ms
  +88.21 ms  msgin   76     capture itself took 8.62 ms
```

The slot was safe at +0.00 and the producer was told at +79.59. Nothing
in those 80 ms belongs to encoding, sending or capturing — the machine
was waiting for a round trip it does not need.

## What this does and does not settle

Settled: the client's ack is on the critical path of the capture-slot
release at fif = 1, by intervention on unmodified code; the effect is
deterministic; the send path is innocent; encode is unaffected.

Not settled here: that removing the gate is safe (the xorgxrdp
SLOT_ONLY handler check in #79 remains blocking), and what the fixed
build does (predicts withheld ≈ 0 and a period flat in D — the same
sweep, rerun, separates the builds).
