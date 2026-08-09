# i103_pipe_handover_20260808 — the raw-frame handover, reproduced outside xrdp

Asked 2026-08-08: the fleet measured xrdp's raw-picture handover to its
ffmpeg child at 7.5–9.1 ms for 13.82 MB — about 1.5–1.8 GB/s, slow for
what should be one copy. Is the IPC itself the bottleneck rather than the
encoder backpressuring it, and can that be reproduced independently of
xrdp and xorgxrdp?

Reproduction: `tools/vmsplice_pipe_bench.c` — a parent that `vmsplice`s a
buffer into a pipe exactly as `feed_vmsplice()` does, and a forked child
that reads it and discards it. No X server, no encoder, no GPU, no
session.

## Verdict, first

**The pipe is not inherently the bottleneck — but on this box it is, and
the cause is the container's uid mapping, not xrdp's code.**

Same pod, same kernel, same binary, same 13.82 MB picture, differing only
in which uid the process runs as:

| runs as | maps to host uid | pipe granted | handover | vs memcpy |
|---|---|---|---|---|
| container root — **what xrdp runs as** | **1000** | **8 KiB**, resize REFUSED | **5.84 ms** | 20× |
| `tester`, container uid 1000 | 1001000 | 1 MiB, resize granted | **0.68 ms** | 2.3× |

memcpy of the same bytes in the same process: **0.29–0.33 ms**
(42–48 GB/s). So a pipe handover with a normal pipe costs about 2.3× a
straight copy, which is a reasonable price and **not** a bottleneck at
0.68 ms of a 24.5 ms frame. With the pipe clamped to 8 KiB it costs 20×
and becomes 24 % of the frame.

## Why the pipe is 8 KiB, and why it is not xrdp's fault

`xrdp_encoder_ffmpeg.c` asks for 1 MiB:

```c
fcntl(inpipe[1], F_SETPIPE_SZ, 1024 * 1024);
```

In this container that call returns **EPERM** and the pipe stays at
**8192 bytes** — so a 13.82 MB picture crosses in **1688 round trips**
instead of 14. The return value is not checked, so the failure is
invisible.

The mechanism, from the kernel's side:

* This is an **Incus/LXC unprivileged container with an id map**.
  `/proc/self/uid_map` reads `0 1000 1` — **container root is host uid
  1000**, an ordinary host user account, and the pods inherit the same
  map.
* `fs/pipe-user-pages-soft` is **64 MiB of pipe buffers per host user,
  host-wide**. Everything that host uid 1000 runs counts against it —
  this container's xrdp, Xorg and ffmpeg children, and whatever else that
  account runs outside it, which is not visible from in here.
* Once a user is over that soft limit, new pipes are clamped to the
  kernel minimum (2 pages = 8192) and `F_SETPIPE_SZ` above it returns
  EPERM — **unless the caller is `capable(CAP_SYS_RESOURCE)`, which is
  checked against the INITIAL user namespace.** Container root has a full
  capability set inside its own namespace (`CapEff 000001ffffffffff`) and
  is still not init-namespace capable, so the kernel treats it as an
  unprivileged user.

Two controls confirm it is the per-user accounting and not the
capability:

* `tester` inside the **same** pod, with the same (absent) init-namespace
  capability, gets its 1 MiB — because host uid 1001000 holds almost no
  pipes.
* As container root, every request above 8192 is refused, including
  16 KiB (`root_pipe_ceiling.txt`). "Ask for less" is not a workaround;
  the account is simply out of pipe pages.

**A bare-metal xrdp running as real root is exempt from the rule
entirely** — `capable(CAP_SYS_RESOURCE)` is true there, the soft limit
does not apply, and the 1 MiB request succeeds. So this is a property of
this measurement environment, not of a normal deployment.

## What that means for numbers already recorded

**Every fleet measurement in this tree was taken inside this container,
as container root, and therefore with 8 KiB pipes.** The 7.5–9.1 ms
"feed" segment reported for #92 is consistent with the clamped condition
(5.8–7.1 ms for the mechanism alone) and would be roughly 0.7 ms on a
host where the resize succeeds.

* **A/B comparisons are unaffected.** Both legs of every pair shared the
  handicap, so ratios stand.
* **Absolute numbers carry it.** The 24.5 ms frame period at 3840×2400
  contains ~5 ms that a normally-privileged deployment would not pay.
  Projected, not measured: a working 1 MiB pipe would put the period near
  19 ms. Proving that in situ needs `fs/pipe-user-pages-soft` raised on
  the host, which is the owner's call — the sysctl is read-only from
  inside and changing it is host-wide.

  **Superseding note, 2026-08-08 (added the same day; the projection
  above is kept as written).** The owner raised the host sysctl to
  262144 pages (1 GiB) and the same four legs were re-run on the same arm
  with no code change. The projection of "near 19 ms" was pessimistic:
  the frame period measured **17.0 ms**, from 24.5 — 41 fps to 59. The
  feed segment went 8.5–9.1 → 1.9–2.1 ms and the drain 1.0–1.2 → 0.38 ms,
  while the encode stayed 13.80–14.36 ms across all eight legs, which is
  the control showing the sysctl touched only the two segments that cross
  a pipe. Evidence:
  `captures/i92_sparse_aux_ab_20260808_233519_s20/`. One leg (a2) is
  quarantined there as an outlier, not averaged.

## The one code change this justifies — LANDED 2026-08-09

Not a redesign: **check the return value and say something.** A silent
5 ms/frame at 4K is the kind of thing that should never be invisible, and
the check is two lines.

**Owner ruling, 2026-08-09, which decided the shape of it:** *"I don't
think xrdp is the place to try modify system settings. It should warn
loud in the logs anyway with external ffmpeg enabled but found a tiny
pipe, add also to our test procedure to watch for that log, request
owner's action (and results invalid) when the pipe is tiny."*

So xrdp changes nothing and reports. `spawn_child()` now reads the
granted size back with `F_GETPIPE_SZ` and, when it is below the 1 MiB
requested, logs `PIPE_TOO_SMALL` at WARNING with the requested size, the
granted size, the NV12 picture size, and the number of writes each
picture now costs against the number it should — for the 8192 measured
here, "1688 writes instead of 14". The harness watches for that token:
`arm_certify.sh` fails certification, `e_gate_run.sh` refuses the run
(before it, on a warm pod) or stamps the VERDICT invalid and exits
non-zero (after it, on a cold one). PRD FR-PROC-6 clause 4 and
FR-BENCH-2. Whether to go further — the frame is already in
shared memory and is being copied back out of a pipe purely because
ffmpeg's raw input is a stream — is an architectural question, and at
0.68 ms with a working pipe the case for it is much weaker than these
numbers first suggested.

## Files

| file | what it is |
|---|---|
| `as_root.txt` | the bench on this box as container root (8 KiB, refused) |
| `as_unprivileged.txt` | the same bench as `nobody`, which maps to an idle host uid (1 MiB, granted) |
| `in_pod_as_root.txt` | the bench **inside arm x030**, as xrdp runs |
| `in_pod_as_tester.txt` | the bench inside the same pod as `tester` — the control |
| `root_pipe_ceiling.txt` | every pipe size container root can obtain: 8192 and no more |
| `in_pod_x030.txt` | the minimal `F_SETPIPE_SZ` probe in the pod |
| `uid_map.txt` | the id mapping, capability set and the soft limit |
