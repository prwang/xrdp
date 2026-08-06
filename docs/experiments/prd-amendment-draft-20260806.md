# PRD amendment draft — 2026-08-06 — NOTHING HERE HAS BEEN APPLIED

**Status: DRAFT. Not applied to `PRD.md`. Not agreed. Do not cite any
sentence in this file as a requirement.**

This file holds ready-to-paste replacement text for the two things the
owner explicitly reserved on 2026-08-06 when limiting PRD authority to
uncontroversial corrections. Both change what a requirement REQUIRES,
so they need owner sign-off before anyone edits `PRD.md`:

1. **FR-FLOW-1 clause 4** — the wire window's shipped default and what
   the netem simulation is allowed to establish.
2. **FR-BENCH-1 status** — splitting the 2× producer-margin verdict, and
   a separate factual problem with the section header.

What was applied today, for contrast, is the emit-split retirement
(FR-ACK-2) and the stale-cross-reference corrections. Those were
authorised separately.

---

## 1. FR-FLOW-1 clause 4 — the wire window default

### Why it needs replacing

Clause 4 currently ends with: *"it has a stated default, chosen with
BACKLOG #81's RTT-harness data, that preserves short-RTT behaviour"*,
and the paragraph below the implementation table
(**"Shipped default C = 2 is a PLACEHOLDER, not a measured value"**)
says that condition is *"NOT yet satisfied"*.

Both sentences make a **netem simulation the source of truth for a
shipped default**. The owner ruled on 2026-08-06 (recorded in
`BACKLOG.md` #80, "RESCOPED 2026-08-06") that this is the wrong
authority: a netem link models delay and nothing else, while real WANs
add loss, cross-traffic, variable RTT, middleboxes and unknown TCP
stacks — and #98 measured that at 4K the binding WAN constraint is TCP
byte behaviour, not the credit count at all. Simulation verifies the
MECHANISM and its BOUND. It does not choose defaults and it does not
certify a value for the wild.

Two consequences the replacement has to carry: the default stops being
a placeholder awaiting data that will never be authoritative (it
becomes a deliberate legacy-equivalence choice under coding rule 2),
and the RTT → C closed form becomes operator guidance to validate per
deployment rather than a table this document certifies.

### Replacement for clause 4 (paste over the current clause 4)

```
4. **C is user configuration, not a PRD constant, and its default is
   chosen for equivalence, not for optimality.** Its correct value
   depends on the deployment (frame rate ≤ (C + 2)/RTT is the frame-
   count bound; on a bandwidth-limited link the byte bound
   (C + 2)·S/B + RTT binds first — see #98). This document requires
   only: it exists; it is enforced at admission; it has exactly one
   documented meaning (at most C + 2 frames unacked at send — capture
   rides ≤ 2 slots above the credit); its bound has a test; and it has
   a stated default.

   **The stated default is C = 2, and its justification is legacy
   `frames_in_flight` equivalence** — coding rule 2, no functional
   regression: at C = 2 an absent or default configuration reproduces
   the behaviour of the shipped `DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT`
   window exactly. It is NOT a simulation-derived optimum and must not
   be quoted as a recommended value for any particular network.

   **What simulation may and may not establish (owner directive,
   2026-08-06).** A netem link models delay; it does not model loss,
   cross-traffic, variable RTT, middleboxes or the receiver's TCP
   stack. It is therefore admissible evidence for the MECHANISM (the
   credit frontier advances, production stops when the client stops
   acking, the queue plateaus) and for the BOUND (`id_server −
   id_client ≤ C + 2` on every send). It is NOT admissible as the
   source of a shipped default, and no simulated leg may be described
   as establishing validity for wild WAN environments.

   **Operator guidance, not a certified table.** The closed form
   ⌈RTT/period⌉ + 1 is offered in the configuration documentation as a
   starting point for an operator to validate on their own link,
   against their own delivery-delay and stall targets. This document
   does not publish an RTT → C table, and a table produced under
   simulation would state what netem did, not what a deployment will
   do.
```

### Replacement for the "PLACEHOLDER" paragraph that follows the table

```
**Shipped default C = 2 is a legacy-equivalence choice, not a measured
optimum** (`XRDP_GFX_WIRE_WINDOW_DEFAULT`). It matches the legacy
`frames_in_flight` so that short-RTT behaviour is preserved bit for
bit, which is what coding rule 2 requires of a new knob. Clause 4's
earlier wording — "a stated default, chosen with BACKLOG #81's
RTT-harness data" — was RETIRED 2026-08-06 by owner directive: the
netem harness verifies the mechanism and the bound, and does not choose
shipped defaults. Do not quote 2 as a recommendation for any specific
network; quote it as "what the server did before this knob existed".
```

### What must NOT change with it

* The clause-3 credit frontier, the enforcement point, and the
  `≤ C + 2·M` per-monitor bound: unaffected.
* The measured WAN paragraph below ("What C costs, measured
  2026-08-03") stays verbatim, including its own warning that the
  clause-4 table must hold the TCP environment fixed and declared. If
  the table is abolished, that warning becomes the reason why, not a
  dangling reference — recommend keeping the sentence and appending
  *"— and that requirement is now the reason no such table is
  published."*
* `BACKLOG.md` #80 already records this rescope; applying the text
  above is what closes the "PRD FR-FLOW-1 clause 4 needs a matching
  amendment — owner sign-off required, not yet applied" line there.

---

## 2. FR-BENCH-1 — split the verdict

### Why it needs replacing

FR-BENCH-1 today has one verdict for two very different uses of the
producer margin. A run whose producer is only 1.09× faster than the
pipeline is genuinely unable to answer *"do capture and encode
overlap?"* — the producer may simply have nothing ready, so the absence
of overlap says nothing about the pipeline. But the same 1.09× run can
still support *"arm A is 12 % faster than arm B"* provided both arms
ran the same payload, because the producer's cadence is then a shared
constant, not a differential.

The owner ruled on 2026-08-06: **split the verdict.** The 2× floor
stays a hard gate for overlap/concurrency claims; throughput and
regression comparisons may proceed at any margin, provided both arms
share a payload and the margin is printed beside every number.

### Replacement for the "Saturation is verified per run" block

```
**Saturation is verified per run, never assumed.** The harness VERDICT
must print both checks below, and the producer margin — the producer's
own frame rate divided by the pipeline's sends/s — beside every rate it
reports:

- **Producer telemetry**: the producer logs its own frame timestamps;
  its standalone rate (`--selftest`, no RDP session) at the same
  geometry on the same box.
- **In-run observable**: damage is pending at pipeline completions —
  operationally, the frame-identity-paired overlap gap
  (`e52_period_decompose.py`) goes negative in a nonzero fraction of
  frames, or an xorgxrdp-side trace shows capture N+1 starting during
  encode N.

**The 2× floor is a gate for some claims and not for others (owner
directive, 2026-08-06).**

* **Overlap and concurrency claims require margin ≥ 2× — hard gate, no
  exceptions.** "Capture is hidden behind encode", "the fifo held a
  successor at pop time", "the stages overlap", and every property
  whose evidence is the ABSENCE of a wait, are undefined when the
  producer may simply have had nothing ready. Below 2× such a claim is
  reported VOID with the producer's own rate beside the pipeline's, and
  it can neither confirm nor falsify the property.
* **Throughput and regression comparisons may proceed at ANY margin,
  under three conditions**: both arms ran the SAME payload at the same
  geometry on the same box; the margin is printed beside every number
  the comparison quotes; and the comparison is stated as a ratio
  between the arms, never as an absolute claim about what the pipeline
  can do. Rationale: a shared producer cadence is a constant common to
  both arms, so it biases the MEAN ratio toward 1 — a slow producer
  caps the faster arm first and understates a real gain. A mean ratio
  measured at a poor margin is therefore a LOWER BOUND on the effect,
  and that is what it must be called. This argument covers means only;
  producer/pipeline phase interaction distorts the TAIL in neither
  direction predictably, which is why percentiles stay forbidden below
  the floor (next bullet).
* **What a poor margin still forbids in a throughput comparison**: any
  absolute rate quoted as the pipeline's capability, any percentile of
  the send interval read as a pipeline property (the producer's own
  tail is in it), and any comparison across payloads.
```

### The separate factual problem with the section header — flagged, not fixed

The header reads:

> `### FR-BENCH-1: The saturating-producer contract (owner directive,
> 2026-07-31 — PASSING as measured; ...)`

and the status paragraph under it reads **"Status: PASSING as measured
(#65 step 0 — chain since renumbered, now #91, 2026-07-31, T4 m=1
3840×2160)"**, on a producer rate of 27.66 fps against a pipeline at
8.21 sends/s.

Three things are wrong with letting that stand as the current status,
and none of them is in today's authorised scope:

1. **The box is gone.** That run was on the T4, decommissioned
   2026-07-31 (`BACKLOG.md`, "Deployed state"). Nothing has re-run the
   contract on the fleet hardware that every current number comes from.
2. **The geometry is not the one now being gated.** It passed at
   3840×2160 on the T4; the arms in use today gate 3840×2400, and at
   that geometry the most recent measurement of the same contract is
   the **1.09× MARGINAL** figure in the `capture ‖ encode` row
   (textflood 16.91 ms/frame against an 18.48 ms pipeline period,
   capture `i75_x014_rewrite_20260801`). A section headed PASSING whose
   own document elsewhere records a 1.09× margin at the current
   geometry is a contradiction a reader will resolve the wrong way.
3. **PASSING/FAILING is now two verdicts, not one** — see the split
   above. Whatever the header says next has to name which of the two it
   is talking about.

**Recommended replacement, for owner decision:** status becomes
**"PASSING at 3840×2160 on the T4 (2026-07-31, box since
decommissioned); MARGINAL at 3840×2400 on the fleet (1.09×,
`i75_x014_rewrite_20260801`) — overlap claims at 4K are gated OFF until
BACKLOG #83's faster producer lands."** Not applied.

---

## Provenance of every claim in this file

| claim | source |
|---|---|
| netem may not choose defaults; closed form is guidance | `BACKLOG.md` #80 "RESCOPED 2026-08-06"; #98 "Interaction with #80" |
| C = 2 matches legacy `frames_in_flight` | `PRD.md` FR-FLOW-1, "Shipped default C = 2 is a PLACEHOLDER"; `xrdp/xrdp_encoder.c` GFX branch |
| at 4K the WAN constraint is TCP bytes, not credits | `PRD.md` FR-FLOW-1 "What C costs, measured 2026-08-03"; `BACKLOG.md` #98 |
| split-the-verdict ruling | owner decision, 2026-08-06, this turn |
| T4 decommissioned 2026-07-31 | `BACKLOG.md`, "Deployed state (2026-08-06)" |
| 1.09× at 3840×2400 | `PRD.md` `capture ‖ encode` row; `BACKLOG.md` #83; capture `i75_x014_rewrite_20260801` |
| FR-BENCH-1 status figures (27.66 fps vs 8.21 sends/s, T4, 3840×2160) | `PRD.md` FR-BENCH-1 status paragraph, unchanged |
