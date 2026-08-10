# i104_x033_strip_20260810_s20 — the first leg on the new matrix, and the first valid rate

**One 20 s leg, arm x033 (the credit frontier at `wire_window = 2`, full
AVC444 with chroma on every frame), one monitor at 3840×2400, oracle
client, `SESSION_KIND=textflood_strip`.** Image
`3ca17beaa84d.xx10fa3aa-tf.p2fde5531`, certified at deploy.

## Verdict, first

**The benchmark is fast enough again: FR-BENCH-1 margin 3.91×** —
pipeline 17.7 ms against a producer running at 4.5 ms/frame. That is
above the PRD's 2.0× floor for the first time in this tree's recorded
history, and it is the whole point of the leg.

**And it clears the doubt raised on 2026-08-09.** The frame periods
reported then were taken at a margin of 1.04–1.05×, where the payload
and the pipeline run at the same speed and a rate may be reporting the
payload. They were not:

| run | arm | payload | producer | margin | period |
|---|---|---|---|---|---|
| `i92_…_233519` leg a1 | x030 | textflood | 17.0 ms | 1.05× | 17.985 ms |
| **this leg** | **x033** | **textflood_strip** | **4.5 ms** | **3.91×** | **17.7 ms** |

**A 3.8× faster producer moved the period by 1.6 %.** So the pipeline
was genuinely the limit, and the 55.6 fps figure for full AVC444 stands
— now at 56.5 fps, measured with an instrument that has margin.

*Not identical builds.* x030 is `7b550f6ae87f`, x033 is `3ca17beaa84d`,
which adds the input-pipe negotiation and guard (#103). Both obtained
1 MiB pipes, so no speed difference is expected from that, but the two
numbers are one commit apart and are quoted as such rather than as a
repeat measurement.

## What the leg says

* **send-to-send gap** mean 17.7 ms, p50 18, p90 19, p99 26 — 952
  cycles, one monitor.
* **both encoder children armed on 100 % of cycles** (`kids_armed=2` on
  952 of 952), which is the mechanism check that x033 really is running
  full AVC444 and not the sparse cadence.
* **encoder input pipe at or above the 64 KiB minimum** — no
  `PIPE_TOO_SMALL` in the pod's log (#103 guard, PRD FR-BENCH-2).
* **E2 clean**: rewrite failed / unsupported / did not return / budget
  exceeded / third capture / fifo_to_proc_depth all 0.

## What NOT to quote from this capture

**The "E5 GATE: baseline 51.1 ms → 2.88×" line is against a STALE
default** (`E5_BASE_MS`), taken on 2026-07-29 at dual-monitor
2560×1440 + 3840×2400 with a different payload. It is not the same
comparison and 2.88× means nothing here. This is the exact trap
`CLAUDE.md`'s scientific-quality gate names in its first example; the
harness still prints it and the default has not been retired.

## Two harness faults this leg exposed, both fixed

1. **`e_gate_run.sh` defaulted `PORT` to a hardcoded 40017.** `E_ARM=x033`
   was passed and `E_PORT` was not, so the client dialled a port belonging
   to an arm deleted an hour earlier, failed `freerdp_post_connect`, and
   the gate reported *"0 GFX_TRACE send records — is XRDP_GFX_TRACE=1 set
   in the arm's env?"* — pointing at the arm when the fault was the
   harness dialling nothing. The port is now derived from
   `k8s/<arm>.yaml`, as `arm_certify.sh` already did.
2. **The `refresh:` header grepped comment lines** out of the arm's
   gfx.toml and printed four lines of prose in place of a number. Now
   anchored to an assignment.

The first run is kept nowhere: it connected to no server and measured
nothing. Its only content is the two faults above.
