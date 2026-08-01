<!--
Experiment record. BACKLOG.md is the OPEN work list; this file is the
closed record it points at. Kept verbatim, wrong claims included.
-->

# #75 — the LTR rewrite was re-serialising a whole picture to edit 30 bytes

2026-08-01. #61e attributed 8.802 ms of a 25.474 ms frame period to
`collect` — popping the encoded NALs out of the two ffmpeg children and
rewriting both views' long-term-reference lists. This is what that cost
was, and what it costs now.

## The question, and why it was worth asking

The rewrite is formality work. It changes the slice header so that main
predicts only from main and aux only from aux: a frame_num, a reference
list modification, an mmco marking. **Tens of bytes.** It had no
business being the largest single piece of xrdp-side CPU in the frame,
and "our AVC444 overhead is heavy" is a claim that has to be proved
rather than assumed — it is the cost the whole 4:4:4 path is judged on.

## What it cost, and against what

`tools/avc444_ltr_rewrite_bench.c` links the shipped
`xrdp_h264_annexb.o` (the file itself, never a copy). Input: 60
pictures per view at 3840×2400 encoded with arm x013's own
`encoder_args`, 20 iterations, dev box.

| | before (`89a0c5dd`) | after (#75) | |
|---|---|---|---|
| rewrite per pair | 11.027 ms | **1.026 ms** | **10.7×** |
| rewrite per byte | 1.90 ns | **0.18 ns** | |
| minor page faults per pair | 4228 | **73** | **58×** |
| `memcpy` of the same buffers (control) | 0.03 ns/byte | 0.03 ns/byte | unchanged |

**The control is the point of the table.** The bench's own `copy-in`
leg is a plain `memcpy` of the same packets into the same working
buffer. Before, the rewrite cost **63× a memcpy of the bytes it was
rewriting**. That ratio is what said the cost was not inherent: no
amount of H.264 formality justifies sixty passes' worth of work to edit
a header. It is now 6×, which is three copies and a scan — the shape
the code actually has.

Scaled to the session #61e measured (3.48 MB/frame): **7.07 ms →
0.63 ms**, so `collect` should fall from 8.802 ms to roughly 2.4 ms and
the period from 25.474 ms to roughly 19.1 ms. That is a prediction, not
a measurement; the deployed arm below is what tests it.

## The bytes did not move, and that is checked two ways

1. **CI**: `tests/xrdp/test_avc444_ltr.c`, 234 assertions including
   `ck_assert_mem_eq` golden byte vectors. 186/186 in the XRDP daemon
   suite, 411/411 overall. **The test file was not touched** — a
   changed assertion here would have voided the entire exercise.
2. **Whole 4K pictures**: the bench now prints an FNV-1a digest over
   every rewritten packet. Before and after produce
   **`12c16104c46343cb`** over 120 pictures of real VAAPI output. CI's
   vectors are small and hand-built; a size-dependent mistake would hide
   from them and not from this.

## What was actually wrong

Four things, in the order they cost. The first is the real one.

**1. The CABAC payload was unescaped and re-escaped for nothing (~72 %).**
`slice_ltr_rewrite` stripped emulation-prevention bytes from the entire
NAL, edited the slice header, then re-inserted emulation-prevention over
the entire NAL — both loops one byte at a time with per-byte bounds
checks. But `cabac_alignment_one_bit` pads the header to a byte boundary
in front of the payload, and the payload's RBSP bytes are copied through
unchanged. **Its escaped bytes are therefore invariant**, provided the
escaper enters the payload in the same state in both streams — and that
state is just "how many trailing zero bytes since the last escape",
computable from either side.

So the fix is: unescape a bounded prefix to reach the end of the header,
rewrite the header, and `memcpy` the child's own already-escaped payload
verbatim. When the entry states disagree the fast path returns -1 and
the caller re-runs the whole NAL on the original code, so **no packet
the old form accepted is refused by the new one**. On the 2400 packets
of the bench the fallback never fired.

**2. Six ~1.7 MB `malloc`/`free` per frame (~19 %).** `out` in
`ltr_rewrite_walk`, `rbsp` and `newr` in `slice_ltr_rewrite`, twice for
two views. All far above glibc's 128 KB `M_MMAP_THRESHOLD`, so each was
an `mmap`+`munmap` pair whose every page faulted on first touch. `rbsp`
and `newr` are now stack buffers sized to the header scan; `out` is held
on `struct xrdp_h264_ltr_state` and reused, released by
`xrdp_h264_ltr_state_free()`. **4228 faults per pair → 73.**

**3. `memset(newr, 0, nal_len + 16)`** zeroed a whole picture buffer
because `put_bit` ORs. Only the bytes the new header occupies need it;
everything past that is overwritten by the payload copy.

**4. `find_start_code` was a byte-at-a-time triple compare** over the
full payload, once per NAL boundary and twice per packet counting the
`packet_intra_is_converted` pre-scan. Now `memchr` for the leading zero.

## What this does NOT say

* **It is not a ratio against anything deployed.** The table is an
  offline bench of one function. The end-to-end number is the arm.
* **The bench's pictures are 2.9 MB of synthetic noise**; the session's
  are 1.74 MB/view of text. The per-byte rate transfers because every
  pass visits every byte; the absolute ms/pair does not.
* **Nothing here is evidence about the T4.** Per the stand-in rule this
  is the dev box's VAAPI output through the shipped rewriter. The
  rewriter is CPU code and the saving is arithmetic, but the claim is
  not measured there.

## The deployed arm: 25.474 -> 18.476 ms, and the producer is now in the frame

One arm, x014, owner-approved. Same xorgxrdp, same `gfx.toml` body as
x013 (verified by `diff`), same payload, same client rig, same geometry,
same day, 45 minutes apart. **The only variable is the xrdp deb.**
Capture: `PR-demo/mac_bisect_matrix/captures/i75_x014_rewrite_20260801`.
Trace integrity: 104 364 records, **0 drops**.

### The mechanism check, run before the rate

| worker cycle, `wait_beg -> wait_beg` | x013 | x014 | |
|---|---|---|---|
| wait for the two ffmpeg children to encode | 16.654 | 16.585 | **unchanged — the control** |
| pop the NALs and rewrite both views' LTR refs | 8.802 | **1.362** | **-7.440** |
| worker holds nothing to encode (`wait`) | 0.0019 | 0.515 | **new** |
| every other stage and gap | 0.018 | 0.013 | |
| **period** | **25.474** | **18.476** | **-6.998** |

n = 3074 cycles, per-cycle closure residual max |0.000000| ms.

**The intervention hit its target and nothing else.** `pump` — the
children encoding, which #75 does not touch — moved 0.07 ms, which is
what a control should do. `collect` fell by 7.440 ms. The period fell by
6.998 ms. The 0.442 ms difference is the new `wait`: **7.440 - 0.515 =
6.925 against a measured 6.998**, closing to 0.07 ms.

Corroborated independently by the server's own send interval: **3075
sends over 56.8 s, mean 18.5 ms, p50 18, p90 19** (x013: 2228 / 25.5 /
25 / 28). 39.2 -> 54.1 fps.

### Read this before quoting 1.38x: the producer is now co-limiting

The margin warning written into `k8s/x014.yaml` before the run was
correct. Producer stamps for the same 59 s window:

| textflood's own loop | x013 | x014 |
|---|---|---|
| frame interval | 16.71 ms | 16.91 ms |
| pipeline period | 25.47 ms | 18.48 ms |
| **margin** | **1.52x** | **1.09x** |

The producer did not change; the pipeline came down to meet it. At 1.09x
the distributions overlap — the producer's p90 is 19.01 ms against a
pipeline p50 of 17.96 ms — and it shows up exactly where it should:

* `wait` is **bimodal and must not be quoted as a mean**. p50 is
  **0.0015 ms**; 66 of 3074 cycles (**2.15 %**) exceed 1 ms and those 66
  carry **1577 ms of the 1582 ms total (99.7 %)**. So 97.85 % of frames
  are still pipeline-bound and ~2 % are producer stalls.
* frames already enqueued when the worker asked: **97.8 %**, against
  100 % on x013.

**Two numbers, and the difference between them is the honest content of
the result.** Delivered period **18.476 ms (1.38x)**. Period with the
producer's stalls removed — `18.476 - 0.515` — **17.96 ms (1.42x)**,
which is exactly the measured p50. The first is what this arm produced;
the second is what the pipeline is capable of and is the one a further
optimisation would start from.

**FR-BENCH-1 is now MARGINAL for textflood at 3840x2400.** It is not
failed — the pipeline is still the slower party for 98 % of frames — but
the contract asks for a producer "strictly faster at every geometry it
gates", and 1.09x with overlapping distributions is not that. **Any
further work on this path needs the faster producer (PRD design B,
7.1 ms/frame offline) before its arm is built, not after.**

### The regression, stated with the win

**p99 send interval went 31 ms -> 46.5 ms**, and cycle p99 went 31 ->
46.5 ms. This is worse than x013 and it is reported because it moved the
wrong way, not because it is unexplained: it is the 66 producer stalls
above, whose own worst interval was 39.18 ms of producer time. Mean and
p50 both improved by ~7 ms; the tail did not. On a payload that could
keep up, the tail should follow the p50 — that is the prediction the
faster producer would test.

### Correctness on the deployed arm

Certificate (`certs/x014.cert`, 3 s at 1920x1080, at deploy):
**ASSERT VERDICT PASS, 7/7**, 471 pictures, **0 black frames**, one
contiguous frame_num chain, 0 gaps. Server log over the 60 s run: **0
rewrite failures, 0 unsupported, 0 pair aborts, 0 budget assertions, 0
third captures** across 3075 frames x 2 views = 6150 rewritten packets —
so the bounded-prefix fast path never once fell back, and never once
emitted something the wire audit rejected.

### Not a result

**`E5 GATE: baseline 51.1 ms -> 2.77x PASS` is void as a comparison**,
for the third time and for the same reason: 51.1 ms is the harness
default measured under `SESSION_KIND=code` at a different geometry. The
comparison that means something is x013's 25.5 ms, measured on this arm's
own configuration. `E5_BASE_MS` still prints its default without
refusing, which remains an open harness gap.
