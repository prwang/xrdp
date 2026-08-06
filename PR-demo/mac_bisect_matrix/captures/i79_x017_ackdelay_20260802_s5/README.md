# i79_x017_ackdelay_20260802_s5 — the underpowered first sweep, kept

Same design as `../i79_x017_ackdelay_20260802_s20` (arm x017, fif = 1,
3840×2400 textflood, oracle client, ack-delay proxy), run at the **5 s
per leg** BACKLOG #79 originally specified. Only the `direct` and D = 0
control legs were run: the control failed and stopped the sweep, which
is what a control is for.

| leg | cycles | egress→cliack p50 | withheld p50 / p90 | stalls | period mean / p50 |
|---|---|---|---|---|---|
| direct | 45 | 13.8 | 0.10 / 35.9 | 48.8 % | 24.3 / 16.9 |
| d0 | 51 | 6.4 | 0.02 / 19.9 | 19.6 % | 19.8 / 16.9 |

**Why it is unreadable.** The gate spends most of a 5 s window on the
cold login, so 5 s yields 1.9 s of frames — 45–51 usable cycles. A
distribution whose whole signal is a 31 % tail cannot be compared at
that n. The tell that this is sample noise and not proxy overhead:
`egress→cliack` came out **7 ms LOWER** through the proxy (6.4 vs 13.8),
which no delay line can cause.

The 5 s figure came from "a mechanism check, not a rate" — correct in
principle, wrong in arithmetic, because it counted the run's wall time
instead of the frames inside it. Kept because it is the evidence for
the sample-size change made in the s20 sweep, and because the failure
mode (a control leg tripping on noise) is worth recognising again.
