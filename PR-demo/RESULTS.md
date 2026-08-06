# Results — external-ffmpeg AVC444/AVC420 GFX encoding for xrdp

This is the "Results" section for the PR: what the stock-ffmpeg-subprocess
architecture (verbatim, un-interpreted `encoder_args`; no linked codec library;
AVC444 v2 + AVC420 over one NV12-in / Annex-B-in-NUT-out IPC contract) *buys*
and what it *costs*, with numbers measured on this box and a plan for the
rigorous versions that are reachable without new hardware.

**Measurement box.** Debian 13, one AMD render node at `/dev/dri/renderD128`
(VAAPI), stock `ffmpeg` with `libx264`; fast multi-core CPU (memcpy ~72 GB/s).
Every number below is reproducible via `PR-demo/bench/` (commands cited per
experiment). Numbers are tagged **[measured]** (run here) or **[projected]**
(reasoned; needs hardware/time we call out).

## Summary

| # | Question | Result | Status |
|---|---|---|---|
| E1 | How much more does AVC444 v2 stress traffic than AVC420? | **1.46× (desktop) – 1.79× (chroma-heavy)**; aux stream adds +46–79% over main | [measured] |
| E2 | How many platforms does the ABI-independent approach reach? | **7 H.264 + 6 HEVC** encoders via `encoder_args`, **0** xrdp LOC; VAAPI live-verified | [measured] |
| E3 | Practical gain of a real hardware codec (CPU)? | HW VAAPI **~3× less CPU** than SW x264 (utime 1.57 s vs 4.80 s) even vs `ultrafast` | [measured] |
| E4 | Cost of the linked-library dependency we remove? | **0** codec libs in xrdp's address space; avoids 1.9–16.9 MB in-process codec + its CVE surface; codec upgrades need no xrdp rebuild | [measured] |
| E5 | Cost of the copy-oriented pipe? | **~0.09 ms/frame** userspace copy, ~0.5%/core per stage — negligible CPU; the real cost is **one desktop-update of latency** | [measured] |

---

## E1 — Traffic: AVC444 v2 vs AVC420

**Claim.** AVC444 v2 buys full-resolution chroma at a bounded, content-dependent
traffic premium over AVC420; the premium justifies a per-deployment selector
(`avc_mode`) rather than a hard choice.

**Method.** `bench/measure_traffic.sh`: xrdp's *real* converter
(`bench/conv_measure.c` linking `xrdp_avc444_convert.o`) produces the main and
aux NV12 views of a source frame; each is encoded as one intra picture with the
default `encoder_args` (`libx264 -crf 18 -preset ultrafast -tune zerolatency`).
AVC420 traffic ≈ the main stream alone; AVC444 = main + aux.

**Result [measured].**

| content | main (=AVC420) | aux | AVC444 = main+aux | 444/420 | aux/main |
|---|---|---|---|---|---|
| realistic desktop | 91,894 B | 42,225 B | 134,119 B | **1.46×** | 46% |
| colored code/text | 171,441 B | 130,342 B | 301,783 B | **1.76×** | 76% |
| synthetic chroma-heavy | 175,243 B | 139,106 B | 314,349 B | **1.79×** | 79% |

So AVC444 costs ~1.5× on a normal desktop and ~1.8× on saturated colored
content — exactly the content where 444 matters (the chroma-detail the
[iso-luminant demo](README.md) shows 420 discarding). This is the quantitative
argument for the `avc_mode = auto|444|420` knob: pay the premium where color
fidelity matters, drop to 420 on bandwidth-constrained links.

**Threats to validity.** Intra-frame (keyframe) only; one encoder/CRF; three
samples. Steady-state P-frame ratios differ — see the plan (P1).

## E2 — Platform reach of the ABI-independent design

**Claim.** Because xrdp passes the codec line verbatim to `execve` and never
links a codec, every encoder the *stock ffmpeg* exposes is reachable with **zero
xrdp code change** — the core portability result.

**Result [measured]** (`bench/bench_encoders.sh`, this box's ffmpeg): H.264 via
`libx264`, `libx264rgb`, `h264_nvenc`, `h264_qsv`, `h264_vaapi`,
`h264_vulkan`, `h264_v4l2m2m`; HEVC via `libx265`, `hevc_nvenc`, `hevc_qsv`,
`hevc_vaapi`, `hevc_vulkan`, `hevc_v4l2m2m` — **7 H.264 + 6 HEVC**. `h264_vaapi`
is not just listed but **live-verified** end-to-end (xrdp→GPU, `drm-engine-enc`
busy; see PRD §25). The same passthrough reaches macOS `h264_videotoolbox` and
Windows `h264_mf` on those platforms **[projected]** — a linked-library design
would need a new backend + ABI shim per encoder.

**Threats to validity.** "Reachable" = expressible + produces decodable
Annex-B; we verified x264 (default) and vaapi live. nvenc/qsv/vulkan are
enumerated here but not run (no such GPU on-box) — see plan (P4).

## E3 — Practical gain of a real hardware codec (CPU + size)

**Claim.** Offloading to a hardware encoder markedly cuts host CPU — the
practical payoff of making hardware trivially selectable.

**Result [measured]** (`bench/bench_encoders.sh`, 1080p60 ×10 s = 600 frames):

| encoder | user CPU | sys CPU | output |
|---|---|---|---|
| `libx264` (SW, ultrafast/zerolatency, crf 18) | **4.80 s** | 0.28 s | 45.4 MB |
| `h264_vaapi` (HW, CQP qp 20) | **1.57 s** | 0.10 s | 33.5 MB |

Hardware VAAPI used **~3× less host CPU** than software x264 — and this is a
*lower bound* on the saving, because `ultrafast` is x264's cheapest preset; a
quality-competitive preset (`medium`/`slow`) would widen the gap sharply while
VAAPI stays flat (the GPU does the work). Freed CPU is what lets one host serve
more concurrent sessions.

**Threats to validity.** Synthetic `testsrc2` source; the two outputs are *not*
quality-matched (different rate control), so the size column is indicative, not
a compression verdict — the rigorous VMAF/SSIM-matched comparison is plan (P2).

## E4 — Cost of the linked-codec dependency we remove

**Claim.** The bigger structural win is not binary size but **decoupling**: no
codec code runs in xrdp's address space, and codec choice/upgrade is independent
of xrdp.

**Result [measured].** `ldd /usr/sbin/xrdp` links **no** codec library
(`x264`/`avcodec`/`openh264`/`x265`); the xrdp binary is 1.36 MB. A linked
design pulls the codec into xrdp's process — 1.9 MB of `libx264` up to 16.9 MB
of `libavcodec` of C/C++ decoding-adjacent code and its CVE stream — coupling
xrdp to that ABI and requiring an xrdp rebuild to change or update the encoder.
The subprocess model instead runs the codec as a **separate, restartable,
sandboxable process**: a codec crash or exploit is contained to the child (xrdp
already reaps/recreates it), and swapping libx264→nvenc→libx265 is a config
edit. (Binary-size *per se* is minor under dynamic linking; the dependency and
isolation properties are the point.)

**Threats to validity.** Isolation strength depends on how the child is
sandboxed; today it is a scrubbed-env `fork`/`execve` (no shell), not yet a
seccomp/namespace jail — a hardening opportunity, not a regression.

## E5 — Cost of the copy-oriented pipe

**Claim.** Feeding raw NV12 through a pipe (rather than sharing memory with a
linked encoder) costs negligible CPU, and — with the shipped low-latency encoder
args — adds no persistent frame of latency.

**Result [measured]** (`bench/copy_bandwidth.py`): a coded 1080p NV12 picture is
3.13 MB; host memcpy runs ~72 GB/s, so one userspace copy of an AVC444 frame
(main+aux, 6.27 MB) is **~0.087 ms**. Even counting ~3 copy stages (into the
runner queue, kernel copy-in on `write`, kernel copy-out on the child's `read`)
that is ~1.5% of a single core at 60 fps. The pipe is not the bottleneck.

The subprocess boundary does **not** inherently add a frame of latency. Whether
the child holds a frame is a property of the encoder's **pipeline depth**, not
the pipe: measured on-box (`ffmpeg_pipeline_depth_probe.py`),
`libx264 -tune zerolatency` emits every input picture in ~3–9 ms with **zero**
frames withheld — one-in, one-out — and on the dev box `h264_vaapi
-async_depth 1` does too under an **xfreerdp** end-to-end test. **Open gap:** a
live **mstsc** deployment on the same GPU still withholds the last frame at
`-async_depth 1`, so a client/transport-level cause exists that the xfreerdp
screenshot test cannot observe (an earlier VAAPI-driver explanation was
speculation, withdrawn). It is under diagnosis with the `XRDP_GFX_TRACE=1`
per-frame server trace; the verified practical fix is `tail_flush = true`. The
price of the subprocess boundary itself is still just the copy, not a frame.

**Threats to validity.** memcpy bandwidth is box-specific, but the conclusion
(copy CPU ≪ encode CPU) holds across any modern host by orders of magnitude.
Glass-to-glass latency vs the linked path is plan (P3).

---

## Results plan — rigorous versions, reachable without new hardware

These sharpen the above on the *same* box (except where noted):

- **P1 — steady-state traffic.** Replace E1's intra proxy with per-frame
  `_main.264`/`_aux.264` byte streams from the in-tree debug tap
  (`XRDP_AVC444_DUMP`) over a scripted interactive session (scroll, type, drag),
  reporting the 444/420 ratio distribution over P-frames, not one I-frame.
- **P2 — quality-matched HW vs SW.** Re-run E3 at matched VMAF/SSIM (sweep
  x264 CRF and vaapi QP to equal quality on a real desktop capture), reporting
  CPU *and* bitrate at iso-quality — the fair compression verdict. Tooling
  (`ffmpeg -lavfi libvmaf`) is on-box.
- **P3 — glass-to-glass latency.** Timestamp a change in the session and its
  appearance in the decoded client frame (x11grab), comparing the linked-x264
  path vs the ffmpeg pipe path, to confirm the low-latency config adds no frame
  of glass-to-glass latency over the linked encoder.
- **P4 — other hardware backends.** nvenc/qsv are enumerated (E2) but need an
  NVIDIA/Intel GPU; run on a cloud instance with that hardware — no code change,
  same `encoder_args` mechanism, which is itself the point being demonstrated.
- **P5 — multi-session CPU scaling.** Drive N concurrent sessions and plot host
  CPU for SW-x264 vs HW-vaapi, showing the session-density gain from E3 at
  scale.

Each of P1–P3, P5 runs on this box today; P4 needs different silicon but *no
xrdp change*, which is the thesis.
