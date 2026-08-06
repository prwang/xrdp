# E5-2 on the T4, textflood payload — BASELINE arm (#45 steps 0–4)

The reference half of the 2026-07-31 pair. Full result, attribution and
reproduction steps are in the batched arm's README:
`../e52_t4_textflood_batched_20260731/README.md`.

| | value |
|---|---|
| mean per send | **122.9 ms** |
| sends/s | 8.14 |
| per-monitor period | 246.8 / 245.8 ms |
| damage coverage | 715 / 714 — **1.00×**, both monitors inked |
| pictures | 1424 |
| black frames | **0** of 1424 |
| wire audit | **7/7 PASS** |
| batch cycles | **0** — this build is pre-step-5 and cannot batch, as expected |

## What was deployed

* xrdp-dev `0.10.80+git20260730013437.5dae11f63adb` — #45 steps 0–4
* xorgxrdp-dev `1:0.10.80+git20260729225933.d77d05463e52` — step 6
* `/etc/xrdp/gfx.toml` = `PR-demo/t4_profile/gfx-t4-nvenc-ltr-g240-gate.toml`
* payload `textflood`, 2560×1440 + 3840×2400, oracle client, 180 s
* smoke-gated before measuring: 8/8 keys, edge 1.000, both sizes

Note the **version-sort trap**: this deb sorts NEWER than the batched one
(`…013437` > `…013346`) because the version carries the COMMIT timestamp,
so install order proves nothing. `deployed_packages.txt` carries the hash
`dpkg -l` reported at run time, which is the proof of which arm this is.

`oracle/` (raw AVC444 dump) was audited by the wire audit and the
black-frame check, then deleted; `gfx_trace.txt.gz` re-derives every number.
