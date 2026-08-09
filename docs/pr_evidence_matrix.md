# The PR evidence matrix — which arms exist, what each one proves

**Status: PROPOSAL, 2026-08-09. Nothing has been built or torn down.**
Written on the owner's directive: as the dev branch matures into the
clean-room pass and the PR write-up, we need *a few representative
instances in the pod, with the frontier in various options plus
simulated conditions, each supporting a point in the writing document* —
and explicitly **not** the maintenance of every legacy result. Most of
what is in `captures/` is retired by this document.

This file is the inventory. It is deliberately organised by **claim
first**, because an arm that supports no claim should not exist and a
claim with no arm is a sentence we cannot write.

---

## 0. THE ONE THING TO FIX FIRST: the matrix must run on the FAST payload

**Every recent run used the slow benchmark payload, and after the pipe
fix that payload is the same speed as the pipeline.** The fast payload
already exists — this is a harness-selection mistake, not missing work.

`PRD.md` FR-BENCH-1 requires the payload to be strictly faster than the
pipeline it gates, with a margin the harness prints on every run. The
floor is **2.0×**. Measured margins, read out of the captures' own
`VERDICT.txt`:

| capture | payload | pipeline | producer | margin |
|---|---|---|---|---|
| `i92_sparse_aux_ab_…_211023` leg b1 (before the pipe fix) | textflood | 24.6 ms | 16.1 ms | 1.53× |
| `i92_sparse_aux_ab_…_233519` leg a1 (after) | textflood | 17.9 ms | 17.0 ms | **1.05×** |
| `i92_sparse_aux_ab_…_233519` leg b1 (after) | textflood | 16.9 ms | 16.2 ms | **1.04×** |
| `i80_c1_nonregression_…_141752` | textflood | 18.8 ms | 16.5 ms | 1.14× |
| `i87_eager_ab_…_180910` | textflood | 19.1 ms | 16.7 ms | 1.14× |
| `i91_window4_m2_…_201346` | textflood | 13.3 ms | 10.7 ms | 1.25× |
| `i80_multimon_strip_…_152223` (two monitors) | textflood_strip | 14.6 ms | 9.9 ms | 1.47× |
| `i83_strip_payload_…_182340` leg r2 | textflood | 17.5 ms | 16.6 ms | 1.05× |
| `i83_strip_payload_…_182340` leg s1 | **textflood_strip** | 17.6 ms | **4.5 ms** | **3.94×** |
| `i83_strip_payload_…_182340` leg s2 | **textflood_strip** | 17.8 ms | **4.6 ms** | **3.90×** |

The last row is the point. **BACKLOG #83 landed a fast payload on
2026-08-06** — `textflood --scroll strip`, deployed as
`SESSION_KIND=textflood_strip` — and it measured **4.5 and 4.6 ms per
frame at exactly the geometry this matrix uses (one monitor,
3840×2400), giving margins of 3.94× and 3.90×**, comfortably above the
2.0× floor. Those are legs `s1`/`s2` of `i83_strip_payload_…_182340`;
legs `r1`/`r2` of the same capture are the slow payload for contrast.

So the position is:

* **`textflood` (16.2 ms/frame) is finished as a measuring instrument
  for this pipeline.** Before the pipe fix it gave 1.53×, which was
  already under the floor; after, 1.04×. At 1.04× the gate's own words
  are: *"Any claim from this run about two pipeline stages OVERLAPPING
  is VOID … Throughput and regression comparisons against an arm sharing
  this payload, geometry and client REMAIN VALID — quote this 1.04×
  beside every number taken from them."*
* **`textflood_strip` (4.5 ms/frame) is the instrument, and the whole
  matrix runs on it.** At a 17 ms pipeline that is ~3.8×, and it stays
  above 2.0× down to a 9 ms pipeline — enough headroom that the matrix
  does not have to be rebuilt the next time the server gets faster.
* **Nothing else in `captures/` used it** except
  `i83_strip_payload_…_182340` legs s1/s2 and
  `i80_multimon_strip_…_152223`. That is two captures out of 92, which
  is most of the reason section 3 retires the rest.

**Consequence for the numbers quoted so far, stated plainly:** the
55.6 fps and 58.6 fps from `i92_sparse_aux_ab_…_233519` were taken at
1.05× and 1.04×, so they may be reporting textflood's ceiling rather
than the server's. The byte comparison from that run is unaffected —
bytes are a property of what was encoded. There is one piece of
counter-evidence worth keeping in view: in #83's own control the 3.6×
faster payload did **not** move the pipeline (17.6 ms on strip against
17.5 ms on textflood), which says the pipeline was genuinely the limit
at ~17.5 ms on that build. Whether that still holds now the pipe is
unclamped is exactly what step 0 below settles.

### Step 0 — one leg, before anything is built

Run **one 20 s leg on the existing arm x030 with
`SESSION_KIND=textflood_strip`** and read two numbers out of its
`VERDICT.txt`: the FR-BENCH-1 margin (expected ~3.8×) and the frame
period (expected ~17 ms if the pipeline is genuinely the limit, lower if
textflood was flooring it). Estimated **~5 minutes**, one arm, no build,
no new image, one variable changed against a run we already have.

That single leg decides whether the fps figures already reported stand
or need withdrawing, and it re-verifies the 4.5 ms producer on the
current build before ten more runs are spent trusting it.

---

## 1. The claims the PR will make, and what each one needs

The clean-room slices are in `docs/avc444_upstream_port_plan.md`. This
table is the evidence side of the same list. "Instrument" is what
produces the evidence, and the cheapest sufficient one is named — CI
where CI can do it, because an assertion in `tests/` costs nothing to
re-run and never drifts.

| # | The claim the document makes | Instrument | Arm | Condition |
|---|---|---|---|---|
| C1 | The AVC444 wire framing is interleaved LC=1 luma → LC=2 chroma, and real clients render it | CI (`test_avc444_metablock.c`) + `arm_certify.sh` wire audit + onscreen on macOS/Windows | R | one 4K monitor |
| C2 | The LTR chain is byte-exact and its intra refresh is scheduled per view | CI (`test_avc444_ltr.c`, 26 golden vectors) — **no arm needed** | — | — |
| C3 | The encoder feed is vmsplice-only and needs a 64 KiB input pipe; a smaller one is announced and invalidates the run | `tools/vmsplice_pipe_bench.c` + the `PIPE_TOO_SMALL` guard | any | none |
| C4 | Legacy flow control stalls the encoder, and the stall is the end-to-end window rather than the emission point | gate A/B, same payload | R vs F1 vs F2 | LAN, one 4K monitor |
| C5 | Widening the legacy knob does not substitute for the frontier | gate A/B | R(window 3) vs F2 | LAN, one 4K monitor |
| C6 | The frontier's bound holds on the wire, including under a frozen client | gate, freeze leg + CI enumeration | F2 | LAN + freeze |
| C7 | The frontier behaves under a real round-trip time | gate A/B under `netem_rtt.sh` | F1 vs F2 | 40 ms RTT, one 4K monitor |
| C8 | Dropping chroma in motion is a bandwidth lever: ~44 % fewer bytes, ~5 % of frame time | gate A/B, bytes per frame | F2 vs S | LAN, one 4K monitor |
| C9 | AVC444 costs little against AVC420 in time, and that is what the extra bytes buy | gate A/B | F2 vs A420 | LAN, one 4K monitor |
| C10 | Multi-monitor works and the window divides by monitor count | gate | F2 | LAN, two monitors |

C2 and C3 need no fleet time at all. C1's onscreen half needs a human
and a real client, and cannot be automated.

---

## 2. The arm set — five arms, ONE image

**Every arm is built from the same commit and differs only in
`gfx/<arm>.toml`.** This is the single most important property of the
set and the current fleet does not have it: the 17 pods live today carry
**five different images**, so no two of them are comparable and any
cross-arm number from them is not evidence of anything.

| arm | `eager_slot_ack` | `wire_window` | chroma cadence | `avc_mode` | exists to be |
|---|---|---|---|---|---|
| **R** | false | 2 | off | 444 | the reference: what upstream does today |
| **F1** | true | 1 | off | 444 | the frontier at the legacy-equivalent window |
| **F2** | true | 2 | off | 444 | the frontier as proposed to ship |
| **S** | true | 2 | 1000 / 100 ms | 444 | F2 plus the sparse-chroma cadence |
| **A420** | true | 2 | off | 420 | the 4:2:0 comparison a reviewer will ask for |

Everything else is a **run-time condition, not an arm**, and must not
become one:

* **round-trip time** — `netem_rtt.sh` on the arm's veth pair;
* **payload** — `SESSION_KIND` in the arm's manifest;
* **geometry and client** — the client rig on this box, which never
  changes as part of a server experiment;
* **bandwidth** — the tier-0 limiter used by #98.

The reason to hold this line is arithmetic: five arms cover the four
config axes, and every condition added as an *arm* multiplies the image
imports, the certifications and the drift surface, while the same
condition applied at *run time* costs one flag.

---

## 3. What is retired

**All 92 existing capture directories are retired as evidence** except
the ones a claim above cites, and the fleet is torn down to the five
arms.

The reasons, in order of how much they matter:

1. **Everything measured before 2026-08-08 23:35 carries a clamped
   encoder input pipe** — ~7 ms per frame at 3840×2400 that no
   deployment with a normal pipe would pay (BACKLOG #103). Ratios inside
   one capture survive; absolute periods do not.
2. **Five different images.** Cross-arm comparison between them is not
   apples to apples and never was.
3. **Not one of them met the FR-BENCH-1 producer floor**, so every rate
   in them carries a caveat that has to be restated anyway.
4. **The PR does not carry `PR-demo/` at all** (CLAUDE.md). These
   captures are the dev branch's working notes; the PR needs a small
   number of clean, current, reproducible results.

**What is kept, and why:**

* `docs/experiments/*.md` — kept verbatim, wrong claims included. That
  is the standing rule and it is not affected by garbage-collecting the
  raw captures. Records that cite a deleted capture keep the citation
  and gain a line saying the capture was collected.
* `i103_pipe_handover_20260808/` — the pipe mechanism and the knee. It
  is not a fleet rate measurement and does not decay.
* The two `i92_sparse_aux_ab_*` runs — the before/after pair across the
  sysctl boundary is the evidence for #103's consequence, and deleting
  it deletes the reason every other capture was retired.
* `certs/` — a certificate is the statement that an arm's bytes are
  conforming, and it is keyed to image + config, so it is self-invalidating
  rather than stale.

---

## 4. What it costs

Estimates, marked as estimates, to be confirmed before anything runs:

| step | estimate |
|---|---|
| one deb + container image build | ~10 min |
| import + deploy 5 arms | ~5 min |
| certify 5 arms (3 s payload each) | ~5 min |
| one gate run, one arm, one condition | ~5 min |
| the ten claims above | ~10 gate runs, ~50 min |

So roughly **20 minutes to stand the matrix up** and **an hour to fill
it**, assuming no arm comes back red. That is a bounded, one-time cost
against a fleet that currently costs a comparison's validity every time
it is used.

None of it should start until section 0 is answered.
