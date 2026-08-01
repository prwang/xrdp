<!--
Experiment record moved out of BACKLOG.md on 2026-08-01.

BACKLOG.md is the OPEN work list: hypotheses, justification, and a
pointer. This file is the closed record it points at -- the conditions,
the numbers, the anomalies and the retractions, kept verbatim as they
were written at the time. Nothing here is a live task.
-->

## #62 — textflood: a payload whose X-side cost is a memcpy (DONE 2026-07-31 — deployed, A/B run, **1.41x RED, attributed**; **2026-07-31 verdict annotated: producer-confounded, re-run under #73 (was #67)** — the 1.41× may understate the batch if both arms were paced by the same 8 fps producer, per FR-BENCH-1)

Closes the instrument half of #61. #59 established that the xterm payload
makes E5-2 measure the X server rather than our pipeline; `PR-demo/textflood/`
renders the **same** corpus (`code_corpus.ansi`, real xrdp source highlighted
by pygments + clangd in solarized-dark) with cairo **in its own process** and
hands X one finished image per frame over MIT-SHM.

Measured on the dev box, Xvfb 6400x2400, 30 s per arm, two `/proc/<pid>/stat`
reads:

| arm | X-server CPU | payload's X-side cost |
|---|---|---|
| idle Xvfb | 13.7 % of a core | — |
| xterm codeflood (server-side XRender glyphs) | **99.0 %** | 85.3 points |
| textflood (client-side cairo + MIT-SHM) | **12.8 %** | ~0, at the idle floor |

**7.7x less X-thread cost for the same content.** textflood's own
rasterization (~87 % of a core) runs in a different process on a different
core; the 3.85 s of X CPU that remains is the SHM copy — ~1050 frames x
61 MB in 3.85 s = 16.6 GB/s, the memcpy floor. Total system work rises, the
bottleneck thread is freed. That is the right trade where Xorg is
single-threaded with idle cores beside it.

Rendering verified from a decoded `xwd`, not assumed: 8/9 solarized entries
present (magenta is on 43 of 3000 corpus lines, so a sample missing it is
expected), 3650 distinct colours, and **21.5 % of pixels are subpixel-AA
fringes** (non-palette, R!=G or G!=B: `#002b37`, `#012b36`, `#165d83`).
Subpixel AA is requested explicitly rather than inherited from the session's
fontconfig — it is what a real desktop renders, and it is the maximal AVC444
stressor, so a silent fallback to greyscale AA would flatter the 4:2:0 arm.

Also removes the failure mode that invalidated three T4 runs: the window is
override-redirect over the whole root, so xfwm4 cannot re-snap it to one
monitor (#53's one-active-one-idle regime) and the xdotool span-fixer loop
is no longer needed.

**Scope note.** That table measures the *payload's* X-side cost on Xvfb — a
property of the payload, not an xrdp measurement, and not a substitute for
one. The E5-2 number still has to come from the T4 with xorgxrdp in the loop.

### T4 RESULT (2026-07-31) — the payload works; the ratio is RED

Deployed to the T4 and the A/B run, both arms 180 s in one sitting, each
deb smoke-gated BEFORE measuring (the #61 precondition):

| | baseline (steps 0-4) | batched (steps 0-7) |
|---|---|---|
| mean per send | 122.9 ms | **87.0 ms** |
| damage coverage | 715/714 = **1.00x** | 1009/1009 = **1.00x** |
| `kids_armed=4` | n/a (cannot batch) | **100 % of 1011 cycles** |
| black frames | 0 of 1424 | 0 of 2014 |
| **ratio** | | **1.41x — RED** |

Captures: `PR-demo/mac_bisect_matrix/captures/e52_t4_textflood_{baseline,batched}_20260731/`.

**The instrument did its job.** Against the same box under the old xterm
payload: session Xorg **92 % of a core -> 28.9 %**; libpixman self
**71.6 % -> 6.3 %**; our capture path **13.8 % -> 37.4 %** of Xorg cycles
(`avc444_decode_row.avx2` 26.5 % self, the top symbol in libxorgxrdp).
Monitor coverage was 1.00x on both arms with no span-fixer — the
override-redirect window removed that whole failure class.

**The ratio is RED and it is NOT a CPU ceiling.** `session_cpu_split.sh`
during the run: Xorg 28.9 %, payload 79.7 %, xrdp encoder side 24.4 % =
**1.33 of 4 cores**. Nothing pinned, 2.7 cores idle. `e52_period_decompose.py`
on the run's own trace says the 173.7 ms period is:

| segment | ms | % | whose |
|---|---|---|---|
| batch arm -> first enc submit (capture + AVC444 pack) | **63.7** | 36.7 | **ours** |
| enc submit -> last=1 (encode + LTR rewrite + EGFX assembly) | **68.4** | 39.4 | **ours** |
| last=1 -> next batch arm (idle, awaiting damage) | 41.5 | 23.9 | payload |
| **our pipeline** | **132.1** | **76.1** | |

**76 % of every period is our own pipeline running serially on a box that
is 33 % busy.** The remaining cost is latency we have not parallelised,
not arithmetic we cannot afford.

Why less than codeflood's 1.5x-2.3x: textflood damages the whole root every
frame, so a pair is ~3.8 MB (main 2.09 + aux 1.69) against codeflood's
~0.6 MB. The batch parallelises the encode of the four children and does
nothing for the 63.7 ms of capture in front or the assembly behind. Amdahl,
measured. The batch is not regressing — it is being measured against a
workload heavy enough to expose what is not batched. This motivates **#63**.

### Harness faults found and fixed during the run

1. `E_COLD` waited only for the Xorg process to die, not for sesman to
   finish teardown (~600 ms more); a client connecting inside that window
   is dropped with `freerdp_post_connect failed`. Baseline won the race,
   batched lost it — a flake that reads as "the batched deb cannot start a
   session". Fixed in `e_gate_run.sh`.
2. A deb install invalidates `xrdp.service` and its drop-ins; restarting
   without `systemctl daemon-reload` silently dropped `XRDP_GFX_TRACE=1`
   and produced a run with zero trace records. `t4_install_arm.sh` now
   reloads and verifies the variable is in the unit environment.
3. A dead ssh port-forward is reported by the harness as
   `connected; recording for 180s`, producing a VERDICT against an empty
   log. Two runs lost before it was spotted. **Not yet fixed** — see below.
4. The gpuflood supervised-alacritty loop never exits when its session
   ends: four orphaned shells were found respawning alacritty 5781 times.
   The `gpuflood` kind is being deleted outright (alacritty is abandoned:
   without a GPU it is llvmpipe/zink, which would be the new bottleneck).

### Remaining, in order

1. DONE — deployed, smoke-gated first, measured.
2. DONE — it does not starve Xorg: 1.33 of 4 cores, nothing pinned.
   (Predicted ~1.6; measured 1.33.)
3. DONE — pixman self 71.6 % -> 6.3 %, capture 13.8 % -> 37.4 %.
4. DONE — E5-2 pair re-run: 1.41x RED, attributed above.
5. TODO — make `e_gate_run.sh` FAIL LOUDLY when the RDP port is not
   reachable or the run produced zero GFX_TRACE records, instead of
   printing `connected` and emitting a VERDICT against an empty log.
6. TODO — delete the `gpuflood` kind and its orphan-prone restart loop.
7. Then use textflood as the instrument for FR-PROC-7 (#40/#41): its
   preempt/breadth/depth policies need a payload that loads the aux view,
   which subpixel-AA text does maximally (aux is 44.8 % of the bytes here).
