# i80_freeze_20260806_180613_s20 — the frozen client stops the producer, exactly at the bound

BACKLOG #80, the freeze leg (owner-approved 2026-08-06). One 20 s gate
run in which the test client was deliberately stopped halfway through.

Every number below was re-derived from the archived ring in `perf/` for
this README; where it differs from the figure first reported to the
owner, the difference is called out in the last section.

## The arm, stated before any number

| what | value |
|---|---|
| arm | `x018`, host loopback port 40034, pod `xrdp-x018-7f9bd48555-pnk8k` |
| image | `localhost/xrdp-bisect:1d5bc0960db8.xx10fa3aa-tf` |
| server build | xrdp-dev `0.10.80+git20260803024109.1d5bc0960db8` — the commit that added the credit frontier |
| producer build | xorgxrdp-dev `1:0.10.80+git20260731212221.10fa3aa23033` |
| ack mechanism | `gfx.toml [avc444_ffmpeg] eager_slot_ack = true`, `wire_window = 1` (`gfx.toml:63,69`) |
| geometry | one monitor, 3840x2400 (9.22 Mpx) |
| payload | `textflood`, the standard throughput payload |
| client | the oracle client on this host — it saves the received bytes and acknowledges each frame *before* decoding it, so it measures the server's ceiling and not a decoder |
| leg | 20 s; the client's whole process group was sent `SIGSTOP` 10 s in and resumed only at teardown (`E_FREEZE_AT=10`) |
| certificate | `x018`, 7/7 wire checks clean, 0 black frames, certified 2026-08-03T12:55:40Z against this exact image + `gfx.toml` hash |

`wire_window` is the credit window the design calls **C**: the highest
frame id xrdp may tell the producer about is the last id the client
acknowledged, plus C (`xrdp/xrdp_encoder.h:114-130`). The producer holds
two capture slots per monitor, so the bound that follows is that a frame
handed to the transport is at most **C + 2** ids ahead of the client's
last acknowledgement (`xrdp/xrdp_encoder.h:95-106`). Here C = 1, so the
bound is 3.

## What the run shows, in one paragraph

Stopping the client stopped the server's production dead, and it stopped
it at exactly the frame the arithmetic predicts — not one frame early,
not one frame late. The last frame the client acknowledged before the
freeze was **362**. Exactly one further frame, **365**, was produced,
encoded and handed to xrdp's transport, and then nothing else was ever
captured. 365 − 362 = 3 = C + 2. The bound is therefore not merely
respected here, it is **attained**: one more frame would have violated
it, and the producer was not given the credit that would have let it
take one.

## The instants, on the wall clock

The freeze instant is recorded in `freeze_instant.txt` on both clocks —
monotonic 2074203745.021 ms and epoch 1786039597117.773 ms, i.e.
2026-08-06T18:06:37.117773Z. The server runs as a local pod on this same
host, so the ring's `real_ns` timestamps and that epoch are the same
clock and no fitting is involved.

| wall clock (UTC) | vs freeze | what happened |
|---|---|---|
| 18:06:30.273 | −6.844 s | first frame of the leg reaches the transport |
| 18:06:37.105 | −0.013 s | last client acknowledgement: **frame 362** |
| 18:06:37.105 | −0.013 s | last credit emitted to the producer: region ack **363** = client 362 + C 1 |
| 18:06:37.113 | −0.005 s | last capture arrives from the producer: **frame 365** |
| 18:06:37.117 | −0.0003 s | frame **364**'s last byte handed to the transport; 1109 KiB still queued |
| **18:06:37.118** | **0** | **`SIGSTOP` to the client's process group** |
| 18:06:37.131 | +0.013 s | encoder worker releases frame 365's slot and blocks with nothing to encode |
| 18:06:37.148 | +0.030 s | frame **365**'s last byte handed to the transport; 4703 KiB still queued |
| 18:06:47.158 | +10.040 s | worker's idle wait ends at teardown — the last record in the ring |

## The three claims, each with the count behind it

**1. Production stopped, and the instrument did not.** In the 10.04 s
after the freeze the ring holds 26 records, all of them the tail of
frame 365's cycle, finishing at +0.030 s. There are then no records at
all until +10.0401 s, where the encoder worker's idle bracket closes at
teardown. That bracket — `wait_beg`/`wait_end`, which the source
describes as "time the encoder had nothing to encode"
(`xrdp/xrdp_encoder.c:3765-3790`) — measures **10026.663 ms**. So the
silence between +0.030 s and +10.040 s is the absence of work, not a
dead ring: the same ring wrote a record at the far end of it.

**2. No credit was emitted after the freeze.** The producer is admitted
to capture only by an ack from xrdp. Two record types carry one:
`ackregion` (the ordinary region-disposing ack) and `ackslot` (the
slot-only credit). The leg contains 363 and 222 of them respectively,
**every one of them carrying C = 1** in the field that reports
`wire_window`, and **zero of either after the freeze instant**. The
client's own acknowledgements (`cliack`) likewise stop at 362 — 363 of
them in the leg, none after the freeze.

That "C = 1 on every credit record" is also the mechanism check for this
arm: it is `encoder->wire_window` read at the emission site, so it proves
`wire_window = 1` reached the encoder. The separate `frames_in_flight`
field on the 1460 network writes reads 1 as well, but it is **not** what
sets the window here — with `eager_slot_ack = true` the ack dispatch goes
to the credit frontier and never consults it (`xrdp/xrdp_mm.c:1799-1822`).

**3. The transport queue plateaus because there is nothing left to
append.** The `egress` record is written when a frame's last byte has
been handed to xrdp's transport, and its third field is
`trans->wait_bytes / 1024` — how many kilobytes xrdp's own output buffer
still holds that the kernel socket has not taken
(`xrdp/xrdp_mm.c:1643-1651`, `4379`). Across the 364 frames before the
freeze that number is **0 KiB on 362 of them**; the only exceptions are
the leg's very first frame (48 KiB) and frame 364 (1109 KiB), whose last
byte was handed over 0.3 ms *before* the recorded freeze instant — close
enough to it that the ordering is inside the precision with which the
freeze instant itself is recorded. Frame 365 then reads **4703 KiB**, and
that is the last egress record of the run. The queue stops growing not
because a limit was enforced on it but because no further frame is
produced and therefore no further bytes exist to append.

## What the wire distance actually did

The bound is written on the frame's own id, so the quantity to read is
the `egress` record's first field minus its fourth: the frame's id, minus
the last id the client had acknowledged, at the instant the frame's last
byte reached the transport. Over the leg's 365 frames:

| distance (frames) | frames | meaning |
|---|---|---|
| 1 | 222 | the client was fully caught up bar the frame just handed over |
| 2 | 142 | one earlier frame also not yet acknowledged |
| **3** | **1** | two earlier frames also outstanding — frame 365, after the freeze: **the bound C + 2, attained** |

(222 + 142 + 1 = 365, the leg's whole frame count.)

This quantity can never be 0: a client cannot have acknowledged a frame
that has not yet been handed to the transport.

## What SIGSTOP does and does not model

`SIGSTOP` freezes the client process entirely. It therefore stops the
client **reading its socket** as well as acknowledging frames, so the
receive buffer fills and TCP advertises a zero window back at the server.
That is a **harsher** condition for the server's queue than an
acknowledgement-only freeze would be, and from the server side the two
cannot be told apart — nothing in this capture can separate "the client
stopped acking" from "the client stopped reading". Read the result
accordingly: it demonstrates the credit frontier halts production under a
client that has gone away completely. It does not, on its own,
demonstrate the behaviour under a client that keeps draining bytes but
falls behind on acknowledgements.

The gate's `VERDICT.txt` prints its usual rate block for this leg
(sends 365 over 6.9 s, mean 18.9 ms, "E5 GATE ... 1.20x: RED"). Those
numbers cover a leg that was deliberately stalled and are **not**
comparable with a normal run; the VERDICT says so itself. The producer
margin the gate reports is 1.23x (pipeline mean 18.9 ms against the
payload's own 15.4 ms frame interval), which under the owner's
2026-08-06 ruling voids any claim from this run about two pipeline
stages overlapping.

## What this adds over CI

The frame half of this behaviour is already asserted exhaustively in CI,
in `tests/xrdp/test_avc444_credit_frontier.c`, case
`test_credit_frontier_frozen_client_halts_at_client_plus_c`. For every
C in 1..3 that test freezes the client at frame F and drives capture, the
encoder children and the transport as fast as they will go, then asserts
that the credit halts at F + C, the region ack at F + C, capture and the
server frontier at F + C + 2, and that the planner then emits nothing at
all no matter how far the downstream stages are pushed.

The live leg reproduces every one of those with F = 362, C = 1: credit
stopped at 363, capture stopped at 365, the server frontier stopped at
365. So what this run adds is not the arithmetic — it is that the **real**
producer, the real encoder children and the real transport behave as the
model says, and one thing CI cannot express at all: that xrdp's transport
queue **plateaus** rather than growing, because the halt happens upstream
of it.

## Reproducing the numbers

The ring is `perf/enc.727` — the pod also carries `enc.31` and `enc.267`
from earlier runs, whose `# perfbase real_ns` headers are both about
3.2 days older; pooling them would double-count. Records are
`<monotonic_ns> <tid> <tag> <six ints>`, placed on the wall clock as
`real_ns = R + (ts − M)` from the header's `M`/`R`. Field meanings are at
the `PERF_TRACE6` call sites: `egress` `xrdp/xrdp_mm.c:4379`, `ackslot`
and `ackregion` `xrdp/xrdp_mm.c:1729,1745`, `cliack` `:1929`, `send`
`:4308`.

## Where this README differs from the first reading of the run

* **"exactly one further frame reached the network" — corrected to "reached
  the transport".** Frame 365's last byte was handed to xrdp's transport
  at +0.030 s, but 4703 KiB of it were still sitting in xrdp's own output
  buffer at that moment, with the client's TCP window at zero. Those
  bytes did not reach the network.
* **The 1109 KiB reading is not cleanly pre-freeze.** It belongs to frame
  364, whose egress is stamped 0.3 ms before the recorded freeze instant.
  The reading is genuine; calling it "the last pre-freeze frame" overstates
  how firmly the two are ordered. What is unambiguous is the contrast with
  the 362 frames before it, which read 0 KiB.

Everything else re-derived to the values first reported: last client
acknowledgement 362, one further frame 365, difference 3 = C + 2, zero
credit records in the following 10 s, last ring record at +10.040 s.
