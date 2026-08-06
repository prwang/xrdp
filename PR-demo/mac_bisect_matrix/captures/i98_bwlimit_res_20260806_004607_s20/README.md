# i98_bwlimit_res — do lower resolutions follow the same bandwidth-limited trend?

BACKLOG #98, owner-directed 2026-08-06 ("also perform lower resolution
(2560*1440, 1920*1080) and see if still matches the trend").

## The experiment: FOUR legs, one pod (x019, credit frontier, C = 1), bbr+tier0, sequential, 20 s each

Same protocol as `i98_bwlimit_20260806_003023_s20` (netem 40 ms +
declared tbf bottleneck, 100 ms buffer, rate verified by bulk
measurement per leg), with the CLIENT RESOLUTION as the new variable.
Single monitor, textflood, oracle client.

  leg r1440_200   2560x1440, 200 Mbit/s
  leg r1440_100   2560x1440, 100 Mbit/s
  leg r1080_200   1920x1080, 200 Mbit/s
  leg r1080_100   1920x1080, 100 Mbit/s

## Predictions, written BEFORE the run

The trend claims are S-relative, with S measured per leg (send-record
bytes / frames). Pixel-scaling prior for S from the 4K legs'
3.58 MB at 9.22 Mpx (bits/pixel roughly constant for this payload):

  S(2560x1440, 3.69 Mpx) ~= 1.43 MB   (+-25 % allowed: content is not
  S(1920x1080, 2.07 Mpx) ~= 0.80 MB    identical across resolutions)

  P-R1  fps = B_measured / S_measured within 15 %:
        with the scaling prior and ~22.4 / ~11.3 MB/s links:
        r1440_200 ~15.7   r1440_100 ~7.9   r1080_200 ~28   r1080_100 ~14
        (all still below the ~54 fps free-running pipeline rate, so
        the link should bind in every leg)
  P-R2  send-to-ack p50 in 0.70-0.90 x [(C+2)*S/B + RTT], flat (+-5 %)
        across each leg:
        bounds ~= r1440_200 232 ms, r1440_100 420 ms,
                  r1080_200 147 ms, r1080_100 253 ms
  P-R3  wire bound id_server - id_client <= 3 on every send
  P-R4  no sustained bottleneck overflow; frames fresh by coalescing

FALSIFIER for the trend: any leg whose fps misses B/S by >15 % or
whose ack latency exceeds the window bound or trends upward -- that
would mean a resolution-dependent mechanism (e.g. a pipeline floor or
a per-frame overhead) the 4K model does not contain.

## Results — the trend holds; frame bytes scale with pixels at ~0.39 bytes/px

| leg | S measured (pred) | fps (B/S pred) | ack p50 | bound | ratio | trend | L p50 |
|---|---|---|---|---|---|---|---|
| r1440_200 | 1.47 MB (1.43) | **15.5** (15.1) | 167.0 ms | 238 ms | 0.70 | −1 % | 18.5 |
| r1440_100 | 1.41 MB (1.43) | **8.1** (8.1) | 345.6 ms | 411 ms | 0.84 | +2 % | 12.1 |
| r1080_200 | 0.81 MB (0.80) | **28.3** (27.7) | 91.0 ms | 148 ms | 0.61 | −0 % | 9.9 |
| r1080_100 | 0.81 MB (0.80) | **13.6** (13.9) | 198.3 ms | 256 ms | 0.77 | −2 % | 9.8 |

* P-R1 met everywhere: fps = B/S within 3 % (band allowed 15 %).
* P-R2 met with one benign band-miss: every leg flat (±2 %) and UNDER
  the window bound; r1080_200's ratio 0.61 is BELOW the predicted
  0.70–0.90 band — less queueing than modeled, because at small S the
  pipeline phase leaves the average frame waiting behind less than two
  full frames. A miss on the good side, noted rather than hidden.
* P-R3 met: wire bound ≤ 3 on every send of every leg. (r1080_100's
  histogram shows 60+104 sends at 0–1 outstanding — at 13.6 fps the
  window is not always saturated; consistent with its slightly larger
  tail: p90 gap 159 ms, and 117 bottleneck drops, the most of any leg.)
* Bytes-per-pixel is the invariant behind the trend: 3.58 MB/9.22 Mpx
  = 0.388, 1.47/3.69 = 0.398, 0.81/2.07 = 0.391 — **~0.39 B/px
  (~3.1 bits/px) at CQP 20 textflood, constant across resolutions**.
  S is predictable from geometry alone on this payload.
* Capture-to-send L also scales down with pixels (18.5 → 9.9 ms).

**Budget mapping (the practical corollary):** against a 100–150 ms
interactivity budget at 40 ms RTT, TODAY'S build with C = 1 + tier0 is
already interactive at 200 Mbit in 1080p (91 ms), marginal in 1440p
(167 ms), and not in 4K (436 ms). Resolution reduction is a crude,
already-working form of Tier 2 (fewer bytes per frame); proper Tier 2
buys the same bytes without giving up pixels.
