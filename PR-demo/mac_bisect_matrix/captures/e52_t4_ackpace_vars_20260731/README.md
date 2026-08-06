# #64c probe result: one capture slot is PERMANENTLY DEAD — the ack undercounts by one

Two uprobe runs on the live T4 session Xorg (m=1 4K textflood, oracle
client, same 8.3-8.4 sends/s regime), `xorg_ackpace_uprobe.sh`:

## Run 1 — event rates (45 s, ../e52_t4_ackpace_probe_20260731)

```
rdpScheduleDeferredUpdate.part.0   4369   97/s   timers genuinely armed
rdpDeferredUpdateCallback          1500   33/s   TIMER FIRES CONSTANTLY
rdpCapRect                          397    8.8/s
rdpCapture                          383    8.5/s  == the send rate
```

H1 (timer never fires) is FALSIFIED: the callback runs at ~the producer
blit rate. 73.5 % of entries are refused before rdpCapRect — at the
capacity gate. H2 confirmed, with the twist below.

## Run 2 — rect_id / rect_id_ack AT CALLBACK ENTRY (40 s, this dir)

DWARF cannot deref the callback's `pointer arg`, so the two counters are
read by raw offset (`+0x12a80/+0x12a84(%dx)`, offsets from the same
commit's local build; at m=1 outstanding == rect_id - rect_id_ack):

```
outstanding 1:   340 callbacks (25.3 %)   -> ALL 340 captured (== rdpCapRect count)
outstanding 2:  1004 callbacks (74.7 %)   -> all refused
outstanding 0:     0 callbacks  NEVER
```

The numbers close exactly: every callback at depth 1 captures, every
callback at depth 2 is refused, and the ring NEVER empties.

## What this means

The two-slot design is RUNNING: both slots are occupied at every refused
callback. But one of the two outstanding rects is a GHOST — the ack
value xorgxrdp receives permanently trails its own rect_id by one frame
beyond the true in-flight count (rack = rid-1 at every capture moment,
rid-2 during encodes; never rid). One slot is pinned by a frame xrdp has
long since disposed of, the budget correctly refuses at "depth 2", and
capture||encode is structurally impossible for the REST OF THE SESSION.

Cumulative-ack arithmetic makes a single lost ack permanent: xrdp acks
with its own count of frames it ENCODED AND SENT (`frame_id_server`).
Any incoming paint msg that is consumed without producing a sent frame
— encoder warmup returning PENDING, an error path, a coalesce during
login churn — desynchronizes the two counters by one, forever. Both
ack call sites (`xrdp_mm.c:1713`, `:4105`) forward correctly in steady
state; the leak is the paint-msg-with-no-enc_done path.

This also explains the fif=4 probe changing nothing (the -1 is in the
ack VALUE, not the window) and does NOT contradict the PRD's 1600x912
"capture fully hidden" measurement (pre-step-6 code acked per-item on a
different path).

## Not yet proven

WHICH early event eats the +1 (genesis). Candidates, in likelihood
order: first-frames encoder warmup (rv=PENDING consumes the capture,
sends nothing), surface churn during login, an early non-avc frame.
Decisive: fresh session, count paint msgs vs enc_done frames from t=0.

Fix direction (#64c): every consumed rect MUST be acked, including
rects that produce no output frame — ack the INCOMING rect_id on the
PENDING/error/no-output paths (or carry rect_id through enc_done and
ack that, not xrdp's own counter). Plus a CI ratchet: drive_pipeline
with a lossy-encode arm asserting the budget still drains.
