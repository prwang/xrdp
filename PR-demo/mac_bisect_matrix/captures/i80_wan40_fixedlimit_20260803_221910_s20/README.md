# i80_wan40_fixedlimit — the 40 ms leg re-run on the FIXED harness

RE-RUN of the wan leg of `i80_wanpair_20260803_125816_s20`, whose netem
carried the kernel-default `limit 1000` — an UNDECLARED 73 MB/s
bottleneck with tail drops (measured 73.5 MB/s vs 1614 unshaped, 64
drops; see `docs/experiments/81-the-netem-rtt-harness.md`). That leg's
derived numbers are VOID (instrument on the measured path). This run
uses `limit 25000` (measured 120.1 MB/s, zero drops) and declares the
remaining single-flow TCP ceiling.

Same arm (x019), same image (1d5bc0960db8), same gfx.toml, same
geometry and payload, same 20 s.

## Predictions, written BEFORE analysis

Environment is now 40 ms RTT + ~100-120 MB/s single-flow ceiling
(declared). A 3.4 MB frame delivers in ~30-35 ms at that ceiling, so
send-to-ack should fall from the voided 122.9 ms toward ~70-90 ms;
with 3 periods >= capture-to-send (~44 ms) + send-to-ack, period
~38-45 ms and rate ~22-27 fps (up from the voided 16.8). Queue behind
egress: outstanding-by-window minus TCP in-flight, so mean ~2-3.5 MB
(down from the voided 4.9). Wire bound still <= C+2 = 3 on every send.
Zero qdisc drops after the leg.
