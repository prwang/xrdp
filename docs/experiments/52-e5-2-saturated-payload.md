<!--
Experiment record moved out of BACKLOG.md on 2026-08-01.

BACKLOG.md is the OPEN work list: hypotheses, justification, and a
pointer. This file is the closed record it points at -- the conditions,
the numbers, the anomalies and the retractions, kept verbatim as they
were written at the time. Nothing here is a live task.
-->

## #52 — E5-2: saturated-payload frame interval (DONE 2026-07-30 — 2.13× GREEN)

**Why.** E5's payload (`SESSION_KIND=code`) is a `sleep 0.1` scroll
loop: 10 Hz damage per monitor, server 78 % idle, pipeline never full —
both the 51.1 ms baseline and the 52.5 ms measurement were readings of
the payload's own clock (#45 GATE RESULTS, reassessed 2026-07-30). A
throughput gate needs the producer strictly FASTER than the pipeline,
so damage coalesces (forced frame loss) and the send interval measures
the server. Scope: fleet/bench only — no change to shipped defaults,
no change to the cadence-deterministic bandwidth payloads.

**Step 0 — fix the instrument first.** `common/log.c:1159`:
`millisec = (tv.tv_usec + 500 / 1000);` → `(tv.tv_usec + 500) / 1000`
(round µs → ms; upstream bug, introduced by `db962399` "log: quit
using lrint and -lm"). Without it, ~10 % of trace stamps are up to
~0.9 s late and every E5-2 percentile would need offline repair.
Add a unit test under `tests/common` — factor the µs→ms conversion so
it is testable pure (assert 999 499 µs → 999, 45 123 µs → 45, 999 µs
→ 1, 0 → 0; the buggy code returns the leading digits instead), and
assert the formatted field is exactly three digits. Candidate for a
separate upstream PR — it fixes every xrdp log timestamp, not just
ours.

**Step 1 — the payload: minimal change.** New `SESSION_KIND=codeflood`
in `PR-demo/mac_bisect_matrix/banner.sh`, inside the existing
`code|codeline|codefast` case: `STEP=25`, `DELAY=0` (others keep
`DELAY=0.1`), i.e. the same deterministic 3000-line corpus scroll with
the metronome removed — self-clocked by consumption, still failing
LOUD if the corpus mount is missing. No WM runs in these sessions, so
the single `xterm -maximized` spans the full virtual screen and BOTH
monitors receive damage continuously (the E5 trace proved one repaint
damages both surfaces, 26 ms apart). `code`/`scroll`/`gray`/`chroma`
are untouched: they are the FR-H264-8 BANDWIDTH baselines, where
cadence determinism is the point. Optional second bound, same
one-liner pattern: `grayflood` (full-screen bands, no sleep) as the
max-encode-cost/full-frame-damage worst case.

**Step 2 — arms (fleet discipline; never mutate a deployed arm).**
`banner.sh` is baked into the image, so rebuild via
`build_and_deploy.sh` and stand up TWO arms simultaneously:
- **arm-s** — the #45 pair (xrdp `f7acb5979788` + xorgxrdp
  `d77d05463e52`), the current arm-r software;
- **arm-t** — the baseline: xrdp at `a0d9e773` (steps 0–4, i.e.
  pre-pump_set/pre-batching) + the SAME xorgxrdp `d77d05463e52`, so
  the producer side is identical and the A/B isolates exactly steps
  5 + 7. (The original 51.1 ms baseline build's capture was never
  committed; E5-2 does not reuse its number for anything.)
Both arms `SESSION_KIND=codeflood` via pod env. arm-r stays up as the
10 Hz reference; arm-q retires (its gate is answered).

**Step 3 — measure.** `e_gate_run.sh` oracle mode, 180 s, E3 geometry
(2560×1440 + 3840×2400), against EACH arm back to back. Harness
change: the hardcoded 51.1 ms baseline becomes `E5_BASE_MS`
(env-overridable); the E5-2 ratio is **flood-vs-flood** (arm-t mean ÷
arm-s mean). Record per arm: mean/percentiles of the send interval
(now trustworthy per step 0), `kids_armed` histogram, worker busy %
(batch→last=1 vs wall clock), per-monitor period, and the rendering-
client end-to-end rate beside it as usual.

**Gate.** ≥ 2.0× GREEN, ≥ 1.5× AMBER, else RED — and the stop rule
carries over verbatim: under 1.5× the remainder is attributed
(capture pacing/budget, vmsplice feed, NUT demux, LTR rewrite, EGFX
assembly) before anything ships; no re-tuning to make the number look
better.

**Predictions, recorded before running (each is checkable in the
capture):**
1. under flood, `set_n=2 / kids_armed=4` becomes the COMMON case
   (it was 0.6 % at 10 Hz) — step 7's premise finally exercised;
2. worker busy % rises sharply; if arm-s approaches saturation the
   2.0× target is genuinely in reach, since the serialized baseline
   pays `2m(w+e)` where the set pays ~`w+e`;
3. if instead the interval pins at a producer period ≫ service time
   with the worker still mostly idle, the constraint IS capture-side —
   then #45's two capture questions (deferred-update pacing / ack
   budget retirement; handing both monitors over together) become the
   next item, now with real evidence instead of a payload echo.

**Acceptance:** log.c fix merged with its unit test (astyle 3.4.14 +
CI green); `codeflood` in-tree; both arms deployed from clean debs and
recorded in the deployed-state table; E5-2 ratio + attribution
committed beside the captures and summarized in PRD FR-H264-6/#45
(the old E5 numbers stay, relabeled as the 10 Hz-cadence measurement);
smoke gate run against any arm handed to a human.


### #52 RESULTS (2026-07-30) — **E5-2: 2.13×, GREEN**

Two arms, one payload, 180 s each, E3 geometry, oracle client. Evidence:
`PR-demo/mac_bisect_matrix/captures/e52_flood2_arm-s_20260730/` (README
carries the tables; `../e52_flood2_arm-t_20260730/` is the baseline arm).

| | arm-t baseline (steps 0–4) | arm-s (steps 0–7) |
|---|---|---|
| xrdp-dev | `+git20260730013437.5dae11f63adb` | `+git20260730013346.52b8798839ad` |
| xorgxrdp-dev | `d77d05463e52` | **the same** |
| mean per send | 63.6 ms | **29.9 ms** |
| p50 / p90 / p99 | 61 / 80 / 86 ms | 31 / 53 / 64 ms |
| sends/s | 15.72 | **33.40** |
| per-monitor period | 127.4 ms | **59.9 ms** |
| pictures pushed | 4 938 MiB (234 Mbit/s) | **10 434 MiB (495 Mbit/s)** |
| worker busy | 16 % | 32 % |
| `kids_armed=4` | n/a | **52 % of 3 889 cycles** |

**Ratio 2.13× — GREEN.** Predictions 1 and 2 from the spec hold:
`kids_armed=4` went from 0.6 % of cycles at 10 Hz to 52 %, and the
parallel set is worth ~2× once both monitors have work. E2 also holds
under the flood (7/7 assertions, zero black frames in 991 pictures at
~0.93 MB per picture, zero rewrite failures / budget assertions).

**Steps as landed.** Step 0: `common/log.c` rounds µs→ms via a factored
`log_usec_to_msec()`, with `tests/common/test_log.c` (8 cases) — full
`make check` 366/366. Verified in situ: arm-t has ZERO out-of-order log
stamps and a flat histogram of fractional parts; arm-s has 1.1 % of
lines out of order by ≤ 8 ms, which is step 5's worker thread
interleaving with the main thread, not clock corruption. Step 1:
`codeflood` + `grayflood` in `banner.sh`; cadence kinds untouched. Step
2: arms `arm-s` (:40018) and `arm-t` (:40019), both from clean debs,
both `SESSION_KIND=codeflood`, same xorgxrdp so the A/B isolates steps
5+7. Step 3: `E5_BASE_MS` replaces the hardcoded 51.1, and the harness
now records the `kids_armed` histogram, worker busy %, per-monitor
period and `SESSION_KIND` per run.

**The first flood pair was RED at 0.91×, and it is kept.**
`captures/e52_flood_arm-{s,t}_20260730/`: removing the metronome was not
enough. A corpus line is ~27 visible columns and the xterm is 6400 px
wide, so the ink sat on the primary monitor only — the oracle dumps
measured **167 MB of pictures on the 2560×1440 monitor against 0.75 MB
on the 3840×2400 one**, which still took full-monitor damage every cycle
because the window spans both. A batch has nothing to overlap when one
of its monitors is blank, while the shared deadline still couples the
active monitor to the idle monitor's full-area capture and upload:
68.5 ms against the baseline's 62.6 ms. `codeflood` now repeats each
corpus line 32× so every row wraps past the right edge of monitor 2.

**Two findings this leaves open (new items, not blockers):**
1. **Idle-monitor coupling is a real ~9 % regression.** One active
   monitor beside an idle one is an ordinary desktop, and there the
   batch loses. The fix is to arm a monitor only when it has changed
   pixels, rather than because a window overlaps it — cheap to test
   with the first flood pair as the ready-made benchmark.
2. **The remaining headroom is capture-side, now with evidence.** At
   2.13× the worker is still only 32 % busy: per-pair service is
   14.7 ms (encode collected 2.8 + rewrite/emit 12.3) against a 59.9 ms
   per-monitor period, and after a frame's `last=1` the same monitor's
   next damage arrives 41 ms (p50) to 105 ms later. Flow control never
   binds (un-acked p50 0 / max 4 of fif=2, client `queue_depth` 0) and
   it is not bandwidth (495 Mbit/s over loopback). #45's two deferred
   capture questions — deferred-update pacing / ack-budget retirement,
   and handing both monitors over together — are the next lever.

---
