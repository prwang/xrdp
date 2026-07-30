# Where the T4's saturated Xorg core actually goes (2026-07-30)

The E5-2 result said the ceiling is a session Xorg pinned at ~92 % of one
core. This is the profile that names the functions, and the answer to
"is the capture what dominates the interval?" — **no, and not by a lot.**

Method: `PR-demo/t4_profile/xorg_perf_probe.sh` (perf 499 Hz, dwarf call
graphs, 45 s) against the session Xorg during a `codeflood` capture run at
2560×1440 + 3840×2400. The xorgxrdp deb is built unstripped with
debug_info; `xserver-xorg-core-dbgsym` was installed from Ubuntu ddebs so
the X-server frames resolve too (without it the six frames above
`rdpCopyArea` are bare addresses, and the first read of this profile
mis-attributed them — see "What this corrected" below).

Raw: `xorg_perf_profile.txt`, `xorg_capture_uprobe.txt`.

## The whole thread, by call path

Xorg is ~92 % of one core; the per-monitor period is ~92 ms, so ~85 ms of
CPU is spent per period. Percentages are of Xorg's own cycles.

| path | % | ≈ms / 92 ms period | whose cost |
|---|---|---|---|
| `ProcRenderComposite → damageComposite → rdpComposite → fbComposite → pixman_image_composite32` | **19.3 %** | ~16 ms | payload: xterm glyph rendering |
| `WaitForSomething → DoTimer → present_fake_do_timer → present_execute_copy → present_copy_region → damageCopyArea → rdpCopyArea → fbCopyNtoN → pixman_blt` | **18.8 %** | ~16 ms | desktop: X **Present** extension in software-emulation mode |
| `ProcCopyArea → damageCopyArea → rdpCopyArea → … → pixman_blt` | **16.5 %** | ~14 ms | payload: xterm scroll |
| **`WaitForSomething → DoTimer → rdpDeferredUpdateCallback → rdpCapRect → rdpCaptureGfxA2 → a8r8g8b8_to_avc444_box → avc444_decode_row`** | **13.8 %** | **~12 ms** | **xorgxrdp: the entire capture** |
| `ProcRenderFillRectangles → rdpCompositeRects → miCompositeRects → damagePolyFillRect → fbFill` | 9.1 % | ~8 ms | payload: fills |
| dispatch, `dixLookupResourceByType`, malloc, kernel | ~22 % | ~19 ms | X server overhead |

By library: **libpixman 71.6 %**, libxorgxrdp 13.5 %, Xorg 8 %, rest 7 %.

## Answers

**Which function is slow "despite the vectorized capture"?** None of ours.
The two AVX2 kernels are `avc444_decode_row.avx2` at **9.85 %** self and
`a8r8g8b8_to_avc444_box.avx2` at **3.59 %** self — 13.4 % together, and
that *is* the whole xorgxrdp capture (13.8 % including its callers). It is
already the fourth-largest path, behind two payload-drawing paths and the
Present copy.

It is also exactly the right size. `tools/avc444_pack_bench.c` on this CPU
predicts 9.03 ms/frame at 3840×2400 plus ~3.6 ms at 2560×1440 = **12.6 ms**
per period; the profile measures **~12 ms**. Bench and profile agree to
5 %, so there is no hidden cost in the capture and nothing to find by
optimising the kernels further — they are doing the arithmetic the design
asks for, at the speed the bench says.

**Does capture alone dominate the interval?** No. Capture is ~13.8 % of
the bottleneck thread, ~12 ms of a 92 ms period. The payload's own X
rendering (glyphs + scroll + fills) is **44.9 %**, and the Present
emulation is another **18.8 %**. Two independent checks agree:

* with **no client connected at all**, the flood alone holds the session
  Xorg at **99.9 %** of a core (`xorg_cpu_probe.sh`) — the benchmark
  saturates the X server before xrdp captures anything;
* `rdpCopyBoxList` — the hw→sw staging copy xorgxrdp does for GLAMOR /
  NVIDIA screens — fires **0 times** in 40 s (`xorg_capture_uprobe.txt`)
  while `rdpCapture` fires 825 and `a8r8g8b8_to_avc444_box` 841. There is
  no redundant copy in the capture on this box; the conversion reads the
  screen pixmap directly.

**Can we push it, and how?** The levers, in measured order, and only the
first two are ours:

1. **`present_fake` — 18.8 %, bigger than the whole capture.** The session
   has DRI3 enabled (`Option "DRI3" "1"` in the xrdp `xorg.conf`) and the
   Present extension with no hardware flip path, so Xorg emulates
   presentation on a timer with a full-region `pixman_blt`. This is
   desktop-compositing overhead in a session that has no local display to
   present to. Removing it is a *configuration* change in something xrdp
   ships, so it is squarely in scope — but see the failed attempt below:
   turning the xfwm4 compositor off via xfconf did not measurably help and
   could not be made to stick, so the lever is **identified, not proven**
   (BACKLOG #59).
2. **Move the conversion off the X server thread.** The 12 ms of AVX2 pack
   is not slow, but it is on the *single* thread that everything else in
   the session is queued behind. Handing xrdp a raw XRGB snapshot and
   packing in the encoder-side worker would move 12 ms off the critical
   path without making the arithmetic any faster. This is the capture-side
   half of #54.
3. Nothing else in xorgxrdp is worth touching: there is no staging copy,
   and the conversion matches its bench.

## What this corrected

A first reading of this profile — taken before `xserver-xorg-core-dbgsym`
was installed, when the frames between `WaitForSomething` and
`rdpCopyArea` were bare addresses — attributed the 18.8 % `pixman_blt` to
xorgxrdp's own `rdpCopyBoxList` staging copy, and concluded that a
redundant memcpy was eating a fifth of the bottleneck thread. The uprobe
count (0 calls) falsified that before it went anywhere. It was
`present_fake` all along. Recorded here because the wrong answer was
plausible, self-consistent, and would have sent an optimisation at code
that never runs.
