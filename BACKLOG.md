# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

Only **upcoming** work lives here. Completed work is recorded in `PRD.md` §25
("Delivered"), with detailed root-cause writeups under `tests/xrdp/avc444/`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

## AVC444 splicable capture: wire-format views from xorgxrdp + vmsplice-only feed — DEPLOYED to T4 (2026-07-26), awaiting owner onscreen perf verdict

**Perf regression in the first-cut packers — FIXED (2026-07-26, same
day).** Owner observed Xorg at 50-70% of a core, cost proportional to
damage size. `tools/avc444_pack_bench.c` (new, checked in; offline, old
vs scalar vs vectorized verbatim copies) quantified it: the scalar
first-cut (`75c1928`) cost ~7x the old vectorized planar loop per pixel
(T4 full-4K 49.4 ms/frame). Root causes: per-sample helper calls with
clamp branches, U/V double-decode, missing RDP_VECTORIZE. Fixed by the
row-decode restructure (`e7ecf30`, PRD FR-CAPTURE-7): T4 full-4K 19.2 ms,
2000x1000 drag rect 4.3 ms, 500x200 0.19 ms. Gates re-run on the dev box
(burr single+dual NO residual, SMOKE PASS) and deb deployed to T4
(sha256 4e0563c5…). Damage-proportional cost itself is by design
(rect-limited packing); the constant was the bug.

**Amdahl checkpoint (2026-07-26, owner-directed profile before further
loop tuning).** New harness `PR-demo/t4_profile/profile_drag.sh` (real
T4 AVC444/nvenc session as throwaway `tester`, scripted 2000x1000
xdotool drag on the 4K screen, perf on the session Xorg). Result: the
bottleneck HAS shifted — process split during drag: ffmpeg ~21-27%,
Xorg ~18%, xrdp ~13-15% of a core (none saturated under the scripted
~50 moves/s load). Inside Xorg: pack loops 36.5% + vectorized
avc444_decode_row.avx2 17.8% (AVX2 clone confirmed selected) + fbBlt
window-move blit 12.2% (X core, not ours) + glyph drawing 2.9%.
Implications: (a) further pack-loop vectorization can reclaim at most
~1/3 of Xorg's 18% ≈ 6pp of a core — diminishing; (b) the largest
single consumer is now the ffmpeg child's INPUT side (pipe read =
27.6 MB/frame kernel-to-user copy + rawvideo framing + nvenc upload) —
the high-leverage next step is sending FEWER BYTES, i.e. the planned
LC=1/LC=2 reframe (skip the aux view when chroma is unchanged), which
halves encoder input AND is the Mac-compat prerequisite; (c) xrdp's
13-15% is vmsplice page-ref + NUT demux kernel time — structural,
small. Owner observation of Xorg near a full core likely includes xfce
compositor damage amplification (tester harness runs WM-less); verify
against the owner's xfce session when they retest. T4 config changes
for the harness, both reversible and recorded: `tester` user (cred in
root-only /root/.tester_cred on the T4), /etc/xrdp/wm1.sh now lets
non-ubuntu users exec ~/.xsession (backup wm1.sh.bak-profile; ubuntu
path unchanged).

**Owner-load profile (2026-07-26, Thunar 2500x1800 circular trace in the
real xfce session, compositing=true, dual-monitor, driven via xdotool in
the owner's :10).** Process split: Xorg 40-47%, ffmpeg 21-24% (two
children, one per monitor), xrdp 10-14% — nothing saturated post-e7ecf30.
Inside Xorg: pack loops 37.9% + avc444_decode_row.avx2 17.2% (conversion
= 55%, same ratio as the WM-less run), xfwm compositor rendering via
rdpComposite/pixman 10.3%, window-move blit rdpCopyArea 6.9%. The Xorg
delta vs the WM-less harness (46% vs 18%) tracks the damage area (2.25x
window) plus the compositor's own rendering — no new mystery component.
Priority order that follows: (1) LC=1/LC=2 reframe — during motion it
skips the aux view end-to-end (aux pack in Xorg, vmsplice, ffmpeg pipe
read, nvenc input), the single biggest cross-cutting win and the Mac
prerequisite; (2) pack-loop vectorization (~10pp of a core under the
real load); (3) optionally disable xfwm compositing on the T4 (~10pp,
cosmetic tradeoff, owner's call).

**Lever accounting in consistent units (pp of ONE core, under the
owner reference load above; harness:
`PR-demo/t4_profile/profile_owner_load.sh`).** Baseline: Xorg ~46
(pack loops 17.4 / decode.avx2 7.9 / compositor render 4.7 / move blit
3.2 / rest ~12.8), ffmpeg x2 ~22, xrdp ~13 — pipeline total ~81.
- Lever 1, vectorize the pack loops: pack is ~69% of conversion (bench:
  decode 6.9ms vs pack 12.3ms full-4K; perf agrees 37.9 vs 17.2). A
  proper shuffle implementation cuts it ~60-70% -> SAVES ~10-12pp.
  Floor: the 7.9pp decode is already vector-optimal.
- Lever 2, LC=1/LC=2 motion-time aux skip: removes the aux view
  end-to-end during motion (Xorg aux pack ~9-10pp + chroma decode ~3pp;
  ffmpeg input/pictures halve ~8-10pp; xrdp splice bytes halve ~3-4pp)
  -> SAVES ~22-26pp across the pipeline, halves wire bandwidth, and is
  the macOS prerequisite. Costs: chroma catch-up bursts off the
  interactive path; 4:2:0 during motion only (Windows-identical).
- Lever 3, compositing off: 4.7pp direct + ~2-3pp damage-halo ripple ->
  SAVES ~6-8pp; zero engineering, cosmetic tradeoff, reversible.
- Interaction: lever 2 removes the aux portion of the work lever 1
  vectorizes; done after lever 2, lever 1's remaining value is ~5-6pp.
  Both together: motion-time conversion ~25pp -> ~9pp, pipeline ~81 ->
  ~45pp (before compositor).

**Owner-decided order (2026-07-26): implement Lever 1 FIRST, then
Lever 2.** (Recorded: with this order lever 1 realizes its full
10-12pp immediately; lever 2 then subsumes the aux share.)
**Order updated (2026-07-26, after the frame accounting + design
review): Lever 1 DONE -> Lever 4B -> Lever 2 (preemptive-aux form).**
4B became a hard prerequisite: Lever 2's preemption signal only
exists once capture overlaps encode (PRD FR-PROC-7 §2).

## Lever 1: vectorize the AVC444 pack loops — DONE (owner-load profile confirms; 2026-07-26)

**Result (xorgxrdp `ee1ec01`).** Clamps hoisted into replicated
row-buffer tails, aux pack split into single-output-stream flat loops,
RDP_VECTORIZE on the fused function. Bench: T4 full-4K conversion
19.24 -> 8.74 ms/frame (pure-decode floor 6.70), 2000x1000 drag rect
4.27 -> 1.64 ms — beats the 10-12pp estimate (~15pp of a core under
the owner load, projected). Gates: dev-box burr single+dual NO
residual (bit-parity oracle), SMOKE PASS; deb sha256 9a7f0b6a…
installed on T4.

**Acceptance run (2026-07-26, owner attached, Thunar 2500x1800 orbit
fully on the bottom 4K monitor).** Pack loops **17.4pp -> 3.2pp** of a
core (~14pp returned, beats estimate); decode.avx2 steady at ~7.7pp
(untouched, as expected). Xorg total ~44pp but NOT comparable to the
~46pp baseline: the saved cycles were reinvested as throughput — the
same 400-move trace that filled the baseline window now drains in
<15s (~30 moves/s vs ~13, roughly 2x frame rate), and the window sat
fully on the 4K monitor (baseline orbit straddled both monitors), so
pixman composite (~11pp) and blt (~13pp) grew with the extra frames.
Per-second CPU flat, per-frame cost halved, drag visibly smoother.
Post-lever-1 Xorg profile is now dominated by compositor/blit (~24pp
combined) — strengthens Lever 3's case after Lever 2.

Restructure the pack half of `a8r8g8b8_to_avc444_box` (xorgxrdp) into
branchless/SIMD-friendly shuffle loops per PRD FR-CAPTURE-7, targeting
~60-70% pack-cost reduction (~10-12pp of a core under the owner load;
T4 full-4K conversion 19.2ms -> ~10ms). Gate with
`tools/avc444_pack_bench.c` (extend with the new variant) BEFORE
deploying; then dev-box burr harness (parity) + smoke; then T4 deb +
`PR-demo/t4_profile/profile_owner_load.sh` re-run for the
user-acceptance number.

## End-to-end frame accounting — DONE (2026-07-26); fps ceiling root-caused

Owner challenge: drag still feels <10fps post-lever-1; cpu% is the
wrong unit for delivered-fps accounting, and the "network can't catch
up" hypothesis needed validation. Built
`PR-demo/t4_profile/frame_accounting.sh`: uprobes on the DEPLOYED
binaries (capture entry / encode entry+return / wire send / xup ack /
client GFX ack with arg values) + per-second `ss -ti` on the RDP
socket. No restart, no redeploy.

**Result (owner load, dual-mon, nvenc):** 20.1 fps in exact lockstep
at every stage — zero drops anywhere. Client `queue_depth=0` on every
ack (decoder idle). Network REFUTED as bottleneck with data: ~8 Mbit/s
used of ~98 Mbit/s measured delivery rate, Send-Q ~0, rtt 17ms flat,
0 retrans in-window. Cycle partition (p50):
5.6ms capture+pack -> **30.1ms synchronous encode_pair** (p90 47.5;
main+aux = TWO full-4K nvenc encodes serially through one ffmpeg,
~24MB piped per frame) -> <1ms send -> ~5ms ack-to-next-capture.
Frame period p50 ~50ms, p90 61ms. The pipeline is fully SERIAL:
capture cannot overlap encode because the single shmem buffer is
borrowed by the encoder for the whole synchronous call (FR-PROC-6),
and the xup ack that frees capture fires only at encode return.
fif=2 send-time module ack verified working — it is not the limiter.
"Feels <10fps": p90 period 61ms + 2-3 mouse steps coalesced per frame.

Consequences for lever order (updated after design review — final
order: 4B then 2):
- **Lever 2 (preemptive aux) is the fps lever, not just a cpu lever**:
  skipping the aux encode during motion halves the dominant 30ms term.
  Its preemptive form (PRD FR-PROC-7, no idle heuristic) requires 4B
  first — see the dedicated items below.
- **Lever 4B: two-slot pipelined capture** — design analysis DONE,
  grounded in code; see the dedicated item below and PRD FR-CAPTURE-8.
  (Supersedes the earlier loose "lever 4" sketch: no slot-tagged acks,
  no ack-protocol change, zero xrdp code change.)
- Also worth a look inside the 30ms: nvenc 2x4K should be ~16ms; the
  remainder is pipe transport + ffmpeg demux framing. Profile the
  ffmpeg side before assuming nvenc is saturated.
- **REJECTED (closed thread, do not re-propose): main||aux in two
  encoder processes.** PRD §6.5: both sub-streams must come from the
  SAME encoder and decode as ONE stream ("never ... one FFmpeg process
  for main and another for auxiliary"). Ground truth
  (`vm/GROUND_TRUTH_win2022_avc444.md`): one SPS/IDR per session, all
  views P-slices on a single shared reference chain / frame_num
  sequence. Two encoders = two chains interleaved into the client's
  single decoder = P-reference desync garbage, plus duplicate SPS
  (the Mac-black class the exactly-one-SPS bound exists to prevent).

## Lever 2: preemptive aux (LC=1/LC=2, no idle heuristic) — TODO, ORDERED AFTER Lever 4B (owner decision 2026-07-26)

Contract: PRD FR-PROC-7 (supersedes NG-6's deferral; replaces the
earlier "aux skip + idle-timer catch-up" sketch — owner rejected any
baked-in idle heuristic). During motion, frames go out `LC=1`
(main only); aux chroma is scheduled by **preemption**: after main N,
the encoder thread's existing fifo pop either finds main N+1 (aux N
preempted — N+1's aux is fresher) or finds nothing (aux N encodes NOW
from the already-captured slot, sent as `LC=2`). Aux always eventually
lands; the only thing that can displace it is a newer main.

**Why after 4B (hard prerequisite, not preference):** the preemption
signal is "successor present in the fifo at pop time", which only
exists when capture overlaps encode (two slots). On the serial
pipeline the producer is ack-gated behind the encoder, the fifo is
always empty at pop time, and any workaround is an ack-RTT wait — an
idle timer in disguise (PRD FR-PROC-7 §2 forbids it).

**Scope shrunk to xrdp-only:** capture contract UNCHANGED — xorgxrdp
keeps packing both views (post-lever-1 aux pack share ~1pp, noise);
the win is the aux ENCODE (~half the 30ms term) + aux WIRE bytes
(~half). Plus the 4B interaction: rect N's xup ack defers until aux N
sent-or-preempted (slot holds aux pixels until the decision).

Effect: with 4B, steady-motion period ~16-18ms (~55-60fps) at half
the wire bytes; full 4:4:4 converges one encode (~15ms) after any
damage gap — deterministic, no policy. Recorded tradeoff (FR-PROC-7
§7): sustained gap-free motion rides at 4:2:0 (= AVC420 quality, =
Windows' 93% LC=1 cadence); periodic override deliberately excluded.
Wire framing per `docs/avc444_lc_reframe_design.md` (LC=1/LC=2 PDUs,
v2); macOS Windows App prerequisite. Acceptance: smoke gate gains a
color-edge fidelity-after-settle check (FR-PROC-7 §8); Mac validation
rides this item. Consolidate with the LC reframe / Mac items below
when picked up.

## Lever 4B: two-slot pipelined capture — IN PROGRESS (implemented 2026-07-26, pending T4 deploy + smoke gate + fps measurement; owner decision 2026-07-26; prerequisite of Lever 2 preemptive aux)

Contract: PRD FR-CAPTURE-8. Capture frame N+1 into the second shmem
slot while ffmpeg consumes slot N; period drops from the serial sum
(~50ms) to ~encode duration (~31-36ms, ~28-32fps); composed with
Lever 2's halved encode -> ~16-18ms, ~55-60fps.

**Implementation note (2026-07-26):** landed as designed with one
addition the design pass missed — per-slot staleness re-pack (now PRD
FR-CAPTURE-8 clause 9): the capture packs only damaged rects while
the encoder consumes the full plane, so each slot tracks the region
it missed while the other slot was being written (`cap_slot_missing`,
initialized to full screen on allocation, emptied when the slot is
captured, grown by fresh damage landing in the other slot) and
re-packs it on its next capture. Without it, frame N-1's damage would
visibly regress every other frame. Slot stride rides a new optional
`slot_bytes[]` out-param of `xup_cap_h264_shmem_layout()`; contract
v20260727. Unit: 84/84 incl. new `test_cap_layout_two_slot_strides`.
The ack-leak test runs live on the T4 (kill ffmpeg mid-drag with a
queued successor, verify acks and capture resume) as part of the
deploy validation.

**Validation record (2026-07-26, first T4 deploy — partial).** xrdp
`4932908b` + xorgxrdp `251bc4d` (contract v20260727) deployed on the
(since-terminated) T4. Results that stand:
- Smoke gate PASS 8/8 at 1920x1080 AND 1024x768, zero lag, zero
  encoder errors, with the new colour-edge check (edge fidelity 1.000
  both sizes — the full-chroma calibration value for FR-PROC-7's
  floor). Uprobe traces showed capture/encode/wire/ack all healthy and
  server-vs-client framebuffers pixel-identical.
- Two REAL bugs found and fixed by the strict offscreen client:
  1. `f0104284` (xrdp): metablock region rects had even origins but
     ODD extents — FreeRDP's SSE 4:4:4 reconstruction hard-asserts
     even widths and aborted; lenient clients (mstsc/Mac) only
     tolerate it. Pre-existing, not a 4B regression.
  2. xorgxrdp `251bc4d`: dual-monitor starvation — the monitor-scan
     rotation used the LIVE rect_id (increments per send); the serial
     gate used to break the loop after one send, masking it, but with
     two outstanding the loop revisited the just-sent monitor,
     skipped the other, and the nothing-changed branch destroyed the
     skipped monitor's damage (frozen bottom 4K during drags). Fixed
     by snapshotting the rotation base per pass.
- NOT captured: the 4B-only fps number. The T4 was terminated before
  the measurement ran; re-measure on the replacement instance.
- The FR-PROC-7 (Lever 2) implementation drafted the same day lived
  only in /tmp during the deb split and was lost to the box restart;
  re-implement from the committed PRD FR-PROC-7 design when picked up
  (design + all decisions are fully recorded there).

**Methodology reset (owner directive, 2026-07-26 — the lesson).** The
first offscreen-rig session was unacceptable: a full day of serial
environment discovery with no fps number. Concrete failures: client
stack built ON the T4 (Ubuntu's freerdp3 ships without H264 — had to
source-build); a special `tester` account whose fresh xfce profile
behaved differently from the owner's session (compositor repaint
stalls consumed hours of false-lead debugging against the transport);
per-run interactive ssh-heredoc measurement scripts; three separate
self-inflicted `pkill -f` shell suicides; harness assumptions (empty
password, uid 1000 Xauthority, qterminal) discovered broken one at a
time. Binding rules now in CLAUDE.md ("T4 test methodology"): test as
`ubuntu` only (cred in root-owned /root/.ubuntu_cred on the T4), never
touch the session-policy script, ALL client-side harness on the dev
box over an ssh -L forward of 127.0.0.1:3389, and on-box measurement
as a persistent checksum-gated deploy invoked non-interactively.
Harness reworked accordingly (smoke gate + `t4_measure.sh`).

**Scope (grounded, exact touch points):**
- xorgxrdp `rdpClientCon.c:909/:946` — double the per-monitor region in
  the `xup_cap_h264_shmem_layout()` sizing; two slot offsets per monitor.
- xorgxrdp `rdpClientCon.c:3420` — slot select: `cap_offsets[...]` gains
  the `rect_id`-parity slot term (offset already rides the existing
  paint message, `:3107/:3254`).
- xorgxrdp `rdpDeferredUpdateCallback:3368` + monitor-loop recheck
  `:3397` — gate `rect_id > rect_id_ack` -> `> rect_id_ack + 1`,
  **conditioned on the AVC444 capture code** (the callback is shared by
  all capture modes; others keep 1 slot).
- `common/xup_client_info.h` — contract version bump (mismatch already
  refuses loudly).
- **xrdp: zero code change** (verified: `xup.c:1187` reads per-frame
  `shmem_offset`, maps whole segment; encoder guards
  `xrdp_encoder.c:1159/:1371` are offset-relative vs `data_bytes`).

**Properties preserved (per the frame-accounting review):**
(1) N+1 drains with no successor damage: capture==send in `rdpCapRect`
(`:3285`), `proc_enc_msg` drains the whole fifo per wakeup
(`xrdp_encoder.c:2266`), `process_enc_done` sends unconditionally —
the fif gate withholds only msg 106, never the client send.
(2) Bounded inventory: worst case 2 raw slots + 2 compressed in flight
(+1 raw vs today); on client-ack stall msg 106 stops -> source freezes
after <=2 frames -> damage coalesces in `dirtyRegion` (drop stays
pre-encode; P-chain forbids post-encode drops anyway).

**Acceptance:**
- frame_accounting.sh: period ~= encode duration; capture overlaps
  encode in the event timeline.
- smoke gate r/g/b/w incl. tail frame; multi-size.
- NEW ack-leak test: kill ffmpeg mid-encode with a queued successor
  frame; both rect_ids must still ack and capture must resume (budget
  leak = silent half-speed at 1, freeze at 2).
- Loud assertions shipped with the change: outstanding <= 2, encoder
  fifo queued depth <= 1 (bound is enforced by the remote producer
  gate — assert it locally so future edits fail noisily).
- Dual-monitor: budget accounting must not let one monitor's stall
  starve the other (today's single global rect_id/ack already
  serializes across monitors — verify, don't regress).
- Slot count is FIXED at 2 in the versioned contract (anti-ratchet: a
  third slot is a contract change requiring owner sign-off, not a
  tuning knob).

**Known tradeoff (recorded):** eager capture means up to one
encode-time of content age under saturation (~15-30ms) — throughput
bought with staleness; the r/g/b/w single-event latency path is
unchanged (slots empty -> capture -> encode -> send, byte-identical).

**Validation record (2026-07-26).** xrdp `52099149` + xorgxrdp `75c1928`
(xup contract v20260726, both daemons refuse loudly on mismatch). Unit:
83/83 incl. new page-aligned layout math; the ffmpeg encode tests
exercise the vmsplice feeder end-to-end. Dev box (vaapi/x264): burr
harness single 4K NO residual, dual owner-layout NO residual (the
truth-vs-client compare is the packer parity oracle vs the in-tree
reference converter), SMOKE PASS both sizes as the last step after
install. T4: deb pair installed (sha256 b5e22ac2…/3a6ed395…), probe OK
(dump_extra=1) 1342 ms, AVC444 v2 matched. NOT yet validated: a real
T4 session (Xorg-side packers + nvenc under drag load — the owner's
onscreen perf verdict) and the Windows/Mac client matrix.

Owner directive (chat, 2026-07-26): "the shmem from xorgxrdp must be
directly vmspliced [to ffmpeg] right now ... xrdp must do zero hot path
work; vmsplice is the only allowed xrdp→ffmpeg interaction." We own the
AVC444 wire format; an intermediate shmem format that is not splicable
is a design defect (and an upstream-PR rejection risk).

**Why.** The YUV444 offload left a SECOND full-frame pass inside xrdp:
`xrdp_avc444_conv_update()` re-walks every pixel per frame with
per-sample bounds-clamped `sample_yuv()` calls (~25M samples per 4K
frame; the luma copy re-fetches U/V it discards), then memcpys the two
views into the runner's staging queue, then write()s 27.6 MB/frame into
the pipe. Live T4 signature (owner, 2026-07-26): 4K window drag is slow
and htop shows xrdp burning MORE CPU than Xorg — the process doing the
actual color conversion. On the T4's weak CPU this is release-blocking.

**Design.**
- xorgxrdp packs the final wire format per damage rect, fused into the
  existing capture conversion: per-monitor shmem region becomes
  `[main NV12][aux NV12]`, both at the FINAL coded size (width aligned
  to the client-derived chroma_align 16/32, height align16), each view
  page-aligned (4096) for vmsplice.
- Aux variant rides the EXISTING `capture_format` contract field using
  the reserved constants: `XRDP_yuv444_v2_stream_709fr` (ChromaV2 aux),
  `XRDP_yuv444_v1_stream_709fr` (v1 banded aux), `XRDP_nv12_709fr` +
  `CC_GFX_AVC444` (main-only, the ffmpeg AVC420 mode). v1 aux may
  repack the full view per frame (diagnostic mode, perf uncritical).
- New `avc444_chroma_align` field in the xup client info; layout
  helpers reworked (page-aligned regions + aux-offset helper);
  `XUP_CLIENT_INFO_CURRENT_VERSION` bumped 20260725 → 20260726, loud
  refusal on mismatch as before.
- xrdp hot path: pointer math + `vmsplice()` only. The runner's inq
  staging memcpy is replaced by a borrowed-iovec queue; pump() feeds
  ffmpeg exclusively via vmsplice (probe too). Pipe enlarged via
  F_SETPIPE_SZ (best effort). SPLICE_F_GIFT is NOT used (pages are
  xorgxrdp's shmem). Borrowed input never outlives the encode call:
  if input is not fully spliced when the synchronous wait ends, the
  call errors and the child restarts (no torn-frame window).
- `xrdp_avc444_convert.c` leaves the hot path and stays in-tree as the
  format REFERENCE (unit tests / oracle).
- Visual gates before deploy: PR-demo/smoke_gate + multimon_burr on the
  dev box (truth-vs-client compare catches any packing error), then T4
  deb pair.

**Perf accounting (4K single monitor).** Removed from xrdp per frame:
~25M clamped samples (the 16ms/frame encoder-thread cost), 27.6 MB
staging memcpy, 27.6 MB write() kernel copy. Added to xorgxrdp: ~0 —
the packer replaces the equal-cost planar YUV444 writes inside the
same per-rect conversion walk, and it is damage-rect-limited where
xrdp's pass was full-frame.

## AVC444 header policy: static gfx.toml `dump_extra` + verify-once probe — DONE (2026-07-26, deployed to T4)

**Validation record (2026-07-26).** Commit `820f558e`; unit suites all
green (libcommon 157, libipm 35, libxrdp 13, xrdp 83 — incl. the four
probe-class tests run against real ffmpeg via `XRDP_TEST_FFMPEG_PATH`).
Deb `xrdp-dev_0.10.80+git820f558ea79c` (sha256 `9532415...dd4607`)
installed on T4; `/etc/xrdp/gfx.toml` sets `dump_extra = true` for
h264_nvenc (backup `gfx.toml.bak-820f558e`). Live daemon connection:
`verifying ffmpeg AVC444 ... (gfx.toml dump_extra=1)` → `xrdp_ffmpeg:
probe OK (dump_extra=1) at 1920x1088 in 1124 ms` → `Matched H264/AVC444
v2`; shim log confirms exactly ONE ffmpeg spawn per verification (the
double-spawn ladder is gone). Not yet exercised live: the cold-boot
TIMEOUT classification (needs an instance reboot; the class is
unit-tested via the hang fixture). Owner onscreen test on T4
(Windows mstsc + macOS Windows App) still the final gate — see the
T4 validation-matrix item.

Owner directive (chat, 2026-07-26): remove the adaptive dump_extra probe
ladder. In-band header policy is per-box administrator configuration (like
`encoder_args`, which gfx.toml already owns); the probe only VERIFIES it.

**Why (T4 cold-boot heisenbug, 2026-07-26).** First connections after the
T4 instance boot fell back to RFX: both ladder attempts burned the full 4s
probe deadline (GPU up 01:07, failures 01:15/01:18; a warm probe at 01:39
passed in 2.2s, and an offline replay of the same nvenc bytes through the
real NUT parser + validators passes every check — cold CUDA first-init is
the failure, not content). Underlying design flaw: the probe returned one
bit, so the ladder could not distinguish CONTENT REJECT (deterministic
evidence of extradata-only headers) from TIMEOUT (environmental), and it
flipped `use_dump_extra` on either. Latent WRONG-BIT hazard: an in-band
encoder timing out pristine then passing a warm dump_extra retry would put
duplicated SPS/PPS on the wire — the exact Mac-black bitstream class
`7927efa7` was built to prevent.

**Scope.**
- gfx.toml `[avc444_ffmpeg] dump_extra = true|false` (default false);
  tconfig field + parse; template and man page document it, including the
  tested NVENC block (Tesla T4, driver 580.159.03, ffmpeg 8.0.1,
  2026-07-26).
- The adaptive ladder in `xrdp_mm.c` is deleted; ONE probe run with the
  configured flag; a timeout (or any environmental failure) must never
  cascade into a different header policy.
- Probe observability debt paid: outcome classes (OK / SPAWN_FAIL /
  TIMEOUT / STREAM_ERROR / CONTENT_REJECT), child stderr logged, child
  exit status logged, elapsed + packet count logged. (Folds in the
  "Probe must log child stderr" item below.)
- Bidirectional contract check: the reset packet must carry EXACTLY ONE
  SPS. `dump_extra = true` on an in-band encoder is refused at the probe
  (duplicates) instead of shipping Mac-black bytes; `dump_extra = false`
  on an extradata-only encoder is refused with a message naming the fix.
  The runtime first-packet check gains the same duplicate guard.
- PRD: FR-PROBE-6 (verify-only contract) + §25 addendum.

**Signed-off shipped-behavior change** (strict-honesty rule): the silent
runtime adaptation is REMOVED. A wrong/missing `dump_extra` now loudly
removes the AVC candidate for that connection (codec order proceeds, e.g.
RFX) with an actionable log line. Owner directive in chat, 2026-07-26.

**Acceptance.**
- Unit: tconfig parses `dump_extra` (absent → false); pristine probe of a
  global-header encoder returns CONTENT_REJECT (not a generic failure);
  the same encoder with dump_extra returns OK; dump_extra on an in-band
  encoder returns CONTENT_REJECT (duplicate SPS); a hanging fake encoder
  returns TIMEOUT; exactly-one-SPS wire guard passes in both configs.
- T4: deb built from the committed branch, installed; `/etc/xrdp/gfx.toml`
  sets `dump_extra = true` for h264_nvenc; a fresh connection logs the
  verification PASS and matches AVC444; failure classes visible in log.

## AVC444 resize-to-black regression (non-16-aligned surface) — FIX DONE (2026-07-25)

**Symptom (owner, single 4K monitor):** resize from fullscreen to a smaller
window -> ffmpeg dies, client (UWP mstsc) shows black.

**Root cause (regression from the YUV444 offload above):** cross-component
stride contract mismatch. xrdp reads the capture's YUV444 planes with a
16-aligned stride `pstride=(w+15)&~15`, plane size `pstride*align16(h)`, and its
GFX encoder guard (`xrdp_encoder.c:1360`) requires `3*align16(w)*align16(h) <=
data_bytes`. But xorgxrdp allocated/strided the buffer at the UNALIGNED surface
size (`rdpClientCon.c:914` `w*h*3`; `rdpCapture.c` `dst_stride=id->width`,
plane offset `id->width*id->height`). A 16-aligned surface (3840x2400) gives
provided==required and works; a non-16-aligned resize (3814x2233) under-runs
the guard, so xrdp drops every frame silently (return NULL) -> ffmpeg never
respawns -> permanent black. Live log confirms: xorgxrdp shmem `bytes 25549986`
(=3814*2233*3) vs guard need `25697280` (=3*3824*2240).

**Repro (offline, deterministic):** `tools/avc444_resize_repro.c` models both
allocation formulas vs the guard requirement and drives the real
`xrdp_avc444_conv_update` on a contract-sized buffer. Pre-fix: 4 non-aligned
sizes report BLACK (provided<required), exit 1. `-DXORGXRDP_ALIGNED`: all OK.

**Fix:** align the capture allocation and plane stride/offset to
XRDP_H264_ALIGN (xorgxrdp commit aa08c63). Matches xrdp's read contract and the
size already reported by `rdpSendMemoryAllocationComplete`. Only the visible
w x h is written; xrdp edge-clamps and never samples the pad.

**Regression test (CI backstop):** `test_avc444_resize_nonaligned_stride_
contract` (tests/xrdp) drives conv_update at non-16-aligned dims on an
EXACTLY contract-sized buffer with a sentinel in the pad, asserting the Y/U/V
planes are read at the correct aligned stride/offset (a skew or short buffer
reads the sentinel or wrong plane). 74/74 green.

GATE: live xvfb/freerdp exercise of the resize path with the fixed debs, then
owner onscreen retest.

## AVC444 dual-monitor drag "burr"/ghost residual — DONE (fix verified onscreen 2026-07-25)

**Symptom (owner):** dragging a window on the 4K subscreen in DUAL-monitor
mode leaves 1-2px residual/burr lines. NOT present in single-monitor mode.
Screenshot `regression_ghost_edge_2026-07-25 114541.png` (untracked).

**REPRODUCED end-to-end** (owner visually confirmed same failure class):
`PR-demo/multimon_burr/multimon_burr_repro.sh` — real Xorg(dummy) client at
the owner's exact layout (canvas 3840x3840, primary 2560x1440 on top at
+594, 4K below at +0+1440; layout asserted by black-pixel count 1843200),
xfreerdp3 /multimon /gfx:AVC444, self-driven qterminal drags. Oracle =
client framebuffer vs session framebuffer after 2.5s settle, baseline-masked
(pre-drag pair subtracts static codec noise — two earlier heuristic
detectors false-positived; do not trust colour heuristics here).
Result on deployed debs (xrdp 2f216a20 + xorgxrdp aa08c63): persistent 1px
solid, 2px/3px dashed ghost lines along the drag paths, full-height dashed
columns on the primary (never dragged on), and 2px dashed vertical ghosts
STRIKING THROUGH both screens at the seam-crossing drag columns
(x 786/1286/2286). MODE=single: clean. Artifacts: burr2_*.png.

**Retracted hypothesis (for the record):** an earlier "global rect_id gating
throttles the 4K surface -> drag lag" theory was wrong — residuals persist
at idle, which latency cannot explain. Retracted before any code change.

**ROOT CAUSE — PROVEN (2026-07-25, per-frame dump forensics):**
Cross-monitor shared-shmem plane overwrite exposed by the metablock fringe
blit. The full chain, each link verified with data:
1. xorgxrdp multimon: ALL monitors write their planar YUV444 planes at
   OFFSET 0 of the ONE shared capture shmem, each with its own geometry
   (4K: stride 3840, planes 3840*2400; primary: stride 2560, planes
   2560*1440). Every primary frame therefore overwrites the 4K monitor's
   persistent plane bytes (primary's 3 planes span offsets 0..11.06M =
   rows 0..2879 of the 4K Y plane) and vice versa.
2. xrdp feeds the full plane extent to ffmpeg each frame, so the encoded
   picture carries that corruption everywhere outside the freshly captured
   damage rects.
3. `out_RFX_AVC420_METABLOCK` (xrdp_encoder.c:822-827) expands each damage
   rect by 1px — UPSTREAM code (b583a8d5, Jay Sorg, May 2024, x264 GFX
   path; an earlier note here misattributed it to us — corrected). Our
   contribution on those lines is only the even-origin rounding (`&= ~1`,
   the mstsc chroma-parity fix), which can widen the left/top fringe by 1px
   more but did not create the expansion. The client therefore blits a
   1-2px fringe BEYOND the freshly captured area —
   painting the corrupted stale bytes -> 1-2px solid/dashed ghost lines at
   damage-rect boundaries. Dashes = the periodic visibility pattern of the
   different-stride overwrite; seam strike-through = both surfaces ghosting
   at the same client x.
4. Single monitor: one writer only -> un-recaptured plane bytes always equal
   current client content -> fringe blit is a no-op -> clean.
**Proof artifacts** (XRDP_AVC444_DUMP per-frame dumps + instrumented
xorgxrdp diag/dirty-trace build, both reverted after): drag frame seq23
(rect 188,490,1842,1256) vs pre-drag full frame seq21 — conv Y planes
differ in 6,467,271 bytes OUTSIDE the captured rect (all 2400 rows),
written by the interleaved primary frame seq22; the client ghost lines of
that run sit EXACTLY on seq23's metablock fringe (col 187 = x1-1, row 489 =
y1-1, row 1256 = y2), corrupted in the dumped conv input (1654/1024/690
bytes). Note: an earlier "refuting" causal A/B was an experimental
artifact — x11grab on the session root provokes an xfwm compositor slab
repaint (full-region DIRTYADD) = a primary frame, injecting the very
corruption the variant meant to exclude (caught by the dirty-trace log).

**Harness:** `PR-demo/multimon_burr/` — `multimon_burr_repro.sh` (visual
repro + oracle), `causal_ab.sh` (variant A/B), `forensic_run.sh` (short
drag with per-frame dumps). Box restored after forensics: xorgxrdp
aa08c63 reinstalled, dump env removed, smoke gate PASS (1920x1080 and
1024x768: ok=8 lag=0 encoder_errors=0).

**Ownership:** BOTH ingredients are upstream — the shared-shmem overlap
(upstream `rdpCaptureGfxA2` NV12 multimon writes every monitor's planes at
offset 0 with per-monitor stride, identical hazard) and the metablock 1px
expansion (b583a8d5, upstream x264 path). The bug is LATENT upstream:
upstream AVC420 GFX multimon should show the same 1-2px ghost class
(prediction, not yet demonstrated — our gfx.toml negotiates RFX for
/gfx:AVC420 clients, so untested here). Our AVC444 work did not create it
but surfaced it: it is the multimon H.264 mode actually deployed, and
YUV444 planes are 2x the NV12 footprint (3 vs 1.5 B/px), doubling the
overlap. Worth an upstream issue/PR note alongside our fix.

**Fix (IN PROGRESS, owner-approved 2026-07-25; per strict-honesty rule the
real fix, not a mask):** give each monitor a DISJOINT region of the capture
shmem (per-monitor plane offset). Internal xorgxrdp<->xrdp contract change,
never visible to RDP clients.

*Scope = the real blast radius (owner directive: do not artificially narrow
to AVC444).* Affected: the GFX H.264 capture family — `CC_GFX_A2` (NV12,
upstream AVC420/x264 GFX) and `CC_GFX_AVC444` — both write per-monitor
planes at offset 0 of the shared shmem AND their encoder consumes the full
plane every frame (persistence assumption). Verified-NOT-affected, left
untouched: legacy `CC_SUF_A2` (session-canvas NV12 layout — no per-monitor
translate, UV plane at session `cap_w*cap_h`, monitors land disjoint by
construction) and `CC_GFX_PRO`/`CC_SUF_RFX` (every RFX tile the encoder
reads is fully rewritten within the same capture call — rgnPART fills the
whole tile first; CRC-skipped tiles are never read — so nothing depends on
shmem persistence).

*Mechanism:*
- Shared pure helper in `common/xup_client_info.h` (the file that IS the
  daemon contract) computes the per-monitor offset table + total allocation
  from `display_size_description` + capture code; xorgxrdp uses it for
  allocation and plane placement; unit-tested in `tests/xrdp`.
- The offset each frame rides the msg-62 WIRETOSURFACE_1 payload as a new
  trailing field after left/top/width/height (per-command `cmd_bytes`
  bounds the parse, so the field is cleanly optional); xrdp validates
  bounds and reads planes at `shmem base + offset`. Field absent -> 0 ->
  exact current behavior.
- Mixed-version safety: `XUP_CLIENT_INFO_CURRENT_VERSION` bumped 20250528
  -> 20260725; both daemons already FatalError/refuse on mismatch at
  connect, so a mixed pair fails LOUDLY instead of silently corrupting.
  Deb pair-guard stays; both sides land together upstream (the same
  lockstep the socket-naming change used).

Sizing: sum of per-monitor regions replaces the session-size formula
(owner layout: 27.6M + 11.1M = 38.7M vs 44.2M today). NOT chosen: capturing
the 1px fringe (masks the fringe blit but leaves poisoned planes in every
encoded frame). Single-monitor: offset stays 0, allocation formula
unchanged in behavior. Gate = multimon_burr harness clean in MODE=dual +
MODE=single + `make check` + smoke gate LAST + owner onscreen.

*Validation (2026-07-25, deployed xrdp-dev 0.10.80+git0070ceb514be +
xorgxrdp-dev 1:0.10.80+gitdd431cc156fd, both from committed branches):*
- `make check` 81/81 (6 new layout-contract tests).
- **Mechanism kill PROVEN at byte level:** forensic re-run reproduced the
  exact pre-fix critical frame pattern (full 4K seq37 -> interleaved full
  primary seq38 -> 4K drag seq39, same rect 188,490,1842,1256); the 4K
  conv Y planes now differ in **0 bytes** outside the damage rect
  (pre-fix: 6,467,271). Cross-monitor plane overwrite is dead.
- **multimon_burr MODE=dual and MODE=single: NO residual** (all passes,
  both screens; residual hot px are scattered unstructured codec noise,
  no lines, no strike-through). The pre-fix signatures (1px trail lines,
  2px/3px dashed, cross-seam strike-through, multi-position trails) are
  gone.
- Two ORACLE false-positive classes were found and fixed in the harness
  along the way (documented in its README, not masked): (a) the mover
  window's LIVE lossy edges flag wherever it stands in an after-grab —
  the pre-fix single-mode "clean" was threshold luck; now the window is
  parked inside the baseline and returned there before every grab, plus a
  printed, geometry-scoped parked-window exclusion; (b) end-of-drag
  PIPELINE LAG: one run showed a full stale window image ~3.5s after the
  last move that self-corrected before the next pass — the verdict now
  uses a second settled grab (+8.5s) and first-grab-only findings are
  reported as LAG, keeping that latency signal visible without conflating
  it with persistence. The drag trail itself is never excluded.
- Smoke gate LAST on the deployed pair: 1920x1080 and 1024x768, ok=8
  lag=0 encoder_errors=0 — SMOKE PASS.
- **Owner onscreen: PASS (2026-07-25, dual-monitor mstsc drag on the 4K
  subscreen — burr gone). Item closed.** Follow-ups tracked separately:
  clean-room slice-order amendment (fix-first, both repos), upstream
  issue/PR for the latent AVC420 multimon hazard, and the end-of-drag
  pipeline-lag observation (perf, reported as LAG by the harness).

## AVC444 CPU conversion is the 4K/dual-monitor bottleneck — DONE (2026-07-25)

**IMPLEMENTED + DEPLOYED.** The RGB->YUV matrix moved off xrdp's encoder
thread to xorgxrdp's capture (autovectorized C, no hand-asm, no new dep).
- xorgxrdp `feat/avc444-yuv444-capture` (branched clean from upstream 49bf2dd,
  NOT on the old ARGB commit): `a8r8g8b8_to_yuv444_709fr` emits three planar
  YUV444 planes; `rdpYuvVectorize.h` gives portable `optimize O3 + tree-
  vectorize` + x86 `target_clones(default,avx2)` (verified: 16- and 32-byte
  vectors + ifunc AVX2 clone at -O2). Commit 76d1433.
- xrdp `dev` (linear): `capture_format = XRDP_yuv444_709fr`; `xrdp_avc444_conv`
  now reads Y/U/V planes and only subsamples (main) + repacks (aux) - no
  matrix. Byte-identical output (same 709fr coeffs) - unit tests 73/73 green.
  Commit 63c37688.
- **Profiled (tools/avc444_convert_bench.c), 3840x2400 + 2560x1440:** xrdp
  encoder-thread convert **167 ms -> 15.9 ms (6 -> 62 fps ceiling), 10.5x**;
  the matrix now runs capture-side at ~2-6 ms on another thread. GPU was
  already idle, so 4K dual-monitor should hit real-time.
- Both dev debs built + installed (gfx.toml preserved); smoke gate: 1024x768
  clean, colours correct on r/g/b/w end-to-end (proves the YUV444 capture ->
  repack path is colour-correct); 1920x1080 shows only the pre-existing white-
  frame-lag (encoder_errors=0), unchanged by this work.
- GATE: owner onscreen dual-monitor 4K perf test (the container guard blocks a
  live xfreerdp run here). The clean work-PR port is studied later, gated on
  that perf PASS.

## (historical) AVC444 CPU conversion bottleneck — root cause (2026-07-25)

**Symptom:** dual-monitor GFX (mon0 3840×2400, mon1 2560×1440), AVC444 v2,
renders <1 fps. **Live capture:** GPU (amdgpu 1002:1586) 0% busy, VAAPI
starved; ONE xrdp encoder thread pinned on a single core (32 cores idle);
both ffmpeg children ~1–3%. So the cap is single-threaded CPU, not the GPU
encode.

**Root cause:** `xrdp_avc444_conv_update` (`fill_main` + `fill_aux` in
`xrdp/xrdp_avc444_convert.c`) is a scalar per-pixel RGB→YUV709 convert — a
function call + a 4-byte `memcpy` per pixel — walking the full surface TWICE
(main, then aux, each re-reading all RGB), for every surface every frame,
sequentially across monitors on one thread.

**Reproducible offline (no X/GPU/client):** `tools/avc444_convert_bench.c`
times the convert on a synthetic frame. Measured here: 3840×2400 = 119.8 ms,
2560×1440 = 47.5 ms, **dual sequential = 167 ms/frame → 6 fps ceiling from
conversion alone** (before capture/pipe/encode/ACK). Matches the observed
<1 fps. Not a multimon regression — the same convert runs single-monitor; 4K
just makes its cost dominate.

**Status quo / scope (why this path alone pays it):** the NATIVE H.264 GFX
path already sets `capture_format = XRDP_nv12_709fr` — xorgxrdp (the capture
side) delivers NV12, so xrdp does NO per-pixel convert there. The ffmpeg
444/420 path is the only one that sets `capture_format = XRDP_a8r8g8b8`
(full-chroma RGB) and converts in-process, specifically to build the aux
view. Measured cost split @3840×2400:
  - AVC420 main-only (RGB→YUV matrix + luma + 2×2 chroma avg) = **87.9 ms**
  - AVC444 full (main + aux repack)                          = **119.8 ms**
  - ⇒ the RDP-specific AVC444 aux chroma repack alone        ≈ **~32 ms**
So ~88 ms of the ~120 ms is the GENERIC colour conversion that libraries
already do fast; only ~32 ms is the irreducible RDP-specific shuffle.

**ffmpeg already does the generic part fast** (its own maintained SIMD, and
it is ALREADY our subprocess — no new dep): `swscale` BGRA→NV12 @3840×2400 =
**~3.6 ms wall (threaded) / ~28 ms single-thread** vs xrdp's 87.9 ms.

**Fix directions — delegate the matrix, keep only the shuffle (NO
hand-vectorized math to maintain, NO new deps):**
1. **AVC420-ffmpeg:** feed ffmpeg raw RGB (`-pixel_format bgra`,
   `-vf format=nv12,hwupload` or `scale_vaapi` on the idle GPU) and dumb-copy
   the XRGB surface; delete `fill_main`. ~88 ms → ~4 ms. Isolated, low risk.
2. **AVC444-ffmpeg:** move the RGB→YUV matrix off xrdp too. Preferred:
   capture a full-chroma YUV from xorgxrdp (mirror the native NV12 capture,
   e.g. a new `XRDP_ayuv`/`yuv444` `capture_format`) so xrdp receives YUV and
   only does the cheap integer 4:2:0-average (main) + aux repack — no matrix,
   no SIMD to maintain. Alt: two ffmpeg inputs (ffmpeg emits main from RGB;
   xrdp packs aux only). The aux repack stays simple C (it is the one thing
   no library provides), and can be threaded across the idle cores if needed.
3. GPU is 0% busy → any remaining convert (or the whole RGB→NV12) can run on
   VAAPI (`scale_vaapi`), which is already open.
Add a perf-regression guard around `tools/avc444_convert_bench.c`. Full
writeup: `vm/perf_capture/ROOT_CAUSE_4k_dualmon_slow.md`.


## macOS Windows App AVC444 black screen — H2 CONFIRMED (our stream is malformed), FIX = Windows-like emission (2026-07-24)

**DECISIVE RESULT (owner, onscreen):** the macOS Windows App (iMac) **rendered
the real Windows host `43.98.187.122` cleanly for 2 min** — a live AVC444v2
session carrying LC=1 + frequent LC=2 aux chroma (~7%) + rare LC=0. This
**refutes H1** (the Mac fully supports AVC444v2 aux/`LC=0` reconstruction) and
**confirms H2**: xrdp's own `LC=0` stream is malformed / non-Windows-like, and
that is why the Mac blacks *our* stream while rendering Windows'. The client is
fine; the defect is in xrdp's AVC444 emission.

**FIX DIRECTION (evidence-backed):** make xrdp emit Windows-like AVC444v2 —
luma-first `LC=1` IDR bootstrap, one shared decode context, aux only as P-slices
on an established reference chain, `LC=2` deferred chroma catch-up as the normal
path, disjoint-region `LC=0`, codec `0x000F`. This is the rewrite (implements the
`LC=1`/`LC=2` deferral PRD NG-6 omits).

**IMPLEMENTED + DEPLOYED + SELF-VERIFIED (branch `dev/avc444_lc1lc2_reframe`,
commit `aa894917`).** Reframe (owner's design): keep chroma dense, only change
the semantic dependence — serialize the existing main+aux H.264 pair as an `LC=1`
luma PDU then an `LC=2` chroma PDU inside ONE gfx frame, instead of one
same-region `LC=0` PDU. Same H.264 bytes, same traffic; only the wire framing
changes. `out_RFX_AVC444_BITMAP_STREAM_view` serializes one view; the live path
queues the `LC=1` PDU inline (non-last enc_done) and returns the `LC=2` PDU, so
both land between the surrounding STARTFRAME/ENDFRAME (atomic — avoids the
luma-only-intermediate that killed the earlier two-GFX-frame split). Wire capture
of the DEPLOYED binary (`avc_mode=444`) confirms: `seq0 LC=1
[AUD,SPS,PPS,SEI,IDR]` (luma-first IDR bootstrap) → `LC=2 [AUD,P]` (deferred
chroma) → `LC=1 → LC=2`, **zero `LC=0`** — byte-structurally what real Windows
emits. All 68 xrdp unit tests pass; astyle clean.

**Smoke gate note (honest):** `PR-demo/smoke_gate/smoke.sh` passes clean at
1024x768 but shows a deterministic 2-keypress "white shows previous frame" lag at
1920x1080. This is **pre-existing, NOT a regression**: the pre-reframe `LC=0`
binary fails 1920x1080 with the identical signature (ok=6 lag=2), `encoder_errors=0`
on both — a keytest/encoder pacing artifact at high res, independent of LC framing.

**OUTCOME (owner onscreen, 2026-07-24): FIXED.** The macOS Windows App renders
our reframed `LC=1`/`LC=2` stream — no black, no functional regression, no server
reconfiguration. mstsc/UWP unaffected. Residual: on the macOS HiDPI display the
isoluminant **1px**-chroma stripes render softened/blended (mstsc/UWP show crisp
grid+checkerboard = true 4:4:4 on the wire). That softening is a client-side
artifact of the macOS Windows App's opaque HiDPI/DSP path (likely a 4:2:2
downscale or Nyquist attenuation at non-1:1 scaling), not reachable from the
server. Accepted as-is.

**AUD IS A RED HERRING — PROVEN (owner onscreen, 2026-07-24).** To confirm the
interleave alone is the fix (and to match the aud-less upstream PR), `-aud 1` was
stripped from `/etc/xrdp/gfx.toml` `[avc444_ffmpeg]` encoder_args. Wire verified
aud-less: `LC=1 [SPS,PPS,SEI,IDR]` → `LC=2 [P]`, no NAL 9; smoke identical to
baseline (1024x768 clean; pre-existing 1920x1080 white-lag, `encoder_errors=0`).
**Both the macOS Windows App AND UWP render clean with NO AUD** — the `LC=1`/`LC=2`
interleave is the entire fix; AUD is confirmed unnecessary and stays out of the
upstream PR. Port gate 1 is now GREEN.

**UPSTREAM PORT — PLANNED, GATED (do not execute yet).** Plan:
`docs/avc444_upstream_port_plan.md`. Owner decisions: FOLD the reframe into clean
slice `239d8d0e` (serializer born as `LC=1`/`LC=2`, no separate fix commit);
**AUD excluded** from the PR (upstream default stays `repeat-headers=1`). Port is
gated on: (1) macOS confirms the aud-less interleave; (2) **NVENC-on-Linux test
regression** fixed — ties to the clean branch's BLANKET-`dump_extra` slice-7
regression (the `c74a09e7` cleanroom artifacts are POISONED for macOS; see
"Re-fold slice 7…"); (3) **multi-monitor** done (see "Multimonitor AVC444…").

### Prior status (kept for history) — ground truth captured

**UNBLOCKED.** Owner provided a real Windows host that emits real `LC=0` AND
`LC=2`: `43.98.187.122`, **Windows Server 2022** (build 20348), **NVIDIA A10-4Q**
vGPU (hardware NVENC). GPO set by us: `AVC444ModePreferred=1`,
`AVCHardwareEncodePreferred=1`. Captured 803 AVC444 frames with the patched
FreeRDP dumper across two chroma-rich payloads (ChromaAnim isoluminant hue
rotation; ChromaScroll scrolling saturated bars + colored text). Full analysis:
`/work/vm/GROUND_TRUTH_win2022_avc444.md`.

**What real Windows actually emits (measured, 803 frames):**
- codec **`0x000F` (AVC444v2) exclusively** — never v1 `0x000E`.
- **Bootstraps luma-only:** first frame is **`LC=1` IDR** (`[AUD,SPS,PPS,IDR×3]`),
  no aux. The chroma/aux view is NOT initialized at connect.
- **Cadence `LC=1` ~93%**, `LC=2` ~7% (56/803), **`LC=0` ~0.25% (2/803)**.
- **Aux is ALWAYS `[AUD,P,P,P]`** — 58/58 aux instances; **never** carries its own
  IDR/SPS. Exactly one IDR/SPS/PPS in the whole session (seq0 luma). One shared
  H.264 decode context; the aux "view" is temporally interleaved as ordinary
  P-frames, routed to main-vs-aux by the `LC` field.
- **`LC=0` streams tile DISJOINT, non-overlapping rects** (main=new-content tile,
  aux=complementary catch-up tiles). Windows **never** emits a same-region
  full-surface `LC=0`.

**What xrdp emits (our dumps `gfxdump_444v1`, `gfxdump_aud`):**
- codec `0x000E` (v1) in 444v1 mode.
- **First AVC frame = `LC=0` dual-stream**, both streams the **same full surface**,
  s1=IDR + s2=**bare P-slice** in one PDU.
- **`LC=0` every frame**, same-region. **Never** emits `LC=1`/`LC=2` (PRD NG-6:
  deferral not implemented).

**Candidate root cause (H2), now with a concrete mechanism:** xrdp bootstraps
AVC444 with a **same-region dual-stream `LC=0` whose aux is a P-slice**, and
repeats `LC=0` every frame — a construction **real Windows never produces**. Real
Windows bootstraps luma-only (`LC=1` IDR), keeps one shared decode context, sends
aux **only as P-slices on an established reference chain**, uses **`LC=2`
deferral** as the normal chroma path, uses **disjoint** regions on the rare
`LC=0`, and uses **v2**. mstsc/UWP (lenient DXVA) accept xrdp's form; Apple
VideoToolbox (stricter) evidently rejects it → black.

**H1 vs H2 update:**
- **H1** (Mac categorically can't do inline `LC=0`): **weakened** — real Windows
  DOES emit `LC=0`. Refuted outright if the Mac renders this host.
- **H2** (our `LC=0` is malformed / non-Windows-like): **strongly supported** by
  the structural deltas above.

**DECISIVE TEST REMAINING (onscreen, owner):** point the **macOS Windows App
directly at `43.98.187.122`** (ordinary RDP host).
- renders ⇒ Mac's AVC444v2/`LC=0` path works ⇒ **H2 confirmed** ⇒ fix xrdp to
  emit Windows-like: **luma-first `LC=1` IDR bootstrap + deferred `LC=2` chroma
  catch-up, v2 `0x000F`, disjoint-region `LC=0`.** This is the rewrite direction
  (implements the `LC=1`/`LC=2` deferral PRD NG-6 currently omits).
- blacks ⇒ problem is broader than stream construction (negotiation / caps /
  VideoToolbox init); re-open H1.

**INTERIM (workaround, NOT a fix):** `avc_mode = "420"` deployed in
`/etc/xrdp/gfx.toml`; renders on all clients but is symptom suppression.

Artifacts: real-Windows dumps `/work/vm/gfxwin_anim`, `/work/vm/gfxwin_scroll`
(raw `.bin` + `manifest.txt`); our dumps `/work/vm/gfxdump{,_desk,_aud,_420,_444v1}/`;
parsers `/work/vm/parse444.py`, `/work/vm/scan444.py`; instrumented FreeRDP
`/work/vm/frdbuild` + `/work/vm/pfreerdp.sh` (`RDPGFX_DUMP_DIR`); payload sources
`/work/vm/ChromaAnim.cs`, `/work/vm/ChromaScroll.cs`. AUD change (harmless
superset, disproven as the fix but kept — real Windows does emit AUD on every AU)
in `xrdp/xrdp_encoder_ffmpeg.c` default + gfx.toml `-aud 1`.

### (superseded) AUD fix investigation — kept for history

Ground truth captured by instrumenting a FreeRDP client with a per-frame RDPGFX
wire dumper (patch in `rdpgfx_recv_wire_to_surface_1_pdu`: raw bitstream + AVC444
header parse; build under `/work/vm/frdbuild`, runner `/work/vm/pfreerdp.sh`,
`RDPGFX_DUMP_DIR=<dir>`). Compared stock **Windows Server 2025** (local KVM VM,
`127.0.0.1:13389`) against **our xrdp** (`127.0.0.1:3389`), both negotiating
AVC444v2 (`0x000F`). To force the true 444 video path (not the static PLANAR
`0x000A` fallback) used a self-animating GDI payload / scrolling terminal.

Wire-proven format deltas (ours vs MS), highest-suspicion first for the macOS
Windows App black screen:

1. **AUD (NAL unit type 9).** MS emits an Access Unit Delimiter on **every**
   access unit (172/172 frames; keyframe `[9,7,8,6,6,5,5,5]`). Our xrdp emitted
   **none** (0/77; keyframe `[7,8,6,5]`). Apple VideoToolbox (behind the macOS
   Windows App) relies on AUDs to delimit access units where ffmpeg/mstsc are
   lenient — leading candidate for the black screen.
2. **Profile/level.** MS = Main@3.2 (`0x4d`/`0x20`); ours = High@4.2
   (`0x64`/`0x2a`). Secondary candidate.
3. **LC field.** MS sent `LC=1` (luma-only, no chroma aux) for our smooth test
   content; ours `LC=0` (full dual-stream 444). Under investigation: which
   payloads make MS emit `LC=0` (background research task; see
   `/work/vm/LC_payload_findings.md`). Hypothesis: MS may only exercise the
   dual-stream path on certain content, so the Mac never hits its 444-recon path
   with MS but does with us.

**AUD fix (this item):** emit an AUD on every access unit, matching MS. AUDs are
inert to the decoders that already worked (ffmpeg/FreeRDP, mstsc render MS's
AUD stream), so this is a backward-compatible superset — no functional
regression (rule #2).
- Code default (`xrdp/xrdp_encoder_ffmpeg.c` libx264 path): `-x264-params
  repeat-headers=1:aud=1`.
- **Active deployed path is `h264_vaapi`** (gfx.toml `[avc444_ffmpeg]`
  encoder_args), so the live knob is `-aud 1` added there. Verified standalone
  (`-aud 1` -> NAL 9 present, `-aud 0` -> absent) and on the wire: after restart,
  **126/126** AVC444 frames carry the AUD; keyframe now `[9,7,8,6,5]`, P-frames
  `[9,1]`. Stock `xfreerdp3` connects and renders the desktop correctly (no
  black, no decode error) -> FreeRDP compatibility preserved.
- STATUS: awaiting on-screen A/B on the real **macOS Windows App + UWP client**
  (owner-driven). If AUD alone does not fix it, test forcing Main profile and
  `LC=1` next.
- FOLLOW-UP (clean PR): AUD is currently per-encoder-backend (vaapi `-aud 1`,
  libx264 `aud=1`); openh264/native-x264 backends
  (`xrdp_encoder_openh264.c`/`xrdp_encoder_x264.c`) are not covered. A robust
  fix guarantees the AUD in xrdp's Annex-B output regardless of encoder (inject
  the NAL, or an `h264_metadata=aud=insert` output bsf).

## H.265 / HEVC via ffmpeg — OUT OF SCOPE / BLOCKED (2026-07-14)

**Decision: not pursued.** The encode side is nearly free (`-c:v libx265` /
`hevc_nvenc` in `encoder_args`; the converter and NUT demux are codec-agnostic;
only a new HEVC Annex-B validator — 2-byte NAL header, VPS/SPS/PPS 32/33/34,
IDR 19/20 — would be genuinely new). **The wire is the wall.** Research against
the current MS-RDPEGFX spec (rev 19.0, 2026-05-11): **no HEVC codecId, no HEVC
caps flag** — the public codecId enum ends at AVC444V2 `0x000F`, all H.264. The
AVD *feature* is documented (a dedicated **"Configure H.265/HEVC hardware
encoding"** GPO, but in the **AVD** admin template `terminalserver-avd.admx`,
not in-box RDS; client shows "Codecs Used: HEVC", event 162 "HevcProfile"), yet
the *protocol carriage* (codecId, enabling capset, container framing) is
undocumented. The FreeRDP-labelled "Azure undocumented" capsets `0x000B0101/
0200/0300` are pinned by the spec as behaviorally == 10.7 with no HEVC
semantics. FreeRDP has an experimental AV1 custom codec but **no HEVC decoder at
all**. So a server emitter would need packet-capture reverse-engineering of a
live AVD↔Windows App session (three unknown values) and would only reach recent
Microsoft clients with the HEVC Video Extension + capable GPU. Revisit only if
those values get documented or captured; the spec-grounded, interoperable
ceiling for xrdp remains AVC444/AVC444v2 (shipped).

*Update 2026-07-23 (revisit-trigger watch):* MS-RDPEGFX v20260511 Appendix A
note <5> now acknowledges capsets `0x000B0101/0200/0300` — behaving as
VERSION107 only on builds *without* KB5089573 (24H2/25H2) / KB5089570 (26H1),
i.e. real v11 features ship behind those KBs; FreeRDP maintainers suspect
HEVC (FreeRDP#12846, nightly probes Azure hosts). We captured `0x000B0101`/
`0x000B0300` flags `0x1a2` live from the Android Windows App (gap analysis
§2a). Still no public codecId/capset semantics — item stays BLOCKED; the
watch condition is those KB-gated semantics or a FreeRDP decode landing.

## ~~Graceful degradation on persistent encoder failure~~ — WITHDRAWN (2026-07-17)

Withdrawn by explicit owner decision: an automatic RFX fallback would *mask*
persistent encoder failure instead of surfacing it, and masking is exactly the
failure mode that prolonged the AVC444 lag investigation (see the honesty rule
in `CLAUDE.md`). A persistently failing encoder must fail loudly (per-frame
ERROR lines, visible breakage) so the root cause gets fixed — on this project,
do not re-add any silent codec fallback without explicit owner sign-off.

## macOS Windows App AVC444 validation — TODO (2026-07-22, highest-value test)

The Mac "Windows App" is the stated blocker that killed the prior
out-of-tree AVC444 rollout (Nexarian: FreeRDP and MSTSC were fine, "But
Mac OS is important enough that it blocked the rollout"; no screenshot,
capture, or root cause exists upstream — see
`PR-demo/UPSTREAM_GAP_ANALYSIS.md` §2a). Our stream lacks the fork's F1
(pair split across frames — now unit-guarded by the avc444_wire tests) and
F2 (no caps gating) defects and is mstsc-verified, so this run is decisive
whichever way it goes.

**Setup:** Mac + Windows App (record app + macOS versions) over the tunnel
to `127.0.0.1:3389`; `avc_mode = "auto"`; capture regardless of outcome:
the `xrdp_mm_egfx_caps_advertise` version/flags lines (the capsets the
Windows App offers — undocumented anywhere), the negotiated-mode log line,
and a screenshot. Optional: dev build + `XRDP_GFX_TRACE=1` for send/ack.

**Expected outcome matrix — interpretation and action:**

1. **Caps ≥ v10, AVC444 v2 negotiated, render clean** (incl. colorkey
   drive and an odd-origin high-contrast edge): historical blocker
   REMOVED. Strongest PR line. Record evidence; done.
2. **Clean until resize, garbled after**: generation/reset handling on
   reconnect-resize. Retest at fixed size via fresh login; if it
   reproduces, treat as OUR bug candidate (reset keyframe / caps redo),
   trace before blaming the client.
3. **Immediate full-frame chroma garble on v2** (Nexarian-symptom):
   client fault isolated (our stream is spec-conformant + mstsc-clean).
   Retest `avc_mode = "420"` — expected clean. If a v1 (0x000E) trial is
   wanted, add a small caps-classifier override knob (config-only,
   follow-up). Ship policy: per-client negotiate-down documented in
   gfx.toml docs; PR narrative = "fault isolated, contained by caps
   gating + config".
   **2026-07-23: observations VOIDED, rerun required.** v2 black and
   `444v1` black were observed, but on a contaminated rig: after
   cycling codecs on one backend session, the known-good AVC420
   baseline ALSO went black on reconnect — xrdp restart does not reset
   the Xorg session, so none of those runs count (screenshot kept in
   `PR-demo/mac_windows_app/` as an observation only). NEW MANDATORY
   DISCIPLINE for every codec trial: use
   `PR-demo/tail_flush_ab/reset_420.sh` — fresh login per trial
   (backend session killed; sesadmin kill is unimplemented, TERM to the
   sesexec pid == session id works) and every trial bracketed by green
   AVC420 baselines; a black 420 bracket voids the trial. The `444v1`
   knob remains available for the disciplined rerun.
4. **Garbled even on AVC420**: NOT a 444 defect — baseline H.264 issue
   (our stream or Mac decoder). Capture and root-cause before any claim;
   do not paper over with RFX (honesty rule).
   **EXCLUDED 2026-07-23:** AVC420 on the iMac is owner-verified fully
   functional — first connect and dynamic resize both render correctly.
   The v2 black-screen defect (outcome 3) is isolated to the 444 layer.
5. **Client advertises only CAPVERSION_81 or AVC_DISABLED**: classifier
   already serves AVC420/RFX — confirm session works stock-like; the
   captured capsets are themselves the deliverable (nobody upstream has
   them documented). **OBSERVED on the Android Windows App (SM-S936U,
   2026-07-23):** `AVC_DISABLED` on all v10 capsets, no `AVC420_ENABLED`
   on 8.1, undocumented `0x000B0101`/`0x000B0300` flags `0x1a2`; session
   correctly ran RFX (capture in `UPSTREAM_GAP_ANALYSIS.md` §2a).
   **Counter-observation (Windows desktop Windows App, 2026-07-23):** the
   desktop variant advertises NO `AVC_DISABLED` (flags 0x0 through 10.7)
   — so the Mac variant plausibly allows AVC too, making outcomes 1/3
   more likely than 5. Reminder: run the Mac test SINGLE-monitor, or the
   multimon gate skips H.264 before any negotiation (as happened in the
   dual-monitor Windows session).
6. **No garble but stalls/frozen frames**: pacing/ack issue, not chroma.
   Dev build + trace; compare `frame_id` ack cadence vs mstsc run.
7. **Fails before GFX negotiation** (TLS/transport): environment, not
   codec — fix tunnel/cert first, outcome not attributable to AVC444.

**Acceptance:** verdict + capsets + screenshot recorded in
`UPSTREAM_GAP_ANALYSIS.md` §2a (required-test #2 closed either way), and
the PR narrative updated ("blocker removed" or "fault isolated + policy").

## Reconnect after codec switch renders black — suspected real bug, TODO (2026-07-23)

Observed live on the dev box (VAAPI): one backend Xorg session, serial
reconnects negotiating v2 → 444v1 → 420; the final reconnect on the
KNOWN-GOOD AVC420 path rendered black. A codec change across
disconnect/reconnect to a persistent session must work — clients
legitimately reconnect with different caps (and admins flip gfx.toml).
Suspects, in order: (1) xorgxrdp capture-mode renegotiation — the session
was created with full-chroma AVC444 capture (`CC_GFX_AVC444`) and the
reconnect renegotiates a different capture/codec combination; (2) stale
per-monitor encoder/converter state in xrdp_encoder across module
reconnect; (3) egfx surface re-create vs xorgxrdp shmem framing mismatch.
- Repro recipe is deterministic and cheap on this box (no Mac needed):
  connect xfreerdp on 420-fresh session (confirm renders), flip avc_mode,
  reconnect, flip back to 420, reconnect → black?
- First forensic: XRDP_GFX_TRACE=1 (dev build) on the black reconnect —
  are frames encoded+acked (client shows black content) or is the
  encoder/capture idle (no damage delivered)?
- Out of upstream-PR scope unless the disciplined rerun shows it affects
  single-codec operation; document as a known limitation if PR#1 ships
  before the fix.

## Port AVC444 wire-layout serializer + test to clean branch — TODO (2026-07-22)

Delivered on dev: the RFX_AVC444_BITMAP_STREAM body serialization was
extracted from `gfx_wiretosurface1_avc444` into an exposed
`out_RFX_AVC444_BITMAP_STREAM()` (`xrdp_encoder.{c,h}`, no behavior change —
identical byte sequence, placeholder/backfill included) and unit tested in
`test_avc444_metablock.c` (`avc444_wire` tcase): ONE PDU, LC=0 in info-word
bits 30..31, cb == metablock+luma length, chroma sub-stream immediately
after, both metablocks over the same rects, stream ends after chroma. This
is the direct regression guard against the prior fork's pair-split-across-
frames defect (luma LC=1 / chroma LC=2 in separate GFX frames).

- Porting rule: NO separate fix commit — fold the serializer extraction
  into slice 5 (metablock emission, same file/pattern) or slice 8 if 5
  stays folded into 8; the `avc444_wire` tests travel with it; the
  `gfx_wiretosurface1_avc444` call-site change lands in slice 8.
- Re-run the per-slice bisectability walk for rewritten slices after.
- Acceptance: clean branch `make check` includes the avc444_wire tests;
  `git diff` dev-vs-clean for these files stays scaffold-only.

## Multimonitor AVC444 (one ffmpeg child per monitor) — IN PROGRESS (2026-07-24)

**GATES the AVC444 upstream port** (owner, 2026-07-24): must be done before the
`LC=1`/`LC=2` reframe is ported to `avc444-ffmpeg-upstream`. See
`docs/avc444_upstream_port_plan.md` gate 3.

The single-monitor MVP limit is one eligibility condition, not
architecture: the encoder data path is per-monitor already
(`avc444_conv[16]` / `avc444_ffmpeg_handle[16]` keyed by `mon_index`,
lazy per-surface create at per-surface dims, per-surface resize/teardown),
and GFX/xorgxrdp already run one surface per monitor (RFX multimon uses
the same dispatch — observed live 2026-07-23, dual-monitor Windows App).

- DONE — code: dropped `monitorCount <= 1` from the ffmpeg-AVC eligibility
  gate in `xrdp_mm_egfx_caps_advertise`; the probe coded size now comes from
  `xrdp_mm_avc444_probe_dims()` = LARGEST single monitor (per-axis max over
  `minfo_wm`, 16-aligned), NOT the virtual-desktop bounding box (which can
  exceed a backend's per-session limit — T4 NVENC 4096x4096 — and would
  wrongly fail the candidate). One ffmpeg child still encodes one monitor's
  surface, so probing one monitor is representative.
- DONE — test: `tests/xrdp/test_avc444_multimon.c` unit-tests the
  probe-dims geometry (no-monitor→screen, dual 1024x768→single 1024x768,
  per-axis max on mixed sizes, 16-align round-up, NULL guard). `make check`
  green (73/73).
- DONE — harness: `PR-demo/multimon_offline/` drives a real 2×1024×768
  `xfreerdp /multimon` (client X = xf86-video-dummy, 2 outputs) and asserts
  the server path: `monitorCount 2`, ONE ffmpeg probe at `1024x768` (NOT the
  2048×768 virtual desktop), `Matched H264/AVC444 (ffmpeg)`, and two mapped
  surfaces — no encoder fallback.
- BLOCKED (env) — live run NOT executed in the dev container: its process
  guard reaps background X servers/clients at tool-call boundaries (dummy
  `Xorg :95` reaped, exit 144; same guard that killed `chroma_strip_anim`).
  A persistent client X + `xfreerdp` + xrdp across the handshake can't be
  held here. HONEST STATUS: multimon geometry is proven by the unit test
  (deterministic, in CI); the live 2-monitor render is pending a run on an
  unguarded host (owner rig / dev box directly) — do NOT claim it green
  until that run passes. Then dual-monitor live matrix (per-monitor resize,
  layout change, mixed sizes) on the Windows App client; extend smoke gate.
- DONE — deployed via clean dev deb (2026-07-25): built
  `dist/xrdp-dev_0.10.80+gitf852e3b3375b_amd64.deb` with
  `scripts/build_dev_deb.sh` (per the new CLAUDE.md "Deployment" rule — no
  hand-copied binary), backed up + preserved the box's `avc_mode=444`/VAAPI
  `gfx.toml` (dpkg confold kept it; sha verified), `apt install`ed,
  `daemon-reload` + restarted xrdp/sesman. Verified: `/usr/sbin/xrdp` is the
  multimon build (`xrdp_mm_avc444_probe_dims` present), services active,
  `:3389` listening, no startup errors. Pre-install binary backed up at
  `/root/xrdp.premultimon.*.bak`.
- SMOKE (single-monitor, new binary, 2026-07-25): 1024x768 CLEAN (ok=8
  lag=0); 1920x1080 RED (ok=6 lag=2) — the WHITE keypress shows the previous
  (blue) frame, `encoder_errors=0`. This is the DOCUMENTED PRE-EXISTING
  1920x1080 white-frame-lag artifact (already A/B-proven pre-existing on
  `ec0598f9`), NOT a multimon regression: the multimon change is
  behavior-identical for single monitor (monitorCount=1 →
  `xrdp_mm_avc444_probe_dims` returns `minfo_wm[0]` = the same 1920x1088 the
  old `screen->width/height` code produced) and touches nothing in the
  encode/delivery path. Per the strict-honesty rule this handoff is NOT
  declared "smoke-clean" — the smoke gate is RED at 1920x1080. The
  multimon-specific validation is the owner's onscreen 2-monitor test +
  the offline harness on an unguarded host.
- Docs: per-backend encoder-session limits (consumer GeForce ~8 NVENC
  sessions; T4/VAAPI effectively unbounded); N children = N sessions.
- Latency note: encoder thread encodes surfaces sequentially per frame
  (~3-9 ms each observed); acceptable 2-3 monitors, parallelize only if
  proven needed.
- Upstream scope recommendation: keep PR#1 single-monitor as certified;
  multimon = follow-up PR (changes eligibility surface, own review).

## Isolate the macOS Windows App AVC444 black screen — our-wire vs client-bug — TODO (2026-07-23)

DISCIPLINED RESULT: the macOS Windows App blacks BOTH AVC444 v2 AND v1
(different aux packing, identical failure → chroma math exonerated) while
rendering AVC420 + RFX. Common factor = the AVC444 dual-view wrapper
(RFX_AVC444_BITMAP_STREAM info word + aux sub-stream). Independently
reproduces Nexarian's 2025 Mac report on our defect-free, unit-tested,
spec-conformant implementation → strong evidence of a real Mac-client
AVC444 defect, but NOT yet isolated from an xrdp-shared wire assumption.
Two cheap discriminators to close it:

- **(a) DONE 2026-07-23 — Windows clients render ours (necessary, NOT
  sufficient).** Three Microsoft Windows clients (UWP Windows App,
  mstsc.exe, RDCMan) negotiated AVC444 v2 (0x000F) and rendered clean on
  the pristine post-fix stream. mstsc = Microsoft's reference decoder →
  our ChromaV2 wire parses on Windows. But these clients tolerate our
  stream; they do not prove a *strict* decoder accepts it.
- **(b) DONE 2026-07-24 — GROUND TRUTH FLIPS THE VERDICT (see item
  below).** A local stock Windows Server 2025 (own KVM VM, no infringe)
  emits AVC 4:4:4 that the macOS Windows App **negotiates AND renders**
  fine. So the Mac 4:4:4 decoder is NOT categorically broken — it works
  against Microsoft's wire. The Mac blacks ONLY on *our* stream. This
  isolates the fault to a **real wire-format delta between our AVC444 and
  Microsoft's** that the Mac's stricter decoder rejects while
  xfreerdp/mstsc tolerate.

Acceptance: verdict recorded in `UPSTREAM_GAP_ANALYSIS.md` §2a; the wire
delta hunt gets a concrete byte-diff + `avc444_wire` assertion.
STATUS: SUPERSEDED. The prior "genuine Mac-client AVC444 defect, our wire
exonerated" conclusion is WITHDRAWN — it rested on lenient decoders only.
Ground truth (b) shows the Mac renders Microsoft's 444, so the defect is
(at least partly) in our stream. New load-bearing item: "Find the AVC444
wire-format delta vs Microsoft" below.

## Ground-truth capture: stock MS AVC444 vs the macOS Windows App — DONE (2026-07-24, VERDICT FLIPPED)

RESULT (load-bearing, not a nicety): a self-owned stock **Windows Server
2025 Datacenter Eval** (build 26100) running locally under KVM on this box
emits AVC 4:4:4 that the **macOS Windows App negotiates AND renders**. The
Mac's 4:4:4 decode path therefore WORKS against Microsoft's wire — it is
not categorically broken. Since the same Mac client blacks on our xrdp
AVC444 (v1 and v2) but renders 420/RFX, the fault is a **real wire-format
gap in our stream** that the Mac's stricter decoder rejects. This WITHDRAWS
the earlier "genuine Mac-client defect / our wire spec-conformant"
conclusion (which rested only on lenient clients: xfreerdp, mstsc, RDCMan).

Confirmed **without tapping TLS** — we own the server, so its own graphics
stack logs the negotiated profile per connection:
- Rig: `/work/vm/` — `win2025.raw` (VHDX→raw, unattend.xml injected offline
  via ntfs-3g for headless OOBE), `run_vm.sh` (q35+OVMF, 8 GiB/4 vCPU, AHCI
  disk + e1000e NIC, user-net hostfwd 13389→3389 / 12222→22, filter-dump
  `rdp.pcap`), creds in `/root/.testvm_cred`. GPO `AVC444ModePreferred=1`,
  `AVCHardwareEncodePreferred=0` (software 444 — GPU-not-required verified).
- Per-connection proof = RdpCoreTS/Operational **Event 162** at the client's
  connect time (from `qwinsta`), attributed to the client via **Event 169**
  `client operating system type`:
  - macOS Windows App: OS type **(6,0)=OSX**, gfx ver `0xB0101`,
    **AVC available: 1, Initial profile: 2048 (0x800 = AVC 4:4:4)** — renders.
  - xfreerdp `/gfx:AVC444`: OS (4,7)=UNIX, `0xA0701`, avail 1, profile 2048.
  - xfreerdp `/gfx:AVC420`: OS (4,7), `0x80105`, avail 0, profile 2 (control).
  QEMU NAT rewrites all sources to 10.0.2.2, so IP can't distinguish clients;
  the OS-type + gfx-version fingerprint does. Client-side corroboration:
  34 `rdpgfx_read_h264_metablock` H264_METABLOCKs in an 8 s xfreerdp capture.
- CAVEAT: Event 162 = negotiated/initial profile, not a per-frame chroma
  guarantee. To prove full 4:4:4 pixels actually land on the Mac, next run a
  chroma test pattern (fine red/blue edges that only survive 4:4:4) on the
  server and confirm sharp on the iMac. This is one build (26100).

Historical context (superseded plan) below; we did NOT need TLS MITM
because owning both endpoints makes FreeRDP the decrypted tap.

We have never compared our AVC444/AVC420 GFX bytes against a genuine
Microsoft RDP server — all "frame sequence" comparisons to date were (a)
our own encoder output diffed across ffmpeg versions/encoders/branches and
(b) reading the Nexarian fork *source*. The MS-RDPEGFX spec is the only
"reference" we've checked our wire against, by reading. A real capture
would be ground truth for: the exact RFX_AVC444_BITMAP_STREAM layout a
Windows client actually expects (LC field, metablock rects, dual-view
packing), the MS non-standard color-conversion matrix (jsorg71's named
hard problem), and whether the macOS Windows App's AVC444 black-screen is
a client bug or something our stream does differently from a real server.

- Setup (NO GPU NEEDED — verified against MS first-party docs 2026-07-23):
  a **Windows 11 Pro or Enterprise** VM, no GPU/vGPU. AVC444 has a
  documented SOFTWARE encoder path; a GPU only accelerates it and is
  mandatory only for HEVC. GPO under Computer Config > Admin Templates >
  Windows Components > Remote Desktop Services > RD Session Host > Remote
  Session Environment: ENABLE "Prioritize H.264/AVC 444 graphics mode for
  Remote Desktop connections"; leave "Configure H.264/AVC hardware
  encoding" Disabled/Not Configured (forces software encode — desired,
  there is no GPU). Not Server-only; the two GPOs are independent (444 =
  codec/mode select, hw-encode = GPU-vs-CPU). Sources: learn.microsoft.com
  graphics-enable-gpu-acceleration ("enable AVC/H.264 even without GPU
  acceleration"; "if you disable or don't configure [hw-encode], we will
  always use software encoding") and graphics-chroma-value-increase-4-4-4
  ("You don't need to use GPU acceleration to change the chroma value").
  VERIFY the server is actually emitting 444-in-software before trusting
  the capture: event log Applications and Services Logs > Microsoft >
  Windows > RemoteDesktopServices-RdpCoreTs > Operational, **Event ID 162
  text = Avc444FullScreenProfile** (444 active; HevcProfile = HEVC
  instead) and **Event ID 170 = AVC hardware encoder 0/absent** (software).
  Then connect the SAME macOS Windows App. If the Mac renders 444 from a
  real MS server, the client is exonerated and the fault is our wire
  (huge — concrete wire-diff bug). If the Mac ALSO blacks from a real MS
  server, the client is broken for 444-on-Mac and our AVC420-for-Mac
  policy is vindicated. Cheapest sanity check before any capture work:
  the ~15-min GPU-less VM + Event-162 read settles the GPU question
  itself.
- Interception: install a trusted root CA on the iMac, MITM the RDP TLS
  (RDP uses TLS/CredSSP; a proxy with the trusted cert can terminate and
  re-originate) and capture the decrypted GFX PDUs; OR run the MS server
  in a VM and packet-capture with the server's private key / a patched
  FreeRDP shim as the recorder. Wireshark's rdpegfx dissector decodes the
  caps + wire-to-surface PDUs once decrypted.
- Deliverable: a byte-level diff of a real server's AVC444 keyframe PDU
  vs ours at the same resolution; feed any delta back into the encoder /
  wire serializer and the `avc444_wire` unit test.
- Authorization: owner-run on owner-controlled hosts only; document scope.

## Find the AVC444 wire-format delta vs Microsoft — TODO (2026-07-24, LOAD-BEARING)

Now the highest-value open item. Ground truth (above) proved the macOS
Windows App renders Microsoft's AVC 4:4:4 but blacks ours → there is a
concrete difference in our RFX_AVC444_BITMAP_STREAM / H.264 bytes that a
strict decoder rejects. Goal: capture both wires at the same resolution
and byte-diff until the rejected element is found; encode the fix as an
`avc444_wire` unit assertion.

- Capture MS side (decrypted, no TLS MITM needed — we own the server):
  patch/point a FreeRDP recorder at the local Win2025 VM (build with
  `WITH_GFX_FRAME_DUMP=ON`, or a small WLog/hook at
  `rdpgfx_recv_wire_to_surface_1_pdu` to dump `codecId` + raw
  `bitmapData`). The Debian `xfreerdp3` build has `WITH_DEBUG_RDPGFX=OFF`,
  so codecId isn't logged — either rebuild FreeRDP with the debug/dump
  options or add the hook. Capture a keyframe at a fixed size (e.g.
  1024×768 and 1920×1080).
- Capture our side: same client, same sizes, against xrdp with
  `avc_mode=444`; reuse the metablock trace already wired up.
- Diff candidates to inspect first (most-likely strict-decoder trip
  points): the AVC444 info word (cbAvc420EncodedBitstream1 length + LC
  bits 30–31), luma/chroma metablock region-rect coverage and count,
  regionRect vs surface bounds, quantQualityVals presence/qp, the H.264
  bitstream framing itself (SPS/PPS in-band vs extradata — our resolved
  dump_extra history), NAL/annexb vs avcc, and the ChromaV2 aux packing
  (0x000F) vs Microsoft's.
- Deliverable: named byte-level delta + a fix in the encoder / wire
  serializer + an `avc444_wire` assertion that pins it; then re-verify the
  macOS Windows App renders our stream.
- Also worth: the chroma test-pattern confirmation (fine red/blue edges)
  to prove MS 4:4:4 pixels actually reach the Mac, closing the Event-162
  "negotiated ≠ per-frame" caveat before deep byte-diffing.

## macOS dump_extra branch mis-render (headerless x264) — WON'T CHASE (2026-07-23)

Under bracket discipline, a fresh-login run of libx264-without-repeat-
headers (ladder correctly engaged: pristine probe fails → dump_extra on,
single SPS/PPS per keyframe verified) still blacks the macOS Windows App,
while the SAME dump_extra branch renders from NVENC on the T4. Not a
regression: this config never worked on any prior build (probe fail →
RFX), and its only real-world occupant (NVENC) is validated. Decision:
do not chase — no shipped default/runbook recipe uses a headerless-x264
encoder, and the probe now logs a WARNING steering toward in-band-header
encoders. If revisited, the structural suspect is slice count (x264
`-tune zerolatency` emits 2 IDR slices; NVENC 1) — testable by pinning
`-x264-params slices=1` and one disciplined Mac run. Cheapest to fold
into the batched T4 hour alongside the NVENC 444 rerun.

## Re-fold slice 7 on the clean branch with the STATIC dump_extra config — TODO (2026-07-23, reshaped 2026-07-26)

**GATES the AVC444 upstream port** (owner, 2026-07-24: "NVENC Linux test
regressed"): the NVENC-on-Linux path must be green — the blanket-`dump_extra`
regression below poisons the cleanroom for macOS — before the `LC=1`/`LC=2`
reframe is ported. See `docs/avc444_upstream_port_plan.md` gate 2.

The clean branch `avc444-ffmpeg-upstream` @ `c74a09e7` carries the
BLANKET dump_extra (slice 7 `04e43ee2`), which is the regression fixed on
dev by `7927efa7`. 2026-07-26 owner directive replaced the adaptive form
in turn with the STATIC gfx.toml `dump_extra` + verify-once probe (see
the item at the top; adaptive had a timeout→wrong-policy hazard). The
slice-7 refold must use the static-config form — do not port the adaptive
intermediate. Same no-separate-fix-commit rule; re-run the bisectability
walk after. The `c74a09e7` cleanroom deb and any artifact built from it
are POISONED for the macOS client — do not hand out.

## Probe must log child stderr — FOLDED (2026-07-26) into "static dump_extra + verify-once probe" (top item)

`xrdp_ffmpeg_avc444_probe()` drains and discards the child's stderr, so a
probe failure logs only `ffmpeg probe FAILED` with no reason. The T4/NVENC
global-header failure (PRD §25, 2026-07-22) took a live shim + off-box NUT
replay to diagnose; child stderr in the log would not have named this
particular cause (the child was silent) but eliminates the largest suspect
class (bad args / missing device / missing encoder) in one glance.

- Scope: probe loop only — feed `err_fd` reads through the existing
  `log_child_line()` (as the runtime path does) instead of discarding.
- Also log WHICH internal check failed (timeout / EOF / NUT error /
  non-monotonic pts / reset-keyframe validation) at WARNING.
- Also log WHY the H264 candidate was skipped when no probe runs at all
  (client caps refusal vs multimon gate vs config) — the 2026-07-23
  dual-monitor Windows App session matched RFX with no probe line and
  the reason was only inferable from code reading.
- Acceptance: a probe failure line is followed by the child's stderr (if
  any) and the failing-check name; unit tests unaffected.
- Lands on the dev branch first; ports to the clean branch only by folding
  into slice 7 (same rule as the dump_extra fix — no separate fix commits
  on the clean branch).

## Upstream clean-room preparation — TODO (2026-07-17)

Transition from the dev branch to a reviewable upstream PR against `devel`.
The dev branch stays as-is (history + scaffold); the PR is rebuilt clean.

### Owner decisions (locked)
- **Strip `XRDP_GFX_TRACE` diagnostics from the PR.** All three layers:
  the send/ack trace in `xrdp_mm.c` (`gfx_trace_on`, the send/ack log lines)
  and the damage-bbox + enc `submitted_seq/returned_seq/inflight/centerY`
  trace in `xrdp_encoder.c` (`gfx_enc_trace_on`, `gfx_trace_rects`). Safe
  because the invariant it revealed is already asserted deterministically at
  the API: `test_avc444_ffmpeg.c` requires every encode call to return
  `READY` with `desktop_sequence == submitted` and `flush_next` → `DONE`
  (commit 4eb72b0c), and the fail-loud `sequence mismatch` / `restarting
  encoder` ERROR path is exercised by the smoke gate. Stripping the trace
  removes a debugging aid, **zero** regression coverage.
- **Strip `tail_flush` from the PR.** It was only ever a workaround for the
  runner desync that the synchronous encode fixed; it is now a structural
  no-op (nothing is ever in flight). Remove the ini knob and both arming
  sites: `xrdp_tconfig.{c,h}` (`avc444_ffmpeg_tail_flush` field + parse),
  `xrdp_encoder.h` (`avc444_flush_enabled`), the arming blocks in
  `xrdp_encoder.c`, and the `tail_flush` docs in `gfx.toml` / `gfx.toml.5`.
  Keep `flush_next` — that is the teardown/resize drain, unrelated to the
  spammer.

### Base the clean-room branch on `origin/devel`, not local `devel`
Cut the clean branch from `origin/devel` (currently 8812646d, 2026-07-16;
remote cache is synced — do not run `git fetch`, this env has no push/fetch
creds). Against that ref our branch is **41 ours-only / 3 origin-only**,
merge-base `3af31df3` (Jul 2). Do NOT use the local `devel` ref (21d38d0c,
Jun 17) as the base or comparison — it is ~a month stale, and that staleness
is why `git diff devel..HEAD` shows a set of changes that are **upstream, not
ours**, and must NOT appear in the PR:
- `libxrdp/xrdp_caps.c`, `xrdp_rdp.c`, `xrdp_sec.c` — upstream CVE fixes
  (CVE-2026-55639 GCC OOB read, and merged fork hardening).
- `vnc/vnc.c`, `vnc/vnc.h` — CVE-2026-41252 heap overflow + desktop-size
  symbols.
- `sesman/sesexec/session.c` — CVE-2026-55626 (Xvnc UDS TCP disable).
- `librfxcodec` submodule pointer bump.
Rebasing the AVC444 layers onto a freshly fetched `origin/devel` drops all of
these automatically (they are already upstream). After fetch, sanity-check:
the only non-AVC444 file the PR touches should be `common/xrdp_client_info.h`
(`CC_GFX_AVC444 = 6`).

### Excluded from the PR (dev-branch scaffold, keep in dev branch only)
`PR-demo/**`, `tests/xrdp/avc444/repro_mbparity/**`,
`tests/xrdp/avc444/FINDINGS_*.md`, `repro_*.py`, `tools/gen_isoluma.py`,
`PRD.md`, `BACKLOG.md`, `CLAUDE.md`, `*_config.md`, `scripts/build_dev_deb.sh`,
`dist/` debs, and all untracked scratch (burr/partialGreen PNGs, `tester_key`,
`xrdp-PR.tar`, `iptables.rules`, `*.Po`, …). Add a `.gitignore` hygiene pass.
**Keep** `tests/xrdp/avc444/PROVENANCE.md` (the NUT independent-implementation
/ licensing attestation) — fold it into the NUT slice and the PR cover letter;
maintainers will ask.

### Divergence risk: none textual, one semantic touchpoint to verify
The only commits on `origin/devel` past our merge-base (3af31df3..8812646d)
are the 3-commit DYNVC multi-chunk reassembly fix (#3829), touching a single
file, `libxrdp/xrdp_channel.c` — which our branch never touches. Zero conflict
surface, so **do not rebase the dev branch to "derisk"**: there is nothing to
resolve, and the clean-room slices apply onto `origin/devel` (which already
has the fix) as a clean textual apply. One semantic note: large full-screen
AVC444 frames are chunked over drdynvc, and #3829 corrects multi-chunk
reassembly — a correctness fix we *inherit* by basing on `origin/devel`.
Confirm during clean-room smoke that large AVC444 frames reassemble cleanly on
the new base (expected: fine / better; not a risk, just a checkpoint).

### Acceptance
- PR branch = fresh `origin/devel` + the slices below; `git diff` touches only
  AVC444 feature files + `CC_GFX_AVC444`; no CVE/vnc/sesman/submodule noise.
- Every slice builds and `make check` passes on its own (bisectable).
  Re-verified 2026-07-22 after the dump_extra rewrite for the four
  rewritten commits (`04e43ee2` → tip `c74a09e7`): per-slice `make` +
  `make check` green, plus the gated real-ffmpeg suite (64/64) against
  ffmpeg 7.1 and 8.1 at every slice. Slices 1–6 are untouched by the
  rewrite (identical hashes).
- No `XRDP_GFX_TRACE`, no `tail_flush` anywhere in the diff.
- astyle (pinned 3.4.14) + cppcheck clean; `/* */` comments only.

## Commit reorganization plan (clean-room slices) — DRAFT (2026-07-17)

Rebuild the feature as ~10 modular commits, each one subfeature, bottom-up so
every commit compiles and tests green (leaf utilities first, wire integration
last). Each slice carries its own `Makefile.am` / test-registration hunk so it
is self-contained. Suggested order:

1. **caps enum plumbing.** `common/xrdp_client_info.h` (`CC_GFX_AVC444`),
   `xrdp/xrdp_types.h`. No behavior change; the capture-capability constant
   everything else references.
2. **RGB→NV12 dual-plane converter + 16/32 chroma alignment.**
   `xrdp_avc444_convert.{c,h}` + `test_avc444_convert.c`. Pure/deterministic;
   includes the mstsc chroma-split (“burr”) fix via `chroma_align` and its
   `test_avc444_width_align` / `odd_dims` guards. Self-contained leaf.
3. **H.264 Annex-B validator.** `xrdp_h264_annexb.{c,h}` +
   `test_avc444_h264.c`. Pure leaf (NAL header / SPS-PPS-IDR checks).
4. **NUT demuxer.** `xrdp_nut.{c,h}` + `test_avc444_nut.c` +
   `fixture_4frame.nut` + `PROVENANCE.md`. Pure leaf; independent-impl note
   ships with it.
5. **AVC420/AVC444 metablock emission.** The `out_RFX_AVC420_METABLOCK` +
   region-rect serialization in `xrdp_encoder.c` + `test_avc444_metablock.c`.
   (If not cleanly separable from dispatch, fold into slice 8.)
6. **AVC444/AVC420 caps negotiation.** `xrdp_avc444_caps.{c,h}` +
   `test_avc444_caps.c`. Pure logic: pick v2 / 420 from the client capset.
7. **External stock-ffmpeg runner (synchronous encode).**
   `xrdp_encoder_ffmpeg.{c,h}` + `test_avc444_ffmpeg.c`. Spawn/argv incl.
   one-frame `-probesize` and the `dump_extra,h264_mp4toannexb` bsf chain
   (global-header muxer vs encoders with no in-band SPS/PPS repeat —
   h264_nvenc; folded in 2026-07-22 after the T4 finding, branch rewritten,
   slice now `04e43ee2`), NUT read loop, **synchronous** encode + sequence
   verification, resize lifecycle. Built correct from the start (no desync/
   deadlock to “fix later”); the test carries both regression guards plus
   the global-header-encoder probe regression. Depends
   on 2–4. **No tail_flush, no trace.**
8. **Encoder integration / dispatch.** `xrdp_encoder.{c,h}`: select the ffmpeg
   backend, feed converter output, emit metablock + bitstream. Depends on
   2–7. **No trace.**
9. **eGFX caps advertise + wire-to-surface send.** `xrdp_mm.c`: advertise the
   AVC444 capset, connect-time encoder probe, send path. Depends on 6/8.
   **No send/ack trace.**
10. **Config + docs.** `xrdp_tconfig.{c,h}` (`[avc444_ffmpeg]` path/avc_mode/
    encoder_args parse) + `test_tconfig.c` + `tests/xrdp/gfx/*.toml`,
    `gfx.toml`, `xrdp.ini.in`, `docs/man/gfx.toml.5.in`. **No tail_flush.**

Notes: build glue travels with its slice (do not defer Makefile edits to a
trailing commit, or intermediate commits won’t build). The synchronous-encode
+ probesize design is baked into slice 7 as the *initial* implementation — the
dev branch’s desync/deadlock/fix archaeology is intentionally not replayed;
its rationale belongs in the PR description, with `PRD.md` §25 as the
long-form reference.

### Slice-order amendment: latent upstream multimon fix FIRST (2026-07-25, owner directive)

BOTH repos' clean-room slicing must put the **GFX H.264 multimon shmem-split
fix first** (the per-monitor shmem offset fix for the latent UPSTREAM
cross-monitor plane-overwrite bug — see "dual-monitor drag burr" item), and
**rebase the real AVC444 feature work on top of it**, so the merged history
attributes scope and ownership cleanly: the bugfix slice touches only
upstream-reachable code paths (`CC_GFX_A2`/NV12 + the msg-62 offset field +
`XUP_CLIENT_INFO_CURRENT_VERSION` bump) and stands alone as an upstreamable
fix for the pre-existing AVC420-x264 GFX multimon hazard; the AVC444 slices
then inherit the corrected layout instead of appearing to introduce/fix the
bug themselves. Applies to xrdp (slices above renumber after it) AND
xorgxrdp (`feat/avc444-yuv444-capture` rebases onto its fix slice). Keep the
fix slice scoped to the real blast radius (GFX H.264 family), not narrowed
to AVC444. Status: TODO, after the fix lands + owner onscreen PASS.

### New-T4 bring-up + fps-methodology findings (2026-07-26 evening) — measurement campaign record

New T4 (3.83.30.88) brought from bare AMI to deployed per DEPLOY_RUNBOOK:
deps + xrdp `4932908b` + xorgxrdp `251bc4d` debs, nvenc gfx.toml
(dump_extra=true), Xwrapper. ubuntu cred generated on-box into root-owned
`/root/.ubuntu_cred` (never printed). Smoke gate PASS 8/8 both sizes,
edge=1.000, AVC444 v2 probe OK incl. 3840x2400. Owner dual-monitor layout
session validated (2560x1440+594+0 over 3840x2400+0+1440).

**End-to-end fps on the offscreen rig measures the CLIENT, not the server.**
Full evidence chain (thunar orbit, owner layout, uprobes + shipped
XRDP_GFX_TRACE + client stack sampling):

- 4B pair end-to-end: 14.5 fps AVC444; ack-credit window pegged at
  frames_in_flight (2 or 4 — fps identical, knob not binding);
  send(N)->ack(N) p50 260 ms; server admission turnaround (ack ->
  capture+encode+send of freed slot) 18 ms p50; serialized dual-monitor
  encode pair 24+44 ms (per-monitor ffmpeg processes exist but the single
  proc_enc_msg thread + synchronous runner never overlaps them —
  `inflight=0` on every trace line).
- Eliminated: WAN RTT 25 ms (additive only, not stop-and-wait — credit
  loop); TCP queues ~0 both ends; X blit 1-3 ms (x11perf); client CPU not
  saturated (busiest thread 39%).
- Convicted: xfreerdp software AVC444 post-decode path ~65 ms/frame
  serial on one channel thread — stack samples: ~66% `yuv444_context_decode`
  (4:4:4 reconstruction), ~20% `sse41_YUV444ToRGB`, ~14%
  `av_hwframe_transfer_data`. A/B proofs: client /gfx:AVC420 -> 27.1 fps,
  send->ack 113 ms (halved with WAN unchanged); VAAPI hw-decode client
  build (Debian ships `-DWITH_VAAPI=OFF`; rebuilt 3.15.0 WITH_VAAPI=ON,
  hw engaged — renderD128 open, hwframe transfers in stacks) -> 13.9 fps,
  UNCHANGED, because H264 decode was never the dominant term.
- Consequence recorded as PRD FR-PROC-7 clause 9: aux (`LC=2`) send now
  additionally requires spare egfx ack credit — client-declared flow
  control, no new tunables; slow clients ride mains-only (measured 27 vs
  14.5 fps upside), chroma converges on settle (smoke edge check pins it).
- Environment incident (honesty rule): Ubuntu unattended-upgrades replaced
  the nvidia userspace under the loaded driver mid-session (18:37); nvenc
  probe failed -> that login silently matched RFX; caught before use,
  tainted trace discarded. Counter-measure: unattended-upgrades disabled +
  apt periodic off on the T4; rebooted to consistent 580.173; AVC444
  re-verified. T4 rig config MUST NOT change under test.

**Oracle (save-only) client** — `PR-demo/oracle_client/`: FreeRDP 3.15.0
patched so `FREERDP_ORACLE_DUMP=1` makes the AVC420/AVC444 gdi handlers
append each encoded surface payload to `/tmp/oracle_avc_s<id>.bin` and
return success before decode/present: the frame ack then measures
server+WAN only (client contributes ~0), and the dump is splittable into
playable .h264/.mp4. Purpose: measure the true server fps ceiling (and make
Lever-2's encoder-busy condition reachable). Timing-only instrument — the
distro client remains the fidelity/smoke client; the smoke gate never runs
against the oracle.

Status: oracle A/B (4B pair vs pre-4B pair `52099149`+`ee1ec01`, server-only
fps, same rig/orbit) IN PROGRESS; box to be restored to the 4B pair +
re-smoked as the LAST step. Note the pre-4B arm is only measurable at all
because the oracle never decodes (old xrdp lacks the metablock even-extent
fix f0104284 that SIGABRTs strict decoding clients).

### Oracle A/B result: 4B vs pre-4B server-only fps (2026-07-26, T4 3.83.30.88)

Same rig, same orbit (thunar circle, bottom 4K of the owner dual layout),
oracle save-only client (acks at arrival), quiet-gated (glycin storms —
see below), as-shipped config (fif=2 default, no trace), nvenc.

| pair | fps (all stages lockstep) | frame period p50 | structure |
|---|---|---|---|
| pre-4B `52099149`+`ee1ec01` | **24.0** | 34.0 ms | serial: encode 25.4 + ack 7.5 + turnaround; capture ack-released (ack->cap 3.4 ms) |
| 4B `4932908b`+`251bc4d` | **29.1** | 26.5 ms | pipelined: period == ENCODE p50 (26.4 ms); encode-throughput-bound |

4B removes the ack+turnaround legs from the period entirely (period ==
encode). The oracle's ack costs only ~7-14 ms, so the serial arm's penalty
here is small; against a real client whose ack is slower the pre-4B period
grows by that full amount per frame while 4B stays at encode — i.e. 4B
makes server fps client-independent. Next ceiling is the serialized
dual-monitor encode pair itself (24+44 ms in one thread) — exactly what
FR-PROC-7 (halve via credit-gated aux deferral) and, later, cross-monitor
submit overlap address. Oracle dump from the 4B arm splits into playable
streams (277 main + 277 aux 3840x2400 frames / 12 s; aux ~20% of bytes).

Deploy-record notes: avc444_pack_bench on the new T4: 4K vectorized pack
7.89 ms/frame (matches old-box record). Two measurement-validity guards
added after live incidents: (a) quiet gate in t4_measure.sh — fresh logins
AND thunar launches spawn ~10 sandboxed glycin-svg icon loaders (~50
CPU-s, all 4 cores pinned ~18 s; owner-spotted mid-run) — recording now
requires sustained >=85 % idle with no live glycin loaders, checked at
session start AND immediately pre-record; (b) keytest.sh moved to its own
client display :98 with verified Xvfb geometry — sharing :99 with the
layout rig's Xorg produced a false smoke FAIL at 1024x768 (client window
mapped at the rig's +594+0 monitor origin; fixed sample coords read
black; screenshots proved the server rendering perfect red/stripes).
Also: /tmp on the T4 does not survive reboots — staged debs must be
re-copied (a 4B "reinstall" silently no-oped on missing files; caught by
dpkg -l verification; also note git-hash deb versions do not sort, always
pass --allow-downgrades and VERIFY dpkg -l after every swap).

Final state: 4B pair restored and verified (dpkg -l), SMOKE PASS 8/8 at
both sizes edge=1.000 post-restore. FR-CAPTURE-8 fps deliverable: DONE
(29.1 vs 24.0 server-only, +21 % on the oracle rig, client-independent by
construction). Next: FR-PROC-7 with the clause-9 credit gate.

### Lever 2 scope finalized: three policies, one construction (owner directive 2026-07-26)

Task #40 scope per PRD FR-PROC-7 clauses 10-12: implement the shared
SUBMIT/COLLECT state machine (pending-completion records, event-driven
collect, ordered completion emission, finalize-as-preempted error paths)
and all THREE scheduling policies on it — (a) aux PREEMPT (clauses 1-9
incl. the clause-9 credit gate), (b) multi-monitor BREADTH (concurrent
per-child submits; cycle = max not sum), (c) DEPTH (outstanding<=2 per
child; upload/encode overlap). DONE only when all three are functional
and validated TOGETHER (oracle fps + cycle-partition signatures + full
smoke gate incl. edge fidelity on the same build); each policy's
correctness proven individually by deterministic offscreen unit tests
(mocked ffmpeg seam, no GPU, no timers — see PRD clause 12 for the
per-policy test matrix). Measured ladder to verify: 29 -> ~38 -> ~55 ->
toward the 2x11 ms/picture T4 hardware floor.

### macOS Windows App black on nvenc — bisect log (2026-07-26 evening, IN PROGRESS)

Record correction first: the "Mac rendered NVENC from the old T4" memory
traces to one ambiguous PRD sentence; the BACKLOG's own T4 Windows/Mac
client matrix was never completed. Treat Mac x NVENC as a NEVER-VALIDATED
cell, not a regression. The only Mac-green data points are dev-box VAAPI
(High, CQP, no HRD) and x264 in-band.

Evidence chain (all grounded, one Mac reconnect per arm):
- T4 black on BOTH 444 and 420, immediately at connect, clean 16-aligned
  single-monitor sessions included; server pipeline healthy; the Mac
  stops sending egfx frame acks after <=2 frames -> 4B capture gate
  starves (rect_id vs rect_id_ack frozen) -> permanent black. Server
  robustness gap noted separately: 2 lost acks must never deadlock us.
- Arm 2: the exact old-T4 pair (71179f67+e86bff0) on the new T4: BLACK ->
  the whole xrdp range 71179f67..4932908b exonerated.
- Offline QuickTime matrix (9 mp4s incl. the REAL black-arm wire bytes):
  ALL render -> the elementary stream is VideoToolbox-decodable; the
  failure lives in the Windows App's in-RDP annex-b/H264 feeding path.
- Traffic diff good(vaapi)/black(nvenc), both AVC420, identical ffmpeg
  8.0.1: nvenc-only features = per-frame pic_timing+buffering_period SEI,
  nal_hrd VUI, Main profile, level 5.2, refs/dpb 3. Reshape of
  profile/refs/dpb via encoder_args: still black -> those three
  exonerated.
- M1 (dev box, single-delta on the Mac-good server): baseline VAAPI CQP +
  "-rc_mode CBR -b:v 20M -sei +timing" (adds HRD VUI + BP/PT SEI, keeps
  High/5.1/refs1): **BLACK** -> conviction pocket = {HRD VUI in SPS} +
  {buffering_period/pic_timing SEI NALs}. Reverted to baseline
  immediately after verdict (owner protocol: revert after every black,
  keep the live diff single-delta); revert byte-verified via oracle dump
  (nal_hrd=0, no per-frame SEI).
- NEXT: M1a = CBR + "-sei identifier" (HRD VUI, NO BP/PT SEI NALs).
  Renders -> SEI NALs convicted (fix: strip SEI types 0/1 in the runner's
  bsf chain for extradata-only encoders; offline-verifiable). Black ->
  HRD VUI convicted (fix: SPS-level, harder; note nvenc emits HRD even at
  constqp, so rc mode itself is not in the nvenc pocket).

Instruments built tonight: oracle save-only client (PR-demo/oracle_client)
= per-arm byte verification without a Mac; QuickTime offline matrix
(container path) now understood to exonerate only the codec layer, not
the App's RDP path.

### 2026-07-26 late: bisect methodology change — containerized matrix (owner directive)

Two more host-breakage incidents from deb-swap iteration (xrdp-dev deb's
`Breaks: xorgxrdp (<< 1:0.10.80~)` silently removed xorgxrdp-dev and
deleted /etc/X11/xrdp/xorg.conf; an M1a config leaked into the M1b arm).
Owner ruling: NEVER iterate a bisect by mutating the single deployed
instance. New rig (CLAUDE.md "Bisect/diagnosis sessions" + task #42):

- Host restored to the untouched Mac-good baseline and frozen for the
  session: xrdp-dev 52099149 + xorgxrdp-dev ee1ec01 + CQP gfx.toml,
  session creation re-verified (AVC420/ffmpeg matched).
- k3s single-node on the dev box (nested LXC; /dev/kmsg symlink,
  KubeletInUserNamespace, conntrack-max-per-core=0, **native
  snapshotter** — overlayfs pod rootfs breaks credential-changing exec:
  sgid unix_chkpwd dies in ld.so RELRO mprotect EACCES, so PAM denies
  every login; plain-dir snapshots restore normal behavior).
- One pod per arm, each with pinned debs + own gfx.toml (ConfigMaps from
  committed gfx/arm-*.toml), all live simultaneously on loopback
  hostPorts; SERVER SIDE ONLY — client harness on the host untouched.
- PR-demo/mac_bisect_matrix/: Containerfile, entrypoint, banner session,
  per-arm tomls, k8s manifests, build_and_deploy.sh, verify_matrix.sh
  (oracle-client byte verification of every arm before human handoff).
- Matrix v1 (supersedes serial M1a/M1b plan — all arms at once):
  arm-a :40000 CQP baseline (control-good) | arm-b :40001 CBR+timing SEI
  (M1 control-black) | arm-c :40002 CBR+timing SEI+strip_sei (SEI NALs
  stripped, HRD VUI stays; xrdp e96e655416dc) | arm-d :40003 CBR only.
  Verdict rule: C renders => SEI NALs convicted (strip_sei = nvenc fix
  candidate); C black => HRD VUI in SPS convicted; D pins whether plain
  CBR drags in HRD VUI (byte-verify decides).

### 2026-07-27: matrix verdict — HRD VUI in the SPS convicted

Owner tested all four arms in one sitting (Mac, Windows App):
40000/arm-a RENDERS; 40001/arm-b, 40002/arm-c, 40003/arm-d all BLACK.
- arm-c black with SEI NALs 0/12 (byte-verified) => per-frame BP/PT SEI
  NALs EXONERATED as the trigger; the poison is in the SPS itself.
- Field-level SPS diff arm-a(good) vs arm-c(black): the ONLY difference
  is nal_hrd_parameters_present_flag=1 + nal_hrd_parameters() structure
  + low_delay_hrd_flag. timing_info_present=1 in BOTH (exonerated);
  profile/level/bitstream_restriction identical.
- arm-d finding: Mesa emits HRD VUI + per-frame SEI from CBR alone
  ("-sei +timing" redundant) — D was a second control-black.
- CONVICTED: nal_hrd_parameters in the SPS VUI kills the Windows App's
  in-RDP VideoToolbox path (QuickTime plays the same bytes fine).
- Fix candidate (encoder-agnostic => covers T4 nvenc): post-encode SPS
  rewrite clearing nal_hrd (+ strip SEI NALs, which reference HRD).
  NEXT: sanitize_hrd knob on the diag branch, unit-tested against the
  captured arm-a/arm-c SPS bytes, deployed as matrix arm-e :40004.
- arm-e (:40004) added, xrdp-dev c693eeab5ec2 (diag branch): CBR poison
  input + strip_sei + NEW sanitize_hrd knob — xrdp_h264_sanitize_hrd()
  bit-exact SPS splice dropping nal_hrd/vcl_hrd + low_delay_hrd_flag,
  EPB-safe, fail-loud, applied in pop_pair/pop_single (encoder-agnostic
  => same knob is the T4/nvenc fix path). 4 new unit tests incl. golden:
  captured arm-c SPS must rewrite to captured arm-a SPS byte-for-byte
  (87/87 pass). AWAITING: byte-verify arm-e on the wire, owner Mac test.
- 2026-07-27 VERDICT: arm-e RENDERS on the Mac => sanitize_hrd+strip_sei
  is the proven fix. Productized: diag commits cherry-picked onto
  dev/avc444_metablock_checkpoint (88/88 make check), deb versioning
  fixed to monotonic commit-timestamp (bare git hashes broke dpkg
  ordering AND tripped xorgxrdp-dev's contract guard Breaks: xrdp-dev
  << 4932908b8842 against a strictly newer build).
- T4 deploy (bd1ab35b791e + xorgxrdp 251bc4d, one apt transaction —
  installing either dev deb alone REMOVES the other via mutual Breaks):
  conffile protocol held (pre-install snapshot /root/xrdp-conf-backup-*,
  --force-confold, cert.pem/key.pem silently replaced by dpkg and
  restored from snapshot, MANIFEST byte-verified afterwards). gfx.toml:
  exactly two lines added (strip_sei/sanitize_hrd), backup
  gfx.toml.pre_sanitize_hrd. nvenc wire byte-verified through tunnel:
  nal_hrd_vui=0, sei 0/33, reset=[SPS,PPS,IDR] then [P] — the SPS parser
  handled the real nvenc SPS (nvenc emits HRD even at constqp).
- Pack bench (deploy record, T4 Cascade Lake): 3840x2400 8.90 ms/frame
  vectorized (48.58 scalar), 2000x1000 2.29, 500x200 0.09 — unchanged
  from the 4B-era numbers (sanitize path touches only SPS-bearing
  packets, not the conversion loops).
- AWAITING: smoke gate result, then owner Mac test against the T4.
- 2026-07-27 T4 verdict: Mac RENDERS on nvenc AVC420 + sanitize_hrd =>
  fix proven on both encoders. Upgraded T4 to avc_mode="444" (one-line
  gfx.toml change, backup gfx.toml.pre_444_upgrade). Wire byte-verified:
  AVC444 v2 LC=1/LC=2 interleave, single sanitized SPS (nal_hrd=0), SEI
  0 anywhere, aux P-frames share the main parameter sets per the
  single-decoder model — the known-good reframe shape. Smoke gate PASS
  8/8 keys at 1920x1080 AND 1024x768 with color-edge fidelity 1.000
  (vs 0.67 under 420) — full 4:4:4 chroma confirmed through the
  deployed binary+config. AWAITING owner Mac 444 test.
- 2026-07-27 NEW SYMPTOM: Mac renders T4 AVC444 with WRONG COLORS
  (regional hue casts: whites->cyan, magenta streaks — aux chroma
  misassembly signature), while the SAME wire is color-correct on
  xfreerdp (smoke classified 8/8 colors, edge 1.000) and mstsc was fully
  functional earlier. 420 was color-correct on the Mac. Colour VUI
  identical VAAPI vs nvenc (709 full range both) — matrix/range
  declaration exonerated. => Mac's 444 chroma reconstruction vs our aux
  packing. arm-f :40005 added: build 52099149 (pre even-align/4B) +
  VAAPI CQP + avc_mode=444. Mac verdict splits: correct colors =>
  packing regressed in 52099149..bd1ab35b (suspect f0104284 metablock
  even-align); wrong colors => Mac 444 color fidelity never validated,
  investigate ChromaV2 interpretation difference.
- arm-h rework (owner rule: no manual container patching — declarative
  fix only): build script now pairs xorgxrdp per-arm; arm-h =
  xrdp bd1ab35b + xorgxrdp 251bc4d (the T4 pair) — the baked-in ee1ec01
  spoke xup contract 20260726 and sesman rejected logins against
  bd1ab35b's 20260727. Rebuilt+redeployed; login verified, wire matches
  T4 shape (nal_hrd=0, sei=0), AVC444 v2.
- 2026-07-27 G/H verdicts (Mac + Windows UWP): G (old pair 52099149+
  ee1ec01, CQP) fully clean on BOTH clients. H (T4 pair bd1ab35b+251bc4d,
  CBR mimic + strip+sanitize) = Mac wrong color LOCALIZED to regions
  after new damage (clean on connect/resize; block-aligned chroma
  garbage trailing window drags — see caseH_localized_wrong_color png);
  UWP on H connects but blurry text (suspect CBR 20M quality starvation,
  not chroma — UWP wallpaper shows NO wrong-color blocks). T4 (real
  nvenc, constqp): Mac wrong color at immediate connect, UWP fine.
  Reading: incremental-damage aux/metablock geometry regression in the
  new pair; strict Mac blits chroma garbage, xfreerdp/UWP tolerate.
- arm-i :40008 queued: new pair + G's EXACT CQP config (no CBR/knobs) —
  single-delta vs G = deb pair only. Mac wrong-after-damage on I =>
  code pair convicted outright; UWP sharp on I => H blur was CBR config.
- T4 instance TORN DOWN (owner, 2026-07-27) until caseH is fixed on both
  UWP and macOS — /root/.t4_host is stale; smoke gate and T4 scripts
  paused. Re-validation path when fixed: fresh T4 from bare AMI via
  DEPLOY_RUNBOOK (proven in task #39), deb pair install, smoke gate,
  owner Mac+UWP test. Bisect proceeds entirely on the local matrix:
  arm-i :40008 (new pair + G CQP config, single-delta vs G) and arm-j
  :40009 (xrdp 649b447c = 4B commit, pre f0104284-even-align, paired
  251bc4d) split the two suspect commits.
- Hypothesis B CLOSED (2026-07-27): tools/sanitize_hrd_corpus_check.sh —
  19/19 streams (VAAPI CBR x10 resolutions incl. 3840x2400; x264
  nvenc-shaped level5.2/refs3, pic_struct, vbr-hrd, x3 resolutions each)
  pass field-exactness (SPS after == SPS before minus exactly the HRD
  block; bitstream_restriction/max_dec_frame_buffering untouched),
  pixel-exact decode (framemd5), idempotency. The rewrite does not
  corrupt any SPS shape in scope. Hypothesis A (decoder-side main/aux
  pairing slip) is now the lead: arm-K next = deliberate one-frame aux
  delay fault injection for visual signature comparison vs the T4.
- arm-K verdict (owner, 2026-07-27): "40010 didn't wedge, reject" — a
  steady one-frame aux/main slip does NOT reproduce the T4 signature
  (and is visually invisible on xfreerdp): hypothesis A in its simple
  form rejected.
- HONESTY-RULE VIOLATION recorded (owner, 2026-07-27): arm-H ("VAAPI
  pretending to be nvenc") was used as a source of wire-level claims
  about the real nvenc path, and the actual T4 captures were left in
  /tmp and lost to a container restart. Both are now codified in
  CLAUDE.md ("Never diagnose the real component through a stand-in";
  captures archived durably under /work). Corrective action: T4
  relaunched by owner (52.205.130.199), REAL nvenc wire recaptured.
- T4 re-bring-up (2026-07-27, restored AMI at 52.205.130.199): found
  mid-teardown state — xrdp/sesman running from DELETED inodes of old
  c74a09e7d000, all xrdp packages `rc`, no /root/.ubuntu_cred, gfx.toml
  pointing at a (benign, argv-logging) /usr/local/bin/xrdp-ffmpeg-shim.
  Redeployed bd1ab35b791e + xorgxrdp 251bc4d in ONE apt transaction
  (both verified `ii`), conffile snapshot /root/xrdp-conf-backup-*,
  path restored to /usr/bin/ffmpeg, knobs = nvenc constqp qp20 +
  dump_extra + strip_sei + sanitize_hrd + avc_mode 444, ubuntu cred
  regenerated on-box into root-owned /root/.ubuntu_cred (never
  printed), stale-session check clean (sesman loaded 0 sessions),
  xdotool reinstalled (missing on this AMI; smoke harness dependency).
  Deb pair identical to the already-benched bd1ab35b791e build — the
  recorded pack-bench numbers stand (no rebuild).
- REAL nvenc wire facts (2026-07-27, captures archived in
  PR-demo/mac_bisect_matrix/captures/): at identical 3840x2400,
  full-SPS field diff real-nvenc vs Mac-clean arm-I (VAAPI CQP):
  level_idc = 51 on BOTH (earlier "level 5.2" claim was WRONG);
  max_num_reorder_frames = 0 on BOTH (reorder exonerated);
  nal_hrd = vcl_hrd = 0, SEI = 0 main+aux, aux carries no SPS
  (sanitize+strip verified on the real path). The REAL declaration
  deltas: profile_idc 77 vs 100, max_num_ref_frames 3 vs 1,
  max_dec_frame_buffering 3 vs 1, pic_struct_present_flag 1 vs 0
  (+ cosmetic aspect/timing-units/mv-range). Lead hypothesis now
  GROUNDED: dpb=3 permits a conformant decoder (VideoToolbox) to hold
  frames before output; delayed output breaks client-side main/aux
  chroma pairing => wrong color. Clean arms all declare dpb=1.
- FIX-CANDIDATE ARM deployed on the T4 (config-only, real encoder):
  encoder_args += "-refs 1 -dpb_size 1" (-refs alone only reached
  refs/dpb=2). Recaptured wire: refs=1, dpb=1 — buffering declarations
  now byte-equal to the clean arms; full-SPS diff vs the refs3 capture
  shows ONLY those two fields moved (single-axis test; profile 77 and
  pic_struct 1 still differ and remain suspects if the Mac still shows
  wrong color). Stream decode-verified. gfx.toml backups:
  .pre_capture_redeploy, .pre_refs1. SMOKE PASS 8/8 keys at 1920x1080
  AND 1024x768, edge fidelity 1.000, 0 encoder errors. AWAITING owner
  Mac test on the T4 (expected: wrong-color-at-connect gone if the DPB
  axis is the cause).
- DPB axis REJECTED (owner Mac test, 2026-07-27): refs=1/dpb=1 wire
  still full-screen wrong color at connect. Deeper structural inventory
  (PR-demo/mac_bisect_matrix/wire_inventory.py, real captures): the
  interleaved decode-order structure is IDENTICAL between real-nvenc
  and clean VAAPI wires — same [SPS,PPS,IDR],[auxP],[mainP],[auxP]...
  LC1/LC2 alternation, continuous frame_num 0..N across main/aux,
  single slice/frame, POC type 2 both (no reordering possible),
  gaps_in_frame_num=0 both, PPS equal modulo deblock-present + High-only
  tail, ref model equivalent (VAAPI explicit MMCO vs nvenc sliding
  window, both = prev-decode-order-frame reference, which is why both
  emit near-IDR-sized P frames). arm-I clean wire even carries SEI =>
  SEI presence/absence is not the axis. Elimination logic: T4 420 with
  the SAME nvenc VUI rendered clean on the Mac => remaining suspects
  must be declarations whose effect is OUTPUT TIMING (invisible in 420,
  fatal to 444 main/aux pairing). Config ladder continued on the REAL
  encoder: -profile:v high deployed (profile 77->100, chroma fields now
  match clean arm). Full remaining wire delta vs Mac-clean arm-I:
  pic_struct_present_flag 1 vs 0 (BEHAVIORAL suspect — output timing),
  constraint_set4/5, aspect(sq), timing units (same 120fps ratio), mv
  hints (all informational). Wire re-verified sanitized (hrd 0/0, SEI
  0). SMOKE PASS 8/8 both sizes edge 1.000. AWAITING owner Mac test on
  profile-high config. If STILL wrong: next is a one-bit in-place SPS
  rewrite clearing pic_struct_present_flag (no bit-shifting — flag flip
  only), then the last resort is slice-data-level (encoder-internal)
  differences.
- Profile axis REJECTED too (owner Mac test, 2026-07-27): high-profile
  nvenc wire still full-screen wrong color. Owner directive: stop
  one-bit-per-test config permutation; ship a DISCRIMINATING payload.
  Built PR-demo/mac_bisect_matrix/chroma_probe.py ("chroma-probe",
  installed on the T4, python3-tk): luma and chroma carry independent
  readable clocks — numerals/labels are luma-only, timed patches use an
  equiluminant palette (constant Y under BT.709 full, hue rotating in
  U/V only), so ANY main/aux desync is readable off one screenshot:
  fast(1Hz)+slow(1/8Hz) clocks measure chroma lag k in damage-frames;
  frozen patch = aux stalled; legend+named bars detect channel
  swaps/casts; 1px red/blue stripes = 4:2:0-vs-4:4:4; static-vs-motion
  zones split connect-time faults from damage-path faults (caseH).
  Validated end-to-end via xfreerdp3 against the live T4 (correct
  decoder control): clocks tick, hues track indices — reference
  screenshots committed (captures/chroma_probe_reference_xfreerdp*.png).
  Probe left running in the T4 session; owner Mac connect reads the
  failure vector directly. T4 config under test: nvenc profile-high +
  refs1/dpb1 + strip_sei + sanitize_hrd + avc_mode 444.
- Probe delivery reworked per owner rule (2026-07-27): remote GUI
  lifecycle = ONLY (1) whole-session logoff or (2) login autostart —
  codified in CLAUDE.md agent execution rules. chroma-probe is now an
  XDG autostart entry (~/.config/autostart/chroma-probe.desktop for
  ubuntu on the T4, versioned as PR-demo/mac_bisect_matrix/
  chroma-probe.desktop); the stale session was logged off cleanly
  (sesman: "Session on display X11-10 has finished"). Next owner Mac
  connect = fresh login at Mac geometry with the probe fullscreen from
  frame one — the video then captures onset from the very first frames.
  Probe additions since first version: periodic FULL REPAINT EPOCH
  (32 s) as accumulation-vs-poisoned-base discriminator; resize
  adaptation. Also recorded: probe "crash" reports were false — PID
  artifacts of setsid fork + self-matching pkill (the recurring lesson,
  now structurally avoided by the two-operation rule).
- MEASURED VERDICT from owner screen recording (2026-07-27, first-30-
  frames analysis; evidence frames in captures/mac_video_k1_20260727/):
  the Mac applies aux chroma exactly ONE damage-frame late on the real
  nvenc wire. Probe reads: t=15s numeral 1 / in-patch 1 / hue
  palette[0]; t=17s numeral 2 / in-patch 2 / hue palette[1] => k=1,
  constant. Corroborating: wallpaper CHROMA visible under probe-black
  LUMA (BT.709 V~220 at Y=0 renders the observed dark magenta beams) in
  once-damaged regions, frozen; twice-painted static regions converge
  correct (one-late chroma of unchanged content is correct content);
  moving bar leaves trailing bleed (owner's cyan accumulation);
  arm-K non-wedge consistent (server-side -1 aux delay just deepens
  stale chroma to 2 on a static desktop, near-invisible). Config axes
  already equalized when this was measured: profile high, refs=1,
  dpb=1, hrd=0, sei=0. Surviving wire delta with a plausible
  VideoToolbox output-timing mechanism: pic_struct_present_flag=1
  (declared, while pic timing SEI is stripped). NEXT: strip_pic_struct
  knob — in-place single-bit clear in the SPS VUI (no length change),
  golden tests, deb, conffile-safe T4 deploy, owner probe re-read
  (k=0 => fixed; k=1 => pic_struct exonerated, next axes: aspect/
  timing-units/mv declarations, MMCO-vs-sliding-window).
- strip_pic_struct knob implemented (2026-07-27): sanitize_walk refactor
  in xrdp_h264_annexb.c (shared SPS walk; sps_rewrite_nal takes
  strip_hrd/strip_ps flags; pic_struct_present_flag is the bit at
  hrd_end, forced to 0 with everything else copied bit-exact),
  xrdp_h264_strip_pic_struct() public, plumbed gfx.toml
  [avc444_ffmpeg] strip_pic_struct -> tconfig -> mm (probe-latched) ->
  encoder -> ffmpeg runner (fail-loud on both pop paths, like
  sanitize_hrd). Verified against the REAL captured nvenc stream:
  ffmpeg trace_headers field diff = ONLY pic_struct 1->0, framemd5
  pixel-exact, idempotent (single byte 0x13->0x11, length unchanged).
  Unit tests: golden clear (real T4 nvenc SPS vector), zero-flag
  untouched (arm-a VAAPI vector), truncated fails - 91/91 make check,
  astyle clean. Purpose: falsify the VT-output-pacing explanation of
  the MEASURED k=1 aux lag; deploying to the T4 for the owner probe
  re-read.
- strip_pic_struct DEPLOYED to T4 (2026-07-27): deb pair
  57a27245b362 + xorgxrdp 251bc4d one transaction (both ii), conffile
  protocol: dpkg silently replaced cert.pem/key.pem AGAIN — and
  comparison against the 19:08 backup shows that install had replaced
  them too and the miss went uncaught (earlier check was inconclusive;
  protocol slip recorded). Restored from the immediate pre-install
  snapshot (the pair the owner's Mac used all evening). Live wire
  re-verified: profile 100, hrd 0, SEI 0, dpb 1, pic_struct_present_flag
  now 0 on the real nvenc stream. Smoke gate first run FAILED red
  (login failure) — root cause a STALE keytest ssh tunnel holding
  127.0.0.1:33890 from the previous smoke run, keytest's own tunnel
  could not bind; killed by PID, rerun: SMOKE PASS 8/8 both sizes,
  edge 1.000, 0 encoder errors. AWAITING owner Mac probe re-read
  (k=0 and no residue => pic_struct convicted; k=1 persists =>
  exonerated, next axes aspect/timing-units/MMCO).
- pic_struct REJECTED (owner live, 2026-07-27 evening): bleed persists
  with pic_struct_present_flag=0 verified on the live nvenc wire.
  Owner also observed occasional bleed RESETS mid-epoch, not aligned to
  the probe's 32s flashes — matching the -g 240 IDR cadence. This
  CONFIRMS the decoder-state divergence model and exposes a probe
  design flaw: the EPOCH flash repaints identical content, the encoder
  skip-codes it, and skip blocks are exactly what propagate the
  client's poisoned reference — only an IDR replaces every MB
  unconditionally. Consequences: (1) top remaining axis = reference
  marking (VAAPI clean wire: explicit MMCO ops per P slice; nvenc
  broken wire: sliding window) — locally falsifiable by stripping MMCO
  from the clean VAAPI stream (new diagnostic knob, single-delta arm);
  (2) probe v3: double-strobe static repaint (second pass with 1-LSB
  tweak forces a second aux update => k-immune correct baseline zone).
- fault_strip_mmco knob implemented (2026-07-27): xrdp_h264_strip_mmco
  in the annexb module — SPS/PPS param cache + non-IDR ref slice
  header rewrite (adaptive MMCO op list -> sliding-window flag, CABAC
  alignment re-padded, entropy payload byte-verbatim, whole-NAL
  unescape/re-escape, fail-loud on any shape our encoders don't emit;
  never grows the buffer). OFFLINE VALIDATION on the captured clean
  arm-i wire (shared cache, runner call pattern): main 6 MMCO trace
  lines -> 0, aux 0 adaptive flags remain, BOTH streams decode
  pixel-exact (framemd5; aux via spliced parameter sets). Plumbed as
  DIAGNOSTIC gfx.toml fault_strip_mmco (WARNING at latch), same chain
  as fault_aux_delay. Unit-test note: no in-tree unit vector (a valid
  CABAC P slice is impractical to embed); coverage is the ffmpeg-
  validated capture run recorded here + fail-loud runtime contract.
  Purpose: arm-l = arm-i's Mac-clean VAAPI CQP config + this knob =
  single-delta reference-marking arm on 127.0.0.1:40011.
- arm-K :40010 live (xrdp 8b8d17c2636a + 251bc4d, CQP 444 +
  fault_aux_delay=true, WARNING-logged). Key datapoint already: the
  deliberate one-frame chroma slip is INVISIBLE through xfreerdp
  (screenshots clean at connect burst and on menu damage) — matching
  the pattern where the T4 wire renders fine on xfreerdp/UWP but wrong
  on the Mac. A pairing slip is only visible to strict reconstructors;
  Mac connect to :40010 decides whether its visual signature matches
  the T4 (wedge + discolor at connect).
- k3s snapshotter native -> fuse-overlayfs (owner directive 2026-07-27,
  "20 minutes on new arm deployment must be resolved"): native
  full-copied the ~100k-file rootfs at first container create per
  image; fuse-overlayfs (userspace, avoids the kernel-overlayfs sgid
  bug) unpacks layers once and mounts overlays. Gates after the
  switch: real RDP PAM login OK (sgid unix_chkpwd regression absent),
  timed pod re-create 8s (was minutes; new-image first create = one
  layer unpack, a few minutes worst case). Fleet re-created 12/12.
  Config comment updated in /etc/rancher/k3s/config.yaml with the
  rollback tell (PAM login failures -> suspect snapshotter first).
- MECHANISM PROVEN (2026-07-27, offline, no live arms): cross-view
  inter prediction in the single-context AVC444 interleave. Full
  static-analysis proof + reproduction commands in
  PR-demo/mac_bisect_matrix/CROSS_VIEW_REFERENCE_PROOF.md. Summary:
  our one-encoder interleave makes every frame's previous decode-order
  frame the OTHER view; nvenc emits ~300 cross-view inter MBs per P
  frame in flat regions (VAAPI CQP emits ZERO inter MBs — clean arms
  were immune by accident); a client decoding the views per-view
  resolves those MBs against same-view references -> wrong prediction
  base -> chroma-dominant error (luma clips at black) that compounds
  through the DPB (temporal) and intra prediction (spatial smooth
  down-right beams), heals only at IDR, skip-coded EPOCH repaints
  cannot heal. Reproduced deterministically in ffmpeg: per-view decode
  of the committed T4 main stream diverges from correct-topology
  decode at P frame 1 (60% pixels off) saturating ~83% / mean |d|~120
  by frame 10 (background ROI black -> magenta 240,80,247, matching
  the Mac video beams); arm-L per-view decode is bit-identical
  (all-intra). The owner's monkeypatching veto was correct: header
  knobs could never fix a payload/topology defect. arm-H's reported
  local bleed is NOT explained (its capture is all-intra) and stays
  quarantined under the stand-in rule.
- TODO (awaiting owner sign-off): AVC444 per-view encoder contexts —
  encode main/aux in two independent encoders (own DPB, own frame_num,
  aux carries SPS/PPS + IDR cadence), removing cross-view references
  structurally; correct for single-decoder AND per-view clients.
  Acceptance: per-view ffmpeg decode of BOTH emitted streams is
  bit-identical to interleaved decode on a probe corpus; in-tree unit
  test asserts aux independence (SPS/PPS+IDR present, per-view
  frame_num); T4 perf re-recorded per deploy rule; Mac onscreen
  validation last.
- RECONCILIATION with the two-encoder rejection (owner challenge,
  2026-07-27): the rejected-thread note (this file, ~line 171; PRD
  §6.5; vm/GROUND_TRUTH_win2022_avc444.md) stands and the "two
  independent encoder contexts" TODO above is WITHDRAWN — two
  processes = two frame_num chains + duplicate SPS into the client's
  single decoder = desync garbage on Windows/xfreerdp, exactly as
  recorded. New ground-truth measurement closes the apparent
  contradiction: the real Win2022 wire is ONE chain (every frame,
  main AND aux, is an nri=3 reference P on one continuous frame_num
  sequence; SPS max_num_ref_frames=3/dpb=3) yet it is
  REFERENCE-PARTITIONED: decoding gfxwin_anim with ALL 9 aux AUs
  dropped leaves every one of 348 main frames BIT-IDENTICAL (mean 0,
  max 0) to the full interleaved decode. Single chain != cross-view
  prediction: Microsoft's encoder keeps a 3-deep DPB so same-view
  references are always available and never predicts main from aux.
  Our wire copies the chain structure but not the reference
  discipline (ffmpeg-CLI nvenc picks the cross-view adjacent frame,
  ~300 MBs/frame in flat regions) — so ours corrupts under ANY client
  deviation from strict in-order single-decoder feeding (drop, defer,
  per-view), while Windows' wire is provably robust to all of them.
  This also means the Mac client's exact behavior (dropper vs
  per-view) is no longer decidable from our data and no longer
  matters: the fix target is the Windows property, not a client
  model.
- TODO (replaces withdrawn two-context item; needs owner sign-off):
  AVC444 reference partitioning within the SINGLE encoder chain —
  main frames must never reference aux frames (and aux never main
  where avoidable). Candidate mechanisms to evaluate: (a) aux frames
  as non-reference (nri=0, excluded from DPB; main chain then
  self-links even at refs=1) — needs deterministic per-frame non-ref
  control (nvenc enableNonRefP / VAAPI / x264 equivalents; ffmpeg
  -nonref_p is "automatic", must verify determinism or find a
  per-frame path); (b) Windows cadence (Lever 2 / FR-PROC-7,
  LC=1-dominant + rare LC=2 catch-up) shrinks exposure ~14x but alone
  does not eliminate cross-view refs at insertion points; (c) refs>=2
  alone is NOT sufficient (original broken T4 wire was refs=3: nvenc
  still picked cross-view refs in flat regions). ACCEPTANCE = the
  ground-truth robustness test: decode our wire with all aux AUs
  dropped and per-view; main frames must be bit-identical to the
  interleaved decode (same test that passes on gfxwin_anim), run as
  an offline corpus check before any Mac onscreen validation.
