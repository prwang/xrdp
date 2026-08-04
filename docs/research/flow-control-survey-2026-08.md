# Literature survey: flow control & figures of merit for interactive desktop streaming

Commissioned by the owner 2026-08-04 ("we are blindly reinventing theory
that might existed well before — 15 years sigcomm mobicom nsdi cloud
gaming flow control; tighter upper bounds or more realistic goals/FoMs,
and how heavy to implement"). Produced by a web-verified research agent;
every citation carries a VERIFIED/UNVERIFIED flag. Context it was given:
the #80 credit frontier, the measured 40 ms equilibrium
(`docs/experiments/80-the-credit-frontier.md` §"Step 4, corrected" and
the 2026-08-04 addendum).

Decisions arising from it are tracked in BACKLOG.md (#98), not here.

---
# Literature survey: flow control & figures of merit for high-bitrate interactive desktop streaming (xrdp 4K/AVC444 over TCP)

Scope: SIGCOMM / NSDI / MobiCom / CoNEXT / ATC / MMSys / INFOCOM + industry systems, ~2011–2026. Every citation was checked against the web during this survey unless flagged UNVERIFIED. Search sources are linked inline.

---

## 1. Theory / bounds

### 1.1 The closed form you measured is a known one

Your regression — `ack_latency = 50.5 ms + queue / 43.1 MiB/s, R² 0.914` — is the textbook steady state of a window-limited flow, and it is stated explicitly in the BBR paper ([ACM Queue 2016](https://queue.acm.org/detail.cfm?id=3022184)): for inflight above the bandwidth-delay product, **RTT = RTprop + (inflight − BDP)/BtlBw**, and delivery rate is pinned at BtlBw. Below BDP, RTT is flat and delivery rate = inflight/RTprop. Your two regimes are exactly BBR's "application/window-limited" and "bandwidth-limited" regions:

- Your drain rate 43 MiB/s ≈ cwnd_max/RTT = 1.7 MB / 40 ms ≈ 42.5 MB/s. **The flow is cwnd-limited, not link-limited** (netem is delay-only). Throughput = W/RTT is the self-clocking law from Jacobson's "Congestion Avoidance and Control" (SIGCOMM 1988) — UNVERIFIED online this pass, but it is the canonical source the BBR article builds on.
- Your standing queue of ~2 frames = (frame-credit window in bytes) − (what the 43 MiB/s pipe holds in one RTT) — i.e. **standing queue = window_bytes − BDP**, the same identity.
- Little's law (L = λW) closes it: 6.7 MB queue = 43.1 MiB/s × 148 ms of queueing delay, which plus your 50.5 ms intercept gives your 203.7 ms send-to-ack. The numbers close; nothing exotic is happening.

**The lower bound** for one frame of size S over bandwidth B, RTT R, with an open window: send-to-ack ≥ R + S/B (one serialization plus one round trip). When the window W < S, the transfer degenerates to ~⌈S/W⌉ round trips — the classic flow-completion-time argument (Dukkipati & McKeown, "Why Flow-Completion Time is the Right Metric for Congestion Control," CCR 2006 — UNVERIFIED this pass). With your cwnd pinned at 0.6–1.7 MB against a 3.4 MB frame, you are in the degenerate regime: each frame needs 2–6 windows, i.e. multiple RTTs, which is precisely your 203.7 ms.

### 1.2 The optimal operating point and why multiple equilibria exist

- **Kleinrock (1979)** showed the optimal operating point is inflight = BDP exactly: max delivered bandwidth, min delay ("power" maximization). **Jaffe (1981)** proved no distributed algorithm converges to it, which is why loss-based TCP settles at the *other* stable point — the full-buffer one. Both results are recounted in [BBR: Congestion-Based Congestion Control](https://queue.acm.org/detail.cfm?id=3022184) (Cardwell, Cheng, Gunn, Hassas Yeganeh, Jacobson, ACM Queue 2016). **VERIFIED** (via the Queue article; Kleinrock/Jaffe originals not fetched).
- Your "bufferbloated variant ran faster" observation is the known flip side: TCP's ack clock needs a standing queue to stay fed, and loss-based CC has two self-consistent equilibria (empty-queue starved vs. full-queue clocked). Delay-based designs institutionalize a *small deliberate* standing queue for exactly this reason — **Copa** ([Arun & Balakrishnan, NSDI '18](https://www.usenix.org/conference/nsdi18/presentation/arun), **VERIFIED**) proves a target rate of 1/(δ·d_q) — i.e. holding a small measured queueing delay d_q — maximizes a throughput/delay objective under a Markovian model, and periodically drains the queue to re-anchor RTTmin. So "keep a controlled 1-frame queue, not zero and not 2+" has a proved-optimal form.

### 1.3 Age of Information: what the optimal credit/window is for *freshness*

- Kaul, Yates & Gruteser, "Real-time status: How often should one update?" (INFOCOM 2012) founded the **Age of Information** (AoI) metric — the age of the freshest delivered update. **VERIFIED** via [Yin Sun's AoI bibliography](https://webhome.auburn.edu/~yzs0078/AoI.html).
- **Sun, Uysal-Biyikoglu, Yates, Koksal, Shroff, "Update or Wait: How to Keep Your Data Fresh"** ([INFOCOM 2016 / IEEE Trans. IT 2017](http://webhome.auburn.edu/~yzs0078/paper/conference/Age_of_info_infocom16.pdf), **VERIFIED**; INFOCOM 2026 Test-of-Time award): the "zero-wait" policy (submit a fresh update the instant the server frees) maximizes throughput and minimizes delay but does **not** always minimize age; under heavy-tailed service times you should deliberately wait ("lazy is timely," Yates ISIT 2015, **VERIFIED** same source).
- Mapping: your admission rule ("admit a capture only when unacked ≤ C+2, else drop at source and coalesce damage") is a *generate-at-will* AoI source. AoI theory's verdict: **for frame freshness the optimal number of frames in flight is small — roughly one per server (pipeline stage) — and pushing the window up to raise FPS strictly increases age once the pipe is full.** Your measured 2-frame standing queue means every displayed frame is ~2 frame-services stale; AoI says that queue should be ~0–1 and admission should be paced to the service rate, not to credit availability. Your damage-coalescing drop-at-source is exactly what the AoI and RTC literatures both endorse (never queue stale frames).

---

## 2. Cloud gaming / interactive video systems

| System | Venue/Year | Mechanism (one sentence) | Result (one sentence) | Flag |
|---|---|---|---|---|
| **Salsify** | [NSDI '18](https://www.usenix.org/conference/nsdi18/presentation/fouladi) | Fuses codec and transport control loops: transport tells the (purely functional, state-save/restore) codec the per-frame byte budget *before* encoding; encoder can re-encode or skip a frame that overshoots | 3.9× lower p95 delay and +2.7 dB SSIM vs. FaceTime/Hangouts/Skype/WebRTC(+SVC) | VERIFIED |
| **Sprout** | [NSDI '13](https://www.usenix.org/conference/nsdi13/technical-sessions/presentation/winstein) | Receiver Bayesian-forecasts cellular link rate and grants the sender a byte budget bounding the risk of >100 ms in-network delay | 7.9× lower self-inflicted delay, 2.2× bitrate vs. Skype on LTE traces | VERIFIED |
| **Copa** | [NSDI '18](https://www.usenix.org/conference/nsdi18/presentation/arun) | Delay-based target rate 1/(δ·d_q) with periodic queue drain and TCP-mode switching | Provably optimal throughput/delay tradeoff under its model; near-full utilization on paths where Cubic/Vegas get <4% | VERIFIED |
| **BBR** | [ACM Queue 2016](https://queue.acm.org/detail.cfm?id=3022184) | Sequentially estimates BtlBw and RTprop, paces at BtlBw, caps inflight at ~1–2× BDP (Kleinrock point) | Deployed on Google B4/YouTube; orders-of-magnitude throughput gains vs. CUBIC on lossy WANs; in mainline Linux since 4.9 | VERIFIED |
| **GCC** (WebRTC) | [MMSys '16, Carlucci et al.](https://c3lab.poliba.it/images/6/65/Gcc-analysis.pdf) | Kalman/trendline filter on one-way delay gradient + loss-based controller; sender rate = min of the two; drives the *encoder target bitrate* | Keeps queues near-empty on variable links; is the deployed Chrome/Meet controller (known to collapse vs. competing TCP) | VERIFIED |
| **Pudica** | [NSDI '24, Tencent](https://www.usenix.org/conference/nsdi24/presentation/wang-shibo) | Paces each frame's packets to probe a "bandwidth utilization ratio," adjusts encoder bitrate holistically for near-zero queuing | 3.1× avg / 4.9× tail frame-delay reduction, 10.3× lower stall rate, +12.1% bitrate; deployed to millions of cloud-gaming players | VERIFIED |
| **SQP** (Google AR/cloud gaming) | [arXiv 2207.11857](https://arxiv.org/abs/2207.11857) | Frame-coupled paced packet trains (each frame paced slightly faster than F/I) sample bandwidth without a standing queue; bitrate, not queue, absorbs congestion | +27% (LTE)/+15% (Wi-Fi) sessions with high bitrate *and* low frame delay vs. Copa in Google A/B; 2× throughput vs. WebRTC emulated | VERIFIED |
| **Mowgli** | [NSDI '25](https://www.usenix.org/conference/nsdi25/presentation/agarwal) | Learns rate control offline from production GCC telemetry logs (no exploration on users) | +15–39% bitrate, 60–100% freeze-rate reduction vs. GCC | VERIFIED |
| **Zhuge** | [SIGCOMM '22](https://dl.acm.org/doi/abs/10.1145/3544216.3544225) | Wireless AP predicts per-packet queueing and pre-delays ACKs so the sender's CC sees congestion sub-RTT early | 17–95% reduction in tail-latency episodes for RTC flows; quotes budgets: RTC <150 ms, cloud gaming <96 ms | VERIFIED |
| **ABC** | [NSDI '20](https://www.usenix.org/conference/nsdi20/presentation/goyal) | Router marks each packet accelerate/brake (ECN-encoded) steering senders to target rate in one RTT | 30–40% more throughput than Cubic+CoDel at similar delay; 2.2× lower delay than BBR on Wi-Fi | VERIFIED |
| **PCC Vivace** | [NSDI '18](https://www.usenix.org/conference/nsdi18/presentation/dong) | Online gradient ascent on a latency-aware utility of measured rate/loss/latency | Outperforms TCP variants/BBR on convergence and bufferbloat; sender-side only | VERIFIED |
| **Verus** | [SIGCOMM '15](https://dl.acm.org/doi/abs/10.1145/2785956.2787498) | Continuously learns a delay-vs-window "delay profile," walks the window along it | >10× delay reduction vs. Cubic on 3G/LTE at comparable throughput | VERIFIED |
| **GamingAnywhere** | [MMSys '13 / TOMM '14](https://dl.acm.org/doi/10.1145/2537855) | First open-source cloud-gaming stack (capture→x264→RTSP/RTP) | 34 ms per-frame server processing delay — 3× lower than OnLive, 10× lower than StreamMyGame | VERIFIED |
| **Pantheon** | [ATC '18, best paper](https://www.usenix.org/conference/atc18/presentation/yan-francis) | Common benchmark harness + calibrated emulators for CC schemes | Scheme rankings vary dramatically with path dynamics; birthed Indigo (learned CC) | VERIFIED |
| **Stadia traffic analysis** | [Carrascosa & Bellalta, Computer Communications 2022](https://arxiv.org/abs/2009.09786) | Measurement: Stadia = WebRTC/GCC dual controller (loss thresholds 0.02/0.1), VP9/H.264 over RTP/UDP | Adapts resolution/framerate to bitrate estimate within seconds; ~44 Mbps median max (4K) per the companion [Di Domenico study](https://doi.org/10.3390/network1030015) | VERIFIED |
| **Vidaptive** ("Tight Loops, Smooth Streams") | [NINeS 2026](https://drops.dagstuhl.de/entities/document/10.4230/OASIcs.NINeS.2026.9) | Shim letting real-time video ride responsive CCAs (Copa-class) without codec changes | Addresses slow reaction of deployed video rate controllers | VERIFIED (existence + thesis; numbers not fetched) |
| **Confucius** | [arXiv 2310.18030](https://arxiv.org/html/2310.18030v2) | Practical queue management at the bottleneck for consistent RTC latency; notes CCA convergence, not feedback delay, dominates after Zhuge | — | VERIFIED (existence) |
| **RDP-UDP / RDPEUDP2** | [MS-RDPEUDP2, Microsoft Learn](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpeudp2/9db34630-e880-4bfd-9d8d-50bc044c3288) | RDP's own UDP transport: v1 had lossy mode + FEC; v3+ (RDPEUDP2) is reliable-only UDP with **URCP**-based rate/delay control, no FEC, designed to beat both RDP-TCP and RDP-UDP-v1 on lossy WANs | Microsoft's stated goals: higher bandwidth share, lower delay variance, fair coexistence; this is what "RDP Shortpath" rides on ([FreeRDP implementer notes](https://www.hardening-consulting.com/en/posts/20230109-udp-support-2.html)) | VERIFIED (spec + implementer account; no published perf paper) |

Notably: **the direction of the whole field since ~2018 is "the encoder's next-frame size is the control variable; the network is probed by pacing frames, never by queueing them."** Salsify, SQP, and Pudica are three independent, deployed instantiations of that one idea.

---

## 3. Figures of merit the field actually uses

What these papers measure and optimize (all from the verified sources above):

1. **Per-frame delivery delay, tail-weighted.** Salsify: 95th-percentile frame delay. Pudica: average *and* tail (p99) frame delay. SQP: fraction of sessions jointly achieving high bitrate *and* low frame delay. This — not FPS — is the field's primary axis.
2. **Stall / freeze rate.** Pudica: stall rate (10.3× reduction is the headline). Mowgli: freeze rate (60–100% reduction). A stall is defined by a frame gap threshold, not by average rate.
3. **Quality × delay Pareto.** Salsify plots SSIM vs. p95 delay and claims Pareto dominance; nobody reports quality or delay alone.
4. **QoE models.** [ITU-T G.1072](https://standards.globalspec.com/std/14350384/itu-t-g-1072) (VERIFIED): an E-model-style MOS (1–5) predictor for cloud gaming from bitrate, framerate, resolution, complexity, packet loss and delay ([reference implementation on GitHub](https://github.com/stootaghaj/ITU-G1072)); ITU-T G.1051 adds a latency/interactivity measurement method. Delay enters as a continuous impairment, no single cliff.
5. **Latency budgets in absolute numbers.** Zhuge (SIGCOMM '22, citing industry): videoconferencing <150 ms, cloud gaming <96 ms end-to-end. Practitioner consensus for cloud gaming input-to-photon: <100 ms playable, 40–60 ms good, <30 ms "local-feel" ([GeForce Now Ultimate claims sub-30 ms click-to-pixel](https://cloudbase.gg/cloud-gaming-latency/); measured services run 25–60 ms). GamingAnywhere's server-side budget: 34 ms per-frame processing. FPS-game human threshold usually quoted <100 ms response.
6. **Age/freshness** (§1.3) exists as theory but is essentially absent as a reported FoM in the systems papers; its practical proxy is "frame delay of the *displayed* frame + drops," which your damage-coalescing design already optimizes for.

**For xrdp the realistic FoM set is: (a) p95/p99 send-to-display frame delay at a fixed quality floor; (b) stall rate (no admitted frame for > 2 frame periods); (c) the quality×delay Pareto as bitrate is adapted. "54 FPS at RTT=0" is not a number the field would chase; "p99 frame delay <100 ms at 40 ms RTT with SSIM ≥ X" is.**

---

## 4. The specific TCP pathologies we hit

- **cwnd validation for rate-limited flows — [RFC 7661](https://datatracker.ietf.org/doc/html/rfc7661)** (Fairhurst, Sathiaseelan, Secchi, 2015, Experimental; obsoletes RFC 2861). VERIFIED. Exactly your diagnosis: a sender that doesn't fill cwnd doesn't get cwnd growth; after a rate-limited "non-validated period" cwnd is pulled back toward what was actually used. Your burst-then-wait pattern (3.4 MB burst, then idle until credit) is the RFC's motivating case. The RFC's own recommended mitigation is **pacing during non-validated periods**.
- **Slow-start after idle** (`net.ipv4.tcp_slow_start_after_idle`, default 1): a flow idle for >1 RTO restarts from IW. The [tc-fq man page](https://man7.org/linux/man-pages/man8/tc-fq.8.html) states explicitly that fq pacing "removes the 'slow start after idle' choice, which badly hits … applications delivering chunks of data such as video streams." VERIFIED.
- **Burst pathology and sender pacing — Trickle** ([Ghobadi, Cheng, Jain, Mathis, ATC '12](https://www.usenix.org/conference/atc12/technical-sessions/presentation/ghobadi), VERIFIED): Google capped YouTube's cwnd to ≈ rate×RTT (rate = 1.2× encoding bitrate), cutting loss 43% and RTT 28% at the same streaming rate — proof at production scale that shaping a video flow to its needed rate, instead of letting cwnd burst, reduces self-inflicted delay.
- **fq / SO_MAX_PACING_RATE / EDT** ([man page](https://man7.org/linux/man-pages/man8/tc-fq.8.html), [ESnet](https://fasterdata.es.net/host-tuning/linux/packet-pacing/), VERIFIED): kernel-level per-flow pacing; post-4.20 TCP sets per-packet Earliest Departure Times. BBR *requires* fq or internal pacing.
- **TCP_NOTSENT_LOWAT** ([Linux commit c9bee3b, 2013](https://github.com/torvalds/linux/commit/c9bee3b7fdecb0c1d070c7b54113b3bdfb9a3d36); [Cloudflare's deployments](https://blog.cloudflare.com/optimizing-tcp-for-high-throughput-and-low-latency/), VERIFIED): bounds *unsent* bytes in the socket buffer so queued-but-unsent data stays in the application, where it can still be reprioritized — or in your case, dropped/coalesced. Cloudflare runs 128 KB fleet-wide at no throughput cost at 1 Gbps; 16 KB for strict prioritization.
- **The UDP escape hatch RDP already defines:** [MS-RDPEUDP/RDPEUDP2](https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpeudp2/9db34630-e880-4bfd-9d8d-50bc044c3288) (VERIFIED, §2 table). Microsoft's own answer to precisely your problem was to leave kernel TCP: URCP rate control in userspace over UDP, reliable mode, no FEC in v2. FreeRDP has (partial) client support. Also relevant: the IETF **Media-over-QUIC** WG explicitly targets remote desktop and cloud gaming, with per-frame Objects that can be deprioritized/cancelled ([Fastly overview](https://www.fastly.com/blog/media-over-quic-can-streaming-finally-have-both-scale-and-low-latency), VERIFIED); a 2025 comparison measured ~30% latency improvement for QUIC-based media paths vs. WebRTC ([arXiv 2505.22132](https://arxiv.org/html/2505.22132v1), VERIFIED existence).

---

## 5. Implementation weight tiers, with expected effect on *your* numbers

**Tier 0 — config only (hours).**
`net.ipv4.tcp_slow_start_after_idle=0`; `fq` qdisc on the egress; switch the listen socket's CC to **BBR**; ensure `tcp_wmem`/`SO_SNDBUF` ≥ BDP+2 frames. Why it should move your numbers: BBR is rate-based — it paces at its BtlBw estimate and does not shrink its model merely because the app went idle, sidestepping the RFC 7661 trap that pins Cubic's cwnd at 0.6–1.7 MB. On your delay-only 40 ms link the true BtlBw is enormous; anything that lets the window open past ~1 frame (3.4 MB ≈ BDP at 85 MB/s) collapses the ⌈S/W⌉-round-trip regime toward one RTT + serialization. Expected: send-to-ack from 203.7 ms toward ~RTT + S/B + one credit wait ≈ 60–100 ms; FPS from 11.5 toward 25–45 with C+2 = 3 credits (bounded by credits/ack-latency, Little's law). Caveat the field documents (SQP paper): bursty app-limited flows give ack-clocked estimators poor bandwidth samples, so BBR's gain must be measured, not assumed. Key refs: [BBR](https://queue.acm.org/detail.cfm?id=3022184), [RFC 7661](https://datatracker.ietf.org/doc/html/rfc7661), [tc-fq](https://man7.org/linux/man-pages/man8/tc-fq.8.html).

**Tier 1 — socket-level code (days).**
`TCP_NOTSENT_LOWAT` (start ~128 KB) so the standing 6.7 MB lives in xrdp, not the kernel — then your existing damage-coalescing can drop staleness the kernel currently forces you to ship; plus **userspace frame pacing**: spread each frame's writes across most of the frame period at ~1.2× the needed average rate instead of one 3.4 MB burst. That is Trickle's production-proven smoothing and SQP's probe design in one, and it also keeps the flow continuously "validated" per RFC 7661, letting cwnd grow legitimately. Expected: kills the queue term of your regression (queue → ≤1 frame), i.e. ack_latency → ~50–90 ms; also removes the burst-loss risk on real (non-netem) links. Key refs: [Trickle](https://www.usenix.org/conference/atc12/technical-sessions/presentation/ghobadi), [SQP](https://arxiv.org/abs/2207.11857), [Cloudflare](https://blog.cloudflare.com/optimizing-tcp-for-high-throughput-and-low-latency/).

**Tier 2 — application rate control (weeks).**
Close the loop the whole field closes: measure delivered rate (bytes-acked/time from `TCP_INFO` or your existing frame-ack telemetry), set the *encoder's* per-frame byte budget = delivered_rate × frame_period, and let quality absorb congestion instead of latency. This is GCC/Salsify/Pudica/Mowgli; your ffmpeg h264_vaapi per-frame bitrate/QP control is sufficient (Salsify's re-encode trick is optional). Arithmetic on your numbers: at 43 MiB/s drain, 30 FPS requires ≤1.4 MB/frame and 60 FPS ≤750 KB/frame — 2.4–4.5× more compression than your current 3.4 MB, which is squarely what these systems do routinely at 4K (Stadia peaked at ~44 Mbps ≈ 0.09 MB per 60 FPS frame). Published gains: Salsify 3.9× p95 delay; Pudica 3.1×/4.9× avg/tail frame delay + 10.3× stall reduction. This is the only tier that survives a link that is *genuinely* slower than your offered load — Tiers 0–1 only help when the bottleneck was self-inflicted. Also at this tier: size the frame credit C to the Kleinrock point, C+2 ≈ ⌈RTT/frame_period⌉ + 1, instead of a constant (your own #76 finding that fif=2 costs 34% is this formula asserting itself).

**Tier 3 — transport replacement (months).**
QUIC (per-frame streams, cancelable mid-flight), an RDPEUDP2/URCP-style reliable-UDP, or a WebRTC data path. What it buys over Tier 2: freedom from kernel CC policy entirely, sub-frame loss recovery without head-of-line blocking, and mid-flight abandonment of stale frames — none of which matter on your delay-only, loss-free testbed, and all of which matter on real lossy WANs. Microsoft judged this worth doing for RDP itself (RDPEUDP → Shortpath); IETF MoQ names remote desktop as a target application. Requires client-side support (FreeRDP's RDPEUDP support is partial per the [implementer write-up](https://www.hardening-consulting.com/en/posts/20230109-udp-support-2.html)) — the heaviest tier by far, and the literature (SQP vs. Copa A/B, Pudica-over-UDP) suggests most of the *latency* win is already captured at Tier 2.

---

## 6. What maps onto xrdp's numbers

**Your equilibrium is a known closed form.** `ack_latency = 50.5 ms + queue/43.1 MiB/s` is BBR's window-limited operating-region equation (RTT = RTprop + excess-inflight/BtlBw) combined with Little's law; the standing 2-frame queue is `window_bytes − BDP`; and drain = cwnd/RTT is Jacobson self-clocking. The "no independent anchor / multiple equilibria" property you found is Kleinrock/Jaffe: without an independent estimate of BtlBw or RTprop, a window system has a family of self-consistent fixed points, and loss-based TCP + RFC 7661 selects a bad one for bursty app-limited flows. BBR's entire design is the published answer to breaking that circularity (estimate BtlBw and RTprop *sequentially*, pace at the estimate).

**The bound you should be chasing:** floor send-to-ack ≈ RTT + S/B. On your link that is ~40 ms + serialization; with your measured 43 MiB/s drain fully attributable to cwnd, the achievable target after un-pinning the window is ~60–100 ms send-to-ack and, with credits sized to ⌈RTT/period⌉+1, ~30–45 FPS at 40 ms RTT *at current frame sizes* — and 60 FPS is reachable only via Tier 2 (frames ≤ delivered_rate/60).

**The FoM the field would hand you:** stop reporting FPS-at-RTT. Report (a) **p99 frame send-to-display delay at fixed quality** (budget: <100 ms for gaming-class interactivity, <150 ms for RTC-class, per Zhuge/industry), (b) **stall rate**, (c) the **quality×delay Pareto** as the encoder budget adapts. ITU-T G.1072 exists if you want a single MOS number for the PRD.

**Cheapest tier likely to move 203.7 ms / 11.5 FPS materially: Tier 0+1 together.** The literature says your bottleneck is self-inflicted (cwnd validation on a burst-then-wait flow), and the three cheap, production-proven levers against exactly that are BBR+fq (rate-based CC that ignores app-limited idling), frame pacing (Trickle/SQP — keeps the flow validated and the queue empty), and TCP_NOTSENT_LOWAT (moves the stale-frame queue back to where damage-coalescing can eat it). Expected together: send-to-ack 203.7 → ~60–100 ms, FPS 11.5 → ~25–45 on the 40 ms link. Tier 2 (encoder budget = delivered rate × period, credits = Kleinrock point) is what gets you to 60 FPS and is where every deployed system in this space (Stadia/GCC, Google SQP, Tencent Pudica, Salsify) ended up; the survey found no deployed counterexample that ships fixed-size frames into an unadapted transport, which is strong evidence the owner's instinct — that a tighter *goal* is per-frame-delay at adapted quality, not raw FPS at fixed bytes — matches the field.

UNVERIFIED items (flagged in text): Jacobson SIGCOMM 1988; Dukkipati & McKeown CCR 2006; Kleinrock 1979 / Jaffe 1981 originals (verified only as recounted in the BBR article). Everything else above was confirmed online during this survey.
