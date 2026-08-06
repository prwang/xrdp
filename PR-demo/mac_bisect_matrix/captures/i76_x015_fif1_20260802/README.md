# i76_x015_fif1_20260802 — BACKLOG #76, strict fif = 1 on the wire

**The arm that showed fif = 2 is hiding a bug.**

The one owner-approved arm for #76. Full analysis:
`docs/experiments/76-fif1-costs-throughput-in-a-bracket-it-cannot-reach.md`.

## Conditions

* arm **x015**, `localhost/xrdp-bisect:73e4cb76d483.xx10fa3aa-tf` — the
  **same image as x014**, no rebuild. xorgxrdp `10fa3aa23033`.
* one monitor at **3840×2400 = 9.22 Mpx**, `SESSION_KIND=textflood`,
  oracle client, 60 s, `intra_refresh_frames = 240`
* `eager_slot_ack = true`, `emit_thread = true`, VAAPI 444
* **`gfx/x015.toml`'s body is `gfx/x014.toml`'s byte for byte** (verified
  by `diff` from `[codec]` onward). The ONLY variable against x014 is the
  environment variable `XRDP_GFX_FRAMES_IN_FLIGHT=1` in `k8s/x015.yaml`.

## Mechanism check, run before the rate

All **8056** `send` records read `fif=1`, and `id_server − id_client` was
**0 on 8052** of them (1 on the other 4) — the server never held more
than one unacknowledged frame. On x014 all 12 300 read `fif=2` with a lag
of ≤ 1 on 97.0 %. **The knob applied.**

## Result

| | x014 (fif=2) | x015 (fif=1) |
|---|---|---|
| sends / 56.8 s | 3075 | 2015 |
| send interval mean | 18.5 ms | **28.2 ms** |
| p50 / p90 / p99 | 18 / 19 / 47 | 28 / 30 / **32** |
| worker `pump` (feed+encode+drain) | 16.616 | **26.728** |
| worker `coll` (LTR rewrite) | 1.363 | 1.498 |
| worker `wait` (nothing to encode) | 0.515 | **0.002** |
| period | 18.508 | 28.245 |
| capture → egress | 34.8 ms | 55.6 ms |
| egress → client ack | 14.4 ms | **6.0 ms** |
| **capture → client ack** | **49.2 ms** | **61.5 ms** |
| producer's own interval | 15.94 ms | 16.26 ms |
| **FR-BENCH-1 margin** | 1.09× | **1.74×** |

Per-cycle closure residual 0.004 ms. Trace: 28 198 records rendered,
59 s span for a 60 s run.

## Three things to read before quoting anything from here

1. **This is a BUG, not a trade-off** (owner directive 2026-08-02, PRD
   FR-ACK-3: *all the concurrency we need, at the cost of fif = 1*). All
   10.1 ms of the regression landed in `pump` — feed + encode + drain —
   which a *client* ack window cannot reach (`frames_in_flight` is read
   only at `xrdp_mm.c:1691` and `:4234`, neither on the encode path).
   And it cannot be flow control: the encoder's depth is provably one
   frame, and the worker's `wait` is 0.002 ms/cycle at fif = 1, so there
   is no queue to fill and no idle worker to feed. **The second credit
   was covering a stall.** Finding it is BACKLOG #76, top priority;
   BACKLOG #78's instrument is its first step.
2. **Latency got WORSE, not better.** fif=1 removed 8.4 ms of client-ack
   lag and the pipeline gave back 20.8 ms of its own: 49.2 → 61.5 ms
   capture-to-ack. That 20.8 ms is the bug.
   Note the payload is NOT in the way here: at fif = 1 the FR-BENCH-1
   margin is 1.74×, not x014's 1.09×.
3. **`E5 GATE: baseline 51.1 ms → 1.81x` in VERDICT.txt is void**, for
   the fourth time and the same reason: 51.1 ms is the harness default
   from `SESSION_KIND=code` at another geometry. Compare against x014's
   18.5 ms.

## Correctness

Certified at deploy (`certs/x015.cert`, 3 s at 1920×1080): assert gate
**7/7**, 0 black frames, one contiguous frame_num chain. Over this run: 0
rewrite failures, 0 unsupported, 0 pair aborts, 0 budget assertions
across 4030 rewritten packets.

## Files

`textflood_stamps.tsv.gz` is the payload's own per-frame render/blit/sync
log, read out of the pod after the run — it is what FR-BENCH-1 is judged
on. The oracle dump (7.03 GB) was written and discarded by the gate, as
designed; it is the timing instrument, not evidence.
