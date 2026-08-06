# #98 — Tier 0 A/B: BBR + pacing sysctls take the 40 ms transport to its theoretical floor

**Status:** run 2026-08-05, owner-directed ("done, now do a/b testing
and compare results against predicted theory bounds") after the metal
host loaded `tcp_bbr`. Capture:
`PR-demo/mac_bisect_matrix/captures/i98_tier0_ab_20260805_235328_s20`
(predictions written into its README before the run). Survey that
predicted this: `docs/research/flow-control-survey-2026-08.md`.

Records in this directory are kept verbatim. Supersede with a dated
note; do not tidy.

## The experiment

Two legs, one pod (x019, credit frontier build, C = 1), netem 40 ms
(fixed harness, zero drops verified per leg), 20 s each, single monitor
3840×2400 textflood. The only difference: pod-netns TCP config,
reverted afterward —

    A (control): cubic, slow_start_after_idle=1, wmem max 4 MB
    B (tier0):   bbr,   slow_start_after_idle=0, wmem max 16 MB

Knob-applied proof (gate 2): `ss -tin` sampled the live RDP socket
every 0.5 s — 38× `cubic` on leg A, 76× `bbr` on leg B.
Control proof: leg A reproduces the 2026-08-03 corrected leg to 0.1 %
(period 86.8 vs 86.7 ms, ack 204.2 vs 203.7, queue 6.65 vs 6.7 MB).

## Results against the pre-registered predictions

| | A cubic | B bbr+tier0 | prediction → verdict |
|---|---|---|---|
| send-to-ack p50 | 204.2 ms | **50.1 ms** | 60–100, stretch 45–60 → **at the stretch floor** |
| frames/s | 11.5 | **36.6** | 21–29 → **exceeded** |
| period p50 / p99 | 87.2 / 108.7 | 25.5 / 44.3 | — |
| queue at egress mean | 6 654 KiB | **24.7 KiB** | ~0 → **confirmed** (270×) |
| worst unacked at send | 2 | 2 | ≤ C+2 = 3 → **held** |
| cwnd p50 (ss) | 4 467 pkts | 22 906 pkts (~32 MB) | the RFC 7661 pin → **gone** |

**The transport is at its theoretical floor at this frame size.** The
floor is RTT + serialization + client ≈ 50–55 ms; measured p50 50.1 ms
on a 40.38 ms link. The regression that explained the cubic leg
(`ack = 54.4 + queue×0.0222 ms/KiB`, R² 0.926) degenerates on the BBR
leg (slope ≈ 0, R² 0.001): latency no longer depends on the queue
because there is no queue.

**Why fps beat the predicted band:** the band assumed capture-to-send
L fixed at ~44 ms. L itself fell 49.9 → 21.3 ms when the transport
stopped exerting backpressure into the server. Period model closes:
(21.3 + 50.1)/3 = 23.8 vs 25.5 measured (7 %).

**Known BBR cost, visible in the tail-of-the-tail:** period max
250.8 ms (p99 44.3) and a cwnd = 4 sample — BBR's ProbeRTT phase
(drains to 4 packets for ~200 ms roughly every 10 s to re-anchor its
RTT estimate). One ~250 ms hiccup per ~10 s; in the max, not the p99.

## What this settles, and what it does not

* The survey's Tier 0 mechanism claim is CONFIRMED by intervention:
  the 40 ms pathology was the congestion window pinned by RFC 7661
  cwnd-validation on a burst-then-wait flow, and a rate-based CC
  releases it with zero xrdp code change.
* With the queue gone, `(L + ack)/(C + 2)` predicts C = 2 reaches
  ~18 ms period (~56 fps) at unchanged frame size — the C-table is now
  meaningful to measure, where under cubic it was measuring TCP.
* NOT measured: tier0 on the LAN (BBR at 0.07 ms RTT — regression
  check owed before any default flips); C > 1 under tier0; Tier 1
  (TCP_NOTSENT_LOWAT + userspace pacing — may remove the ProbeRTT
  hiccup exposure and is still the lever for real lossy links); any
  quality-floor FoM (decision 1 of #98 is still open).
* Deployment shape decision owed: tier0 is currently a pod-netns
  sysctl applied by hand and reverted. Shipping it means either
  per-socket `TCP_CONGESTION` in xrdp (Tier 1 code, works without any
  sysctl) or documented host guidance — owner's call in #98.

---

# Addendum 2026-08-06: bandwidth-limited links — graceful by the pre-registered definition, and the window bound is the whole story

Owner-directed: "bandwidth limited links: the fps needs to downgrade
gracefully without excessive added delay, report that test." Harness
gained a DECLARED bottleneck (tbf under the pod-side netem, drop-tail,
100 ms buffer, server→client only, rate verified by bulk measurement
per leg — no repeat of the undeclared-ceiling incident). Four legs on
x019 (C = 1, 40 ms RTT): bbr at 400/200/100 Mbit, cubic at 200 as the
contrast. Capture: `i98_bwlimit_20260806_003023_s20`, predictions
pre-registered in its README.

Verdict, per the pre-registered criteria: **GRACEFUL at every rate.**
fps = B/S within 3 % (12.8 / 6.5 / 3.35 against links of 44.1 / 22.4 /
11.3 MB/s); send-to-ack sits at 0.72–0.90 of the window bound
`(C+2)·S/B + RTT` and is flat across each leg (±2 % — nothing
accumulates); wire bound held on every send; drop-by-coalesce kept
every delivered frame fresh; bottleneck drops 0–30 packets per leg.
cubic ≈ bbr when the link binds (window is the governor; CC is
second-order there), though cubic pushes the standing 2 frames back
into xrdp's wait_s and re-inflates capture-to-send L 21 → 52 ms.

The number that matters going forward: **the bounded delay is still
large in absolute terms** — ~0.44 s at 200 Mbit, ~0.9 s at 100 Mbit —
because at fixed S the window bound IS (C+2) frame-serializations.
Interactivity budgets (<100–150 ms) at these rates require S ≈ 0.5 MB
at 200 Mbit: Tier 2 encoder rate adaptation, as the survey concluded.

## Addendum 2026-08-06 (later): the trend holds at 2560×1440 and 1920×1080

Owner-directed follow-up. Four more legs (bbr+tier0, C = 1, 40 ms,
declared bottleneck): {1440p, 1080p} × {200, 100 Mbit}. Capture:
`i98_bwlimit_res_20260806_004607_s20`, predictions pre-registered.

The falsifier did not fire: fps = B/S within 3 % on every leg, ack
latency flat and under the window bound everywhere, wire bound held.
Frame size followed pixel count almost exactly — **0.39 bytes/pixel at
CQP 20 textflood, constant from 2.07 to 9.22 Mpx** — so S (and
therefore the whole bandwidth-limited behaviour) is predictable from
geometry on this payload. One benign band-miss: 1080p@200 queues even
less than modeled (ratio 0.61 vs the 0.70–0.90 band). Practical
corollary recorded in the capture README: at 40 ms / 200 Mbit, today's
build is already inside a 100–150 ms interactivity budget at 1080p
(91 ms), marginal at 1440p (167 ms), not at 4K (436 ms).
