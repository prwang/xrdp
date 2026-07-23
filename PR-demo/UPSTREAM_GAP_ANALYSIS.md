# Upstream gap analysis — the case for AVC444 via stock ffmpeg

**What this document is.** Evidence from the upstream trackers that the problem
this PR solves is real, user-reported, maintainer-acknowledged, and unfilled.
It replaces the old PRD §26 "related work" survey.

**Method / provenance.** All issues, PRs and discussions below were fetched
directly from the GitHub REST API for `neutrinolabs/xrdp` and
`neutrinolabs/xorgxrdp` on **2026-07-22** (search sweeps over
AVC444/YUV444/chroma/blurry/H.264/GFX/ffmpeg/vaapi/nvenc/hardware-encoding plus
full comment threads for the load-bearing items). Quotes are verbatim from the
fetched JSON. Raw captures: `/tmp/upsurvey/*.json` (box-local, regenerable).

**The gap in one sentence.** Upstream xrdp's H.264 GFX path is AVC420-only —
users have reported the resulting chroma-subsampled ("fuzzy red text")
rendering for over a year, the maintainer has said three times that YUV444 is
the fix, and every attempt to land AVC444 or a flexible encoder backend has
stalled out of tree — this PR ships both.

---

## 1. The user-visible problem: chroma-subsampled text (xrdp #3375, open)

["Bad quality visuals when using x264"](https://github.com/neutrinolabs/xrdp/issues/3375)
(opened 2025-01, **still open**, 22 comments, multiple independent reporters
with screenshots) is the flagship demand thread:

- Reporter (sshaikh): *"'Fuzzy' rendering of graphics, particularly fonts. Most
  seen with red writing on black"*; green text shows *"a fringe"*.
- Diagnosis (pnowack, gnome-remote-desktop developer): *"What you experience
  here is the result of subsampling, as the H.264 encoded stream uses a YUV420
  surface for the source frame."* Thin red-on-black / red-on-blue text is the
  canonical reproducer — chroma is decimated on 2×2 grids.
- It worsens under motion (Varbin): scrolling a terminal is *"sometimes leading
  to completely unreadable text"*.
- Corroborating reports: ThelloD (*"The only issue is red text, particularly
  bold red text"*), tsz8899 (at 1920×1080 *"text and icon edges feel blurred …
  especially in specific colors like red"*).

The maintainer's position, stated **three separate times** in that thread
(metalefty): *"Also we need to support YUV444 mode for the fundamental quality
enhancement"* (2025-01-06); *"YUV444 is needed for further quality I'm afraid"*
(2025-01-08); *"As I mentioned just above, we need YUV444"* (2025-01-23).

What users do today, absent AVC444 — every option is bad:

1. **Fall back to RFX** — maintainer-documented workaround, at the cost of
   *"huge bandwidth (100 Mbps or more)"* (metalefty, 2025-04-02); and RFX has
   its own ceiling ([xorgxrdp #392](https://github.com/neutrinolabs/xorgxrdp/issues/392),
   open: "RFX can't sustain 60FPS", whose reporter is on AVC420 *despite* the
   quality because RFX can't keep up). See also
   [xrdp #3489](https://github.com/neutrinolabs/xrdp/issues/3489): "H.264 more
   slaggish and blurry than RFX".
2. **Hand-patch the color matrix** — late in #3375, users patch
   `rdpCapture.c` with AI-generated BT.601-limited conversion code (needing
   `--without-simd` to even take effect) to blunt the fringe; a follow-up
   tester found it *"demolishes color accuracy"*. Users are trading color
   fidelity for legibility because the real fix (4:4:4) isn't available.
3. **Run an out-of-tree fork** (next section).

Direct evidence AVC444 fixes it — an A/B posted *in the issue* (tsz8899,
2025-04-23), upstream AVC420 vs the Nexarian AVC444 fork: *"When using Nexarian
mainline_merge_avc444, the clarity of red text is acceptable. **Is it possible
to integrate the avc444 feature of Nexarian?**"* — to which the maintainer
replied: *"Yes, sure! Securing sponsors would help us speed things up."*

## 2. Why the gap is still open: every AVC444/encoder-flexibility attempt stalled

The demand is old and the code has existed out of tree for years; none of it
landed:

- **Nexarian's `mainline_merge_avc444` fork** is the only place AVC444 has ever
  worked. Its xorgxrdp side was PR'd upstream once —
  [xorgxrdp #255](https://github.com/neutrinolabs/xorgxrdp/pull/255) ("Mainline
  merge avc444") — and **closed the same day by its author, unmerged**
  (2023-03-31; withdrawn, not maintainer-rejected). Users run the fork anyway
  to get AVC444/GPU encode and hit unsupported territory
  ([xrdp #2635](https://github.com/neutrinolabs/xrdp/issues/2635): fork breaks
  with multi-monitor). What "not stable" meant concretely is dissected in §2a
  below.
- **The GFX mainline merge deliberately left the 444 code out.** When the egfx
  work was merged to `devel` (PR #2891 / discussion
  [#2383](https://github.com/neutrinolabs/xrdp/discussions/2383)), jsorg71
  listed what was dropped: *"yuv 444 bits because, like Nex said, not stable
  yet"*; Nexarian: *"4:4:4 NVENC doesn't yet work stably on XRDP."* The merged
  [openh264 PR #3311](https://github.com/neutrinolabs/xrdp/pull/3311) was
  *"taken from … mainline_merge_avc444 and modified"* — i.e. reduced to AVC420.
  The `XRDP_yuv444_v1/v2_stream` capture constants sit unused in `devel` today;
  there is **no AVC444 encoder anywhere in upstream `devel`** (verified against
  the source at base `8812646d`).
- **Progress is sponsorship-bound.** Nexarian (2025-05-04, #3375): *"The
  combination of lack of sponsorship for this and Microsoft's bad
  implementation for AVC444 on the Mac OS client are going to stall progress
  here."* Same pattern on next-gen codecs
  ([xrdp #3769](https://github.com/neutrinolabs/xrdp/issues/3769), AV1):
  *"we had a sponsor, but unfortunately the sponsorship was canceled. The
  priority is no longer high"* (metalefty, 2026-03).
- **The closest active encoder PR is stalled and is AVC420-only anyway.**
  [xrdp #3774](https://github.com/neutrinolabs/xrdp/pull/3774) (FlyGoat,
  open 2026-03) links `libavcodec` in-process for VAAPI/Vulkan AVC420. After
  jsorg71 explained the constraints (keep GPU APIs in `xrdp_accel_assist`; and
  the two hard problems: *"MS uses a non standard color conversion matrix"*
  and *"the YUV444 algorithm in GFX that for sure needs custom GPU code"*),
  the author withdrew the approach: *"I'll give up FFMpeg given that it's
  indeed a source of headache, but I'd like to keep Vulkan and DMA-BUF."*
  Neither #3774 nor its accel_assist successor addresses AVC444 or the color
  matrix — the two things jsorg71 named, and the two things this PR implements
  (in tested CPU code, not "custom GPU code").

This PR needs no sponsorship pipeline, no fork, and no new GPU API surface in
xrdp: the AVC444 assembly, MS color matrix, caps negotiation (v2/v1/AVC420
fallback per client), and a working encoder path arrive together, tested.

## 2a. Forensics: what exactly was "not stable" in the prior attempt

There was no failed upstream test run to point to — xorgxrdp #255 had zero
review comments and the 444 code never entered upstream CI. The recorded
failure is a **field symptom that was never root-caused**, documented only in
prose — **no screenshot of the garbling was ever posted** (checked all 154
comments of #2383 and the #3375 thread). The three primary-source statements,
all by Nexarian:

1. [#2383, 2025-04-28 03:46 UTC](https://github.com/neutrinolabs/xrdp/discussions/2383#discussioncomment-12964900):
   *"4:4:4 NVENC doesn't yet work stably on XRDP. I had an old branch where I
   tried it: https://github.com/Nexarian/xrdp/tree/mainline_merge_avc444 —
   The output on the Mac OS client was garbled, and I never figured out
   why."*
2. [#2383, 2025-04-28 16:16 UTC](https://github.com/neutrinolabs/xrdp/discussions/2383#discussioncomment-12972036):
   *"Microsoft has a bug in their 'Windows App' (I hate that name) on Mac OS
   that basically doesn't work with this branch. … FreeRDP and MSTSC seemed
   fine with my 4:4:4 implementation, however. **But Mac OS is important
   enough that it blocked the rollout of this feature.** I haven't been able
   to discuss this with Microsoft yet."*
3. [#3375, 2025-05-04](https://github.com/neutrinolabs/xrdp/issues/3375#issuecomment-2848883434):
   *"The combination of lack of sponsorship for this and Microsoft's bad
   implementation for AVC444 on the Mac OS client are going to stall progress
   here, I'm afraid."*

Quote 2 is the load-bearing one: the Mac Windows App was **the stated blocker
for the entire AVC444 rollout** — while working on FreeRDP and MSTSC — and
the "Microsoft bug" attribution was never verified (no packet capture, no
screenshot, no Microsoft contact). Supporting user reports: the fork's 444
showed *"no major difference"* through xfreerdp in one test (tabletseeker —
consistent with FreeRDP being a lenient decoder that masks 444 defects), and
*"the load was too high"* (tabletseeker, 2025-08).

Reading the fork's source (`Nexarian/xrdp` branch `mainline_merge_avc444`,
`xrdp/xrdp_encoder.c` + `xrdp_mm.c`, fetched 2026-07-22) identifies four
concrete defects consistent with "garbled on a strict client, fine-ish on
mstsc, invisible on FreeRDP":

| # | Fork defect (file:evidence) | Failure it produces | This PR's counterpart |
|---|---|---|---|
| F1 | **AVC444 pair split across two GFX frames** in the NVENC path: luma PDU, then `frame_end` / `frame_id++` / `frame_start`, then the chroma PDU (`xrdp_mm.c`, `xrdp_mm_process_enc_done`). The halves carry `LC=0x01` / `LC=0x02`, each above a literal `// TODO: Specify LC code here` (`xrdp_encoder.c`, 3 sites). | The client may present after the luma-only frame, and frame-level ack pacing/drops can apply chroma to the wrong luma — timing-dependent garbling. Matches "NVENC 444 unstable" + Mac garbling that mstsc's pacing mostly hides. | Both views packed in **one** `RFX_AVC444_BITMAP_STREAM`, `LC=0`, one PDU, one frame — a split pair is structurally impossible (`xrdp/xrdp_encoder.c` LC=0 serializer; PRD §1). Pair integrity further pinned by the synchronous encode + `desktop_sequence` verify (regression-tested, `tests/xrdp/test_avc444_ffmpeg.c`). |
| F2 | **No AVC444 caps gating**: `#define AVC444 1` compile switch; any client that qualifies for H.264 — including **CAPVERSION 8.1**, whose capset per MS-RDPEGFX supports AVC420 only — is sent `AVC444V2 (0x000F)` unconditionally (`xrdp_mm.c` caps loop + send path). | Sending 0x000F to a capset that never advertised it = undefined client behavior. A then-lagging Mac client negotiating 8.1/10.x-low is a prime garbling candidate. | Per-client classifier `xrdp_avc444_classify_caps` + `xrdp_avc444_caps_supports_v2`: 8.1 → AVC420 only, v10 → v1, v10.1+/10.2–10.7 → v2, `AVC_DISABLED` honored — unit-tested including exactly the 8.1-never-gets-444 case (`tests/xrdp/test_avc444_caps.c`). |
| F3 | **Metablock region rects emitted raw** — no origin alignment (`build_rfx_avc420_metablock` writes `rrects` verbatim). | Odd region origins flip chroma parity on region-strict decoders (mstsc/mstscax/RD Client family) → edge fringing on every update. | Origin even-alignment in the shared emitter, both linked-x264 and ffmpeg paths — unit-tested (`tests/xrdp/test_avc444_metablock.c`) + 3-level reachability proof (`AVC444_metablock_reachability_PROOF.md`). |
| F4 | **Software 444 = double encode with no low-latency contract** (pipelined thread + FIFO, the frame-ack-drift design the #2891 merge thread fought as "green banding"). | "Load was too high"; stale/withheld frames under encoder delay. | Synchronous runner with deterministic failure (restart loudly, never serve a stale pair); hardware encode via any ffmpeg encoder by config — VAAPI validated end-to-end on this project's rig. |

Honesty box — what we can and cannot claim:

- **Cannot claim:** "the Mac client now renders 444 correctly." Nobody
  root-caused the Mac garbling (Nexarian attributes it to a Microsoft client
  bug; his own code had F1/F2 confounders, so the attribution is unproven).
  We have not run the macOS Windows App against this branch. What we can
  claim: the two server-side protocol hazards that could produce it (F1, F2)
  are structurally eliminated and unit-tested, and a client that does not
  advertise 444 caps is never sent 444 — worst case it gets working AVC420.
- **Cannot claim yet:** NVENC stability (his unstable combo) — our NVENC path
  is config-only and untested on this rig (no Nvidia GPU); and subprocess
  latency vs linked x264 is unbenchmarked (`PR-demo/RESULTS.md` P3).
- **Verified live:** AVC444 v2 on mstsc (region-strict) — the v1 chroma fringe
  drops 50.4% → 0.6% of affected pixels
  (`tests/xrdp/avc444/FINDINGS_magenta_burr.md`), and the resize-comb
  chroma-split defect was root-caused and fixed
  (`tests/xrdp/avc444/repro_mbparity/FINDINGS_mstsc_split.md`).

### Required tests to fully retire the prior-attempt failure surface

Already in tree (each mapped to a fork defect): `test_avc444_caps.c` (F2),
`test_avc444_metablock.c` (F3), `test_avc444_convert.c` (ChromaV2 U|V split
geometry — the mstsc comb), `test_avc444_ffmpeg.c` (pair sync + probesize
regressions, F1/F4 class), `test_avc444_h264.c`/`test_avc444_nut.c` (bitstream
validity), plus the two-resolution live smoke gate.

Still required:

1. **Wire-layout unit test for the AVC444 PDU** (gap found while auditing the
   fork): assert one PDU, `LC=0`, `cbAvc420EncodedBitstream1` == metablock +
   sub-stream-1 length, both views present, single frame — the direct
   regression guard against F1 ever reappearing. **DONE 2026-07-22** (dev
   branch): `out_RFX_AVC444_BITMAP_STREAM` extracted and unit tested
   (`test_avc444_metablock.c`, `avc444_wire` tcase); clean-branch port
   folds into the slices per `BACKLOG.md` (no separate fix commit).
2. **Client matrix, live**: mstsc onscreen A/B (planned, region-strict);
   **macOS Windows App** session — the exact client that garbled; without this
   run, claim containment (F2 gating), not resolution. iOS/Android RD Client
   optional. The Mac run is disproportionately valuable because quote 2 in
   §2a makes the Mac client *the* stated rollout blocker, and either outcome
   advances the PR: clean rendering removes the blocker that killed the prior
   attempt; garbling on our spec-conformant, mstsc-verified stream isolates
   the fault to the client (turning Nexarian's unverified attribution into an
   evidenced one) and justifies a documented per-client policy (Mac gets v1
   or AVC420 via the caps classifier / config) instead of an open mystery.
   Either way the test also documents empirically which capsets the Windows
   App advertises — the input our classifier keys on.
   **First Windows App data point (Android, SM-S936U, 2026-07-23, live):**
   the client advertised `AVC_DISABLED` (0x20) on every v10 capset it
   offered (10.0/10.2/10.3/10.4, flags 0x22/0x20), no `AVC420_ENABLED` on
   8.1 (flags 0x02), plus `0x000B0101`/`0x000B0300` capsets with flags
   `0x1a2`. Our classifier honored it and the session correctly ran RFX
   with zero H.264 negotiation — exactly the F2 guard working (the fork
   would have sent 0x000F unconditionally to this client). Implication
   for the Mac run: the macOS Windows App may likewise advertise
   `AVC_DISABLED` toward non-AVD servers, which would make the historical
   garble unreproducible on current clients and the captured capsets the
   deliverable; check for a client-side H.264/hardware-decode setting
   before concluding.
   **Second Windows App data point (Windows desktop/UWP, dual monitor,
   2026-07-23, live):** the DESKTOP variant advertised the full ladder
   8.0–10.7 with flags `0x0` — i.e. **no `AVC_DISABLED`**: caps-wise this
   client is AVC444v2-eligible, unlike its Android sibling. H.264 was
   skipped in this session only by our documented single-monitor
   eligibility gate (`monitorCount == 2`; no probe attempted — distinct
   log signature from a caps refusal). It also advertised `0x000B0101`
   flags `0x0`, `0x000B0200` flags `0x400`, `0x000B0300` flags `0xc00`,
   and **`0x000B0500` flags `0x2c00`** — a capset version and flag bits
   (`0x400`/`0x800`/`0x2000`, one new bit per v11.x step) that appear in
   NO public source as of this capture. Cross-client comparison also
   validates the `_DISABLE` polarity of bits `0x80`/`0x100`: set by the
   mobile client (features it lacks), clear on desktop.
   **Verified publicly unidentified (exhaustive sweep 2026-07-23):**
   `0x000B0500` (and any `0x000B0400`) and flag bits
   `0x400`/`0x800`/`0x2000` are absent from FreeRDP master (enum ends at
   `RDPGFX_CAPVERSION_113 = 0x000b0300`, added June 2026 PR #12871
   "Azure undocumented stuff"; no later gfx PR through #13077 touches
   capsets), Wireshark master (version table ends at "11.3"), IronRDP
   (stops at 10.7), MS-RDPEGFX (latest revision remains v20260511;
   errata unchanged since 2023), Sourcegraph global code search and
   GitHub issue/PR search (zero hits for `0x000B0500` /
   `RDPGFX_CAPVERSION_115` in any RDP context) — **this capture appears
   to be the first public record.** Feature candidates for the bits
   (INFERRED, no public mapping): HEVC decode (AVD GA'd HEVC June 2025,
   Windows App >= 2.0.503.0; FreeRDP maintainers suspect 11.x gates it,
   issue #12846) and screen-capture-protection/watermark rendering (the
   Azure-only `PROTECT_SURFACE 0x0019` / `WATERMARK 0x001A` commands,
   FreeRDP PR #12872). The accretion-per-version reading (11.2→0x400,
   11.3→0x800, 11.5→0x2000, desktop-set = positive capability bits) is
   ours alone.
   **Third Windows App data point (macOS, iMac, 2026-07-23, live):** the
   Mac variant — THE historical blocker client — connected and ran our
   external-ffmpeg **AVC420** GFX stream, including three client-driven
   dynamic resizes (~20 ms each, fresh encoder generation per resize, no
   errors). AVC420 was forced by server config (`avc_mode = "420"`), not
   by caps: the Mac advertised a third distinct flags profile —
   `AVC_DISABLED` on 10.0/10.2/10.3 but CLEAR on 10.4 (`0x02`) and 10.7
   (`0x82` = SMALL_CACHE|SCALEDMAP_DISABLE) — so the classifier rated it
   AVC444-capable (10.7 confirmed). Like Android it offered only
   `0x000B0101`+`0x000B0300` (no 0200, no 0500), flags `0x82`. The
   decisive AVC444 run (same client, `avc_mode = "auto"`) followed the
   same day (below). **AVC420 on the Mac is owner-verified fully
   functional (2026-07-23): first connect and dynamic resize both
   render correctly** — matrix outcome 4 (baseline H.264 broken) is
   excluded; the defect below is isolated to the 444 layer.
   **AVC444 v2 result (same iMac, avc_mode=auto, 2026-07-23): REPRODUCED
   the historical Mac failure on our spec-conformant stream.** The
   client negotiated AVC444 v2 (0x000F, capset 10.7 confirmed) and then
   rendered near-black frames with a garbled color-noise strip along
   the top edge and faint ghost structure in the field (evidence:
   `PR-demo/mac_windows_app/avc444v2_blackout_2026-07-23.png`). The
   session stayed alive (client-driven dynamic resize 8 s in), so
   transport/decode-init are fine — the client decodes and composes the
   dual view wrongly. Fault isolation: the SAME v2 stream renders on
   mstsc (region-strict) and xfreerdp, AVC420 renders on this SAME Mac,
   and our wire layout is unit-asserted (single PDU, LC=0) — i.e.
   Nexarian's unverified "Microsoft has a bug in their Windows App on
   Mac OS" attribution is now EVIDENCED, minus his F1/F2 confounders.
   Discriminator: `avc_mode = "444v1"` knob added (pin codec id 0x000E,
   suppress ChromaV2) to determine whether the defect is
   v2-aux-packing-specific or all-AVC444; either result yields a
   shippable per-client policy via config.
   (0x2) | `AVC_DISABLED` (0x20) | `SCALEDMAP_DISABLE` (0x80 — public,
   MS-RDPEGFX v20260511 §2.2.3.10: scaled-output/scaled-window surface
   mapping unsupported) | `0x100` — absent from the spec; FreeRDP master
   (`rdpgfx.h`, "11.0+, undocumented, Azure only", PR #12871) and
   Wireshark both name it `RDPGFX_CAPS_FLAG_SCP_DISABLE`, almost
   certainly Screen Capture Protection (Microsoft's own "SCP"; pairs
   with the undocumented Azure EGFX commands `PROTECT_SURFACE` 0x0019 /
   `WATERMARK` 0x001A), exact semantics unpublished. The `0x000B*`
   capset versions are acknowledged in MS-RDPEGFX v20260511 only in
   Appendix A note <5>: on OS builds *without* KB5089573/KB5089570 they
   behave exactly as VERSION107 (0x000B0300 recognized only by Win11
   26H1); what they gate *with* those KBs is unspecified — FreeRDP
   maintainers suspect HEVC/H.265 (issue #12846). No 0x000B capset is
   normatively defined; our classifier's conservative skip-unknown
   behavior is correct.
3. **NVENC configuration run** on Nvidia hardware (the fork's unstable
   combo). **DONE 2026-07-22** (owner-validated, Tesla T4, ffmpeg 8.0.1,
   driver 580.159.03): probe OK, persistent encoder child, correct
   display, `nvidia-smi` shows the session ffmpeg as a GPU compute
   process; small sizes OK, no chroma fringe (region-strict client).
   Surfaced and fixed a real portability defect in the process (NUT
   global-header mode vs encoders without in-band SPS/PPS repeat —
   dump_extra now chained in `build_argv`, regression-tested).
4. **Latency/load benchmark vs linked x264** (RESULTS.md P3) — answers "load
   too high" quantitatively.
5. **Soak + resize storm** against the deployed binary (encoder restarts = 0
   over hours; resize destroys/recreates the child) — extends the current
   smoke gate's per-login checks.

## 3. The second gap: encoder coupling breaks users at runtime

Upstream's H.264 backends are compile-time-linked (`--enable-x264` /
`--enable-openh264` / NVENC via accel_assist), which pushes codec problems onto
distros and end users:

- [xrdp #3711](https://github.com/neutrinolabs/xrdp/issues/3711) ("RHEL: xrdp
  not working with H.264 codec", 26 comments, 2026-01): RHEL 9 ships a
  `noopenh264` **stub** and a different openh264 version than EPEL built xrdp
  against, so H.264 silently dies with *"OpenH264 Codec is not installed
  correctly. H.264 will not be used"*. Maintainer (matt335672): *"xrdp is part
  of EPEL however, xrdp will be built against the version of openh264 which
  ships with EPEL"* — the linked-library ABI contract is exactly what broke.
  Resolution for the user: give up on H.264 (reorder gfx.toml to Xorg/RFX).
- Same family of pain: [xrdp #3141](https://github.com/neutrinolabs/xrdp/issues/3141)
  ("x264 not working", self-built `--enable-x264`),
  [xrdp #3405](https://github.com/neutrinolabs/xrdp/issues/3405) (AlmaLinux
  H.264 vs 32-bpp client).

The subprocess design sidesteps this class: xrdp execs the distro's own
`ffmpeg` binary at arm's length — no encoder ABI compiled into xrdp, no
version-matched codec RPM, and the encoder library legal/patent question stays
where distros already solved it (their ffmpeg packaging). The linked x264 /
OpenH264 backends remain untouched as alternatives.

## 4. The third gap: hardware-encode demand vs narrow coverage

Hardware H.264 encoding is one of the longest-running asks
([#1422](https://github.com/neutrinolabs/xrdp/issues/1422) GFX epic, 205
comments; discussion [#2383](https://github.com/neutrinolabs/xrdp/discussions/2383),
154 comments of users chasing GPU-accelerated setups; jsorg71: *"NVidia is
90%+ of what people want to use xrdp with hardware acceleration"*). Current
coverage:

- Upstream `devel` hardware encode = **NVENC only**, via `xrdp_accel_assist`
  ([PR #3320](https://github.com/neutrinolabs/xrdp/pull/3320)), with its own
  driver constraints; users still ask whether Nvidia accel is even supported
  ([xorgxrdp discussions #317](https://github.com/neutrinolabs/xorgxrdp/discussions/317),
  [#361](https://github.com/neutrinolabs/xorgxrdp/discussions/361)).
- VA-API requests remain open ([xrdp #3119](https://github.com/neutrinolabs/xrdp/issues/3119):
  VA-API for WSL2 GPU-PV), and #3774 (VAAPI/Vulkan) was redirected (§2).

Through a stock ffmpeg child, this PR reaches **every encoder the installed
ffmpeg has** — `h264_vaapi` (validated end-to-end on this project's rig),
`h264_nvenc`, `h264_qsv`, `libx264` software fallback — selected by config
(`gfx.toml encoder_args`), not by rebuilding xrdp. AVC444 rides on all of them.

## 5. Issue-to-deliverable map

| Upstream evidence | Status | What this PR delivers |
|---|---|---|
| #3375 fuzzy/fringed text on x264; metalefty: "we need YUV444" ×3 | open | AVC444 v2 (0x000F) + v1 (0x000E) server encode; the #3375 A/B already showed 444 fixes it |
| #3375 users hand-patching color matrices, losing color accuracy | open | MS-RDPEGFX full-range BT.709 converter, unit-tested — the "non standard color conversion matrix" jsorg71 named in #3774 |
| xorgxrdp #255 closed unmerged; #2383 "444 bits … not stable yet"; fork-only AVC444 (#2635) | never landed | In-tree, bisectable slices; caps-gated per client (v2 → v1 → AVC420 fallback); deterministic-failure runner, no fork needed |
| #3711 / #3141 / #3405 linked-codec + packaging breakage | recurring | Encoder as arm's-length subprocess of the distro's ffmpeg; zero codec ABI in xrdp; linked backends untouched |
| #3119 VA-API ask; #3774 VAAPI stalled; #3320 NVENC-only accel | open/partial | Any ffmpeg HW encoder by config: VAAPI validated, NVENC/QSV reachable, same AVC444 on all |
| #3769 codec evolution (AV1) sponsor-stalled | open | Encoder-agnostic pipe/NUT plumbing: future codecs become mostly config + caps once the protocol side exists |

## 6. Anticipated objections (from the same threads), answered

- **"GPU work belongs in `xrdp_accel_assist`"** (jsorg71, #3774). Agreed — and
  this PR adds **no** GL/Vulkan/CUDA/OpenCL API surface to xrdp at all. GPU
  specifics live inside the ffmpeg child. It is complementary to accel_assist,
  not a competitor for that role; a future accel_assist/Vulkan encoder can feed
  the same AVC444 assembly, which is codec-source-agnostic.
- **"FFmpeg was already tried and dropped in #3774."** What was dropped was
  **linking `libavcodec` in-process** — FlyGoat's *"source of headache"* is the
  library ABI/API churn, the same coupling problem as §3. This PR deliberately
  uses the opposite arrangement: the stock `ffmpeg` **CLI** over pipes, no
  libav headers, no link-time dependency. The failure mode that killed #3774
  does not apply; the two hard problems jsorg71 cited there (MS color matrix,
  YUV444 algorithm) are precisely what this PR implements.
- **"The GFX 444 method is ugly; AV1 will have real YUV444"** (jsorg71, #3769).
  True — and AV1-over-RDP is an experimental FreeRDP-side draft with no
  Microsoft client support, explicitly deprioritized upstream after a
  sponsorship fell through. AVC444 is what every deployed mstsc speaks today;
  #3375's users are waiting now.
- **"Microsoft's Mac client has a broken AVC444 implementation"** (Nexarian,
  #3375). This is why the PR's caps classifier negotiates per client and falls
  back v2 → v1 → AVC420: a broken client that doesn't advertise the caps never
  gets 444, and behavior without the feature is unchanged (upstream coding
  rule: no functional regression when disabled).
- **"Only nit-picky devs care about 4:4:4"** (Nexarian, #2383). The sustained
  multi-reporter thread with screenshots (#3375), the "completely unreadable
  text" scrolling report, and the maintainer's own thrice-stated "we need
  YUV444" say otherwise — text-heavy remote development is xrdp's core use.
- **Real costs, stated plainly:** one raw-frame copy over a pipe per update
  (no dma-buf zero-copy — measured ~0.09 ms/frame on this rig, see
  `PR-demo/RESULTS.md` §E5) and one long-lived child process per session.
  Latency vs the linked x264 backend is not yet benchmarked (RESULTS.md P3).

## 7. Open items to raise during PR review

- **Enable the real-ffmpeg regression tests in upstream CI.** The guards for
  the two field bugs (content/region desync; low-resolution probesize
  deadlock) are gated on `XRDP_TEST_FFMPEG_PATH` and skip without it. CI would
  need an ffmpeg install + env var in `.github/workflows/build.yml` —
  maintainers' call; propose, don't pre-commit.
- Benchmark subprocess vs linked-x264 latency before claiming parity.
- Re-check #3774 / #3769 / accel_assist status at PR time (this survey is a
  2026-07-22 snapshot; metalefty's stated next focus is Wayland).
