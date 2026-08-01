# #61f Step 1, first attempt: the egress cork was the wrong lever

2026-08-01. Reverted the same day (commit `b588a954`, reverting
`1981a3d9`, `fe9afccb`, `41e829e6`, `a6033bff`). Kept verbatim, wrong
claims included, per the record-keeping rule.

## What the item asked for, and what was built instead

BACKLOG #61f Step 1: *cut the main thread's 16 ms xup service latency —
the frame exists 16 ms before the thread that must enqueue it reads the
message.* The data dependency is:

```
xup fd readable -> main thread reads the rect -> fifo_add -> encoder worker
```

Nothing in it requires the client egress to be cheaper. What was built
was a batching of the drdynvc write path (`trans_cork()`/`trans_uncork()`
around `xrdp_egfx_send_data`'s ~2400-PDU loop, plus a per-send cap and a
drain loop) — a change to the **network egress**, outside the item's
scope, that does not touch the dependency at all. The owner's review
named it correctly: *"optimizing the network egress is clearly scope
creep … you didn't identify what is real data dependency but take the
wrong route of compressing network egress."*

Three arms and three container image builds went into it. The arms are
torn down and their manifests removed.

## What the three shapes measured

One monitor, 3840x2400, `SESSION_KIND=textflood`, oracle client, 60 s.
Each treatment is quoted against an x006 control run **in the same
pass**; the control's own drift across the day is discussed below.

| arm | shape | mean ms/frame | control, same pass |
|---|---|---|---|
| x008 | cork, whole frame in one `trans_send` | **51.2** | 41.8 |
| x009 | cork + 64 KB cap per *call* | **127.3** | 41.2 |
| x010 | cork + 64 KB cap per *send*, drain to EAGAIN | never run — reverted first | — |

Both mechanisms are understood, and both are properties of the code the
cork ran into rather than of the idea of batching:

* **x008.** `ssl_tls_write()` (`common/ssl_calls.c`) has no partial-write
  mode: it loops internally on `SSL_ERROR_WANT_WRITE` until the whole
  length has gone. Handing it one 3.5 MB frame parks the caller until the
  peer has drained all 3.5 MB — a longer, harder block than the ~2400
  small writes it replaced, each of which returned as soon as the socket
  took 1500 bytes.
* **x009.** With the cap applied per *call*, `trans_send_waiting()`
  returned after one 64 KB chunk, so a 3.5 MB frame advanced 64 KB per
  main-loop pass. Decomposition: time inside the send window 43 ms
  (x006: 19 ms), time **outside** it 87 ms (x006: 21 ms) — ~55 passes at
  ~1.6 ms of unrelated work each.

## What survives the revert, as measurement

These were taken while chasing the wrong lever but are about the machine,
not about the cork:

1. **The rect arrives while the main thread is mid-send**: 1515 of 1532
   frames (x006, 2026-08-01). Of the 17.5 ms mean `cap sent_us -> msgin`
   wait, 9.9 ms falls inside an egress send window and 7.6 ms does not.
2. **The main thread is on-CPU 58 % of the time it spends inside that
   window** and 17 % outside it; 15.0 s of CPU over 57.0 s, ~9.9 ms per
   frame. So the window is roughly half our own work and half waiting for
   the peer — but see the contamination note below.
3. **The oracle client's pauses are not CPU contention.** Run-delay
   (`/proc/<tid>/schedstat` field 2) summed over all client threads is
   0.326 s in 57 s, 0.5 % of its CPU time, and it is no higher inside the
   ack holes (6.0 ms/s) than outside (5.4 ms/s). The client runs at
   ~0.65 cores throughout, on a 32-core box at load ~1. This is the whole
   answer #61g Step 1 was after; what the client is doing instead remains
   unattributed.

## Two things that make the above less precise than it looks

* **The window is bracketed by the instrument.** `GFX_TRACE` and
  `ACK_TRACE` are `LOG(LOG_LEVEL_INFO, …)`, i.e. log.c's unbuffered write
  under a global mutex — ~12 lines per frame, on the main thread, on the
  per-frame hot path. The "17 ms send window" is the interval between two
  such log lines, so an unquantified part of it is the logger rather than
  the write. `common/perf_trace` (FR-TRACE-1) was added in #61e for
  exactly this and is still used only by the encoder worker. Any re-run
  of this decomposition must move the main thread's per-frame records
  onto the ring first — see BACKLOG #61h.
* **The control drifted 4–5 ms across the day.** x006 measured 36.8 ms
  in the morning, 37.6 ms with the sampler running, and 41.8 / 41.2 ms in
  the evening pair — same arm, same image, same payload. Cause not
  established (host load average went 1.0 → 1.9 over the same window).
  Every treatment above is quoted against a control from its own pass for
  this reason, and no number here may be compared with a number from
  another sitting.

## The instrument that was also reverted

`i61g_sched_sampler.py` sampled `/proc/<tid>/schedstat` at 100 Hz for the
client and the pod. It is a custom wheel built next to `common/perf_trace`,
its entire yield is finding 3 above, and sampling ~250 threads cost 36 %
of a core — enough to move the rate it was measuring (37.6 vs 36.8 ms).
It also sampled itself on the first run, because the target patterns are
its own argv and `pgrep -f` matched it, which put that 36 % into the
client's CPU total. Both faults were fixed before the run that produced
finding 3; the tool is still not kept.
