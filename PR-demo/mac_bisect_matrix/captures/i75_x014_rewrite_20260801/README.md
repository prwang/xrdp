# i75_x014_rewrite_20260801 — BACKLOG #75, the rewrite optimisation on the wire

The one owner-approved arm for #75. Full analysis:
`docs/experiments/75-the-rewrite-was-re-serialising-the-picture.md`.

## Conditions

* arm **x014**, `localhost/xrdp-bisect:73e4cb76d483.xx10fa3aa-tf`
  (xrdp `73e4cb76d483`), xorgxrdp `10fa3aa23033`
* one monitor at **3840×2400 = 9.22 Mpx**, `SESSION_KIND=textflood`,
  oracle client, 60 s, `intra_refresh_frames = 240`
* `eager_slot_ack = true`, `emit_thread = true`, VAAPI 444
* **`gfx/x014.toml`'s body is `gfx/x013.toml`'s byte for byte** — the
  only variable against x013 is the xrdp deb

## Result

| | x013 (`82babb9fe4ba`) | x014 (`73e4cb76d483`) |
|---|---|---|
| sends / 56.8 s | 2228 | **3075** |
| send interval mean | 25.5 ms | **18.5 ms** |
| p50 / p90 / p99 | 25 / 28 / 31 | **18 / 19 / 46** |
| worker `collect` | 8.802 ms | **1.362 ms** |
| worker `pump` (control) | 16.654 ms | 16.585 ms |
| period | 25.474 ms | 18.476 ms |
| producer's own interval | 16.71 ms | 16.91 ms |
| **FR-BENCH-1 margin** | 1.52× | **1.09×** |

Trace: 104 364 records, 0 drops. Per-cycle closure residual
max |0.000000| ms.

## Three things to read before quoting anything from here

1. **p99 is a REGRESSION: 31 → 46.5 ms.** Mean and p50 improved by ~7 ms
   and the tail did not. It is the 66 of 3074 cycles (2.15 %) in which
   the worker waited on the producer; those 66 carry 99.7 % of all wait
   time, so `wait`'s 0.515 ms mean is meaningless — read the shape.
2. **FR-BENCH-1 is MARGINAL at 1.09×**, not failed. The producer's p90
   (19.01 ms) overlaps the pipeline's p50 (17.96 ms). With the producer
   stalls removed the period is 17.96 ms (1.42× over x013); as
   delivered it is 18.476 ms (1.38×). Both are stated in the record and
   the difference between them is the point.
3. **`E5 GATE: baseline 51.1 ms → 2.77x PASS` in VERDICT.txt is void.**
   51.1 ms is the harness default from `SESSION_KIND=code` at another
   geometry. The comparison that means something is x013's 25.5 ms.

## Correctness

Certified at deploy (`certs/x014.cert`, 3 s at 1920×1080): assert gate
7/7, 471 pictures, 0 black frames, one contiguous frame_num chain. Over
this run: 0 rewrite failures, 0 unsupported, 0 pair aborts, 0 budget
assertions across 6150 rewritten packets — the bounded-prefix fast path
never fell back.

## Files

`textflood_stamps.tsv.gz` is the payload's own per-frame render/blit/sync
log, read out of the pod after the run. It is what FR-BENCH-1 is judged
on and it is archived here because the margin is now the load-bearing
caveat, not a formality.
