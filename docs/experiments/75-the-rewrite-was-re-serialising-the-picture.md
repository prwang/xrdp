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

## Deployed arm

See the section below once the arm has run.
