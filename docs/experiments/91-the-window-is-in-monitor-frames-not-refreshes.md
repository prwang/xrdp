# The window counts monitor-frames, not refreshes — why it divides by monitor count, and why two knobs disagree

**Static analysis, 2026-08-07, owner-directed.** No experiment was run
for this; every claim below is a code reference or an already-committed
measurement. Where two models of mine disagreed I say so and do not pick
a winner.

## The one-sentence answer

A "frame id" is **one monitor's frame, not one screen refresh**. The
window is denominated in frame ids and is a single session-wide number,
so at M monitors a refresh consumes M ids and each monitor gets between
⌊C/M⌋ and ⌈C/M⌉ ids of headroom. The capture slots underneath it are
per monitor and do **not** divide. That is the whole asymmetry.

## The chain, verified

**The id is a scalar, and it advances once per monitor.**
`int rect_id;` is a plain field on the client connection
(`/workUpdateXorgXrdp/module/rdpClientCon.h:153`), incremented once per
*send* at `rdpClientCon.c:3501`, `:3548` and `:3621` — and a send is one
monitor's capture, issued from the per-monitor loop in
`rdpDeferredUpdateCallback`. The driver says so in its own words at
`rdpClientCon.h:120-121`:

> rect_id advances by the monitor count between one monitor's
> consecutive sends, which pinned each monitor to a single slot

That comment exists because deriving the capture slot from `rect_id`
parity failed at even monitor counts — the same fact, discovered
earlier, for a different reason.

**That id is echoed unchanged all the way to the client and back.** xrdp
reads it out of the producer's blob, re-emits the same value in the EGFX
frame markers, assigns it verbatim to `frame_id_server`, and the
client's frame acknowledgement lands on the same axis in
`frame_id_client`. So all three terms of the credit frontier —
`frame_id_consumed`, `frame_id_server`, `frame_id_client` — are counted
in monitor-frames.

**The credit that goes back carries no monitor index.** The ack is
message 106 and its entire body is two words
(`/work/xup/xup.c:1255-1257`):

```
out_uint16_le(s, 106);
out_uint32_le(s, flags);
out_uint32_le(s, frame_id);
```

The `flags` word holds two bits, neither of them a monitor. The paint
direction, by contrast, **does** carry a monitor index — in the top
nibble of its own flags word (`rdpClientCon.c:3688`,
`mon = (id->flags >> 28) & 0xF`). So the producer tells the consumer
which screen a frame belongs to, and the consumer's answer cannot say
which screen it is about.

**The producer retires per-monitor rings against that one global
number.** `xup_cap_budget_has_capacity(budget, mon, rect_id_ack, cap)`
(`/work/common/xup_client_info.h:482-491`) indexes a per-monitor ring
`ids[mon][...]` but takes the single session-global `rect_id_ack` as the
frontier every monitor is retired against.

## The arithmetic, and how it matches what was measured

Between the client's acknowledged position and the credit ceiling there
are **C ids**, dealt round-robin across M monitors.

| | ids of headroom above the client's ack | belonging to one monitor | in whole refreshes |
|---|---|---|---|
| M = 1, C = 2 | 2 | 2 | 2.0 |
| M = 2, C = 2 | 2 | 1 | 1.0 |
| M = 3, C = 2 | 2 | 0 or 1 | 0.67 |

So **C = 2 at two monitors gives each screen the same headroom as
C = 1 at one monitor.** That is a configuration we have measured
directly, and the stall rates line up:

| configuration | headroom per monitor | cycles stalled > 10 ms |
|---|---|---|
| C = 1, M = 1 | 1 | 16.3 % |
| C = 2, M = 2 | 1 | 21.5 / 22.0 % |
| C = 2, M = 1 | 2 | 0.8 % |

The two one-headroom configurations sit in the same regime and the
two-headroom one does not. This is consistency between an arithmetic
prediction and two independently measured runs; it is **not** proof that
the window is the binding term (see the open question below).

## Why the two knobs disagree — one was decided, the other inherited

**The capture budget is per monitor because a global pool was measured
failing.** Decision D13, `docs/experiments/45-intra-refresh-and-pump-set.md:344`:

> Capture budget is ≤ 2 outstanding PER MONITOR — never a global pool of
> 2m. A pool lets one damaged monitor take all of it: 4-deep on 2 slots
> at m = 2, i.e. bufferbloat, +2 frames of latency, and slot aliasing.

The measurement behind it (2026-07-29, two monitors, 1100 sends): the
same capture slot was reused on **1079 of 1079** consecutive full-pass
sends, one monitor's second slot was **never written** for the whole
run, and 543 of 1100 sends went out with the global budget already at
its cap. The prohibition is now restated in the header both daemons
compile, and the slot count is fixed at 2 in the versioned contract —
explicitly *not* a tuning knob.

**The window is session-wide because it always was.** PRD's own
provenance trace dates it to a 2017 commit whose parameter was the value
the *client* advertises in the Frame Acknowledge capability — a single
per-connection integer with no monitor dimension, from a protocol
capability that has none. The GFX path then **cut that tether**: the
client's advertised value is no longer read, and the number became a
local constant. Nothing re-derived what it should bound when that
happened, and monitor count was never part of the discussion.

`wire_window` inherited that scope unexamined. It is a scalar in every
struct it appears in, one TOML integer, one value per session.

**So one mechanism is per monitor on measured grounds and the other is
global by inheritance from a number this project has documented as
having no protocol meaning.** That is the inconsistency, stated exactly.

**It was known before `wire_window` existed.** BACKLOG #91 has recorded
it since before #80 was filed, and there is a CI test pinning the
present behaviour with the fixed behaviour written beside it —
`test_overlap_m2_global_window_pins_each_monitor_to_one`, which asserts
depth 1 per monitor today and carries `drive_pipeline(2, 2, 2*2, 2, 32)
>= 2` as the assertion a window scaled by M would satisfy.

## What is NOT settled, and it decides the fix

Two models built for this analysis **disagree about which term binds at
M ≥ 2**. One has `frame_id_client + C` binding on 99 % of ticks at C = 2
for every monitor count, with C = 2·M moving it off the network. The
other has `frame_id_server + 1` binding first at M ≥ 2 and concludes
that raising C changes nothing — "C buys inventory, never depth".

Both are models. Neither is a measurement, and the fleet capture does
not report term attribution, because at the instant a credit is emitted
all three terms are equal — the same tie that defeated attribution at
one monitor.

**The experiment that settles it is one leg: `wire_window = 4` at two
monitors.** If the window is the binding term, the stall should fall
toward the one-monitor figure; if it is `server + 1`, it will not move.
That leg is also numerically identical to "scale the default by monitor
count" at M = 2, so it prices the fix and tests the diagnosis at once.
One arm, configuration only, about ten minutes.

## Documentation defects found while reading

These are admin-facing and independent of which fix is chosen.

1. **The man page never says the window is session-wide.** Its one
   sentence about monitors — "at most `wire_window` + 2 frames per
   monitor" — parses two ways, and the reading a careful admin is more
   likely to take (`(C+2)` per monitor) over-provisions by a third at
   two monitors. The true bound, asserted in code and measured at 6, is
   `C + 2·M`.
2. **The frame-rate guidance silently assumes one monitor.** "at 80 ms
   RTT the default gives about 50 frames per second" is 4/0.080 — that
   is `C + 2` with no M. At two monitors the same formula from the code
   gives 6 ids, i.e. 75. The page's two paragraphs disagree with each
   other at M ≥ 2.
3. **Adding a monitor halves the per-screen window with no config
   change, no log line and nothing to see.** Nothing warns about it.
4. The sample `gfx.toml` that ships contains neither key, so the man
   page is the entire contract.

## The options, with what each costs

| | wire change? | changes a default? | what it needs |
|---|---|---|---|
| **Per-monitor window** — each screen gets its own C | **Yes.** A scalar cumulative ack cannot express "screen A may go ahead, screen B may not"; needs a monitor index in message 106, so a contract version bump and a lockstep upgrade of both daemons | The value 2 stays but its meaning changes: bound `C + 2·M` → `(C+2)·M`, 6 → 8 at two monitors | The window-of-4 leg first; deliberate retirement of the CI test that pins today's behaviour |
| **Scale the default by M** — credit term becomes `client + C·M` | **No.** Server-side arithmetic only; xrdp already has the monitor count where the frontier is computed | Byte-identical at one monitor; a real change at M ≥ 2 (bound 6 → 8), and it re-means any value an admin already set | The same leg — at M = 2 that leg *is* this option. Plus a view on the bytes: each unit of C is ~3.3 MB of transport queue at 4K, and this multiplies that by M |
| **Document the scope, change nothing** | No | No | Nothing beyond what is measured. Cost: the ~22 % two-monitor stall stays, and every stall claim upstream stays one-monitor-only |
| **Unify into one admin concept** | No, if C is derived from a per-monitor number; the slot count itself is contract-fixed and not exposable | Yes — re-means a key that shipped 2026-08-07. The rename window is open now and closes at release | Same leg, plus a PRD clause rewrite: FR-FLOW-1 currently *requires* that C have "exactly one documented meaning", and the PRD itself states two formulas eight lines apart |

## One more knob a reviewer will notice

`XRDP_GFX_FRAMES_IN_FLIGHT` is still live on the legacy path — an
environment variable, range 1–16, undocumented in any man page. We have
the measurement showing that raising it pays the full queue cost and
returns nothing. A maintainer asking why two window controls exist can
be answered today without new evidence; that answer should be written
down before it is asked.
