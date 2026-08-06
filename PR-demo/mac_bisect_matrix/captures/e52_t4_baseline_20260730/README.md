# E5-2 on the T4 — baseline arm (#45 steps 0–4), 2026-07-30

The **denominator** of the T4 E5-2 gate: the same box, same GPU, same
`gfx.toml`, same `codeflood` payload and the same oracle client as
`../e52_t4_batched_20260730/`, with an xrdp that has neither `pump_set`
(step 5) nor the two-monitor batch (step 7). One monitor's pair at a time.

| | value |
|---|---|
| mean per send | **77.3 ms** (p50 77, p90 83, p99 103) |
| sends/s | 12.94 = 6.47 pairs/s per monitor |
| per-monitor period | 154.6 ms (surface 0) / 155.1 ms (surface 1) |
| pair service dmg→last=1 | 13.9 ms |
| `last=1` → next own dmg | 144.5 / 136.8 ms |
| worker busy | 18 % |
| pictures pushed | 1 330 MiB in 175.4 s = 63.6 Mbit/s |
| batch cycles | 0 — pre-step-5 build, one child per pump |

77.3 ms is the number passed to the batched run as `E5_BASE_MS`. **A T4
result may only ever be divided by this**, never by the dev box's 51.1 ms:
different CPU, different encoder, different everything.

## What was deployed

* xrdp-dev `0.10.80+git20260730013437.5dae11f63adb` — #45 steps 0–4 plus the
  #52 log clock fix (branch `bench/e52-arm-t-baseline`)
* xorgxrdp-dev `1:0.10.80+git20260729225933.d77d05463e52` — identical to the
  batched arm, so step 6 is *not* part of the delta being measured
* Tesla T4 / driver 580.173.02, Xeon Platinum 8259CL, 4 vCPUs
* payload `codeflood`, 2560×1440 + 3840×2400, oracle client, 180 s

Note the version-sort trap: this deb was built ~1 minute *later* than the
batched one, so it sorts NEWER (`20260730013437` > `20260730013346`) even
though its code is older. Going from here to the batched arm is a
**downgrade** to dpkg. `deployed_packages.txt` is the proof of which arm
actually ran.

## Both monitors were inked

Damage coverage: surface 0 = 1 138 events, surface 1 = 1 130. That balance
is what makes this a valid denominator — a baseline measured with one idle
monitor would flatter the batched arm, because the batch can only overlap
monitors that have something to send. The harness now states coverage on
every run and warns when it is worse than 3:1 (`e_gate_run.sh`, gate G5).

## E2 / wire — clean

* zero on all six server-side counters
* `avc444_ltr_wire_audit.py --assert --intra-refresh 240`: **7/7 PASS** over
  2 274 pictures, cuts paired at ordinals 0/240/480/720/960, 0 frame_num gaps
* black-frame check: 8 black main-view pictures at indices 0–16 of 2 274 and
  none after — the XFCE login paint-in, the same artifact analysed in the
  batched arm's README

`oracle/` (1.40 GB) was audited by those checks and then deleted; it is not
committed.
