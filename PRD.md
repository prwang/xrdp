# Product Requirements Document v2: External Stock-FFmpeg AVC444 Backend for xrdp

**Status:** Resolved MVP design after specification, codebase, and executable-behavior review  
**Target repositories:** `neutrinolabs/xrdp` and `neutrinolabs/xorgxrdp`  
**Reviewed branch/baseline:** `dev/ipc_avc444`, working tree at commit `4d61d13b` (forked from `devel`). All Section 6 seam line numbers are against this baseline; §19 requires re-pinning immutable commit IDs immediately before implementation.  
**Code-review date:** 2026-07-13  
**MVP deployment target:** Linux and supported Unix-like xrdp/xorgxrdp servers  
**MVP display topology:** One monitor / one RDPGFX surface  
**Primary interoperability target:** Microsoft Windows `mstsc.exe`  
**Working name:** `ffmpeg-process` AVC444 backend

---

## 1. Executive summary

This document specifies an xrdp Graphics Pipeline AVC444 backend that uses a **user-selected, unmodified stock `ffmpeg` executable** rather than linking xrdp against FFmpeg development libraries or an encoder SDK.

The MVP adds exactly one encoder process to an existing xrdp/xorgxrdp session: one persistent child `ffmpeg` process containing one H.264 encoder stream. The relevant encoder boundary is therefore a two-process boundary—`xrdp` and its FFmpeg child—but the complete session still includes the existing Xorg/xorgxrdp and xrdp service processes.

Microsoft RDP AVC444 is not a conventional H.264 4:4:4 stream. xrdp constructs two synthetic YUV420 pictures for every logical desktop update:

- a main YUV420 view; and
- a Chroma420 auxiliary view.

The two pictures are written in that order to the **same** FFmpeg child. MS-RDPEGFX requires them to be encoded by the same H.264 encoder and decoded by a single H.264 decoder as one continuous stream.

The xrdp side:

- evaluates the client capability sets and the configured codec order;
- runs a bounded probe of the exact FFmpeg command **before** selecting AVC444 in `RDPGFX_CAPS_CONFIRM_PDU`;
- selects AVC444 v1 only for eligible RDP 10.x capability sets;
- receives a persistent XRGB8888 full-chroma surface from xorgxrdp;
- reconstructs the complete main and auxiliary NV12 views for every submitted MVP update using the MS-RDPEGFX full-range BT.709 equations;
- pads coded dimensions to multiples of 16 while preserving arbitrary visible RDP dimensions through the AVC region mask;
- writes complete main and auxiliary raw frames to the FFmpeg child over a nonblocking pipe;
- reads a **standard NUT stream with normal syncpoints** from stdout;
- demuxes NUT packets without decoding H.264;
- validates Annex-B H.264, packet order, and reset parameter sets;
- packages both packets as `RFX_AVC444_BITMAP_STREAM` with `LC=0`; and
- sends the result with RDPGFX codec ID `0x000E` to `mstsc.exe`.

The FFmpeg child:

- reads alternating main/auxiliary NV12 frames;
- encodes every picture through one persistent `libx264` encoder context for the MVP;
- uses zero-latency/no-reordering settings and repeated parameter-set headers;
- muxes one encoded packet per picture to standard `-f nut`; and
- writes the NUT stream to stdout.

Every visible resize or coded-dimension change destroys and reaps the child, discards the old parser/stream generation, recreates the converter buffers, and starts a new child. The first update after restart is a full-surface `LC=0` pair whose first packet contains SPS, PPS, and IDR NAL units.

The design accepts the copy and throughput cost of serializing complete subframes. Encoder throughput is not an xrdp correctness guarantee. xrdp is responsible for bounded queueing, deterministic failure, and protocol-correct output—not for making an underpowered encoder sustain the requested rate.

### 1.1 Resolved MVP decisions

| Area | MVP decision |
|---|---|
| Capability floor | AVC444 v1 on eligible RDP 10.0 and 10.2–10.7 sets; Windows Server 2016-era MSTSC is the minimum target |
| Codec order | Preserve existing `order` semantics; an earlier `RFX` entry still wins. Extend `h264_encoder` with `ffmpeg`; that backend supplies AVC444 v1 only in MVP and does not silently switch to a linked encoder |
| Capture | XRGB8888 |
| Color | Exact MS-RDPEGFX full-range BT.709 conversion |
| Dimensions | Arbitrary visible dimensions; coded width and height padded to 16 |
| FFmpeg MVP | Stock executable with `libx264` |
| Process model | xrdp plus one persistent FFmpeg child |
| NUT | Standard NUT with normal syncpoints and `-write_index 0`; no experimental PIPE mode |
| H.264 headers | `h264_mp4toannexb` plus `libx264` `repeat-headers=1`; startup NAL validation is authoritative |
| Reset | Kill/reap and recreate child on every resize or discontinuity |
| Timeout | Bounded startup and pair deadlines; no runtime force-IDR RPC in MVP |
| Output sizing | Dynamically growing best-effort buffers under a separate hard safety ceiling; legacy GFX compressed limit is not reused |
| Topology | Single monitor only |
| Server platforms | Linux and supported Unix-like xrdp/xorgxrdp servers; no non-target platform-specific material in this MVP |

---

## 2. Problem statement

Current xrdp H.264 support is build-time coupled to in-process software encoder libraries. In the current `devel` code:

- `set_h264_encoder_methods()` selects x264 or OpenH264 function pointers at compile time;
- `xrdp_encoder_create()` selects `CC_GFX_A2` and `XRDP_nv12_709fr` for GFX H.264;
- `gfx_wiretosurface1()` emits one AVC420 metablock and invokes one H.264 encode operation; and
- `xrdp_encoder_x264_encode()` copies compression rectangles into an encoder-private persistent NV12 frame and calls x264 in process.

This arrangement creates four relevant limitations:

1. Every linked encoder creates build, packaging, ABI, and maintenance branches.
2. A distribution-provided FFmpeg executable may already contain the preferred local software or hardware encoder, but xrdp cannot use it without another linked backend.
3. Current GFX H.264 is AVC420 and loses chroma resolution needed for sharp colored desktop edges.
4. The existing synchronous one-frame/one-buffer API does not represent the two-picture AVC444 transaction or a packetized child-process output stream.

The proposed MVP deliberately narrows the first implementation to a stock FFmpeg executable with `libx264`, one logical monitor, AVC444 v1, standard NUT, complete-view reconstruction, and process restart on every resize. This creates one portable executable integration path before Linux hardware-specific profiles are attempted in later changes.

## 3. Goals

### G-1: Windows MSTSC AVC444 interoperability

Implement Microsoft-compatible RDPGFX AVC444, initially using `RFX_AVC444_BITMAP_STREAM` with `LC=0`, and validate it with supported Windows `mstsc.exe` clients.

### G-2: Stock executable integration

Use a user-installed `ffmpeg` executable with no compile-time dependency on FFmpeg headers or libraries.

### G-3: One generic build path

Avoid separate xrdp compile-time implementations for libx264, QSV, VAAPI, NVENC, and other encoder APIs. The MVP adds `ffmpeg` as a runtime `h264_encoder` choice and validates stock-FFmpeg `libx264`; later Linux hardware profiles remain runtime behavior, not new xrdp SDK linkages.

### G-4: Correct H.264 stream semantics

Encode main and auxiliary AVC444 views through the same persistent FFmpeg output stream and preserve exact coded-picture order.

### G-5: Bounded latency

Prevent encoder or pipe backpressure from building an unbounded stale-frame queue. Drop or coalesce only work that has not been submitted to the H.264 stream.

### G-6: Deterministic reset

Terminate and recreate the FFmpeg child on every resize, coded-dimension change, process failure, parser failure, or stream discontinuity. Resume with a full `LC=0` reset pair.

### G-7: Codebase-aligned integration

Reuse xrdp's existing encoder worker thread, GFX command processing, one existing H.264 handle slot for the MVP surface, processed FIFO, and RDPGFX output path where practical.

---

## 4. Non-goals

### NG-1: Guaranteed real-time encoding

The project does not guarantee that an arbitrary FFmpeg binary, encoder, driver, or hardware device can sustain a requested picture rate. For AVC444, 60 desktop updates per second means 120 encoded H.264 pictures per second.

### NG-2: Linking against libavcodec

The initial backend does not include or dynamically load `libavcodec`, `libavformat`, or `libavutil`.

### NG-3: A custom long-running encoder daemon

The initial design directly spawns and owns a stock FFmpeg child. It does not define a separately installed service, Unix-domain RPC daemon, or shared-memory encoder protocol.

### NG-4: Dynamic resolution changes

The child is not reconfigured in place. It is killed and restarted for every coded-size change.

### NG-5: AVC444v2 in the first milestone

The first milestone targets AVC444 codec ID `0x000E`. AVC444v2 codec ID `0x000F` is a later feature because its Chroma420 construction algorithm differs even though the outer wire structure is similar.

### NG-6: Deferred chroma in the first milestone

The first milestone always uses `LC=0`: main and auxiliary views are sent together. `LC=1`/`LC=2` scheduling is deferred.

**SUPERSEDED in design (2026-07-26).** `LC=1`/`LC=2` scheduling is now
specified as FR-PROC-7 (preemptive aux — no idle heuristic), ordered
after FR-CAPTURE-8, which is its structural prerequisite.

### NG-7: Zero-copy process transport

Complete reconstructed subframes are serialized to FFmpeg. The extra process-boundary copies are an accepted portability tradeoff.

**SUPERSEDED (2026-07-26, owner directive).** Zero-copy transport is now a
requirement, not a non-goal: the capture shmem carries the final wire-format
views and xrdp feeds FFmpeg exclusively via `vmsplice(2)` — see FR-CAPTURE-6
and FR-PROC-6. The MVP-era copies (in-xrdp repack + staging memcpy + pipe
`write()`) were measured as the encoder-thread bottleneck on a weak-CPU 4K
host (T4, 2026-07-26): xrdp burned more CPU than the X server that performs
the actual color conversion.

### NG-8: H.264 decoding in xrdp

xrdp demuxes NUT and inspects/normalizes H.264 NAL units. It does not decode the H.264 pictures. The RDP client is the decoder.

### NG-9: FR-ACK-1 as filed — the rect_id "ghost" fix (withdrawn 2026-07-31)

Specified and implemented in one day as the #64 correctness BLOCKER,
then refuted by static analysis the same day: the ack value is an echo
(`frame_id_server = enc_done->frame_id`, sole assignment; zero drift
over 494 live frames) and the filed ghost does not exist. Not a
requirement. The surviving machinery (echoed identity, ack totality,
displayed flag, region return) is specified under **BACKLOG #70**,
the eager slot-release ack, whose rationale is concurrency — the
2026-07-31 T4 measurement attributed the whole 113.6 ms m=1 cycle and
located the serializer at the ack's emission point. History:
implementation checkpoint `wip/fr_ack_1_checkpoint` (xrdp
`8bd989c0`/`0b295631`, xorgxrdp `59210b2`, CI green 380/380);
withdrawal `e8d00594`; reconciliation `0040e603`; measured resolution
`0db74f6e` (the FR's full former text lives at that commit).

---

## 5. Terminology

| Term | Meaning |
|---|---|
| Desktop update | One logical xrdp display update submitted to AVC444. |
| Main view | The normal YUV420 view defined by MS-RDPEGFX AVC444. |
| Auxiliary view | The Chroma420 view used with the main view to reconstruct YUV444. |
| Pair | Main picture followed immediately by its auxiliary picture. |
| Coded picture | One H.264 picture generated from one raw NV12 input frame. |
| Child | One persistent stock FFmpeg process for the single MVP RDPGFX surface. |
| NUT demuxer | In-tree parser that extracts encoded packet boundaries, timestamps, flags, and codec data from FFmpeg's NUT stdout. It is not an H.264 decoder. |
| Submitted | Any byte of a raw picture has been committed to the child input pipe. |
| Unsubmitted | A pending update whose main picture has not begun writing to the child. |
| Reset pair | A full-surface `LC=0` pair sent after child creation or stream reset. |
| Actual dimensions | Width and height of the RDP surface. |
| Coded dimensions | NV12 dimensions given to FFmpeg, including any required padding. |

---

## 6. Codebase review and current-state evidence

This section records the integration assumptions reviewed against the `devel` branch on 2026-07-13. Line numbers are approximate and should be refreshed immediately before implementation.

### 6.1 Current xrdp encoder worker

In `xrdp/xrdp_encoder.c`:

- `xrdp_encoder_create()` creates `fifo_to_proc`, `fifo_processed`, a mutex, `xrdp_encoder_event_to_proc`, `xrdp_encoder_event_processed`, termination wait objects, and the `proc_enc_msg` worker thread.
- `xrdp_encoder_delete()` signals termination and waits up to five seconds before deleting codec handles and synchronization objects.
- `gfx_send_done()` creates `XRDP_ENC_DATA_DONE`, appends it to `fifo_processed`, and signals `xrdp_encoder_event_processed`.
- The GFX H.264 work is therefore already off the main xrdp thread. The external child is an additional process boundary inside the existing encoder worker path, not the first asynchronous boundary.

**Design consequence:** The first implementation should keep one synchronous `encode_pair()` transaction inside the existing encoder worker. Its implementation must use nonblocking pipes and `poll()` so it can write raw input while concurrently draining NUT output and stderr.

### 6.2 Current GFX H.264 path

In `xrdp/xrdp_encoder.c`:

- `xrdp_encoder_create()` currently selects `CC_GFX_A2`, `XRDP_nv12_709fr`, and GFX mode when H.264 is selected.
- `process_enc_h264()` for the older surface-command route is a dummy; the implemented H.264 route is in the GFX path.
- `gfx_wiretosurface1()` parses the GFX command, selects a monitor index, writes `RFX_AVC420_METABLOCK`, checks the NV12 byte count, lazily creates a per-surface H.264 handle, calls the H.264 encode function, and then calls the generic RDPGFX serializer `xrdp_egfx_wire_to_surface1()` (`xrdp/xrdp_egfx.c:496`, declared `xrdp/xrdp_egfx.h:206`).
- `codec_handle_h264_gfx[16]` provides the current per-surface handle seam.

**Design consequence:** Add a separate AVC444 serializer path adjacent to `gfx_wiretosurface1()`. The MVP uses exactly one external handle for the single supported surface; the existing monitor-slot array is an integration seam, not an MVP multi-monitor promise.

### 6.3 Current x264 reconstruction behavior

In `xrdp/xrdp_encoder_x264.c`:

- the encoder owns persistent `yuvdata`;
- changed Y and UV rows are copied into it for each compression rectangle;
- the x264 input color space is `X264_CSP_NV12`;
- `i_width` and `i_height` are rounded up with `(dimension + 15) & ~15`; and
- the persistent reconstructed frame is submitted as one complete picture.

The external implementation must allocate its NV12 planes from the **coded** aligned dimensions. It must not assume an allocation based only on visible dimensions is large enough.

**Design consequence:** Dirty rectangles are metadata/state inputs, not independently encoded H.264 tiles. The AVC444 MVP maintains complete main and auxiliary NV12 buffers, reconstructs both from the full XRGB surface for each submitted update, and sends complete pictures to FFmpeg.

### 6.4 Current xorgxrdp shared memory

In `xorgxrdp/module/rdpClientCon.c`:

- shared capture memory is allocated with a mapped pointer and fd;
- `CC_GFX_A2` currently allocates approximately two bytes per pixel and selects the H.264 capture path;
- resize reallocates capture memory when dimensions change.

The current GFX H.264 capture format is specifically **`XRDP_nv12_709fr`** — BT.709 *full-range* NV12, not generic BT.601 NV12. `xrdp_encoder_create()` sets `capture_format = XRDP_nv12_709fr` (`xrdp/xrdp_encoder.c:234-235`); the only implemented GFX converter is `rdpCaptureGfxA2` → `rdpCopyBox_a8r8g8b8_to_nv12_709fr` (`xorgxrdp/module/rdpCapture.c:1454`, whose luma `Y=(54R+183G+18B)>>8` over `[0,255]` confirms BT.709 full-range); the constant is defined at `common/xrdp_constants.h:349`. Plain `XRDP_nv12` (BT.601-style) is used only by the non-GFX `CC_SUF_A2` path (`xrdp/xrdp_encoder.c:244`). The AVC444 main/auxiliary views must therefore use matching **BT.709 full-range** colorimetry (§8.3), consistent with the existing GFX path.

**Design consequence:** AVC444 requires a new full-chroma capture mode. The current `XRDP_nv12_709fr` source cannot be reused because 4:2:0 conversion has already discarded the chroma samples needed to build the auxiliary view. Three full-chroma format constants **already exist but are unimplemented** — `XRDP_yuv444_709fr` (`common/xrdp_constants.h:352`, code 67), `XRDP_yuv444_v1_stream_709fr` (`:356`, code 68; MS-RDPEGFX AVC444 v1) and `XRDP_yuv444_v2_stream_709fr` (`:360`, code 69) — with no `yuv444` converter anywhere in `/work` or `xorgxrdp/module/`. The new capture mode (FR-CAPTURE-2) should **reuse or supersede these existing constants rather than duplicate the format numbering**; note the capture is XRGB-source (FR-CAPTURE-1), and these constants name the intended *view* colorimetry.

### 6.5 Microsoft AVC444 requirement

MS-RDPEGFX defines an AVC444 bitmap stream as two AVC420-form structures. For `LC=0`, the first carries the main YUV420 picture and the second carries the Chroma420 picture. Both structures are consumed as ONE H.264 stream: exactly one SPS/PPS pair, one `frame_num` chain, monotonic decode order, acceptable to a single in-order decoder.

**Design consequence (amended 2026-07-28; supersedes the earlier “same encoder” reading):** the binding requirement is on the WIRE, not on the process count. How many encoder processes produce the bytes is an implementation detail, provided the merged output is one conformant chain with exactly one parameter-set pair. The shipped architecture (FR-H264-7) uses two FFmpeg children — a main child that owns the reference chain, and an all-IDR auxiliary child whose packets are rewritten into non-reference, non-IDR I leaves on that chain. What remains forbidden is what the original wording was actually protecting against, both measured failures: two *reference* chains and/or duplicated SPS/PPS fed to one decoder (desync garbage on Windows; parameter-set duplication blacks the macOS Windows App). In addition the stream must satisfy decode-topology invariance (FR-H264-7): the 2026-07 Mac bisect proved real clients do not all feed a single in-order decoder, so main frames must never reference aux frames.

### 6.6 Current compile-time guards that must change

The current source does not merely select x264/OpenH264 at runtime:

- the implemented body of `gfx_wiretosurface1()` (`xrdp/xrdp_encoder.c:794`) is wrapped by `#if defined(XRDP_X264) || defined(XRDP_OPENH264)`;
- GFX H.264 selection in `xrdp_encoder_create()` (`xrdp/xrdp_encoder.c:228`) is under the same compile-time condition;
- per-surface H.264 handle deletion (`xrdp/xrdp_encoder.c:414`) is similarly guarded;
- `init_libh264_loaded()` (`xrdp/xrdp_mm.c:57-73`) derives H.264 availability from compiled-in library support, but keys on a **different** macro set: `#if defined(XRDP_OPENH264)` (runtime probe) / `#elif defined(XRDP_H264)` (set loaded) / `#else` (unavailable). `XRDP_H264` is a *derived umbrella macro* defined in `xrdp/xrdp.h:41-43` as `#if defined(XRDP_X264) || defined(XRDP_OPENH264) || defined(XRDP_NVENC)`. `XRDP_X264` does not appear in this function, so an x264-only build reaches the `XRDP_H264` branch via the umbrella.

**The capability-split work must therefore account for three macros — `XRDP_X264`, `XRDP_OPENH264`, and the `XRDP_H264` umbrella — not the two named in earlier drafts.**

**Design consequence:** The external process backend must be compilable and selectable when neither x264 nor OpenH264 is linked. AVC capability availability must be split into at least:

- compiled-in in-process software H.264 availability; and
- configured/probed external FFmpeg AVC444 availability.

The GFX parser, AVC420 metablock helper, generic H.264/RDP serializers, and external child lifecycle must not remain accidentally excluded by x264/OpenH264-only preprocessor guards.

### 6.7 Integration seam matrix

| Repository/file | Current seam | Required change | Synchronization significance |
|---|---|---|---|
| `xrdp/xrdp_mm.c` | `init_libh264_loaded()` | Separate linked-library availability from externally probed AVC444 availability | The capability decision must be made before creating the GFX encoder |
| `xrdp/xrdp_mm.c` | `xrdp_mm_egfx_caps_advertise()` | Select and retain explicit AVC420/AVC444 mode and codec ID | Establishes the session-wide wire contract |
| `xrdp/xrdp_encoder.c` | `xrdp_encoder_create()` | Select full-chroma capture and external pair backend | Must complete before xorgxrdp begins the selected capture mode |
| `xrdp/xrdp_encoder.c` | `proc_enc_msg()` / `process_enc_egfx()` | Keep existing worker ownership; invoke pair backend from GFX command processing | Provides the single xrdp-side serialization point for each work item |
| `xrdp/xrdp_encoder.c` | `gfx_wiretosurface1()` | Split AVC420 and AVC444 serializers; parse common command fields once | Pair commit and output completion occur inside this worker path |
| `xrdp/xrdp_encoder.c` | `gfx_send_done()` | Reuse unchanged where possible | Existing handoff from encoder worker to xrdp main thread |
| `xrdp/xrdp_encoder.h` | existing H.264 handles and function pointers | Add one pair-oriented external handle/API and child generation state for the MVP surface | One owner for child PID, fds, parser, tags, and reconstructed views |
| `xorgxrdp/module/rdpClientCon.c` | capture mode allocation and resize | Add full-chroma AVC444 mode and selected-codec command metadata | Source pixels must be stable before the existing encoder work item is queued |
| `xrdp/gfx.toml` and typed config | software H.264 configuration | Add executable/profile/probe/process limits | Configuration is resolved before child creation; no shell parsing |
| xrdp build files | x264/OpenH264 source guards | Compile external backend and common AVC serializers without FFmpeg libraries | Enables the promised single build path |

---

## 7. Proposed architecture

```text
Windows mstsc
    │
    │ RDPGFX capabilities
    ▼
xrdp main thread
    │  selects AVC444 and creates GFX encoder
    ▼
xorgxrdp full-chroma shared capture surface
    │  dirty/compression rectangle metadata
    ▼
xrdp encoder worker: process_enc_egfx()
    │
    ├─ gfx_wiretosurface1_avc444()
    │      │
    │      ├─ reconstruct complete main NV12 view
    │      ├─ reconstruct complete auxiliary NV12 view
    │      └─ external_ffmpeg_encode_pair()
    │              │
    │              │ raw NV12 main then auxiliary
    │              ▼
    │         child fd 3 / anonymous pipe
    │              ▼
    │         stock ffmpeg process
    │         one H.264 encoder stream
    │              │
    │              │ NUT on stdout
    │              ▼
    │         xrdp NUT demuxer
    │              │
    │              ├─ encoded main packet
    │              └─ encoded auxiliary packet
    │
    ├─ Annex-B validation/normalization
    ├─ RFX_AVC444_BITMAP_STREAM serializer, LC=0
    ├─ xrdp_egfx_wire_to_surface1()
    └─ gfx_send_done()
           │
           ▼
xrdp main thread sends RDPGFX data
           │
           ▼
mstsc uses one H.264 decoder and combines both views
```

### 7.1 Process cardinality

The MVP supports one logical monitor and one RDPGFX surface. In current xrdp state, the legacy single-display topology may be represented by `monitorCount == 0`, while an explicit one-monitor layout uses `monitorCount == 1`; both are MVP-eligible. Any value greater than one is multi-monitor and ineligible:

```text
one xrdp process + one persistent FFmpeg child + one FFmpeg H.264 stream
```

The child receives this immutable picture order:

```text
update 0 main
update 0 auxiliary
update 1 main
update 1 auxiliary
...
```

A second monitor is not assigned another child in the MVP. If the initial topology has more than one monitor, the external AVC444 candidate is unavailable and normal pre-confirm codec-order fallback applies. If topology changes to multiple monitors after capability confirmation, xrdp terminates the child and fails/restarts the GFX connection path; MVP does not perform an in-stream codec switch or remap encoder contexts.

The data model should avoid gratuitously preventing a later per-surface extension, but no multi-stream or multi-child behavior is part of MVP acceptance.

---

## 8. Functional requirements

## 8.1 Capability negotiation

### FR-CAP-0: Backend availability

External AVC444 availability must not depend on `XRDP_X264`, `XRDP_OPENH264`, or a linked H.264 library. The GFX AVC serializers and external process backend must compile when neither in-process library is linked.

The external AVC444 candidate is available only when all of these conditions hold:

1. the feature is enabled;
2. the session topology is a single logical display (`monitorCount <= 1` in the current representation);
3. the configured FFmpeg executable and profile pass the bounded behavioral probe; and
4. the client advertises an eligible AVC444 v1 capability set.

### FR-CAP-1: Explicit selected mode

xrdp must retain the selected wire mode rather than reducing all H.264 to `XRDP_EGFX_H264`:

```c
enum xrdp_gfx_avc_mode
{
    XRDP_GFX_AVC_NONE = 0,
    XRDP_GFX_AVC420,
    XRDP_GFX_AVC444,
    XRDP_GFX_AVC444V2
};
```

Store the selected mode, capability version, flags, and codec ID through `xrdp_encoder_create()` and GFX serialization.

### FR-CAP-2: Exact MVP capability table

| Advertised capability set | H.264 interpretation for this MVP |
|---|---|
| `RDPGFX_CAPVERSION_8` | No AVC candidate |
| `RDPGFX_CAPVERSION_81` with `AVC420_ENABLED` | AVC420 candidate only |
| `RDPGFX_CAPVERSION_81` without `AVC420_ENABLED` | No AVC candidate |
| `RDPGFX_CAPVERSION_10` with `AVC_DISABLED` clear | AVC444 v1 candidate |
| `RDPGFX_CAPVERSION_101` (`0x000A0100`) | Reserved-only capsData — no AVC flag field (MS-RDPEGFX 2.2.1.10); **not eligible** for the v1-only MVP (treated as AVC444v2 territory) |
| `RDPGFX_CAPVERSION_102`–`107` with `AVC_DISABLED` clear | AVC444 v1 candidate |
| Any v10.x set with `AVC_DISABLED` set | No AVC candidate |

`AVC_THINCLIENT`, where present, is a preference indication, not a prerequisite for AVC444 selection.

When several eligible sets of the same mode are advertised, choose the highest supported version. Do not identify the client by Windows version; capability contents are authoritative. Windows Server 2016-generation MSTSC is the minimum acceptance target because AVC444 appeared in that generation, but it is selected only when the client actually advertises an eligible v10.0 or v10.2–v10.7 set. A client that advertises only v10.1 is not accepted by the v1-only MVP.

### FR-CAP-3: Codec-order and encoder-backend interaction

Preserve the existing `gfx.toml` `order` semantics and extend the existing `h264_encoder` selector:

- Iterate `order` entries exactly as today.
- If `RFX` appears earlier and has an eligible Progressive capability set, RFX wins.
- When `H.264` is reached, consult the configured H.264 backend:
  - `h264_encoder = "ffmpeg"`: the MVP supplies only a successfully probed AVC444 v1 candidate. A v8.1 AVC420-only client therefore causes this `H.264` entry to be skipped and the next configured codec to be considered.
  - `h264_encoder = "x264"` or `"OpenH264"`: retain the existing linked-backend AVC420 behavior when compiled and available.
- Never silently change from `ffmpeg` to a linked backend because the probe failed. Backend selection is administrator policy; codec-order fallback remains separate.

The MVP does not require a new top-level codec-order token. The selected log line must state the backend and wire mode, for example `ffmpeg/AVC444`, `x264/AVC420`, or no H.264, rather than merely “H264”. A future explicit AVC-mode order can be considered after AVC444v2 or external AVC420 exists.

### FR-CAP-4: Probe timing and immutable connection choice

Run the exact FFmpeg behavioral probe before sending `RDPGFX_CAPS_CONFIRM_PDU`. A failed probe removes the external AVC444 candidate before codec-order selection.

No persistent probe cache is required for MVP. The **client-advertised capability does not change** because FFmpeg changes; the server's readiness to select and honor AVC444 can change between xrdp processes because the executable, package, permissions, or profile may have changed. Probe once in the connection process before confirmation and retain that result for the connection.

After confirmation:

- a runtime child failure follows the bounded reset/restart policy;
- no partially emitted pair may be replaced with another codec; and
- after the configured failure threshold, fail the GFX/session path cleanly. MVP does not perform a mid-connection switch to RFX or AVC420, even if the confirmed capability set could theoretically describe another codec.

### FR-CAP-5: AVC444 v1 codec ID

The MVP sends `XR_RDPGFX_CODECID_AVC444` / `0x000E`. It must not select capability version 10.1 and then send AVC444 v1. AVC444v2 / `0x000F` is deferred.

### Integration seam

Primary location: `xrdp_mm_egfx_caps_advertise()` in `xrdp/xrdp_mm.c`.

The current code sorts advertised sets, records one `best_h264_index`, and then applies codec order. Replace the single H.264 index with explicit candidates such as `best_avc444_v1_index` and `best_avc420_index`, populated only after external-probe availability is known.

---

## 8.2 Full-chroma capture

### FR-CAPTURE-1: MVP format

Use xorgxrdp's existing **`XRDP_a8r8g8b8` / logical XRGB8888** 32-bit capture representation as the initial full-chroma source. Conversion cost is not an MVP concern; clarity, portability, and reuse of existing 32-bit xorgxrdp handling take precedence.

Read each pixel as the format's native 32-bit logical value and extract `R = (pixel >> 16) & 0xff`, `G = (pixel >> 8) & 0xff`, and `B = pixel & 0xff`; do not hard-code little-endian byte offsets in the converter. Alpha is ignored. Add a format/byte-order unit vector so the capture producer and converter cannot silently disagree.

### FR-CAPTURE-2: Distinct capture mode

Add a distinct capture code, provisionally:

```c
CC_GFX_AVC444
```

Do not overload `CC_GFX_A2`, whose allocation and semantics are tied to `XRDP_nv12_709fr` AVC420. For the capture *format* field, prefer reusing or superseding the pre-existing unimplemented `XRDP_yuv444*_709fr` constants (§6.4) rather than minting a new number.

### FR-CAPTURE-3: Required metadata

The capture allocation/work item must provide:

- actual width and height;
- source stride;
- mapped XRGB pointer;
- sufficient full-chroma bytes;
- dirty/compression rectangles;
- the single MVP surface identity; and
- existing frame/capture identifiers used for acknowledgement.

### FR-CAPTURE-4: Resize

Every visible dimension change reallocates/revalidates the capture surface and triggers mandatory FFmpeg child replacement, even when the new dimensions round to the same 16-pixel coded dimensions. This avoids retaining converter or RDP region state across a changed visible geometry.

### FR-CAPTURE-5: Source lifetime

The current encoder work item and capture acknowledgement remain the source-lifetime contract. The converter reads the complete persistent XRGB surface only while processing that work item. After the complete main and auxiliary NV12 buffers have been reconstructed, the source capture memory is no longer needed by the FFmpeg child.

**Amendment (2026-07-26, FR-CAPTURE-6):** with wire-format views in the
capture shmem and `vmsplice` feeding, "no longer needed" is defined by the
synchronous encode: the shmem pages are referenced by the pipe until the
FFmpeg child has read them, which the synchronous wait guarantees happens
before the encode call returns and the frame is acknowledged. If input is
not fully spliced when the wait ends, the call must error and the child be
replaced — borrowed pages never outlive the encode call.

### FR-CAPTURE-6: Wire-format views in the capture shmem (2026-07-26)

The capture side (xorgxrdp) produces the **final encoder input**, fused into
its per-damage-rect conversion pass; xrdp performs **zero pixel-domain work**
on the hot path (no color matrix, no subsampling, no repacking, no staging
copies — pointer arithmetic and `vmsplice` only).

1. Per-monitor shmem region layout: `[main NV12][aux NV12]`, both at the
   FINAL coded geometry — width aligned to the client-derived
   `chroma_align` (16 FreeRDP / 32 mstsc), height 16-aligned — with each
   view and each region page-aligned (4096) so `vmsplice` can move whole
   pages.
2. The auxiliary-view variant rides the existing `capture_format` contract
   field, binding the §6.4 reserved constants: `XRDP_yuv444_v2_stream_709fr`
   (ChromaV2 aux), `XRDP_yuv444_v1_stream_709fr` (v1 banded aux), and
   `XRDP_nv12_709fr` under `CC_GFX_AVC444` (main-view-only; the external
   AVC420 mode). The v1 variant is diagnostic; it may repack its full view
   per frame.
3. `chroma_align` joins the xup client info; any change to this contract
   bumps `XUP_CLIENT_INFO_CURRENT_VERSION` and both daemons refuse loudly
   on mismatch (never silent corruption).
4. Rationale (recorded): the previous intermediate planar-YUV444 shmem was
   neither the capture format nor the encoder format, forcing a second
   full-frame per-pixel pass inside xrdp (~25M bounds-clamped samples per
   4K frame) — the measured T4 bottleneck. The owner owns the 444 wire
   format; the shmem must be splicable as-is.
5. `xrdp_avc444_convert.c` remains in-tree as the executable REFERENCE for
   the view layout (unit tests / oracle), off the hot path.

### FR-CAPTURE-7: Conversion loops must be vectorization-friendly (2026-07-26)

The capture-side color conversion is the pipeline's only pixel-domain pass
(FR-CAPTURE-6) and runs on the X server thread; its per-pixel constant is
the interactive-latency budget. Any packed output layout — including
future ones — MUST be implemented so the ARGB→YUV matrix math stays in a
flat, contiguous, branch-free loop the compiler auto-vectorizes:

1. Decode each source row ONCE through a single `RDP_VECTORIZE`d flat
   loop (contiguous loads/stores, branchless clamps); do layout packing
   as separate cheap shuffle/gather steps over the cache-hot row buffers.
   The layout never forces scalar matrix math: strided formats cost a
   pack step, not the matrix.
2. No per-sample function calls, no per-sample coordinate clamping in
   interior loops (hoist edge replication to the row decode / pad tails),
   no re-decoding a pixel separately for U and for V.
3. Every conversion function carries `RDP_VECTORIZE` (the module builds
   at -O2; the attribute supplies O3 + tree-vectorize + AVX2 clones).
4. Changes to these loops are benchmarked with `tools/avc444_pack_bench.c`
   (offline, no session needed; verbatim copies of the shipped loops —
   keep them in sync) before deploying.

**Negative example (measured 2026-07-26, the reason this FR exists).**
The first-cut FR-CAPTURE-6 packers (xorgxrdp `75c19283ab87`) violated all
three rules: per-sample `avc444_px()/px_u()/px_v()` helper calls with two
clamp branches each, U and V re-decoding the same pixel, and no
`RDP_VECTORIZE` attribute. Result — ~7x the per-pixel cost of the old
vectorized planar loop, observed live as Xorg burning 50-70% of a core
during 4K drags with cost proportional to damage size:

    ms/frame              old planar   scalar packers   row-decode fix
    T4    3840x2400 rect        6.88            49.40            19.24
    T4    2000x1000 rect        1.24            10.77             4.27
    T4     500x200  rect        0.06             0.52             0.19
    dev   3840x2400 rect        3.27            23.60             7.30

("old planar" was only xorgxrdp's HALF of the pre-FR-CAPTURE-6 pipeline;
xrdp then re-walked every pixel again, full-frame, regardless of damage.
The row-decode fix is the whole pipeline's pixel work, damage-limited.)

### FR-CAPTURE-8: Two-slot pipelined capture (designed 2026-07-26; ordered FIRST — structural prerequisite of FR-PROC-7 preemptive aux)

**Motivation (measured, `PR-demo/t4_profile/frame_accounting.sh`).** The
delivered frame rate is capped at ~20 fps by a fully serial cycle: the
capture stage refuses to run while the previous rect is unacked
(`rect_id > rect_id_ack`), and the ack arrives only at encode return —
so the ~30 ms synchronous main+aux encode serializes with the ~10 ms
capture/handoff even though the network runs at <10% utilization and
the client decode queue is empty on every ack.

Contract, when implemented:

1. The FR-CAPTURE-6 per-monitor region is doubled into exactly **two
   slots** (each the full `[main NV12][aux NV12]` layout, page-aligned).
   The slot count is FIXED at 2 in the versioned contract: a third slot
   is a contract change requiring owner sign-off, never a tuning knob —
   each extra slot adds one frame of raw inventory and one frame of
   backpressure lag (see 5).
2. Slot selection is `rect_id` parity, carried in the **existing**
   per-frame `shmem_offset` field of the paint message. The ack
   message (106) and its semantics are UNCHANGED: ack of rect N means
   xrdp is done with N's slot (its borrowed pages are proven consumed
   at encode return by the FR-PROC-6 `in_iov_pending` check).
3. The capture gate relaxes from one outstanding rect to two
   (`rect_id > rect_id_ack + 1`), **conditioned on the AVC444 capture
   code** — the deferred-update callback is shared by all capture
   modes, and modes with single-slot layouts keep today's gate.
   **Single-monitor wording (flagged 2026-07-29).** The budget is global
   while the slots are per-monitor, so at m monitors this allows only one
   frame in flight per monitor and pins each monitor to a single slot at
   even m. It becomes **≤ 2 outstanding PER MONITOR — never a global pool
   of `2m`** (a pool lets one damaged monitor run 4-deep on 2 slots:
   bufferbloat, +2 frames latency, slot aliasing), with a per-monitor slot
   index replacing global `rect_id` parity; clause 4's "no third capture"
   and the loud budget assertion are then stated per monitor — otherwise
   the assertion fires on every legal multimon frame. Beyond a monitor's
   cap the policy stays drop-and-coalesce via the dirty region, never a
   deeper queue. BACKLOG #45 step 6 (D13–D17).
4. **No third capture.** With both slots outstanding, damage
   accumulates only in the dirty region (union + extents collapse), as
   today. Frames are dropped before they exist — the only legal drop
   point, since the single H.264 reference chain (§6.5) forbids
   discarding an encoded frame.
5. Bounded inventory and backpressure (explicit): worst case is 2 raw
   slots + 2 compressed frames in flight (+1 raw frame vs the serial
   design). On a client-ack stall the module ack is withheld, the
   source freezes after at most the slot budget, and coverage merges.
   The encoder input fifo's ≤1 queued depth is enforced remotely by
   the producer gate — implementations MUST assert it (and the
   ≤2-outstanding budget) locally and loudly.
6. Frame N+1 must drain with **no successor damage**: this rides
   existing paths (capture==send in `rdpCapRect`; the encoder thread
   drains its whole fifo per wakeup; enc_done sends are unconditional)
   and MUST NOT acquire a dependency on timers armed by new damage.
   Error paths must preserve ack accounting: an encoder failure/child
   restart with a queued successor still acks every outstanding
   rect_id (a leaked ack is silent half-speed at one, capture freeze
   at two). This is a required test.
7. xrdp requires **no hot-path change** (the per-frame offset is
   already read and bounds-checked offset-relative against the mapped
   size); the xup client-info version bumps and both daemons refuse
   loudly on mismatch.
8. Recorded tradeoff: eager capture carries up to one encode-time of
   content age under saturation (~15–30 ms) — throughput bought with
   staleness. The single-event (r/g/b/w responsivity) path is
   byte-identical to the serial design: with idle slots, capture,
   encode and send happen immediately and nothing waits for a
   successor.
9. Slot staleness re-pack (implementation consequence, 2026-07-26):
   the capture packs only damaged rects and the encoder consumes the
   full plane, so a slot's planes miss whatever was captured into the
   other slot while it sat idle. Each slot therefore tracks a
   per-monitor "missing" region (initialized to the full screen on
   allocation, emptied when the slot is captured, grown by the fresh
   damage that lands in the other slot) which is unioned into that
   slot's next capture region. Without this, damage from frame N−1
   visibly regresses on the wire every other frame.

Expected effect: period drops from the serial sum (~50 ms) to ~the
encode duration (~31–36 ms); composed with FR-PROC-7 preemptive aux
(halved encode during motion) → ~16–18 ms, i.e. ~55–60 fps. Note the
FR-PROC-7 interaction: once preemptive aux lands, the ack for rect N
defers until aux N is sent or preempted (the slot holds the aux
pixels until that decision).

### Integration seams

- `xrdp_encoder_create()` in `xrdp/xrdp_encoder.c`
- shared capture-code/format declarations
- `rdpClientConProcessMsgClientInfo()` and resize/allocation logic in `xorgxrdp/module/rdpClientCon.c`

---

## 8.3 AVC444 view reconstruction and color

### FR-CONVERT-1: Microsoft two-view mapping

Implement the Microsoft AVC444 v1 mapping from the full-chroma source into:

1. a persistent main NV12/YUV420 view; and
2. a persistent Chroma420 auxiliary NV12/YUV420 view.

This is the mapping defined by MS-RDPEGFX section 3.3.8.3.2, not a generic pair of chroma-downsampled images.

### FR-CONVERT-2: Exact color conversion

Use the MS-RDPEGFX **full-range BT.709** forward transform. For 8-bit `R`, `G`, and `B`, use the specification's integer form and clamp each result to `[0,255]`:

```text
Y = ( 54*R + 183*G +  18*B) >> 8
U = ((-29*R -  99*G + 128*B) >> 8) + 128
V = ((128*R - 116*G -  12*B) >> 8) + 128
```

Implementation must make signed arithmetic and rounding/shift behavior explicit and match specification test vectors. In particular, do not replace the signed `>> 8` operation with C integer division that truncates negative chroma numerators toward zero; use an explicit portable arithmetic-floor helper. Alpha is ignored for the video planes.

The FFmpeg command marks the input/output as full-range BT.709:

```text
-color_range pc
-colorspace bt709
-color_primaries bt709
-color_trc bt709
```

These metadata options do not replace the required pixel-domain formula. The startup probe records the resulting stream metadata but MSTSC wire acceptance and visual vectors remain authoritative.

### FR-CONVERT-3: Complete-view reconstruction for MVP

For every submitted desktop update, reconstruct **all visible pixels** of both main and auxiliary views from the persistent XRGB surface, then refresh deterministic padding. This intentionally avoids incremental AVC444 mapping errors in the prototype and is consistent with the accepted decision that conversion compute is not an MVP concern.

Dirty/compression rectangles remain useful for:

- deciding whether an update exists;
- coalescing unsubmitted work;
- constructing the conservative RDP region metadata; and
- future optimization measurements.

They do not limit which source pixels are converted in MVP. Empty/no-damage work is not submitted. Incremental luma/chroma reconstruction and independent damage tracking are deferred.

### FR-CONVERT-4: Padding

The converter owns padded main and auxiliary buffers sized to coded dimensions. Right and bottom padding must be deterministic and initialized. Replicate the final valid column and row into padding so prediction/deblocking near visible edges does not reference uninitialized or high-contrast synthetic content.

Because MVP reconstructs the complete views, every submitted update refreshes all right/bottom padding edges.

### FR-CONVERT-5: Buffer mutation and input commit

Once the first byte of a pair is written to the child, neither reconstructed buffer may be changed until both complete raw pictures have been accepted by the input pipe. The MVP performs conversion and transfer synchronously in the existing encoder worker, so one persistent main buffer and one persistent auxiliary buffer are sufficient.

### Suggested interface

```c
int
xrdp_avc444_update_views(
    struct xrdp_avc444_converter *ctx,
    const uint8_t *xrgb,
    int xrgb_stride,
    int actual_width,
    int actual_height);
```

---

## 8.4 External FFmpeg process

### FR-PROC-1: Spawn model

Invoke an absolute or administrator-approved FFmpeg executable directly with `posix_spawn()` or `fork()`/`execve()`. Never invoke a shell.

### FR-PROC-2: Single handle contents

The one MVP external handle owns:

- child PID and generation;
- raw-input write fd;
- NUT stdout read fd;
- stderr read fd;
- actual and coded dimensions;
- input picture counter;
- pending output-tag FIFO;
- NUT parser state;
- H.264 validation state;
- persistent main and auxiliary NV12 buffers;
- active argv/profile identity; and
- health, timeout, and restart counters.

### FR-PROC-3: Descriptor layout

Prefer a dedicated inherited raw-media descriptor:

```text
parent raw writer ──> child fd 3
child stdout ───────> parent NUT demuxer
child stderr ───────> parent diagnostics
child stdin ────────> /dev/null
```

This permits `-nostdin` and keeps FFmpeg interactive stdin separate from media input.

### FR-PROC-4: FD behavior

Parent fds are nonblocking and close-on-exec. Child inheritance is restricted to intended descriptors. The parent drains input progress, stdout, and stderr in one `poll()` loop.

### FR-PROC-6: vmsplice-only input feed (2026-07-26, owner directive)

`vmsplice(2)` is the ONLY permitted mechanism for moving media bytes from
xrdp to the FFmpeg child. No `write()` on the media descriptor, no staging
buffer, no memcpy of pixel data anywhere in xrdp's hot path.

1. The runner queues borrowed `{pointer, length}` segments (capture shmem
   for live frames; heap fixtures for the probe) and the poll loop feeds
   them with `vmsplice(fd, iov, 1, SPLICE_F_NONBLOCK)`.
2. `SPLICE_F_GIFT` is forbidden — the pages belong to the xorgxrdp shmem.
3. Borrowed segments never outlive the encode call that queued them
   (FR-CAPTURE-6 amendment): unsplice'd input at wait end is an error and
   replaces the child.
4. The pipe capacity is raised best-effort via `F_SETPIPE_SZ` to reduce
   syscall count; failure to raise it is not an error.
5. Page-aligned segments take the kernel's reference path (true zero-copy);
   unaligned tails fall back to an in-kernel copy — still never a
   user-space copy.
6. **Why the feed is lazy (rationale, recorded 2026-07-29).** Queueing and
   transferring are deliberately separate: `in_iov_push()` only declares
   intent, and `feed_vmsplice()` runs solely inside `pump()`. This is
   forced, not stylistic. The pipe holds ~1 MB against a ~15 MB 4K frame,
   so one frame needs many rounds of splice-then-wait, with
   `SPLICE_F_NONBLOCK` returning partial counts and `EAGAIN`; feeding must
   therefore be readiness-driven, i.e. inside the `poll()` loop. Eager
   feeding would have to block on the input direction while the child's
   stdout/stderr go unread — the documented deadlock (§"A blocking 'write
   two frames, then read two packets' implementation can deadlock"). Any
   future `submit_single()` must respect this: it pumps until
   `!in_iov_pending()`, and the "drive all directions concurrently" rule
   is what a `main‖aux` union poll extends from one child to the pair.

### Concurrency state of the encode pipeline (measured 2026-07-28; the baseline FR-PROC-7 builds on)

Verified in code, not assumed. Keep this table current — it is the map every performance decision starts from.

| Stage pair | Concurrent? | Evidence |
|---|---|---|
| capture ‖ encode | **CONDITIONAL for m = 1** — true only while the encoder is slower than the capture path (see the 2026-08-01 correction at the end of this cell); **partial and accidental for m ≥ 2** | `XUP_CAP_AVC444_SLOT_COUNT = 2`; `rdpClientConCapSlotIndex()` alternates on `rect_id`; `MaxOutstandingRects() = 2` for `CC_GFX_AVC444`. Measured **at one monitor** (1600×912, 2026-07-29): frame period equalled encode duration at p50 27.9 ms with `cap->enc_entry` adding a further 23.8 ms that never reached the period — **but that run also had the per-frame trace on `log.c`, so the durations are unverified (see #61h below)**; capture appearing fully hidden is the claim #61e has to re-establish. Cost: up to one frame of added latency. **Multimon was never in this FR's scope** (clause 3 relaxes the gate to two outstanding rects globally, with no `monitorCount` term) and behaves differently: the per-item ack (`xrdp_mm.c:4088`) frees monitor 0's slot when monitor 0's encode ends, so the overlap that occurs is *cross-monitor* interleaving, while the two-slot mechanism itself is inert — `rect_id` advances by m between a monitor's consecutive sends, so at even m the `(rect_id + 1) & 1` parity is constant per monitor and each monitor is pinned to one slot. **Measured 2026-07-29** (BACKLOG #45 recon gate R1; arm-q, 2 × 1024×768, 1100 sends): same slot on **1079/1079** full-pass consecutive sends, one monitor never left its first slot at all, and a monitor held two outstanding frames in two slots **once in 1100 sends (0.09 %)** — the two-slot mechanism is inert per monitor at m = 2, while the global budget saturates (543/1100 sends at depth 2). **FIXED 2026-07-29 by BACKLOG #45 step 6** (xorgxrdp `d77d054` + xrdp `6f80b0fe`): the budget is now m INDEPENDENT caps of 2 (never a pool — D13), the slot is a per-monitor counter advanced once per that monitor's send (D17, no layout or contract change), the completed-scan clear is replaced by an explicit coverage intersect (D16), and the accounting is pure logic in `common/xup_client_info.h` (`struct xup_cap_budget`) so it is unit-tested in xrdp's `make check` — xorgxrdp has no C test harness. **Consequence measured into every subsequent number:** with the slot alternating again, each capture must re-pack the previous frame's damage for that monitor (FR-CAPTURE-8 clause 9), which was INERT while parity pinned the slot. That union is also what goes on the wire as the frame's dirty rects, so the E5 baseline (51.1 ms, taken before step 6) never paid it and the FR-H264-8 bandwidth gate now measures a different capture region. **ALL 2026-08-01 TIMING IN THIS CELL IS VOID (BACKLOG #61h).** A correction dated that day reported the row FAILING with the emit split on — worker `wait` 2.249 ms/cycle over a 36.764 ms period, 544 of 1543 cycles stalling, a capture/encode margin falling from 15.8 ms to 1.9 ms, and a further root-cause putting an 8.0 ms capture trigger and a 16.2 ms xup transit behind a 17 ms egress window. Every one of those runs carried ~12 unbuffered `log.c` writes per frame, nine on the xrdp main thread, INSIDE the period being attributed — and the 17 ms window was the interval between two of those writes. The numbers are withdrawn; the runs are deleted from the tree and in git history only. **SETTLED 2026-08-01 on the ring-traced build (arm x013, one monitor at 3840×2400 = 9.22 Mpx, textflood, eager ack + emit split, 2227 sends, 0 trace drops): the row HOLDS at m = 1.** The worker's `wait` bracket — time spent holding nothing to encode — is **0.0019 ms mean with 0 of 2226 cycles above 1 ms**; **100 %** of frames were already on the fifo when the worker asked for one; and `fifo_to_proc_depth` was **0 at all 2227 takes**. Gate 2b was run before this was believed, because the worker skips the wait entirely when it carries items and an empty bracket could have been empty by construction: `wait_beg` fired in all 2227 cycles holding `n_items = 0` in every one, so the worker entered a real blocking wait each cycle and it returned in ~2 µs. **The CONDITIONAL in this row's verdict is the whole content of the result** — capture is hidden *because encode is slow*: the period is 25.474 ms and 100 % of it is encoder-worker serial work (16.654 ms waiting for the two ffmpeg children, 8.802 ms popping NALs and rewriting both views' LTR references, 0.018 ms everything else, per-cycle closure residual max |0.000000| ms, confirmed independently by the 25.46 ms send-to-send interval). A materially faster encoder makes this a live question again, and the 9.22 Mpx capture cost that would decide it is NOT measured here. Record: `docs/experiments/61e-the-period-is-encode-and-rewrite.md`. Two things survive because they are orderings and counts rather than durations: `fifo_to_proc_depth` is **1 at all 3088 enqueues** (the queue never holds more than one frame, so capture can hide behind encode only while encode is the slower of the two), and the xorgxrdp frontier reads `ack = N−2, shown = N−3` at **1524/1533** captures — the two-slot budget is permanently at cap and re-opens once per encoded frame, so FR-CAPTURE-8 buys no lookahead at m = 1. Also surviving, as method: pair by frame IDENTITY, never by time window — an earlier draft of this cell called capture "demand-clocked by the pipeline" on a metric paired by time window, which in the majority of cycles picks up the frame AFTER next. Record: `docs/experiments/61h-the-logger-was-in-the-measurement.md`; follow-ups BACKLOG #61e, #61f. **RE-OPENING PREDICTED AND ARRIVED, SAME DAY (2026-08-01, BACKLOG #75, arm x014).** The cell above says a materially faster encoder makes this a live question again. It did: #75 took the LTR rewrite from 8.802 to 1.362 ms/frame and the period from 25.474 to 18.476 ms, and the row is now **MARGINAL at m = 1, 3840x2400, textflood** — not falsified. 97.85 % of cycles still have an empty `wait` (p50 0.0015 ms) so capture is still hidden for the large majority of frames, but **66 of 3074 cycles (2.15 %) now stall on the producer**, those 66 carry 99.7 % of all wait time, frames-already-enqueued fell from 100 % to 97.8 %, and the p99 send interval REGRESSED from 31 to 46.5 ms while the mean improved by 7 ms. Read `wait`'s SHAPE, never its 0.515 ms mean. **The binding constraint is now the benchmark payload, not the pipeline**: textflood's own interval is 16.91 ms against an 18.476 ms period, an FR-BENCH-1 margin of **1.09x**, with the producer's p90 (19.01 ms) overlapping the pipeline's p50 (17.96 ms). What this row asserts about a REAL desktop workload is therefore untested below ~19 ms and cannot be tested until the faster producer (design B) exists. Record: `docs/experiments/75-the-rewrite-was-re-serialising-the-picture.md`. |
| monitor₁ ‖ monitor₂ | **NO** — multimon the FEATURE ships; multimon CONCURRENCY does not | Do not conflate the two. Shipped: per-monitor encoder handles (`avc444_ffmpeg_handle[16]`), geometry (`avc444_actual_w/h[16]`), capture plane-split offsets, and per-monitor LTR state (`ltr` lives inside each `xrdp_ffmpeg_avc444`) — two monitors work. NOT shipped: any parallelism between them. `xrdp_encoder_create()` spawns exactly ONE worker (`tc_thread_create(proc_enc_msg, self)`, `xrdp_encoder.c:468` — the only such call in the encoder) draining ONE FIFO; monitor 2's damage arrives as a separate EGFX command carrying `mon_index` in `flags >> 28` and is encoded strictly after monitor 1's. The per-monitor arrays are what would make threading here SAFE (state is already fully partitioned, no locking on encoder state) — that is why this is the natural threading axis, not a claim that it is done. **Concretely with m monitors: 2m ffmpeg processes are alive and exactly ONE is fed or awaited at any instant**, in the order main₁ → aux₁ → main₂ → aux₂ (`proc_enc_msg` → `process_enc_egfx` command loop → `gfx_wiretosurface1_avc444` → the SYNCHRONOUS `encode_pair`), so the pair cost is `2m(w+e)` where a set-pump over all children would give `w+e`. Multimon is therefore where serialization hurts MOST, and it needs no new mechanism — the children simply join the same poll set (FR-PROC-7's BREADTH policy is this, not threads). |
| encode_main ‖ encode_aux | **YES since 2026-07-29** (was NO) | **Superseded, kept for the record:** `encode_pair()` used to run `encode_single(main)` to completion, then `encode_single(aux)` — each a synchronous submit-then-block-for-packet round trip. BACKLOG #45 step 5 (`3ceed31d`) replaced that with `submit_pair` / `pump_pairs` / `collect_pair`: both children are armed in ONE `pump_set` poll set with one shared deadline, so the two views encode concurrently on the one worker thread. **Measured 2026-08-02** with the deployed arm's own ffmpeg, VAAPI device and `encoder_args`, inside its pod, at 3840×2400: one poll set **9.7 ms**, strictly one child at a time **19.0 ms** — **2.0×**. Probe: `PR-demo/mac_bisect_matrix/pump_split_probe.c`. |

**FR-ACK-3: the pipeline's concurrency must not depend on more than one
frame in flight (owner directive, 2026-08-02).**

Every stage pair in the table above is expected to hold at
`frames_in_flight = 1`. The client ack window exists to bound what the
*client* has outstanding; it is not a mechanism this server may lean on
to obtain concurrency, and a second in-flight frame is a queue in front
of the display that costs a frame of latency. The design target is
therefore: **all the concurrency we need, at the cost of fif = 1.**

> **NARROWED 2026-08-03 (owner directive), after the provenance trace
> and FR-FLOW-1 below.** `frames_in_flight` is RETIRED as a concept:
> the constant is untethered in GFX (no protocol meaning, see
> provenance) and was doing two jobs at once. They separate:
> (a) The CONCURRENCY requirement stays, restated wire-free — every
> stage pair in the table must hold with the end-to-end wire window at
> its TIGHTEST setting. Pipeline concurrency comes from pipeline
> structure, never from wire-window slack.
> (b) The WINDOW is end-to-end and its correct value depends on the
> deployment's RTT, so it is USER CONFIGURATION, not a PRD constant.
> This document requires that it exist, that it be enforced at capture
> admission (FR-FLOW-1), that it have one documented meaning and a
> stated default — and it no longer requires or names fif = 1.
> (c) The corollary below is NARROWED to short-RTT networks
> (ack latency < frame period): there a tighter window must cost no
> throughput and a regression stays a BUG. On long RTT,
> frame rate ≤ window/RTT is physics (Little's law), not a defect.

The corollary, and it is why this clause was written rather than assumed:
**a throughput regression from fif = 2 to fif = 1 is a BUG in this
pipeline, not a property of the workload.** Measured 2026-08-02 (arms
x014/x015, identical image and `gfx.toml` body, one environment variable
apart, mechanism confirmed on 8056 `send` records): fif = 1 cost **34 %
of throughput** (18.5 → 28.2 ms period) *and* made end-to-end latency
worse (49.2 → 61.5 ms capture to client ack). It cannot be a flow-control
effect: the encoder's own depth is provably one frame — `pump_pairs`
waits for the just-submitted set and `collect_pair` verifies
`desktop_sequence`, so no second frame is ever inside a child — and the
worker's `wait` bracket is 0.002 ms/cycle under fif = 1, meaning it is
never starved. **So the second credit was hiding a stall, and finding
that stall is the top open item (BACKLOG #76).** Until it is found, the
`capture ‖ encode` row above is qualified: what it asserts is measured at
fif = 2, and fif = 2 is not the configuration this requirement targets.

**FR-ACK-3 PROVENANCE (traced 2026-08-03, and it changes what the clause
is claiming).** The window is not this project's design. It arrives with
`fde04e80` (2017-02-11, Jay Sorg, "rfx fixes for large tile sets,
performance change, **Xorg will start next frame earlier**"), whose
parameter was `client_info->max_unacknowledged_frame_count` — the value
the CLIENT advertises in the Frame Acknowledge capability set
(`libxrdp/xrdp_caps.c:743-749`). So the original intent WAS bounding
end-to-end frames in flight, at the client's own stated limit, and the
window was the safety condition attached to a performance change rather
than a flow-control mechanism anyone designed.
**That tether is cut in the GFX path** (`xrdp/xrdp_encoder.c:427-476`):
when `client_info->gfx` is set, `frames_in_flight` is
`DEFAULT_XRDP_GFX_FRAMES_IN_FLIGHT` = 2 plus an environment override,
and the client's advertised value is consulted only in the legacy
`else` branch. In GFX the number therefore has **no protocol meaning**:
not the client's limit, not the pipeline's depth, and nothing re-derived
what it should bound when the tether was cut. Every fif = 1 vs fif = 2
result in BACKLOG #76/#78/#79 is a result about that untethered
constant.

**FR-ACK-3 AMENDMENT — the bound must count the WIRE, not only the
pipeline (owner directive, 2026-08-02).** The clause above says the
window "exists to bound what the *client* has outstanding". Read in code
on 2026-08-02, while designing BACKLOG #79's fix, **it does not do that,
and this document never said what does.** Three facts, none of them
measured — all read from the source, and the last two confirmed by the
#79 layer-1 sweep:

* **Egress is not gated by the window at all.** `enc_done` hands the
  frame to `trans_write_copy_s()` unconditionally (`xrdp_mm.c:4320`).
  Under a 40 ms injected ack delay at fif = 1 — where `client + 1 >
  server` forbids *any* outstanding frame — sends split 556 / 552 / 552
  across 0 / 1 / 2 frames outstanding. Two thirds of all sends violated
  the bound this clause claims the window enforces.
* **The window is applied to the PRODUCER's slot credit instead**
  (`xrdp_mm.c:1697`). So what `frames_in_flight` actually bounds is
  capture admission. Client-outstanding is bounded only *emergently* —
  by starving the producer until the pipeline drains — which is
  approximate (it settled at 2, not 1) and is the entire cost measured
  in BACKLOG #76/#78/#79.
* **There is no other rate control on this path.**
  `trans_write_copy_s()` cannot fail for want of a wire: the remainder
  is `malloc`ed onto the unbounded `self->wait_s` list and 0 is returned
  (`common/trans.c:644-676`). The one byte throttle,
  `si->source[my_source] > MAX_SBYTES` with `MAX_SBYTES` = 0
  (`trans.c:35, 219, 376`), charges bytes only when
  `si->cur_source != XRDP_SOURCE_NONE` (`trans.c:653`), and `cur_source`
  is set only inside a *transport's* `trans_check_wait_objs()`
  (`trans.c:396`) — enc_done arrives on a **wait object**
  (`xrdp_mm.c:4061, 4538`), so a GFX frame's bytes are charged to nobody
  and throttle nothing.

Four requirements follow. They are binding on any change to the ack
path, #79 included.

1. **The gate is on the wrong stage, and one gate may not do both
   jobs.** *(placement SUPERSEDED 2026-08-03 by
   FR-FLOW-1.3 — with capture-frontier admission the egress gate is
   dead code by induction; the two-quantities analysis stands.)* The client ack window bounds what the CLIENT holds, so it
   belongs on **egress**. The producer's slot credit bounds what the
   PIPELINE holds, so it must be gated on the pipeline's own depth. Any
   design in which a single comparison serves both is rejected: the two
   quantities have different correct values, and conflating them is what
   made fif = 1 — a *latency* target — silently impose a *concurrency*
   limit of 1.
2. **"Bufferbloat" in this document covers the wire, not only the inner
   pipeline.** Until today the prohibition (§FR-CAPTURE-8 clause 3,
   "never a global pool of 2m ... bufferbloat, +2 frames latency, slot
   aliasing") was written entirely about capture slots. An unbounded
   `wait_s` is the same defect one stage further out, and worse: it
   grows in the server's heap, it is invisible to every metric this
   project has built, and the frames in it are stale by construction.
   **Every queue in this path carries a stated bound, in frames, and a
   test that the bound holds.**
3. **No design may remove a bound without replacing it.** Specifically:
   ungating the eager SLOT ack from the ack window *without* a
   replacement horizon is **REJECTED, unconditionally** — not as a
   default, not as an opt-in, not behind `eager_slot_ack`, not "for
   evaluation". It would leave the encoder with no rate control of any
   kind against a link slower than its output, which is every WAN. A
   configuration flag does not make an unbounded queue acceptable; it
   only decides who discovers it.
4. **The horizon H, and the two regimes it distinguishes.** *(mechanism SUPERSEDED
   2026-08-03 by FR-FLOW-1.3 — H counted from the egress frontier,
   which is what would have needed an egress hold; the two-regime
   analysis stands.)* The
   replacement is a finite horizon on the slot credit —
   `client + H > server` with H taken from the pipeline's depth (two
   capture slots; three stages) rather than from `fif`. What H buys is
   regime-dependent, and the crossover is **ack latency versus frame
   period**:
   * *Ack latency < period* (LAN): the client's ack arrives before the
     next frame is ready, so H never binds and nothing is ever held.
     Full pipeline speed at fif = 1, with client-outstanding still ≤ 1.
     There is no tradeoff to make here — the stall in this regime is
     pure loss.
   * *Ack latency > period* (WAN): completed frames would accumulate,
     and H is what bounds the accumulation to H frames of buffered
     video and at most H periods of added staleness. Beyond H the
     producer throttles to the link, which is the correct behaviour and
     is what the current code accidentally achieves.

   Measured support for both rows, same sweep: cycles whose credit
   arrived promptly ran at **16.3–16.9 ms in every leg, flat under a
   40 ms ack delay**, while gated cycles went 31.9 → 52.0 ms. H = 2
   covers the measured LAN ack latency (7.6–10 ms against a ~16.4 ms
   period); H = 3 covers ~33 ms. Record:
   `docs/experiments/79-layer1-the-ack-delay-sweep-confirms-the-withheld-slot-credit.md`.

**FR-FLOW-1: backpressure is nearest-neighbour; drop is end-to-end at
the source (owner directive, 2026-08-03).** Binding on every queue and
signal in the frame path, and the test every flow-control change is
reviewed against.

1. **A lossless stall consults only the immediate downstream
   neighbour.** A stage may wait only on "my neighbour has no capacity"
   (capture slot busy, depth-1 stage buffer full) — never on state
   further down the pipe, and never on the network. The 2017 gate
   (provenance above) is the precedent violation: a capture-admission
   signal was made to wait on a client round trip, and the measured
   cost is BACKLOG #76/#78/#79.
2. **The lossy guard runs farthest end → nearest end, and its only
   response is drop-by-coalesce at the source.** The client's ack
   frontier reaches exactly one decision point: capture ADMISSION.
   Denied admission = damage coalesces in the dirty region
   (FR-CAPTURE-8 clause 4) — the frame is dropped before it exists,
   the only legal drop point. No intermediate stage may hold or drop a
   completed frame for wire reasons.
3. **The window counts from the CAPTURE frontier, so egress needs no
   gate.** Admit capture k only while `k ≤ frame_id_client + C`. The
   client frontier only rises and sends are in id order, so
   `k − client ≤ C` at send — by induction every frame that exists is
   inside the window at egress, and an egress gate is dead code.
   Enforcement point: the credit frontier
   `credit = min(frame_id_consumed, frame_id_server + 1,
   frame_id_client + C)`, emitted unconditionally whenever it advances.
4. **C is user configuration, not a PRD constant.** Its correct value
   depends on the deployment's RTT (frame rate ≤ (C + 2)/RTT). This
   document requires only: it exists; it is enforced at admission; it
   has exactly one documented meaning (at most C + 2 frames unacked at
   send — capture rides ≤ 2 slots above the credit); it has a stated
   default, chosen with BACKLOG #81's RTT-harness data, that preserves
   short-RTT behaviour; and its bound has a test. The RTT → suggested
   value guidance belongs with the config docs, fed by #81.
5. **Every queue carries a stated bound in frames and a test**
   (restating amendment clause 2). The egress queue's bound follows
   from 3: ≤ C + 2 frames on `wait_s`.

**IMPLEMENTED 2026-08-03 (BACKLOG #80, behind `eager_slot_ack`).**
Where each clause now lives, so a reviewer can check the code against
the requirement rather than against a description of it:

| clause | code | test |
|---|---|---|
| 3, the credit itself | `xrdp_gfx_credit_frontier()`, `xrdp/xrdp_encoder.h` | `test_credit_frontier_each_term_binds` |
| 3, emitted unconditionally | `xrdp_gfx_plan_acks()` — the whole decision, pure, no branch that can emit nothing while the frontier advanced; called from `xrdp_mm_emit_credit_frontier()`, `xrdp/xrdp_mm.c` | `test_planner_never_withholds_an_earned_credit`, and INV-LIVE in `test_joint_machine_enumeration` |
| 2, one decision point | the region-disposing ack is clamped by the same window (`xrdp_gfx_region_ack_target()`), because it is not `SLOT_ONLY` and therefore also moves the producer's slot frontier | `test_region_ack_obeys_the_same_window` |
| 4, C is user config | `gfx.toml [avc444_ffmpeg] wire_window`, range 1–64, out-of-range REFUSED; documented in `gfx.toml(5)` | `test_tconfig_gfx_avc444_wire_window{,_out_of_range_refused}` |
| the wire bound | — | INV-WIRE in `test_joint_machine_enumeration`, over an exhaustive enumeration of the joint xrdp/xorgxrdp state space; asserted TIGHT (the bound is attained) so it is not a vacuous inequality |
| 5, egress queue observable | `struct trans::wait_bytes`, an O(1) counter; surfaced per frame in the `egress` perf-trace field c, in KiB | live only — the counter is what makes the frozen-client leg of #80 step 4 readable |

Two things the implementation states that the requirement did not, both
recorded because quoting "≤ C + 2" without them would be wrong:

* **The bound is per monitor.** xorgxrdp's capture budget is
  `XUP_CAP_AVC444_SLOT_COUNT` per monitor, never a global pool, so the
  bound is `C + 2·M` frame ids with M monitors.
* **One ack is deliberately not clamped**: the `NOT_DISPLAYED`
  region-return for a frame that produced no output (`xrdp_mm.c`, the
  `!displayed` branch of the `enc_done` handler). That frame never
  reached the transport, so it occupies no wire, and its pixels are
  owed straight back to the producer under FR-ACK-1 Invariant III.
  Releasing its slot does lift the admission ceiling by one frame, so
  "≤ C + 2·M unacked at send" is a statement about frames that reached
  the transport, and a run of discarded frames relaxes it transiently.

**Shipped default C = 2 is a PLACEHOLDER, not a measured value**
(`XRDP_GFX_WIRE_WINDOW_DEFAULT`). It matches the legacy
`frames_in_flight` so short-RTT behaviour is preserved, and clause 4's
"stated default, chosen with BACKLOG #81's RTT-harness data" is NOT yet
satisfied. Do not quote 2 as a recommendation.

**What C costs, measured 2026-08-03 (BACKLOG #80 step 4, capture
`i80_wanpair_20260803_125816_s20`).** #81's netem harness landed and the
frontier ran at 0.078 ms and 40.4 ms RTT, one monitor, C = 1. Two
durable facts:

* **A 4K AVC444 frame is 3 386 KiB on the wire** at 3840×2400
  textflood. The window is denominated in FRAMES, so **each unit of C
  buys up to ~3.3 MB of transport queue per monitor** — the FR-ACK-3
  "queue in front of the display", in bytes. At 40 ms RTT the measured
  queue behind egress was 4.9 MB mean (1.95 frames of a C + 2 = 3 frame
  bound; 0.0 % of samples above it), and it showed up end to end: 4.9 MB
  draining at 58 MB/s is 84 ms, and the server measured egress→client
  ack at 122.9 ms against a 40.4 ms link.
* **The wire bound holds live.** `id_server − id_client` at send never
  exceeded 2 on either leg, against the C + 2 = 3 the enumeration in
  `tests/xrdp/test_avc444_credit_frontier.c` asserts.

Choosing the default is therefore a trade between (C + 2·M)/RTT of frame
rate and ~3.3·C MB per monitor of display latency at 4K. Two RTT points
at one C are not enough to state it; the table clause 4 asks for is still
owed.

**The headroom is real and measured.** Under a 3840×2400 session the NVENC engine runs 25–28 % (peak 43), shader core 4–5 %, clocks 585 MHz of 1590, ffmpeg children ~6 % CPU each, load 0.22 on 4 vCPU — nothing is saturated while a pair costs 67.5 ms. Isolated on the same box: one 4K stream 51 fps (~19.6 ms/frame), the same through a pipe 52 fps (the pipe costs nothing), and **two 4K streams in parallel 53 fps each — concurrency is free**. The 4K ceiling is therefore serialisation, not silicon: ~14 fps at 4K versus ~34 fps at 1600×912 is arithmetic on 6.3× the pixels.

`encode_single()` is already **submit-then-collect** internally (it pushes to the vmsplice iov queue, then blocks in `pump()`), so the call sites split cleanly — but **the split alone buys nothing unless `submit` transfers** (corrected 2026-07-28, refined 2026-07-29). `in_iov_push()` performs no I/O; it appends to an iov array, and `feed_vmsplice()` has exactly one caller, inside `pump()`. A split whose `submit_single()` is only the push half sends nothing to the aux child until `collect_aux()` pumps it, so "submit both, then collect both" stays serial. `submit_single()` must pump until `!in_iov_pending()` without waiting for output; with that, the two children — independent processes with independent fds — genuinely encode concurrently, and sequential writes already yield `2w + e` in place of `2(w + e)` (4K: `e` = 19.6 ms measured, `w` = a ~15 MB vmsplice ⇒ ~43 ms → ~24 ms). A **union-poll pump across all children** under one shared deadline is **REQUIRED, not an optional robustness upgrade** (decision 2026-07-29, BACKLOG #45 D2/D3 — the earlier "measure the simple form first and add the union poll only if the measurement demands it" wording left the design half-specified and is withdrawn). It is not needed to avoid deadlock (the parent is never blocked on the child it is not draining), but `F_SETPIPE_SZ` is applied only to the INPUT pipe (1 MB), leaving the output pipe at the 64 KB default — several times smaller than a 4K intra packet — so an undrained child stalls mid-write and erodes the overlap precisely when packets are largest. The target shape is `n = 4`: main₁, aux₁, main₂, aux₂ armed in ONE `pump_set` call from the ONE existing worker thread.

**What actually couples main and aux (enumerated 2026-07-29).** Very little, and none of it is data. `self->leaf` is a full `struct xrdp_ffmpeg_avc444`: the aux child owns its own `in_fd`/`out_fd`/`err_fd`, `pid`, NUT demuxer, `in_iov` queue, packet FIFO, sequence FIFO and output buffer. The data plane is fully decoupled. What remains is (1) **the single thread of control** — one `proc_enc_msg` worker, and `pump()` polls ONE child's fds, so with a lazy feed the two children are serialized by the *scheduler*, not by any dependency; (2) **`struct xrdp_h264_ltr_state ltr`** — the shared frame_num counter and `started`/`aux_seeded`, order-dependent (main takes N, aux takes N+1) but applied AFTER both packets are collected and costing microseconds, so it constrains collection ORDER only, never encode concurrency; (3) lifecycle/pair state (`rekey_pending`, `ltr_aux_fresh`, leaf caches, ship-the-pair-or-nothing). Consequence for #45 step 5: the fix is not threads. A union poll is the minimal change that lets the ONE existing thread drive TWO independent feed schedules — it adds no ownership and no locking, whereas a thread-per-child would force locking around `ltr` and the pair contract to buy nothing (the work is kernel-side page-reference movement, not CPU). Sequential submit overlaps the ENCODES but still serializes the FEEDS (`2w + e`); the union poll interleaves the feeds too (`w + e`).

**The transport must be set-shaped, not pair-shaped (design decision 2026-07-29).** A `pump2(main, aux)` helper would hard-wire "two issue, two retire" into the transport, which breaks under FR-PROC-7: with preemptive aux the shape is variable (LC=1 main-only, or LC=2 main+aux). The transport therefore takes a SET — `pump_set(kids[], n, deadline)`: arm each child's three fds into one pollfd array, poll once, service each — and the CALLER owns the completion predicate, so shape lives with policy. `n = 1` is LC=1, `n = 2` the pair, and `pump()` degenerates to `pump_set(&self, 1, ...)` leaving existing callers untouched.

**Threads: not on the main/aux axis.** Main and aux share `ltr` (order-dependent: main N, aux N+1) and the ship-the-pair-or-nothing contract, so threading them adds locking around precisely the state that must stay ordered, and buys nothing — the work is kernel-side page-reference movement, not CPU — while fragmenting FR-PROC-6's "drive all directions from one loop" invariant. **Nor on the monitor axis (corrected 2026-07-29, BACKLOG #45 D1).** An earlier revision of this paragraph read "main/aux is a poll-set problem; multi-monitor is a threading problem" — the per-monitor state IS fully partitioned (separate `avc444_ffmpeg_handle[mon_index]`, encoder pair and LTR state), so threading monitors would be *safe*. It is nonetheless **not what we are building**, and "safe but unnecessary" is not a specification anyone can approve. Multi-monitor is a poll-set problem too: with each monitor's damage arriving as its own `fifo_to_proc` item, batching the queued items and arming all `2m` children in ONE `pump_set` gets the same concurrency from the ONE existing thread, with no ownership transfer, no locking around `ltr` or the pair contract, and no fragmentation of FR-PROC-6's "drive all directions from one loop" invariant. **The encoder has exactly one worker thread before and after this work.** A future threading proposal must first demonstrate a measurement that a set-pump cannot reach. **Landed 2026-07-29** (`3ceed31d`, BACKLOG #45 step 5): `pump()` is now the n = 1 case of `pump_set(kids[], n, deadline)` — arm all, poll once, service all, ONE shared deadline (a per-child deadline costs `n x pair_timeout_ms` when one child stalls), and the failing child is identified so the right handle is torn down. The pair path became `xrdp_ffmpeg_avc444_submit_pair` / `_pump_pairs` / `_collect_pair`, the submit/collect construction FR-PROC-7 also needs; `encode_pair()` is submit + pump(1 handle) + collect, so every existing caller keeps the synchronous contract. `pump_pairs` reports the armed child count (2 per handle), which is what E4 asserts — `test_ffmpeg_pump_set_four_views_one_thread` drives four real ffmpeg children through one poll set. Deliberately NOT done: pumping to `!in_iov_pending()` inside submit (BACKLOG 5.3's literal wording). At 4K a frame is far larger than the 1 MB input pipe, so a flush loop there would block on child 1 and re-serialise exactly what the set exists to overlap; the collect loop pumps the whole set instead, which meets the requirement behind that clause.

**FR-PROC-7 corollary — never submit aux and discard it.** The preempt decision must precede aux SUBMISSION, not sit between main-collect and aux-submit. Submitting aux and dropping the packet would advance the aux child's DPB (it coded a P referencing its previous picture) while the decoder never received that picture, so the next aux P would reference a picture that does not exist client-side — chain broken. "Preempted" must therefore mean aux N is never encoded at all; the aux child simply does not receive frame N and its chain stays continuous in its own terms (sparse aux cadence is already modelled by `test_ltr_dpb_sparse_aux_cadence`). This moves the fifo peek AHEAD of submission — a shape the set-pump expresses naturally and a pair-pump cannot.

**Concurrency scaling measured locally (dev box AMD gfx1151 / Mesa VAAPI, 2026-07-29, `PR-demo/vaapi_concurrency_bench.sh`).** Taken BEFORE re-provisioning a cloud GPU, to decide whether #45 step 5 is worth paying for. N concurrent `h264_vaapi` encoders, shipped child argv (rawvideo NV12 in, NUT + `h264_mp4toannexb` out, `-async_depth 1 -bf 0 -refs 1`), file input so ENCODE is isolated from the vmsplice feed:

| N | 1920×1088 ms/frame | 3840×2400 ms/frame | per-stream cost vs N=1 |
|---|---|---|---|
| 1 (one view) | 2.14 | 6.97 | baseline |
| 2 (main+aux, one monitor) | 1.92 | 6.53 | **0.90× / 0.94× — free** |
| 4 (two monitors × two views) | 2.97 | 11.40 | 1.39× / 1.64× |

N=2 is free at both resolutions (marginally FASTER per stream — concurrent submission hides per-frame submission latency). N=4 costs well under the 4× that saturation would imply. So at 4K the pair goes `2 × 6.97 = 13.9 ms` serial → `6.5 ms` concurrent (**2.1×**), and two monitors `27.9 ms` → `11.4 ms` (**2.4×**).

**These are AMD VAAPI numbers, NOT nvenc/T4 numbers** (this box does 4K in 6.97 ms against the T4's 19.6 ms — roughly 3× faster), and per the stand-in rule they may generate hypotheses but cannot convict or exonerate the T4 path. What transfers is the RATIO, and there the two agree independently: local N=2 = 0.94×, T4 N=2 = 53 vs 51 fps = 0.96×. The conclusion "two concurrent encodes are free" now rests on two different vendors' hardware.

**What `main‖aux` can and cannot buy (gain model — measure, do not assume).** The end-to-end quantity is the **frame period**, not encoder milliseconds, and parallelising the pair only removes the smaller of the two per-child encode terms:

| term | 3840×2400 measured | changed by `main‖aux`? |
|---|---|---|
| pair cost in `encode_pair()` | 67.5 ms | partly |
| one isolated 4K encode | 19.6 ms (51 fps) | — |
| two isolated 4K encodes in parallel | ~19 ms each (53 fps each) | — |
| unattributed remainder (67.5 − 2 × 19.6 ≈ 28 ms) | **not yet attributed** | **no, if it is per-pair rather than per-child** |

**MEASURED OUTCOME of the multimon batching (2026-07-29, REASSESSED
2026-07-30 — BACKLOG #45 GATE RESULTS and #52; arm-r, 2560×1440 +
3840×2400, oracle client, 1688 pairs per view).** The oracle frame
interval went from 51.1 ms to **52.5 ms mean (0.97×)** — a null result
that turned out to measure the payload, not the server; the real
outcome, **2.13×**, is recorded two paragraphs down. The batching
mechanism works — the worker demonstrably armed four children in one
poll set — but under this payload it fired in only **21 of 3346 worker
cycles (0.6 %)**.
The first attribution blamed the capture side; the timestamp-repaired
reanalysis moved it one level upstream: **the gate's payload
(`SESSION_KIND=code`, a `sleep 0.1` scroll loop) clocked the entire
experiment**. Measured on repaired stamps: per-monitor period p50
102 ms with the monitors 26 ms apart in phase, every steady-state long
gap exactly one skipped payload beat, service per pair **11.8 ms**
(encode collect 4.2 + rewrite/emit 7.6 — the earlier "~26 ms
encode-and-emit" was an artifact of the log-clock bug below), oracle
ack 2.1 ms, worker busy **22 %**. Both the baseline and the measurement
ran the same 10 Hz payload, so the 0.97× ratio compares metronome to
metronome: it could not show an encoder-side gain and neither convicts
nor exonerates the capture path. The saturated re-benchmark (unclocked
`codeflood` payload, flood-vs-flood baseline) is **BACKLOG #52
(E5-2)**. Corroboration unchanged: with a slower consumer (the
rendering client) the batch fired in 11 % of cycles, and the
end-to-end rate was unchanged at 2.96 pairs/s per monitor against 2.97
before the work. **Instrument caveat for every ms-level number derived
from xrdp logs to date: upstream bug `common/log.c:1159` prints the
leading digits of `tv_usec` as the millisecond field (`tv.tv_usec +
500 / 1000`), so ~10 % of log lines are stamped up to ~0.9 s late.
Means over long windows are robust; percentiles and two-line deltas
are not. Fixed by #52 step 0.**

**THE MULTIMON BATCHING IS WORTH 2.13× — measured 2026-07-30 under a
saturating payload (BACKLOG #52 / E5-2; evidence
`PR-demo/mac_bisect_matrix/captures/e52_flood2_arm-s_20260730/`).** Two
arms differing ONLY in the xrdp-side steps 5+7 (same xorgxrdp, same
encoder block, same geometry, same `SESSION_KIND=codeflood` payload,
180 s each, oracle client, log clock fixed on both): the baseline
(steps 0–4) delivers **63.6 ms mean per send** (15.72 sends/s,
per-monitor period 127.4 ms) and the batching build **29.9 ms**
(33.40 sends/s, per-monitor period 59.9 ms) — **2.13×**, over the 2.0×
gate. Pictures pushed in the same window went from 4 938 MiB to
10 434 MiB (234 → 495 Mbit/s). `kids_armed=4` in **52 % of 3889
cycles** against 0.6 % at 10 Hz, so step 7's premise is exercised, and
the E2 wire assertions hold under the flood (7/7, zero black frames,
~0.93 MB per picture).

**On the T4 the same code is worth 1.5×–2.3× (2026-07-30, BACKLOG #55/#60;
evidence `PR-demo/mac_bisect_matrix/captures/e52_t4_*_20260730/`).** Same
A/B, run on the representative low-to-average old-CPU target (Tesla T4 /
NVENC, 4-vCPU Xeon 8259CL): the 180 s pair gave baseline **77.3 ms** →
batched **46.3 ms** (per-monitor period 155 → 92 ms, `kids_armed=4` in
**93 %** of cycles) = 1.67×; repeats found the box **bimodal**, and two
further pairings gave 1.51× and 2.26×. Every pairing clears 1.5×, so the
conclusion holds while the single number does not — **quote the band**. The
bimodality is not root-caused (#60); a pairing is only trustworthy when
both arms report the same mean bytes per picture, which the 180 s pair does
(602.7 vs 594.0 KB).

AMBER, and attributed rather than re-tuned: the session **Xorg is a single
thread at ~92 % of one core**, and the profile says where it goes (#59) —
payload glyph+scroll+fill rendering **44.9 %**, the X **Present** extension
running in software emulation **18.8 %**, and xorgxrdp's **entire capture
just 13.8 %** (~12 ms of a 92 ms period, matching `avc444_pack_bench`'s
12.6 ms prediction to 5 %). The four NVENC children cost ~7 % of a core
each, the worker idles 55 % of the time, and flow control never binds. So
the capture is *not* the dominant term even on the box where the pipeline
is capture-bound: the X server is, and 12 ms of the capture's cost is
merely stuck on the same single thread — which is why #54's remedy is to
move the pack off that thread rather than to make it faster.

**Quote the ratio with its box**: 2.13× is a VAAPI/32-core number and
~1.7× is what a 4-vCPU NVENC box gets, and the second is the one a reader
with old hardware should expect.

Three durable qualifications on that number:

1. **A payload that damages both monitors is part of the measurement.**
   The first flood attempt scored 0.91× because a 27-column corpus line
   in a 6400 px xterm left the second monitor blank (167 MB of pictures
   on monitor 1 against 0.75 MB on monitor 2) while it still took
   full-monitor damage every cycle. A parallel set has nothing to
   overlap when one member encodes an all-skip frame.
2. **Idle-monitor coupling is a real regression of ~9 %.** That first
   pair is the measurement of the common "one active monitor, one idle"
   desktop: the shared deadline ties the active monitor to the idle
   one's full-area capture and upload, and the batch loses to the
   serialized path. Arming a monitor only when its pixels changed is
   the open fix.
3. **The remaining headroom is capture-side.** At 2.13× the worker is
   still 32 % busy: per-pair service 14.7 ms (encode collected 2.8 +
   rewrite/emit 12.3) against a 59.9 ms per-monitor period, with the
   wait sitting on the next damage handoff (41 ms p50). Flow control
   never binds (un-acked p50 0 / max 4 of fif = 2, client
   `queue_depth` 0) and it is not bandwidth (495 Mbit/s over loopback).

So the honest bound is: best case ≈ 48 ms (one encode term removed) ⇒ ~21 fps; the advertised "~20 ms ⇒ ~40 fps" only follows if the 28 ms remainder is itself per-child work. **Attributing that 28 ms with `PR-demo/t4_profile/frame_accounting.sh` is a prerequisite to quoting any speed-up**, not a follow-up. Two further ceilings sit above it: the frame period is `max(capture, encode_pair)` under FR-CAPTURE-8, so a capture stage that is currently hidden can become the new bottleneck and absorb the whole win; and the *client* can be the binding constraint entirely — xfreerdp's software 4:4:4 reconstruction measured ~65 ms/frame at the owner layout, capping end-to-end at ~15 fps regardless of server speed (§FR-PROC-7 clause 9). **Confirmed at dual-monitor 2560×1440 + 3840×2400 on 2026-07-29** (BACKLOG #45; one arm, 60 s each, back to back): the rendering client delivered 5.94 sends/s (2.97 pairs/s per monitor, send-gap mean 169 ms) against the oracle client's 19.57 sends/s (9.79 pairs/s per monitor, mean **51.1 ms**) — **client-bound by 3.29×**, the client costing ~117 ms per surface frame on top of the server's 51 ms. Consequence: a server-side speed-up is chased and gated on the **oracle frame interval** (the send-to-send interval with a client that acks before decode/present); the rendering client's rate is reported beside it as the end-to-end figure but cannot show a server gain until the client side moves. Report the T4 gain per client (mstsc / macOS / xfreerdp), each as a frame period, and say which of the two instruments produced each number.

### FR-BENCH-1: The saturating-producer contract (owner directive, 2026-07-31 — PASSING as measured; the per-run verification below is what caught its own filing being wrong)

The benchmark producer (`PR-demo/textflood/`) exists to make the
pipeline the bottleneck. Its design intent is three requirements, in
priority order:

1. **Strictly faster than the pipeline under test**, at every geometry
   it gates. Not "fast", not "faster than xterm" as an end in itself:
   the producer must keep damage pending at every pipeline completion,
   because every property this benchmark judges is undefined when the
   producer is the limit — the E5 ratio measures the producer's cadence,
   capture ‖ encode cannot be observed (no second frame exists to
   capture), and FR-PROC-7's preemption signal ("successor physically
   present in the fifo at pop time") never fires. Clause 2 of FR-PROC-7
   below warns that a starved fifo degenerates its design into an idle
   timer; a slow producer realizes that degeneration by another route.
2. **Representative pixels**: CPU-rendered, subpixel-antialiased,
   colored text — the real desktop workload and the maximal AVC444
   chroma stressor.
3. **Minimal X-side and system footprint**: the X server's cost is one
   blit, and the producer must not perturb the measurement by competing
   for the cores the pipeline needs (the reference box has 4 vCPUs).

**Saturation is verified per run, never assumed.** A gate run is valid
only if BOTH hold, and the harness VERDICT must print both:

- **Producer telemetry**: the producer logs its own frame timestamps;
  its standalone rate (`--selftest`, no RDP session) is ≥ 2× the
  pipeline's measured sends/s at the same geometry on the same box.
- **In-run observable**: damage is pending at pipeline completions —
  operationally, the frame-identity-paired overlap gap
  (`e52_period_decompose.py`) goes negative in a nonzero fraction of
  frames, or an xorgxrdp-side trace shows capture N+1 starting during
  encode N.

A run that fails either check is **producer-limited: it is not an E5
result and can neither confirm nor falsify any pipeline property.** It
is reported as VOID with the producer's own rate beside the pipeline's.

**Status: PASSING as measured (#65 step 0 — chain since renumbered, now #71, 2026-07-31, T4 m=1
3840×2160).** With `--stamps` telemetry (default-on): the producer runs
at **27.66 fps** against the pipeline's 8.21 sends/s — 1.7× over the
floor, p50 2 fresh damage frames pending during every encode. Both
verification checks green. The section's original FAILING status was
filed on "textflood delivered 8.19 fps", which conflated the PIPELINE's
send rate with the producer's frame rate — exactly the unverifiable
inference this FR's verify-per-run rule exists to forbid, and its own
step-0 instrumentation is what caught it. The serializer is the
pipeline's ack-paced capture (BACKLOG #70; measured to 0.0 ms
unattributed, commit `0db74f6e`): the capture arm is
phase-locked to the previous frame's ack (stdev 11.8 ms) and
uncorrelated with damage arrival (stdev 30.7 ms), while the per-monitor
budget's second slot is never used.

**Compute is NOT the constraint (recon 2026-07-31,
`PR-demo/textflood/ring_recon.c`, run ON the T4, offline).** An earlier
revision of this paragraph asserted "~100 ms cairo render, a single
render thread cannot exceed 10 fps" — that number was an unmeasured
inference and the recon falsifies it:

| producer design | T4 ms/frame | fps | vs 16.4 fps floor |
|---|---|---|---|
| A full-frame live render (shipped loop, verbatim) | 24.1 | 41.4 | 2.5× |
| B memmove scroll + strip render (live text kept) | 7.1 | 141 | 8.6× |
| C pre-rendered ring (steady state = 1 memcpy) | 6.8 | 147 | 9.0× |

Two concurrent frame-sized copies sustain 8.8 GB/s aggregate (near-2×
single-thread) — memory bandwidth is not the wall at this depth either.
So the producer computes 41 fps offline yet delivers 8.19 fps deployed:
a **5× gap that no bench above explains**. The open hypothesis is the
serialized `render → XShmPutImage → XSync` loop paying the X thread's
own per-frame work (blit copy, damage, the ~20 ms capture pack) inside
every `XSync`, plus possible phase effects with the deferred-update
timer. Since decomposed: the i55 uprobe redo (commit `0db74f6e`)
located the pacer in the pipeline's ack emission, not the producer —
see BACKLOG #70. Design consequence:
the fix is **decoupling** (render the next frame during the previous
frame's sync; bound outstanding blits at 2), and design B is preferred
over the ring because it keeps live per-frame CPU text rendering
(requirement 2 in its strictest reading) at an 8.6× compute margin and
~18 % of one core at a 25 fps target (requirement 3). The ring remains
the fallback if in-session measurement shows even B producer-limited.
Consequences until fixed: the 2026-07-31 m=2 textflood A/B (1.41×) is
annotated as producer-confounded — both arms may have been paced by the
same producer and the batch's true gain understated; the m=1 "0/205
overlap" T4 run convicts the producer, not the pipeline; and the PRD's
`capture ‖ encode = YES for m = 1` row is CONDITIONAL on this contract
holding, which its 1600×912 evidence satisfied and 4K does not.
Tracked under the linear chain **BACKLOG #70 → #70B → #71 → #72 → #73** (renumbered twice, last 2026-07-31 after the m=1 serializer was measured; earlier chain forms and this paragraph's history at commit `0db74f6e`).

### FR-TRACE-1: The perf tracer must not be able to perturb what it measures (owner directive, 2026-08-01)

**An instrument that serializes the threads it observes produces
fiction, and it produces it in the shape of a plausible result.** The
perf-trace sink is not a logger and is not held to a logger's
standards: it is a measuring device inside the hot path, and the
following are hard requirements, not preferences.

1. **The SOURCE must never block.** The call site — the `PERF_TRACE`
   macro, invoked from the encoder worker, the EGFX assembler and the
   main thread — must not execute any syscall, must not perform I/O,
   must not allocate, and must not acquire any lock that a slow path
   can hold. Reading a coarse monotonic clock through the vDSO is
   permitted because it is not a syscall. Nothing else is.
2. **SOURCE and SINK must live on different threads, joined by a ring
   buffer.** The source appends a fixed-size record to the ring and
   returns. A dedicated sink thread — and only that thread — drains the
   ring, formats, and writes to disk. The sink is *allowed* to block,
   serialize and be slow, because nothing measured is waiting on it.
3. **Overflow drops, and drops are COUNTED and REPORTED.** When the
   ring is full the source discards the record and increments a
   counter; it must never block, never spin and never grow the ring in
   the hot path. The drop count must be emitted into the trace so that
   an analysis can tell a complete trace from a truncated one. A
   silently truncated trace is worse than no trace (the "no silent
   caps" rule).
4. **No shared `FILE*`, ever.** More than one thread using stdio on one
   stream is the defect this requirement exists to forbid.

**Why this is a requirement and not a nicety — the measurement it
destroyed (2026-08-01, BACKLOG #61e).** The original sink was a
`fprintf` onto a shared `FILE*`. `fprintf` takes `flockfile`, so every
event serialized against every other event *in whatever thread issued
it*, and the wait landed inside whichever stage bracket happened to be
open. While that sink was written to by ONE thread it was invisible.
Adding a single `enq` record on the **main** thread — one event per
frame, ~140 events/second total — put a second thread on the stream for
the first time and the measured frame period went **40.4 ms → 135.3 ms,
a 3.3× regression that existed only while the instrument was armed**.
The stages that inflated were exactly the CPU-side ones (`subm`
3.53 → 16.60, `emit` 5.89 → 30.06, and a `book` bracket containing three
integer increments and one log write measured at **12.55 ms**), while
the child-blocking `pump` was untouched — the signature of serialization,
not of work. Two hypotheses were floated and killed by measurement
before the real cause was found: log VOLUME (identical at 14.1
lines/frame, and 3.3× *fewer* per second on the slow arm) and pod
CPU/memory (`avc444_pack_bench` identical at 3.32 vs 3.23 ms/frame).
**The instrument was the bug.**

**Implementation decision (2026-08-01): an in-tree, per-thread SPSC ring
in C. `spdlog` was considered and REJECTED.**

spdlog was the obvious off-the-shelf answer — Debian ships it
(`libspdlog-dev`, trixie 1:1.15.2), it is MIT, and its async mode has
the right shape (a `circular_q` drained by a dedicated backend thread).
It was rejected on two counts, both recorded so the decision is not
re-litigated from scratch:

- **It would put `libstdc++` into the shipped `xrdp` binary**, which
  links no C++ runtime today — only the optional `vrplayer/` Qt tool is
  C++. Paying a permanent runtime dependency in the RDP server for a
  diagnostic that is disarmed in production is the wrong trade, and it
  is a dependency the upstream `devel` PR would rightly refuse.
- **It is not lock-free anyway.** `mpmc_blocking_q` is a `std::mutex`
  plus condition variables around the ring, so the source still takes a
  shared lock — a weaker guarantee than clause 1 deserves, for a
  library whose whole appeal was not having to think about this.

The in-tree design gives a **stronger** guarantee than spdlog would:

- **One ring per producer thread**, claimed from a fixed pool at first
  use, so producers never contend with each other at all — there is no
  shared lock on the source path, not merely a short one. Each ring is
  single-producer / single-consumer, so `head` is written only by its
  producer and `tail` only by the sink.
- **The pool is allocated when the sink is ARMED**, never on the source
  path, so clause 1's "must not allocate" holds literally. A thread that
  finds the pool exhausted drops and counts, and never blocks.
- **`tag` is stored as a pointer, not copied** — every call site passes
  a string literal, which is a documented precondition of the API, and
  it keeps the source path free of any formatting work.
- **Only the sink thread ever touches the `FILE*`**, satisfying clause 4
  by construction rather than by convention.

The ring's push/pop/overflow behaviour is pure logic and is unit-tested
in `tests/common/` against the SPSC specification (capacity `N` yields
`N-1` usable slots; FIFO order; the `N`th push drops and increments the
counter) — the expected values come from the ring specification, never
from running the implementation.

### FR-ACK-1: WITHDRAWN 2026-07-31 — see NG-9 and BACKLOG #70

The filing (ack-on-consume as the fix for a rect_id "ghost") was
refuted the same day it was specified: the ack value is an echo and
never drifted. Its machinery — echoed identity, ack totality, the
displayed flag, region return on non-display — survives verbatim in
**BACKLOG #70** (eager slot-release ack) with the corrected rationale
(concurrency, not correctness) and an earlier emission point
(max(absorb N, egress N−1)). The eager ack is only half the change:
see **FR-ACK-2**, which makes the assembly split of BACKLOG #70B a
requirement rather than a follow-up, with the measurements showing why
the ack alone only relocates the wait. Full former text of this FR, with the
invariant proofs, is preserved at commit `0db74f6e`; history pointers
in NG-9.

### FR-ACK-2: the eager slot-release ack is incomplete without the emit split (2026-08-01, measured)

> **SUPERSEDED IN PART, 2026-08-01 (same day), by measurement.** The
> split was built and measured twice.
>
> Under the `codeflood` payload it came out at **0.96x** (33.3 ->
> 34.7 ms) — but that measurement is VOID as a throughput number: #61c
> showed the session Xorg was at **96.4 % of one core** and at 98.9 %
> with no client attached at all, so the producer set the period and the
> worker had 28 % slack before the split was applied. Re-run under
> `textflood` at 3840x2400, with the producer at 25.3 % and FR-BENCH-1
> passing at a **2.61x** margin, the same knob measures **1.12x**
> (40.1 -> 35.9 ms).
>
> **The acceptance criterion below is still not met**, and the
> 1.22x-1.44x projection stays withdrawn — now for a reason that
> survives the payload fix. It was computed at 2560x1440, where `emit`
> was 6.39 ms of a 24.02 ms serial chain (27 %). `pump` and `coll` scale
> with pixel count and `emit` does not, so at 3840x2400 `emit` is 17 %
> of a 35.04 ms chain and the most the split could buy is smaller.
> **The size of the gain is resolution-dependent; the mechanism is
> not.** What the textflood run confirms is that the mechanism does what
> this FR specifies: 5.97 ms of serial work removed, 4.30 ms of period
> recovered (72 % conversion), wire audit 7/7, zero black frames.
>
> The correctness content below — the join point, the thread shape, the
> shared-state rules — held on every run and is NOT superseded.
> Evidence:
> `captures/i70b_x001_ab_20260801 (DELETED by #61h, git history only)`
> (codeflood, void) and `i61b_x004_ab_20260801 (DELETED by #61h)`
> (textflood, 1.12x). Text below kept verbatim, wrong projection
> included.

**The eager slot-release ack (BACKLOG #70) MUST NOT be shipped without
the assembly (`emit`) split of BACKLOG #70B.** On its own it converts a
producer-side wait into a worker-side queue and stops there.

Measured, m=1 at 2560×1440 under a saturated payload (arm-u/arm-v/arm-w,
1290–1730 frames each):

| | shipped ack | eager ack |
|---|---|---|
| `absorb(N) → msgin(N+1)` (p50) | 18.0 ms | **−1.9 ms** |
| `msgin(N+1) → submit(N+1)` (p50) | 2.4 ms | **10.3 ms** |

The eager ack does exactly what it claims — the next frame is *already
in the fifo* before the current one is absorbed on 61 % of frames — and
the wait simply moves in front of the encoder worker. Period improves
1.11×, and no further.

#### Why a ready capture does not stop the children starving

The two FFmpeg children are fed by `submit` and driven by `pump`, and
**both run on the encoder worker thread**. Input readiness is therefore
necessary but not sufficient: any worker-thread time not spent feeding
the children is time they are idle *with work available*. Per 32.56 ms
cycle, measured:

```
pump           10.78 ms   children have work
pump_end -> coll_beg 2.60
coll                 3.22   NUT pop + LTR rewrite
coll_end -> emit_beg 2.25
emit                 5.96   <-- assembly: pure CPU, touches no child
emit_end -> drain    4.04
drain + subm         3.70
               -------
               21.77 ms   children have NOTHING, and a frame is queued
```

**The children are idle 67 % of wall time** while the stage they are
waiting behind is not encoding at all. `emit` is the largest such stage
and is provably independent of them — it reads the *already collected*
bitstream and touches no child, no capture page, and (FR-PROC-6) no
borrowed shmem. That is what makes it separable.

#### The join point is a correctness requirement, not a tuning choice

With `emit` on its own thread, the worker MUST join the previous frame's
assembly **before `collect(N+1)`**, and MUST NOT join it before
`submit(N+1)`:

- **Before `collect(N+1)` — required for correctness.**
  `xrdp_ffmpeg_avc444_collect_pair` returns a pair whose `main_data` /
  `aux_data` point into that handle's own `main_buf` / `aux_buf`, which
  the *next* collect on the handle overwrites in place. Joining any
  later — for instance before the next ack, the intuitive choice — lets
  `collect(N+1)` overwrite buffers the assembler is still reading.

  *Correction, 2026-08-01:* this FR first gave the failure mode as
  "silent wrong pixels, not a crash". That understates it.
  `collect_pair` calls `grow(&self->main_buf, &self->main_cap, ...)`
  before each rewrite, and `grow` **reallocs** — so `collect(N+1)` can
  free the very allocation `pair.main_data` points at. The failure mode
  is a use-after-free that presents as wrong pixels *most* of the time.
  The consequence for the design is that the stated alternative —
  "double-buffer those two buffers per handle" — is not sufficient on
  its own: alternating two buffers still reallocs the one being written.
  Either the join stays where it is, or the handoff takes an owned copy
  of the two byte ranges.
- **Not before `submit(N+1)` — required for the gain to exist.**
  Joining at the top of the loop leaves the children idle for the whole
  of `emit`, which is the starvation this FR exists to remove: the work
  would have moved to another thread and bought nothing.
- **Between them it is free.** `emit` (5.96 ms) fits entirely inside
  `submit(N+1) + pump(N+1)` (14.5 ms), so the join is not expected to
  block the worker at all at this geometry. A bounded depth-1 handoff
  expresses the join, keeps PDU order trivially (one assembler thread),
  and requires no change to either ack.

#### The assembler is ONE permanent thread, not a thread per frame

**Required shape.** Exactly one assembler thread, created in
`xrdp_encoder_create` alongside `proc_enc_msg` and living for the
encoder's lifetime. Spawning a thread per emit is forbidden, for three
independent reasons:

1. **Order.** `fifo_processed` carries the PDU stream in wire order, and
   for a given `XRDP_ENC_DATA` the `last=1` enc_done must be its last —
   that is what releases it (`gfx_close_egfx_msg`, FR-ACK-1 rule 2).
   Two concurrent assemblers give no defined order for either property.
2. **The frame budget below is only provable at assembly depth 1.** N
   concurrent assemblers put N frames in assembly and the resident set
   is no longer `{capture N+2, children N+1, assembly N}`.
3. **Cost.** `clone` + stack + first-touch is tens of µs against a
   5.96 ms body, paid every frame, to buy nothing the permanent thread
   does not already give.

**Handoff.** A depth-1 slot in `struct xrdp_encoder`, guarded by two
counting semaphores (`tc_sem_create`/`_dec`/`_inc`, already in
`common/thread_calls.h`; no new primitive):

```
emit_req   init 0   worker -> assembler: a set is in the slot
emit_idle  init 1   assembler -> worker: the slot is free
```

```
worker                                   assembler
  drain / group                            for (;;)
  submit(N+1)                                tc_sem_dec(emit_req)
  pump(N+1)                                  if (slot.quit) break
  tc_sem_dec(emit_idle)   <-- THE JOIN       for each item: process_enc
  collect(N+1)                               tc_sem_inc(emit_idle)
  release_slots(N+1)  [eager ack]
  fill slot with set(N+1)
  tc_sem_inc(emit_req)
```

The join is one line, and **its position is the specification**: the
`tc_sem_dec(emit_idle)` sits after `pump` returns and before the first
`gfx_batch_collect_one`. Moving it earlier or later is the correctness
question above, not a tuning knob.

This also settles the enc_done ordering that the split would otherwise
put at risk. `fifo_processed` gains a second producer — the worker still
emits the #70 CONSUMED ack from `gfx_batch_release_slots`, while every
PDU and the terminal ack now come from the assembler. The fifo itself is
already safe (`fifo_add_item` under `self->mutex`). What makes the
*order* safe is the join: `release_slots(N+1)` runs after
`tc_sem_dec(emit_idle)`, so CONSUMED(N+1) cannot overtake the terminal
ack of frame N. A deeper queue would break this; depth 1 is what buys
it.

**Teardown.** The worker owns the assembler and joins it: on leaving its
loop the worker sets `slot.quit`, posts `emit_req`, waits for the
assembler to exit, and *only then* sets `xrdp_encoder_term_done`.
`xrdp_encoder_delete`'s contract is therefore unchanged — one wait
object, one 5 s timeout, one `g_free(self)`. This matters because that
delete does **not** join: it times out and frees `self` regardless, so a
wedged worker is already a use-after-free today. The split must not
widen that window, which is why the assembler never signals
`term_done` itself and is never visible to `xrdp_mm`.

#### Blast radius: what `emit` must stop touching first

`emit` is separable because it needs no child and no capture page — but
it is not yet *isolated*. Every `self->` field the AVC444 emit path
touches, classified:

| field | emit | worker | verdict |
|---|---|---|---|
| `avc444_batch_pair[m]`, `_seq[m]`, `_have[m]` | R | W | safe under the join |
| `avc444_surface_id_live[m]` | R/W | — (main thread W) | already `self->mutex`-guarded |
| `avc444_chroma_align`, `_v2`, `eager_slot_ack`, `_ltr_rekey_surface_reset` | R | — | config, written once at create |
| `avc444_seq` | W | W | dead in the batched path (`enc_rv` is forced READY); assert it |
| **`avc444_ffmpeg_handle[m]`** | **W** | **W** | **UNSAFE — blocks the join point** |
| **`avc444_surface_reset_pending[m]`** | **W** | R | **UNSAFE — same fix** |

**The handle array is the blocker.** `emit` writes
`avc444_ffmpeg_handle[m] = NULL` at three sites — the geometry-change
teardown inside `gfx_avc444_handle_for`, the encode-error path, and the
post-ship `rekey_pending` teardown — while `submit(N+1)` reads and
creates through the same array for the same monitor. Joining *after*
`submit(N+1)` therefore races a `xrdp_ffmpeg_avc444_delete` against a
`xrdp_ffmpeg_avc444_submit_pair` on the freed handle. This is rare (a
resize, or one frame in ~65 000) and it is a use-after-free, which is
the worst combination to ship.

**Required before the split, as its own change:** make `emit` read-only
with respect to the child. The worker evaluates
`xrdp_ffmpeg_avc444_rekey_pending(ff)` immediately after
`gfx_batch_collect_one` — it has the handle in hand there — and applies
the teardown at the **top of the next cycle, before `submit`**. Cost:
the re-key is deferred by exactly one frame. The margin covers it with
room to spare: `XRDP_H264_LTR_FRAME_NUM_REKEY` is 2^16−512 and the hard
stop is 2^16−8, so one frame spends 1 of 504.

**Also required:** `avc444_debug_dump` takes `main_view` / `aux_view`,
which point into capture shmem the eager ack may already have released.
The GFX_TRACE `centerY` read is already skipped for exactly this reason;
the dump is not, and moving it to the assembler widens the window from
µs to ms. Under the split the dump either loses its NV12 arguments
(bitstream only) or runs in the worker before the join. A forensic
capture that silently records the *next* frame's pixels under this
frame's sequence number is worse than no capture.

**Out of scope.** The split is confined to the batched branch
(`batching = avc444_ffmpeg && avc444_aux_ltr_chain`). The item-at-a-time
branch — jpg, rfx, h264, non-LTR egfx — keeps calling `process_enc`
inline on the worker and is not to be touched. Like #70, it ships behind
a config knob, default off, until measured.

#### Frame budget

The split does **not** cost a frame of latency and does not alter
FR-BP-2's bound. During `pump(N+1)` the previous frame is *already*
alive on the main thread being egressed; an assembler thread does not
raise the number of resident frames, it relocates work already in
flight. The resident set stays {capture N+2, children N+1, assembly N},
which is what the two-slot capture budget and the `slots + 1` held-region
map (`XUP_CAP_SENT_SLOTS`) already size for.

#### Acceptance

Projected period 22.6–26.6 ms from 32.56 ms — **1.22×–1.44×**; the range
is the 4.04 ms inter-cycle gap, which this change does not determine.
Quote the range, not its optimistic end. The transport is not the
constraint at either figure: the main thread is at ≤39 % occupancy and
binds only near a 12.6 ms period (~79 fps).

### FR-PROC-7: Preemptive aux — LC=1/LC=2 scheduling without an idle heuristic (designed 2026-07-26; ordered AFTER FR-CAPTURE-8, which is its prerequisite)

Supersedes NG-6's deferral. During motion, frames are sent luma-first
(`LC=1`, main view only); the auxiliary chroma is scheduled by
**preemption, not by idle detection**. There is NO timer, NO idle
threshold, NO "is the user active" policy anywhere in the pipeline.

1. After encoding and sending main N (`LC=1`), the encoder thread's
   existing fifo pop is the decision point:
   - pop returns frame N+1 → aux N is **preempted** (superseded — N+1's
     aux is fresher) and main N+1 encodes immediately;
   - pop returns empty → aux N encodes NOW from the already-captured
     slot and is sent as `LC=2`.
   The aux therefore always eventually lands: the only thing that can
   displace it is a newer main, by construction.
2. **FR-CAPTURE-8 is a hard prerequisite.** The preemption signal is
   "successor physically present in the fifo at pop time", which exists
   only when capture overlaps encode (two slots). In the serial
   single-slot pipeline the producer is ack-gated behind the encoder,
   the fifo is always empty at pop time, and any workaround degenerates
   into waiting one ack round-trip — an idle timer in disguise. Do not
   implement FR-PROC-7 on the serial pipeline.
3. The capture contract is UNCHANGED: xorgxrdp keeps packing both views
   every frame (post-FR-CAPTURE-7 the aux pack share is ~1pp — noise).
   The skip saves the aux **encode** (~half the dominant per-frame
   encode term) and the aux **wire bytes** (~half), not the pack.
   FR-PROC-7 is therefore xrdp-only.
4. Slot-ack timing (FR-CAPTURE-8 interaction): the xup ack for rect N
   is deferred until aux N is **sent or preempted** — its slot holds
   the aux pixels until that decision. Worst case one extra aux-encode
   of slot hold; the two-slot budget still clears at full rate.
5. Stream construction stays within §6.5 (as amended) and FR-H264-7:
   two encoder children (main + all-IDR aux leaf child), ONE reference
   chain owned by the main child, monotonic interleave; `LC=2` aux
   frames ship as non-reference, non-IDR I leaves on the established
   chain (their own SPS/PPS never reach the wire); the exactly-one-SPS
   session bound is unchanged. This is the Windows-shaped LC=1 (~93%)
   / LC=2 cadence. Correctness does NOT depend on this cadence:
   reference partitioning holds at any aux rate (owner directive
   2026-07-27) — FR-PROC-7 is a bandwidth/perf lever, never a
   correctness lever.
6. A damage burst arriving while aux N encodes queues normally and
   waits at most one aux encode (~15 ms) — bounded, self-correcting,
   and that frame's aux is again preemptible.
7. Recorded tradeoff: under sustained motion with no damage gap, aux
   starves and chroma rides at 4:2:0 — i.e. AVC420 quality, which is
   what motion gets from every shipping codec today, and consistent
   with real Windows' 93% `LC=1` cadence. A periodic aux override is
   explicitly NOT included; adding one later is a policy change
   requiring owner sign-off and demonstrated visual evidence.
8. Responsivity contract (the r/g/b/w gate): a single event produces
   main N immediately (colors present at 4:2:0) and, with an empty
   fifo, aux N lands one encode later (~15 ms) — full 4:4:4 fidelity
   within one frame time, deterministically. The smoke gate gains a
   color-edge fidelity check after settle to pin this.
9. **Slow-client amendment (2026-07-26, measured).** The clause-1
   trigger (fifo pop empty/non-empty) is server-centric and provably
   blind to a client-bound pipeline: on the T4 offscreen rig the
   encoder idled (admission turnaround 18 ms) while the CLIENT was the
   bottleneck — xfreerdp's software 4:4:4 reconstruction + colour
   convert costs ~65 ms/frame at the owner layout (stack-sampled:
   ~66% `yuv444_context_decode`, ~20% `YUV444ToRGB`; VAAPI hw decode
   changed nothing because H264 decode was never the dominant term),
   capping end-to-end at ~15 fps while an AVC420 (mains-only) stream
   ran 27 fps on the same rig. As designed, pop-empty would fire every
   frame and ship aux to a client that cannot merge it in time.
   Therefore aux N additionally requires **spare egfx ack credit**:
   send `LC=2` only when `frame_id_server - frame_id_client <
   frames_in_flight`; a saturated window defers aux exactly as if
   preempted (it lands through the existing paths when credit frees —
   at idle the acks catch up by construction). This is NOT an idle
   heuristic: the credit state is client-declared flow control the
   server already maintains, and the only parameter is the existing
   upstream `frames_in_flight` knob. Recorded conflation, accepted:
   window saturation cannot distinguish slow-decode from deep-WAN —
   both correctly mean extra chroma bytes will not be consumed in
   time. Fast clients keep today's every-frame aux cadence; slow
   clients get the mains-only rate (measured 27 vs 14.5 fps) with
   chroma convergence on settle, still pinned by the smoke-gate
   edge-fidelity check.

10. **Shared construction (architecture, 2026-07-26).** The atomic
    blocking encode call is the single cause of three measured
    serializations (aux always rides the pair, 44 vs 17 ms; upload
    serialized behind encode, 34 % NVENC idle at max drive; monitors
    serialized against each other, 24 ms QHD queued behind 44 ms 4K
    despite per-monitor ffmpeg children). Lever 2 therefore splits
    encode into **SUBMIT** (feed pictures to the child, non-blocking)
    and **COLLECT** (read packets back when ready) with a
    **pending-completion record** in between: `{enc, mon_index,
    frame_id, got_frame_id, surface_id, pixel_format, codec_id,
    twidth/theight, dst_rect, d_rects (ownership moved), num_rects_d,
    aux_view, nv12 layout}`. The encoder thread loop becomes
    event-driven on the fifo AND the children's stdout fds:
    (1) COLLECT ready packets — in submit order per handle,
    sequence-checked, loud restart on mismatch (the runner's
    integrity contract is unchanged; only the allowed outstanding
    depth grows); (2) emit completions IN ORDER — the data enc_dones
    were sent withholding frame-id+last, the completion enc_done
    carries them (comp_bytes=0, last=1); (3) ADMIT the next submit by
    priority: fifo non-empty → main(s), newer main preempts that
    monitor's pending aux; else pending aux with spare ack credit
    (clause 9) → submit aux; else wait. The ack/slot discipline is
    written ONCE for every pending kind: cumulative-ack ordering
    (flush pendings before emitting any undeferred got_frame_id),
    finalize-as-preempted on ffmpeg death / geometry change /
    teardown (an ack can never leak, a shmem slot can never wedge —
    FR-CAPTURE-8 §4's rect ack defers until aux sent-or-preempted).
    §6.5 (as amended) and FR-H264-7 stand untouched: no second
    reference chain, one SPS, monotonic interleave — the two encoder
    children (main + aux leaf) ARE the FR-H264-7 architecture, and
    ALL concurrency lives in submit scheduling against the existing
    children, never in stream structure.
11. **Three policies, one machine.** (a) **PREEMPT** — clauses 1–9:
    aux as a deferred, credit-gated, supersede-able pending.
    (b) **BREADTH** — multi-monitor concurrent submits: submit each
    monitor's frame to its own child without waiting, collect both;
    dual-monitor cycle becomes max(44, 24) not 44+24.
    (c) **DEPTH** — outstanding ≤ 2 per child: frame N+1's ~14 MB
    upload flows while N encodes, recovering the measured 34 % NVENC
    idle toward the ~11 ms/picture hardware floor (T4, 3840x2400,
    p1 — measured 2026-07-26; profile already optimal: p1 fastest,
    NUT mux free, `ull` slower than `ll`).
    Measured ladder on the reference rig (oracle client, owner dual
    layout): 29 fps today → ~38 (preempt) → ~55 (+breadth) →
    approaching the 2×11 ms floor (+depth). Expectations to verify,
    not acceptance gates.
12. **Completion criteria (owner directive 2026-07-26).** Lever 2 /
    FR-PROC-7 is COMPLETE only when **all three policies are built,
    functional, and validated TOGETHER** on the reference rig: oracle
    server-only fps plus cycle-partition signatures proving each
    mechanism live simultaneously (mains-only stream during motion
    with LC=2 on settle; per-monitor encode windows overlapping in
    the trace; inter-submit gap under the single-encode duration),
    and the full smoke gate including edge-fidelity-after-settle
    green on the same build. **Individual correctness must be proven
    by deterministic offscreen unit tests per policy** (Check
    framework, CI, no GPU / no live session; the ffmpeg child seam
    is mocked at the fd/function boundary; no timers):
    - preempt: pending aux dropped on newer same-monitor main;
      submitted on empty-fifo + spare credit; held while credit
      saturated (clause 9); frame-id/last withheld from data
      enc_dones and emitted exactly once by the completion; ack
      never leaked across preempt / ffmpeg-death / resize / teardown
      finalize paths.
    - breadth: interleaved submits across ≥2 mock handles collect
      without cross-monitor completion reorder; cumulative-ack
      ordering preserved against rect_id order; per-handle sequence
      checks independent.
    - depth: outstanding=2 on one handle collects strictly in
      order; sequence mismatch fails LOUDLY (no silent drop);
      depth bound respected; a slot's pixels never released before
      its submit is consumed.

Expected effect, composed with FR-CAPTURE-8: steady-motion period
~16–18 ms (~55–60 fps) at half the wire bytes, with full-chroma
convergence one frame after any damage gap.

### FR-PROC-5: MVP FFmpeg command

Structural arguments are owned by xrdp. The initial supported profile is stock FFmpeg `libx264`:

```text
<ffmpeg-path>
  -hide_banner
  -nostdin
  -loglevel <configured-level>
  -f rawvideo
  -pixel_format nv12
  -video_size <coded-width>x<coded-height>
  -framerate <2 * desktop-update-rate>
  -color_range pc
  -colorspace bt709
  -color_primaries bt709
  -color_trc bt709
  -i pipe:3
  -map 0:v:0
  -an
  -sn
  -dn
  -fps_mode passthrough
  -c:v libx264
  -bf 0
  -preset ultrafast
  -tune zerolatency
  -crf <configured-quality>
  -g <configured-gop>
  -x264-params repeat-headers=1
  -bsf:v h264_mp4toannexb
  -flush_packets 1
  -write_index 0
  -f nut
  pipe:1
```

With `-color_range pc`, ffmpeg tags the stream as full-range; on 8-bit YUV this surfaces (in `ffprobe`) as the deprecated `yuvj420p` pixel format rather than `yuv420p`. This is expected, does not affect the NUT/H.264 output, and implementers should not treat `yuvj420p` as an unexpected/rejected format.

The argv builder may use the older equivalent of `-fps_mode passthrough` only when the behavioral probe proves identical no-drop/no-duplicate behavior.

### FR-PROC-6: Standard NUT only

Do not pass `-syncpoints none`, `-f_strict experimental`, or any NUT PIPE/no-syncpoint option in MVP. Standard NUT syncpoints are mandatory. `-write_index 0` disables the trailing/growing index while retaining normal syncpoints.

### FR-PROC-7: Structural invariants

User configuration may adjust quality, GOP, preset, and other explicitly allowlisted libx264 options. The command builder must merge any allowed x264 suboptions into one `-x264-params` value while forcing `repeat-headers=1`. It may not override:

- input format or dimensions;
- input/output descriptors;
- stream count or map selection;
- picture ordering;
- `-bf 0` / no-reordering behavior;
- `repeat-headers=1`;
- Annex-B bitstream filtering;
- standard NUT output;
- output destination; or
- lifecycle/termination behavior.

### FR-PROC-8: Future encoders

QSV, VAAPI, NVENC, and other Linux/Unix FFmpeg encoders are deferred. They must land as later profiles with their own behavioral evidence. They do not change the process/NUT/RDP seams unless an actual backend demonstrates a requirement.

---

## 8.5 Raw input stream protocol

There is no custom wire protocol on the FFmpeg input pipe. The stock FFmpeg rawvideo demuxer reads fixed-size frames.

### FR-IN-1

For coded dimensions `CW × CH`, each NV12 picture is exactly:

```text
CW * CH + CW * ceil(CH / 2)
```

when `CW` is even and the UV stride equals `CW`. The implementation should define the exact plane strides and total bytes centrally and reject inconsistent buffers.

### FR-IN-2

A desktop pair is serialized exactly as:

```text
main picture bytes
auxiliary picture bytes
```

No other picture from that H.264 context may be interleaved between them.

### FR-IN-3

The backend must append logical tags to an internal pending-output FIFO before or atomically with submission:

```c
struct xrdp_ffmpeg_picture_tag
{
    uint64_t generation;
    uint64_t desktop_sequence;
    uint64_t input_picture_index;
    enum { AVC444_MAIN, AVC444_AUX } view;
};
```

These tags do not cross the pipe. They map ordered NUT output packets back to xrdp transactions.

### FR-IN-4

Once any byte of the main picture has been written, the pair is committed. The auxiliary picture must follow unless the child dies. A committed pair cannot be dropped, replaced, or reordered.

### FR-IN-5

Input picture PTS generated by FFmpeg must remain monotonic and correspond one-for-one to the raw input order. The behavioral probe must verify this.

---

## 8.6 Full-duplex process I/O and exact synchronization

A blocking “write two frames, then read two packets” implementation can deadlock if FFmpeg fills stdout or stderr while the parent blocks on the input pipe. The backend must drive all directions concurrently.

### FR-SYNC-1: Capture ownership point

The GFX work item is eligible for conversion only after xorgxrdp has completed its source pixel writes and queued the existing encoder message.

The initial implementation should preserve the existing xrdp/xorgxrdp acknowledgement lifetime rather than introduce an early capture-buffer release optimization.

### FR-SYNC-2: Pair reconstruction point

Before the first raw byte is submitted:

- both persistent reconstructed views must be complete for the current full XRGB surface;
- pair metadata and output tags must be allocated;
- the child generation and dimensions must match the request; and
- restart/reset processing must be complete.

### FR-SYNC-3: Input commit point

The pair changes from `UNSUBMITTED` to `COMMITTED` when the first byte of its main picture is successfully written to the child input pipe.

Before this point, a pending update may be replaced or coalesced. After this point, neither member may be dropped.

### FR-SYNC-4: Input consumption point

A picture is considered transferred when its exact raw byte count has been accepted by the pipe. This only proves transport to the child process; it does not prove that the encoder has consumed or released the picture. Because the data has been copied into the pipe/FFmpeg process, xrdp may reuse its reconstructed view memory after all bytes for that picture have been written.

For the synchronous pair implementation, both reconstructed buffers may be reused after both complete picture byte ranges have been written.

### FR-SYNC-5: Output association point

A NUT video packet is associated with the oldest pending picture tag only after the NUT demuxer has parsed a complete packet and validated:

- expected video stream;
- monotonic timestamp/order;
- valid packet length within configured limits;
- H.264 codec identity; and
- no unsupported reorder condition.

### FR-SYNC-6: Pair completion point

A desktop update is `PAIR_COMPLETE` only when:

- one valid encoded packet/access unit has been associated with its main tag;
- one valid encoded packet/access unit has been associated with its auxiliary tag;
- both belong to the current child generation;
- main precedes auxiliary in coded-picture order; and
- both payloads are normalized to the RDP-required H.264 byte-stream representation.

Only then may xrdp serialize and emit an `LC=0` AVC444 bitmap stream.

### FR-SYNC-7: Existing xrdp completion point

After RDPGFX serialization, the worker uses the existing `gfx_send_done()` / `fifo_processed` / `xrdp_encoder_event_processed` path. The `XRDP_ENC_DATA` work item remains owned until the existing completed-item lifecycle releases it and allows the corresponding xorgxrdp acknowledgement.

### FR-SYNC-8: Poll loop

The synchronous `encode_pair()` call must use `poll()` or an equivalent event loop over:

- writable child input fd;
- readable child NUT stdout;
- readable child stderr;
- child termination detection or periodic `waitpid(..., WNOHANG)`;
- xrdp encoder termination request; and
- an operation deadline.

At each iteration it must:

1. write as much pending raw input as possible;
2. drain all currently available stdout into the NUT parser;
3. drain stderr into a bounded line buffer/log sink;
4. process all complete NUT packets;
5. check child exit;
6. check cancellation/termination;
7. enforce configured limits and timeout.

### State model

```text
IDLE
  │ reconstructed pair available
  ▼
UNSUBMITTED
  │ first main byte written
  ▼
MAIN_WRITING
  │ complete main bytes written
  ▼
AUX_WRITING
  │ complete auxiliary bytes written
  ▼
PAIR_SUBMITTED
  │ main NUT packet complete
  ▼
MAIN_OUTPUT_READY
  │ auxiliary NUT packet complete
  ▼
PAIR_COMPLETE
  │ RDPGFX serialized and queued
  ▼
IDLE
```

Output may begin before input writing finishes. The implementation must therefore treat input and output progress as orthogonal counters rather than assuming the strictly linear timing shown above.

---

## 8.7 NUT demuxer requirements

The in-tree component is a **NUT demuxer**, not a media decoder. It reconstructs packet boundaries and metadata from child stdout; it never decodes H.264 pixels.

### FR-NUT-1: Supported subset

Implement a bounded streaming parser for the standard NUT subset generated by the MVP command. It must support:

- NUT identifier;
- main header;
- exactly one H.264 video stream header;
- time bases;
- codec-specific data/extradata;
- frame-code table;
- variable-length integer parsing;
- packet payload length and PTS reconstruction;
- keyframe flags;
- repeated headers;
- **standard syncpoints**;
- required CRC validation; and
- arbitrary pipe read fragmentation.

Experimental NUT PIPE/no-syncpoint streams are explicitly unsupported and deferred.

### FR-NUT-2: Rejection and bounds

Reject:

- more than one media stream;
- non-video streams;
- codec tags other than H.264;
- unsupported NUT version/features;
- malformed frame-code tables or variable-length fields;
- invalid CRCs;
- non-monotonic/reordered packet output;
- excessive metadata/header repetition; and
- any header or packet exceeding the independent parser safety ceilings.

The existing `XRDP_GFX_MAX_COMPRESSED_BYTES` / `gfx.max_compressed_bytes` value is **not** the AVC444 subframe or pair limit.

### FR-NUT-3: Best-effort output allocation

Grow packet and pair buffers dynamically up to separate implementation-safety caps. MVP defaults:

- maximum NUT header/metadata bytes: 1 MiB;
- maximum one encoded picture packet: 128 MiB;
- maximum complete AVC444 pair payload, excluding outer xrdp transport overhead: 256 MiB.

These are denial-of-service/allocation guards, not rate-control or expected compressed-size limits. They are configurable downward by administrators. Exceeding one kills the child and fails the generation; normal output is not truncated.

### FR-NUT-4: API

```c
enum xrdp_nut_event_type
{
    XRDP_NUT_NEED_MORE,
    XRDP_NUT_STREAM_READY,
    XRDP_NUT_PACKET,
    XRDP_NUT_ERROR
};
```

A packet event includes stream ID, PTS/DTS if available, keyframe flag, payload pointer/length, and current codec-extradata generation.

### FR-NUT-5: Licensing and clean-room boundary

NUT is a publicly documented container format; FFmpeg's NUT implementation is published under LGPL-2.1-or-later. The MVP executes a separately installed FFmpeg binary and does not link or redistribute it as part of xrdp; downstream packaging policy remains separate. This PRD does not provide legal advice, and maintainers make the final licensing determination.

For the xrdp parser:

- implement from the public NUT format documentation and independently generated files;
- do not copy FFmpeg parser/muxer functions, tables, control flow, comments, or tests;
- independently define only the wire constants and algorithms necessary for interoperability;
- keep a design note identifying every public source consulted; and
- have a reviewer compare behavior and tests, not source-text similarity.

### FR-NUT-6: Test-vector provenance

Generate committed fixtures with stock FFmpeg commands. For each fixture record:

- exact generator argv;
- FFmpeg version/configuration output;
- input generator description;
- expected packet metadata;
- SHA-256 digest; and
- whether corruption/truncation was applied after generation.

Required fixtures include valid standard-syncpoint streams, repeated headers, one-byte fragmentation, truncated headers/packets, invalid CRCs, oversized variable integers, malformed frame codes, and fuzz-derived regressions. No fixture may be copied from FFmpeg's own test suite without explicit license review.

### FR-NUT-7: Untrusted input handling

Treat stdout as untrusted even though the process is local. Fuzz the parser independently and bound all arithmetic, allocation, loop counts, and resynchronization scanning.

All size and offset computations derived from NUT variable-length integers (payload length, header length, and any count×element products) must use **overflow-checked arithmetic** and be validated against the FR-NUT-3 ceilings (`max_nut_header_bytes` / `max_encoded_picture_bytes` / `max_encoded_pair_bytes`) **before** any allocation, seek, or copy. Reject any intermediate value that would overflow `size_t`/`off_t` or exceed a ceiling, and reject zero/negative stream or packet counts (CLAUDE.md rule 3: validate numeric bounds; reject zero/negative/extreme values; avoid integer overflow). Any NUT-metadata-derived string that is logged or surfaced must follow NFR-SEC-8 (constant format string, escaped control characters).

---

## 8.8 H.264 normalization and validation

### FR-H264-1: Annex B

Every H.264 access unit placed in `RFX_AVC420_BITMAP_STREAM` must conform to H.264 Annex B byte-stream format.

### FR-H264-2: MVP normalization strategy

The command always requests:

```text
-bsf:v h264_mp4toannexb
-x264-params repeat-headers=1
```

`h264_mp4toannexb` converts length-prefixed NAL units but is **not sufficient by itself** to guarantee SPS/PPS occur in the first NUT packet. A local review test with Debian FFmpeg 7.1.5 and libx264 produced Annex-B packet data but kept SPS/PPS only in NUT extradata until `repeat-headers=1` was added.

Therefore the MVP:

1. parses and bounds NUT extradata for format validation;
2. does not synthesize parameter sets from extradata;
3. requires `repeat-headers=1`; and
4. rejects the profile unless the first packet itself contains the required parameter sets and IDR.

Extradata-based synthesis is deferred until a real later encoder profile demonstrates the need.

### FR-H264-3: Packet-to-picture mapping

Exactly one NUT video packet must correspond to each submitted raw picture. The probe rejects split, combined, duplicated, dropped, or reordered packetization.

### FR-H264-4: FIFO association

Associate output packets to ordered tags:

```text
(main N), (auxiliary N), (main N+1), (auxiliary N+1), ...
```

Delayed output is allowed within deadlines; display-order reordering is not.

### FR-H264-5: Startup/reset validation

For the first main packet of every child generation require:

- NUT keyframe flag set;
- at least one SPS NAL unit, type 7;
- at least one PPS NAL unit, type 8;
- at least one IDR VCL NAL unit, type 5; and
- valid Annex-B start-code framing and bounded NAL traversal.

For the immediately following auxiliary packet require at least one valid VCL NAL unit, type 1 or 5, and strict second-picture order in the same stream.

Do not require AUD NAL units. This is not a full H.264 parser: split bounded Annex-B start codes and inspect only the one-byte NAL header's `nal_unit_type` field. Do not parse slice syntax, reference-picture semantics, or decode pixels.

### FR-H264-6: Scheduled intra refresh + bounded deadlines (REVISED 2026-07-28)

**Superseded rationale.** The original FR-H264-6 ("Timeout rather than runtime keyframe control") stated that the MVP has no child control channel and no force-IDR request, so a *new child* is the only way to obtain a keyframe, backed by bounded deadlines. That was a scope decision, and downstream work (including FR-H264-8's aux respawn) hardened it into an assumed prohibition. Two 2026-07-28 measurements make it untenable:

1. **Respawn is expensive.** A fresh ffmpeg+NVENC child needs **~630 ms** before its first packet (T4: 650 / 634 / 1099 ms for 1 / 2 / 30 frames ⇒ ~630 ms fixed init, ~16.6 ms/frame after). It sits inline in `encode_pair()`, so any configuration with a finite GOP stalls ~0.65 s per IDR — a ~7–8 s hitch cadence at `-g 240`.
2. **The stock ffmpeg binary DOES accept a keyframe schedule**, on both backends. Measured with `-force_key_frames`, `-g 30000`, 30 fps:

| encoder | option | result |
|---|---|---|
| `h264_nvenc` | `expr:gte(t,n_forced*0.5)` or `expr:eq(mod(n,15),0)` | intra at **exactly** frames 0,15,30,45 |
| `h264_nvenc` | *without* `-forced-idr` | **non-IDR I** (nal type 1), `frame_num` CONTINUES — DPB not reset |
| `h264_nvenc` | *with* `-forced-idr 1` | real IDR (nal 5), `frame_num` resets |
| `h264_vaapi` | same expressions | intra at exactly 0,15,30,45; always a real **IDR** (nal 5) regardless of `-forced-idr` |

Cost at 3840×2400 (180 frames, nvenc): throughput unchanged (52 fps unrefreshed vs 55 / 51 / 58 fps at every 240 / 60 / 15 frames — all within noise); only bitstream size moves (294 KB → 294 / 367 / 734 KB). On real desktop content the added cost of a **paired** refresh is ≈ `((I_main−P_main)+(I_aux−P_aux))/N` per frame ⇒ **≈ +4 % at N=240**, +17 % at N=60. **MEASURED 2026-07-29 on the shipped path** (arm-r, VAAPI CQP 444, the code-scroll corpus at 2560×1440 + 3840×2400, 1688 pairs per view with 8 paired cuts): a P pair is 47 614 B (main 18 593 + aux 29 021) and a paired cut adds (140 206 − 18 593) + (117 716 − 29 021) = 210 308 B, i.e. **876 B/pair = +1.84 % at N = 240** — under half the predicted figure on this corpus.

**Requirement (replaces the prohibition).** The runner MUST drive intra refresh by schedule, not by respawn:

- Both children are spawned with an **identical frame-indexed** `-force_key_frames` schedule, so the refresh indices are deterministic and known before submission (this also makes the future `main‖aux` parallel submit race-free — see FR-PROC-7). **This assumes 1:1 main/aux pairing**: both children see the same frame indices. FR-PROC-7's sparse aux cadence breaks that assumption and must re-derive the aux schedule from the aux child's own frame index — so it may not land first (BACKLOG #45 D12, #40).
- **The interval is `intra_refresh_frames`** (gfx.toml key; C field `avc444_ffmpeg_intra_refresh_frames`), **default 240**, accepted range **[24, 4096]** — refused by the loader and clamped by the runner outside it — effective only when `aux_ltr_chain = true`. There is deliberately **no 0/off value**: an off switch would keep the deleted aux-respawn path alive as a shadow fallback, and a silent degradation path is exactly what the strict-honesty rule forbids. **`-g` is set EQUAL to `intra_refresh_frames`** in the child argv, retiring the `-g 30000` interim: GOP boundaries then coincide with scheduled indices, so every intra picture is a scheduled one whatever shape the backend gives it, and "unscheduled mid-stream IDR" ceases to be a reachable state rather than merely a rare one.
- The rewriter converts the scheduled intra picture of **both** views into a **paired cut**: main → non-IDR I self-marking LT0, aux → non-IDR I self-marking LT1, **no IDR and no DPB flush anywhere**. Either child shape is acceptable input (nvenc's non-IDR I or VAAPI's IDR) because the rewriter relabels the header; what matters is only that the picture is intra-coded. *Parameter sets are NOT repeated at a cut* (revised 2026-07-28; **implemented and made unambiguous 2026-07-29, BACKLOG #45 D18**): a non-IDR I is not a decoder entry point, an RDP stream never seeks, and EGFX is reliable, so a repeated SPS/PPS costs bytes at every refresh and buys nothing — **measured: 206 B per cut on the VAAPI shape** (SPS 29 + PPS 4 + SEI 173, arm-o capture). An earlier wording of this clause said child-emitted parameter sets still pass through on the main view, which cannot both hold on VAAPI, whose cut IS a child IDR carrying them. Resolved: at a CONVERTED cut the child's SPS/PPS/SEI are DROPPED; at a real epoch entry they pass through unchanged. Guarded — only a set proven BYTE-IDENTICAL to the one already on the wire may be dropped, and a CHANGED set fails the packet, because swallowing a changed SPS is silent whole-picture corruption. The AUD is not a parameter set and still ships on every frame (Windows shape).
- **IMPLEMENTED 2026-07-29** (xrdp `9653bd1f` + `a0d9e773`; BACKLOG #45 steps 1-4). Both shapes are accepted, in both views, and the mid-stream main IDR is converted instead of flushing. Three things worth carrying forward from the implementation: (a) the EMITTER needed no change at all — the existing non-IDR arm of `slice_ltr_rewrite()` already produces the exact required bytes for both new shapes and is view-agnostic, so the work was input-parse and walker state only; (b) the reject at `:2128` was the SMALL half — three walker-level gates keyed on `ntype == 5` rather than on the picture being intra, so fixing only the slice-level reject would still have refused an nvenc aux cut and would not have seeded LT1 from one; (c) `convert_intra` is decided ONCE per picture (in a bounded pre-scan, because the parameter-set decision must be made before the first NAL is emitted) and every slice of a multi-slice picture must agree with it or the packet fails. **Original survey (2026-07-28), for the record:** a non-IDR I fell into the `B/SP/SI or non-IDR I` reject at `:2128`, and a mid-stream IDR on the main view was accepted but reset the shared counter to 0 and cleared `aux_seeded` (`:2447`, `:2527`) — precisely the flush this FR abolishes. Supporting both is the substance of the work, not a detail: the two backends genuinely differ, so a build that handles only nvenc's shape is broken on VAAPI and vice versa. The conversion mechanism itself already exists and is proven in production — `to_seed_i` performs exactly this IDR → self-marking non-IDR I relabel for the aux seed; it must be generalised to the main view.
- The session's **first** picture remains a real IDR (the decoder's entry point and the origin of LT0).
- **Verify, never assume:** at a scheduled index the slice MUST parse as `slice_type == I`. A P where intra was expected is a loud failure of the same class as an aux P with LT1 unseeded — never a silent emit. Four verification layers, in the order they run — only the last is onscreen: (1) the pure-C DPB simulator (`tests/xrdp/test_avc444_ltr.c`) replays the cut sequence in 1-context and 2-context modes and reports `missing_ref`/`overflow`/range violations without any decoder; (2) byte-exact goldens from `ltr_splice_ref.py`; (3) a **runtime** observed-vs-scheduled check in the rewriter, which is the only layer that runs before the client sees the picture, and fails the pair rather than shipping it; (4) wire captures from the fleet/T4 audited by `tools/avc444_ltr_wire_audit.py`. These structural checks are the WHOLE verification of I3 — the invariant is invisible onscreen in the success case (bounded and unbounded depth decode to identical pixels over a lossless pipe), so no amount of watching a healthy screen confirms it. Client-side risk must be stated precisely (corrected 2026-07-28 after conflating two boundaries): the bitstream fully determines reference structure, so a client cannot read a dependency across a cut that the server did not emit. The cut's only NOVEL wire element is a mid-stream non-IDR I slice — nri=3, the mmco6 self-mark/slot-replacement, the explicit rplm and the per-view frame_num stride already ship on every picture today and render correctly on mstsc/macOS/xfreerdp. A decoder that mishandled it would corrupt or wedge AT THE CUT CADENCE — a distinctive, immediately visible signature covered by the ordinary smoke gate. The separate, owner-blocked onscreen observation belongs to the **re-key boundary** (real IDR + epoch restart at the frame_num-wrap re-key): whether a 2-context client re-initialises its aux decoder at the epoch change is decoder-lifecycle behaviour, unprovable from the bitstream. Finally, containment CAN be made observable by injecting the divergence in the client harness: an oracle-client run that corrupts/drops exactly one P and measures pixel re-convergence — heals within **≤ `intra_refresh_frames` + 1 pairs** (241 at the default) measured from the injected corruption, and persists indefinitely on a `-g 30000` control — turns I3 into a measured recovery time with a discriminating control.
- The aux-child respawn path is then dead code for this purpose and MUST be removed; a mid-stream main IDR ceases to exist by construction. **Removed 2026-07-29** (`a0d9e773`), together with `ltr_aux_fresh`. If LT1 were ever unseeded mid-chain the aux rewrite refuses the packet and the pair fails loudly — the correct mechanism, in the right place. Respawning would restart that child's frame index while the main child keeps counting, de-phasing the shared schedule: one fault made permanent. **Consequence to keep in view:** with the reset gone, the frame_num-wrap re-key is now the ONLY wrap protection (it used to be masked by the GOP IDR resetting the shared counter). `xrdp_ffmpeg_avc444_ltr_counter_cap()` and its "re-key unreachable" warning modelled that reset and became FALSE exactly at the D7 target of `-g 240`; both were deleted rather than adjusted, and the guard now lives where the mechanism is, as `test_ltr_cut_midstream_idr_keeps_chain`.

**Invariant this exists to enforce (I3).** Direct reference age in the LTR topology is always exactly one picture, so "staleness" is *transitive dependency depth*: the distance back to the last picture in that view coded without a reference. A pure P chain leaves it unbounded, meaning any encoder/decoder divergence (client decoder bug, a rewrite bug, a frame the client skips under load) persists until reconnect. The refresh interval N is precisely the bound, and it must be bounded in **both** views — a main-only refresh does not bound aux. Note this is about *divergence containment*, not loss recovery and not seeking: an RDP stream is live and never seeks, and a fresh connection always builds a new encoder that opens with a real IDR.

**Retained from the original FR.** Bounded deadlines remain the safety net, unchanged:

- child spawn and NUT stream-ready default: 2 seconds;
- first main packet default: 2 seconds after full main input transfer;
- auxiliary/pair completion default: 2 seconds after full auxiliary transfer.

Timeout or failed NAL checks kill/reap the child and fail the generation. Values are configurable with hard upper bounds.

**Still absent (honest limit).** A stock ffmpeg binary offers no *on-demand* keyframe request: its interactive stdin commands address filters only, and our stdin carries the raw frame stream. On-demand refresh (e.g. honouring a client `KEY_FRAME_REQUESTED`, which the AVC444 path does not wire up today — it is handled only in the RFX path) therefore still requires either a child restart or a patched encoder, and is out of scope. A sufficiently dense schedule bounds staleness without it.

### FR-H264-7: Decode-topology invariance (reference partitioning) — REQUIRED, not configurable (owner directive 2026-07-28)

**Requirement.** The AVC444 wire stream must decode to bit-identical pixels under every client decode topology:

1. a single in-order decoder consuming the full interleave (Windows/mstsc/xfreerdp shape);
2. a decoder consuming the main view with ALL auxiliary NAL units dropped;
3. two independent decoders, one fed the main view and one fed the auxiliary view (the macOS VideoToolbox shape observed in the 2026-07 bisect).

Equivalently: no picture may ever use a cross-view reference. The main chain references only main frames; auxiliary frames read nothing (intra) and write nothing (non-reference, `nal_ref_idc = 0`, non-IDR so the DPB is never flushed). This must hold at ANY auxiliary cadence — a scheme that is only correct at a particular main:aux ratio is forbidden (heisenbug class; owner directive 2026-07-27).

**Mandatory architecture.** The external backend implements this with two FFmpeg children per surface: the main child encodes only main frames (its chain self-references), and the auxiliary child encodes all-IDR; each aux packet is rewritten by `xrdp_h264_aux_to_leaf()` into non-reference, non-IDR I leaves on the main chain (aux SPS/PPS/SEI/AUD dropped; `frame_num` = main + 1 per the non-reference rule; CABAC payload byte-verbatim; loud failure on any stream shape outside the compat guard). This is NOT a configuration option: the former `aux_intra_leaf` gfx.toml knob is removed and the pair path always partitions. Rationale (measured, 2026-07-27/28): the single-child cross-view interleave corrupts any client that deviates from topology 1 (Mac chroma bleed, root cause cross-view inter prediction); VAAPI's clean result was accidental immunity (a Mesa all-intra mode-decision quirk), not a property to build on; partitioning also collapses main P-frame sizes (nvenc: 25–76 KB → 0.6–2.5 KB) because same-view references make inter prediction effective.

**Alternative “aux references previous aux” — status CORRECTED (2026-07-28).** An earlier revision of this paragraph rejected the two-prediction-chain design with an exaggerated risk rationale (“per-frame PicNum arithmetic, silent wrong-pixel failure modes, forfeits structural topology invariance”). That rationale priced a short-term-reference construction the real Windows server does not use, and it is withdrawn: ground-truth measurement (PR-demo/win2022_ground_truth/GROUND_TRUTH_win2022_avc444.md, LTR addendum) shows Win2022 ships exactly this design via constant long-term-reference slots — no per-frame arithmetic, no eviction pinning — and every RDP client, VideoToolbox included, renders it daily. The design is now specified as EXPERIMENTAL FR-H264-8 below; the leaf architecture in this section remains the shipped default, and this FR’s three-topology invariance remains the requirement for the default path.

**Regression test (macOS-emulating two-decoder check).** `tools/avc444_topology_check.sh` takes a captured interleaved wire stream and verifies, via `ffmpeg` framemd5, that topologies 1, 2 and 3 above produce bit-identical frames (main rows of the interleaved decode == the main-only decode; leaf rows == the aux-only decode through a second, separate decoder instance). It must be run — and pass — on a fresh wire capture for every change touching the encoder/conversion/rewrite path, alongside the existing smoke gate; a mismatch is a red result (strict honesty rule: no fallback, no cadence tweak to mask it).

**Validation scope note (FR-H264-5 unchanged).** Startup/reset validation remains NAL-header-only. The reference-partitioning rewriter is a separate bounded slice-header splice in `xrdp/xrdp_h264_annexb.c` with its own fail-loud contract; it does not relax FR-H264-5’s “no slice parsing” rule for the validator.

### FR-H264-8 (EXPERIMENTAL): aux-refs-aux via Windows-style long-term reference slots

**Status: EXPERIMENTAL** — specified 2026-07-28 from ground-truth measurement of the Win2022 server (PR-demo/win2022_ground_truth/GROUND_TRUTH_win2022_avc444.md, LTR addendum); not implemented, not shipped. FR-H264-7 (all-intra leaves) remains the default and the shipped architecture. Changing the default requires the full acceptance gate below plus owner sign-off.

**Purpose.** Close the all-intra aux cost of FR-H264-7 (~33 KB/leaf at 1600×900; ~8 Mbps on static content at the current 1:1 cadence, shrinking but not vanishing once FR-PROC-7 ships sparse aux) by letting the aux view predict from its own previous frame — using the exact reference topology the Windows server ships, which is the strongest possible client-compatibility pedigree and the easiest upstream story.

**Measured recipe (Win2022, gfxwin_anim, 357 AUs / 9 aux / 3 slices per AU).**

1. One `frame_num` chain across both views; ALL pictures `nal_ref_idc = 3`; `max_num_ref_frames = 3`; `gaps_in_frame_num_allowed = 0`.
2. IDR: `long_term_reference_flag = 1` (seeds LT slot 0).
3. EVERY P slice, `dec_ref_pic_marking`: adaptive mode, `mmco 6` marking the CURRENT picture long-term into its view’s slot — `long_term_frame_idx` 0 = main, 1 = aux — then `mmco 0`. Slot reassignment replaces the previous occupant: no sliding-window dependence, no eviction management.
4. EVERY P slice, `ref_pic_list_modification_flag_l0 = 1`: `modification_of_pic_nums_idc = 2` selecting `long_term_pic_num` of the slice’s OWN view (main → 0, aux → 1), then idc 3. All marking/modification syntax is CONSTANT per view — no PicNum arithmetic anywhere.
5. Measured quirk: the first aux after the IDR selects `long_term_pic_num = 0` (references the main IDR); subsequent aux frames reference LT1 (counts: 1044 = 348×3 vs 24 = 8×3).

**Topology-3 epoch rule (added 2026-07-28, measured during implementation).** The aux-only feed structurally cannot carry an IDR (an IDR in the shared wire would empty the one-context DPB and kill LT0), so a STATEFUL aux-only decoder has no reset signal at chain-restart events (mid-stream main IDR, pre-wrap re-key). MEASURED: ffmpeg’s aux-only decode is bit-identical within an IDR epoch but silently stalls at an epoch boundary (backward frame_num jump without IDR: a 4-GOP VAAPI run decoded 122/400 aux-only frames continuously, yet per-epoch — aux decoder context recreated at each boundary — all 400/400 decode bit-identical; the 1-context and drop-aux feeds are continuous-proven across the same boundaries, 800/800 and 400/400). Topology-3 pixel identity is therefore required PER EPOCH; the assumption that a two-context client re-initializes its aux decoder when the stream re-keys is a client-model assumption that the macOS gate item must verify across a re-key boundary (real Windows has the same property — one IDR per session, aux feed never independently decodable at all — so this is still strictly stronger than the Windows shape). Consequence for configuration: `aux_ltr_chain` arms should run a LONG GOP (e.g. `-g 30000`; VAAPI defaults to gop 120 = an epoch every ~12 s) so epochs coincide with the ~hourly re-keys; periodic IDRs buy nothing on a reliable transport.

**Invariance contract (UPGRADED 2026-07-28 from the earlier “relaxed, topology-3 expected-RED” wording — owner review).** Windows fails strict topology 3 for two reasons: its first aux references LT0 (the main IDR — a cross-view reach into the main context), and the aux-only feed observes frame_num gaps. The first is a Windows quirk we do NOT copy: our first aux is a self-contained non-IDR I slice that self-marks LT1 without referencing anything (the FR-H264-7 leaf conversion with `nal_ref_idc > 0` plus mmco6, instead of `nri = 0`), so every aux prediction resolves inside the aux chain. The second is unavoidable syntax-level (the shared frame_num counter increments on main frames the aux-only decoder never sees) but does not affect prediction, which flows exclusively through the LT slots. Required, therefore, in BOTH decode modes: (a) 1-context (MSTSC/xfreerdp shape) — interleaved decode bit-identical to each child’s own decode; (b) 2-context (macOS shape) — main-only decode bit-identical (topology 2) AND aux-only decode PIXEL-identical by framemd5 (topology 3), with decoder frame_num-gap warnings tolerated and recorded (pixel identity, not warning-free logs, is the criterion). Both modes must PASS against the childrens’ ground-truth decodes; a topology-3 pixel mismatch is a RED result, no longer an accepted expectation.

**Implementation sketch.** Same two children as FR-H264-7; the aux child encodes a normal `refs=1` P chain instead of all-IDR. The splicer rewrites both views’ slice headers (pre-CABAC, existing machinery): shared frame_num counter, constant mmco6 self-mark, constant LTR list-modification; both children’s CABAC payloads stay byte-verbatim (each child’s `ref_idx 0` remaps to its LT slot via the modification list). SPS splice: raise `max_num_ref_frames` (and, when a VUI bitstream_restriction is present, `max_dec_frame_buffering` — decoders that size the DPB from the VUI would otherwise evict LT1) to 3, re-check level DPB limits. Fail-loud on any slice shape outside the guard, as today.

**frame_num width + wrap re-key (added 2026-07-28, measured during implementation).** The rewrite WIDENS the frame_num field to 16 bits (`log2_max_frame_num_minus4 = 12`, the legal maximum) regardless of the child’s width — x264 emits a 4-bit field (sized from DPB+1, no knob; keyint does not change it, correcting an assumption in the earlier unit-test spec wording), and narrow fields alias per-view under sparse aux cadences. MEASURED wrap hazard (ffmpeg 7.1.5): a per-view feed whose frame_num steps by 2 (the 2-context shape) SILENTLY STOPS DECODING at the frame_num wrap — 300 aux pictures through an 8-bit field produced 129/300 output frames with zero warnings; the 1-context and drop-aux feeds survive the same wrap bit-identically. Note Windows itself wraps at 256 (gfxwin_anim AU 311 carries frame_num 55) and relies on decoder leniency; our upgraded topology-3 contract cannot. Therefore the wire must never let ANY decoder see a frame_num wrap: the runner **re-keys** when the shared counter reaches the configured threshold. The re-key is **bitstream-only** — the encoder pair is destroyed, so the replacement child opens with a real IDR, the shared counter resets, and that frame declares the WHOLE surface as its damage region (the capture is a full frame and the picture is a fresh IDR, so the claim is accurate rather than a widened guess). It MUST NOT emit any EGFX surface lifecycle event. Tearing down and recreating the surface is a protocol-defined decoder reset and was specified as belt-and-braces on top of the IDR, but it is not what the re-key is FOR — the wrap protection comes entirely from the encoder restart — and clients that repaint the output when a mapped surface is replaced show a **black flash at every boundary**. MEASURED 2026-07-29 on macOS, in BOTH emission orders (`DELETE→CREATE→MAP→pixels`, which maps a zero-filled surface, and `CREATE→pixels→MAP→DELETE`, which never does): black at every boundary, gone once the churn is masked. The oracle capture of the second order decodes 4122/4122 pictures with ZERO black frames and both surfaces are created identically (1024×768 at 0,0), so the fault is not in the H.264 and not a mis-sized replacement: the client blanks on surface CHURN itself. The threshold is the settable `ltr_rekey_frame_num`, default `XRDP_H264_LTR_FRAME_NUM_REKEY` (2^16 − 512), accepted range [64, 65024] — refused by the loader and clamped by the runner outside it, so a boundary is exercisable in seconds instead of ~18 min. `ltr_rekey_surface_reset` re-enables the surface teardown, default **false**; it exists only to reproduce the known-bad behaviour deliberately. The boundary is exercised by fleet arm **arm-p** (`PR-demo/mac_bisect_matrix/gfx/arm-p.toml`, port 40015) at threshold 536, and audited offline by `rekey_boundary_audit.py` (wire order, including the invariant that a surface is never mapped before it has pixels) and `oracle_black_frame_check.py` (every picture decodes, no mid-stream black frame). Cost of a boundary: one full-frame update plus the encoder-pair respawn (~1 s, owner-observed, not instrumented). At the default threshold that is ~18 min of continuous 30 fps animation; frames encode only on damage, so an idle session may never re-key. Wrap correctness is structural, not timing-dependent. A mid-stream main IDR (finite GOP) likewise empties the DPB including LT1. The ORIGINAL mitigation — respawn the aux child so its next packet is IDR-shaped and re-seeds LT1 — is SUPERSEDED by the revised FR-H264-6 (2026-07-28): the respawn costs ~630 ms inline in the encode path, and stock ffmpeg accepts a deterministic `-force_key_frames` schedule on both nvenc and VAAPI. The runner MUST instead schedule a PAIRED intra refresh in both views (main-I self-marking LT0, aux-I self-marking LT1, no IDR, nothing flushed), which bounds transitive dependency depth in BOTH chains (invariant I3) and removes mid-stream main IDRs by construction. Until that lands, `aux_ltr_chain` arms run a long GOP (`-g 30000`) so the respawn is never triggered in practice, and an aux P while LT1 is unseeded remains a loud rewrite failure, never a silent emit.

**Unit-test specification (required BEFORE the spike is called done; same golden-byte-vector style as the FR-H264-7 leaf tests in `tests/xrdp/test_avc444_h264.c`).** The change surface is pure bit-level logic plus reference semantics, all unit-testable in CI without ffmpeg or hardware:

1. *Emitter vectors (bit-exact golden bytes, both views).* (a) mmco6 self-mark: P slice in → `adaptive_ref_pic_marking_mode_flag=1`, `mmco 6` with the view’s `long_term_frame_idx` (0 main / 1 aux), `mmco 0` terminator; CABAC payload byte-verbatim after header re-alignment. (b) LTR selection: `ref_pic_list_modification_flag_l0=1`, `modification_of_pic_nums_idc=2`, `long_term_pic_num` of the OWN view, idc 3 end. (c) Main IDR: `long_term_reference_flag=1`. (d) First-aux conversion: aux child IDR → non-IDR type-1 I slice with `nal_ref_idc>0` self-marking LT1, referencing nothing (the two-context enabler; a golden vector must pin the exact byte diff vs the FR-H264-7 leaf, whose only deltas are nri and the marking syntax). (e) Ground-truth cross-check: a real Win2022 aux slice header from the committed capture (`PR-demo/win2022_ground_truth`, via `assemble_annexb.py`) parsed field-by-field; our emitter must produce the identical syntax-element sequence for items (a)–(b) — the measured recipe, not our reconstruction of it, is the reference.
2. *frame_num slots.* Shared-counter rewrite over a synthetic `M A M A …` interleave: golden per-AU frame_num sequence; wrap vectors for the counter arithmetic (the DPB-simulator model exercises the wrap at `log2_max_frame_num=8`; the SHIPPED chain uses the widened 16-bit field and re-keys before the counter can wrap, so no decoder ever sees a wrap — see the wrap re-key paragraph below); guard vectors for aux cadences ≠ 1:1 (sparse aux must not desynchronize the counter).
3. *Reference-resolution / long-term-pinning model (the core new semantics — a small pure-C DPB simulator implementing §8.2.5 marking: sliding window, mmco6, IDR `long_term_reference_flag`).* Feed the synthesized AU sequence in BOTH modes — 1-context wire order, and 2-context per-view (main-only and aux-only) — and assert: every P slice’s ref-list position 0 resolves to the intended SAME-VIEW frame in both modes (bit-identical resolution — the no-ambiguity claim as a machine-checked invariant); sliding-window operations NEVER evict LT0/LT1 across ≥ 512 frames including the frame_num wrap (long-term pinning; long-term frames are exempt from the sliding window and the test proves our streams rely on nothing else); mmco6 reassignment REPLACES the slot occupant (measured Windows semantics); `max_num_ref_frames` accounting stays within the SPS bound at every step.
4. *Negative/fail-loud vectors.* Slice/SPS shapes outside the guard must hard-reject, never silently emit: unexpected slice_type (B/SP/SI, non-IDR I from a child), a pre-existing ref-pic-list modification, an mmco5 (full reference reset — its state change cannot survive the marking replacement) or invalid mmco op, and an SPS whose level/DPB budget cannot hold 3 reference frames (or an unknown level_idc). AMENDED 2026-07-28: benign child marking — sliding window or short-term mmco chains (Mesa VAAPI emits `[mmco1 diff=0, mmco0]` on every P slice, measured from the committed captures) — is parsed and REPLACED by the constant LTR self-mark, not rejected; rejecting it would reject every VAAPI child. Only state-bearing ops (mmco5) and malformed chains hard-reject.

Pixel-level decode equivalence across the three topologies remains the integration backstop (`tools/avc444_topology_check.sh`, gap warnings tolerated per the invariance contract) — it complements, not replaces, the unit matrix above.

**Semantic roundtrip PSNR harness (planned with the spike; catches decoder-state corruption offline, before onscreen hunting).** The identity checks above are decoder-vs-decoder: they cannot flag a corruption that affects both decode modes identically, and when a divergence exists they cannot say whether it is one LSB or a chroma cast. A new offline tool (`tools/avc444_roundtrip_psnr.sh`, portable, software encoder by default, VAAPI on the box) closes that gap:

1. *Roundtrip vs SOURCE.* Generate a deterministic synthetic sequence (moving luma+chroma structure, per-frame markers), run it through the REAL pipeline — 444 packing into main/aux views, the two child encoders, the FR-H264-8 splice — then decode in BOTH modes (1-context interleaved; 2-context per-view) and reconstruct RGB. Report per-frame, per-plane (Y/U/V after 444 reconstruction) PSNR against the source. Pass requires: absolute PSNR within the encoder’s expected band for the configured QP in both modes; per-frame |PSNR₁ctx − PSNR₂ctx| within a small epsilon; and NO monotonic decay across the sequence (DPB drift accumulates — the Mac corruption grew frame over frame until IDR; a trend check catches slow drift that still sits above an absolute floor). Chroma planes are the sensitive channel and get the tightest scrutiny.
2. *State-machine stressors.* Dedicated sequences crossing the events where reference state can corrupt: IDR restart mid-stream, frame_num wrap (≥ 512 frames), sparse/changing aux cadence (Lever-2 shape), slot reassignment timing, resize/reset re-keying. PSNR must stay in band across every event in both modes.
3. *Harness sensitivity validation (a checker proven unable to fail is worthless — same rule as the topology checker, which was validated RED on the pre-fix wire before use).* (a) Fault-injection vectors: deliberately drop one aux AU, swap two frame_nums, retarget one LTR index — each injected fault must turn the harness RED; (b) Tier-B mode on wire captures (no source available): inter-mode PSNR between the 1-context and 2-context decodes of the same capture — run against the pre-fix nvenc capture (`t4_ps0`), where it must reproduce the known chroma-collapse signature, and against the FR-H264-7 leaf wire, where it must be clean.
4. *Baseline first.* The harness runs against the FR-H264-7 leaf arm before the FR-H264-8 arm exists; the leaf PSNR band on identical content is the reference FR-H264-8 must match (bandwidth may improve; fidelity may not regress).

Gate wiring: item (1) of the acceptance gate additionally requires the roundtrip harness green in both modes, including the stressor sequences, with the sensitivity validation recorded — before any live-arm deployment.

**Correctness invariants (state them when touching this path).**

- **I1 — one reference, always the immediately preceding same-view picture.** Enforced by `-refs 1` plus the guard `num_ref_idx_l0_default == 0`. This is load-bearing: the whole LTR relabeling is sound *only* because a child can never reach further back than one picture. A child with `refs > 1` would emit silently wrong pixels through a rewrite that parses as perfectly correct.
- **I2 — the wire relabels that reference to the view's long-term slot**, and the slot always holds the immediately preceding same-view picture (mmco6 self-mark on every picture + list-modification idc = 2). Enforced by construction; an aux P with LT1 unseeded is a loud failure.
- **I3 — bounded transitive dependency depth.** Direct reference age is always exactly one picture, so "staleness" is not about the slot; it is the distance back to the last picture in that view coded without a reference. Only the paired scheduled refresh of the revised FR-H264-6 bounds it, and it must be bounded in **both** views — a main-only refresh does not bound aux.

**Gate status (2026-07-28).** Machine-side items are CLOSED:

1. Unit matrix green under `make check` (322/322, incl. 116 in the xrdp suite); C rewriter and the independent Python reference splicer produce byte-identical output; roundtrip PSNR harness green in both decode modes with fault-injection sensitivity recorded.
2. Drop-aux bit-identity verified on live captures; wire bit-identical in both modes.
3. Both backends verified from real captures — VAAPI (fleet arm-n) and **NVENC (T4)**. T4 structure at three geometries (1024×768 / 1600×912 / 3840×2400): every P slice in both views retargets its own slot, zero cross-view references, and **zero frame_num gaps across 3–7 mid-stream IDR epochs**. A control run on the leaf arm through the same parser shows the opposite shape (aux 166/166 intra, 164 chain gaps), so the check discriminates topologies rather than confirming the expectation.
4. Bandwidth gate PASSED. VAAPI line-scroll baselines (the gating workloads): aux 70.13 → 17.12 KB/frame (−76 %) on `code`, 326.29 → 101.85 (−69 %) on `scroll`, at equal delivered pairs/s. NVENC A/B on identical content, per picture: 1600×912 aux 66177 → 463 B (−99.3 %), pair −98.0 %; 3840×2400 aux 83231 → 2730 B (−96.7 %), pair −84.5 %; `queue_depth = 0` on every frame. Non-gating record: tick −98.6 %, gray −83 %, codefast −21 %, scrollfast −29 %, and an honest adversarial regression on flat saturated `chroma` bands (+224 %).
5. Onscreen: owner reports the T4 renders correctly on **both Windows (incl. multimon) and macOS** (2026-07-28).

Open before the default can change: the scheduled-paired-refresh work of the revised FR-H264-6 (until it lands, arms run `-g 30000`, which trades away the I3 bound — acceptable only as an interim), the **frame_num-wrap re-key** (specified in the wrap re-key paragraph above — **DONE 2026-07-29**: the re-key is bitstream-only, the threshold is settable, and the EGFX surface teardown that an earlier revision of this clause mandated is REMOVED, having been falsified onscreen — it made macOS flash black at every boundary while contributing nothing to wrap protection. Owner-confirmed both ways: black at every boundary with the teardown, gone without it. This RETIRES the "watch macOS across a re-key" observation: it was performed, it failed, and the design changed to match), and owner sign-off. Timing correction: the counter hits the re-key threshold after ≈ 18 min of continuous 30 fps animation (32 512 pairs), not "once an hour"; idle sessions may never re-key.

**Acceptance gate (to leave EXPERIMENTAL).** (1) The unit-test matrix above green under `make check`; the offline synthesized stream decodes bit-identical (framemd5) to each child’s own decode; and the semantic roundtrip PSNR harness green in BOTH decode modes including the stressor sequences, with its sensitivity validation (fault injection + pre-fix-capture RED) recorded; (2) drop-aux bit-identity on a live capture; (3) fleet arm (VAAPI) + T4 (nvenc) wire captures verified in BOTH decode modes per the invariance contract (topology 3 pixel-identity required, gap warnings recorded); (4) macOS onscreen verdict by owner; (5) **bandwidth gate (owner directive 2026-07-28): `PR-demo/mac_bisect_matrix/bandwidth_bench.sh` is the benchmark harness for this optimization, and its result GATES acceptance.** The line-by-line scroll baselines (`code` and `scroll` workloads, 1 line/0.1 s — the classes where main is properly inter-compressed and the leaf aux dominates the pair, measured 2026-07-28: code aux 70.1 KB/frame = 85% of an 82.3 KB pair on the leaf arm) must be run in MODE=frames against the FR-H264-8 arm and the leaf arm on identical content; acceptance requires a material reduction of the steady aux KB/frame versus the leaf baseline with delivered pairs/s equal between arms, recorded as absolute per-view KB/frame in `BACKLOG.md`. A result that does not beat the leaf baseline on these workloads FAILS the gate regardless of other criteria. The ME-defeating stress variants (`codefast`, `scrollfast`) and the flat-band `chroma` bound are recorded alongside but do not gate; (6) owner sign-off recorded in BACKLOG before any default change.

---

## 8.9 AVC444 wire serialization

### FR-WIRE-0

The selected xrdp GFX AVC mode is the source of truth for the outgoing codec ID. Current `gfx_wiretosurface1()` parses `codec_id` from the incoming GFX command. The AVC444 implementation must either update the xorgxrdp command producer to emit `0x000E` or derive/override the value in xrdp, and in all cases validate that a stale AVC420 command cannot be serialized with AVC444 data.

### FR-WIRE-1

Add a dedicated serializer, provisionally:

```c
gfx_wiretosurface1_avc444(...)
```

rather than overloading the existing AVC420 function with deeply conditional behavior.

### FR-WIRE-2

For MVP `LC=0`, construct the `RFX_AVC444_BITMAP_STREAM` (MS-RDPEGFX 2.2.4.5):

```text
uint32 avc420EncodedBitstreamInfo
RFX_AVC420_BITMAP_STREAM main
RFX_AVC420_BITMAP_STREAM auxiliary
```

where the info word (spec field name `avc420EncodedBitstreamInfo`) splits into:

```text
cbAvc420EncodedBitstream1 (bits 0..29) = byte length of the first RFX_AVC420_BITMAP_STREAM (main)
LC                        (bits 30..31) = 0 for the LC=0 both-views case
```

The `cbAvc420EncodedBitstream1` length must follow the exact MS-RDPEGFX definition, including the first stream's `RFX_AVC420_METABLOCK` metadata and H.264 data as specified.

### FR-WIRE-3

Each substream must contain its own `RFX_AVC420_METABLOCK` and encoded H.264 access unit.

### FR-WIRE-4

For the first implementation, use the same destination rectangle and conservative region set for both substreams.

### FR-WIRE-5

Call the existing generic `xrdp_egfx_wire_to_surface1()` path with AVC444 codec ID and the completed bitmap data.

### FR-WIRE-6

Do not expose a conventional H.264 4:4:4 bitstream as AVC444. RDP AVC444 is specifically the two-view construction.

---

## 8.10 Resize and restart lifecycle

The user-visible requirement is explicit: **kill FFmpeg on every resize**.

### FR-RESIZE-1

Any change to actual dimensions, coded dimensions, pixel format, monitor/surface identity requiring context recreation, or encoder profile must increment the per-handle `generation`.

### FR-RESIZE-2

Before any byte of a new-size picture is written:

1. stop accepting new pairs for the old generation;
2. discard/coalesce old-generation updates that are still unsubmitted;
3. terminate the old child;
4. discard incomplete input and NUT parser state;
5. clear pending picture tags and encoded half-pairs;
6. allocate/reinitialize both reconstructed views;
7. calculate coded dimensions and padding;
8. spawn the new child with the new dimensions;
9. parse a valid NUT header;
10. submit a full-surface main+auxiliary pair;
11. validate reset output;
12. send it as `LC=0`.

### FR-RESIZE-3

Termination sequence:

1. close the child raw-input fd;
2. optionally drain stdout for a short bounded interval only if needed for clean logs;
3. send `SIGTERM` if the process remains alive;
4. wait for a configured grace interval;
5. send `SIGKILL` if required;
6. call `waitpid()` and close all fds.

The resize path must not wait indefinitely for FFmpeg to flush delayed frames. Old-generation output is discarded.

### FR-RESIZE-4

A dimension mismatch detected inside `encode_pair()` is a mandatory safety-net restart even if a higher-level resize hook is also implemented.

### FR-RESIZE-5

The first pair after restart must use a full-surface dirty region. Partial reconstruction is invalid because the child and client decoder state are new.

### FR-RESIZE-6

Child crash, NUT parse error, timeout, broken pipe, H.264 validation error, or packet-order error follows the same reset sequence. Repeated failures disable the backend for the connection and fail the GFX/session path; MVP does not switch codecs after confirmation.

---

## 8.11 Backpressure and queue policy

### FR-BP-1

The backend must not guarantee throughput. It must guarantee bounded queueing and internally consistent H.264 order.

### FR-BP-2

At most:

- one pair may be committed/in progress for the single FFmpeg child; and
- one newest unsubmitted desktop update may wait for that context.

If current xrdp scheduling makes even the single waiting item unnecessary, an initial zero-waiting-item implementation is acceptable.

### FR-BP-3

When overloaded, replace older unsubmitted work with a newer update whose damage region is the union needed to reconstruct the newest correct state.

### FR-BP-4

Never drop:

- a main picture after its first byte is written;
- the auxiliary picture belonging to a committed main;
- an encoded picture already returned by FFmpeg while preserving later dependent pictures; or
- a packet from the middle of the active H.264 stream.

### FR-BP-5

The kernel pipe must not become an implicit deep frame queue. Do not enlarge it to hold many complete 4K pairs. Input progress is explicitly tracked, and stale work is eliminated before commit.

### FR-BP-6

If a committed pair does not complete before the operation deadline, terminate the child and reset. Do not continue the stream after an unknown half-pair state.

### FR-BP-7

Metrics must expose:

- raw input bytes;
- write-blocked time;
- NUT output latency;
- pair completion latency;
- queued/coalesced/dropped-before-submit updates;
- process restarts;
- timeout count;
- parser errors; and
- encoder-reported stderr warnings.

---

## 8.12 Runtime configuration

Suggested `gfx.toml` meaning:

```toml
[codec]
order = ["H.264", "RFX"]
h264_encoder = "ffmpeg"

[avc444_ffmpeg]
enabled = true
path = "/usr/bin/ffmpeg"
encoder = "libx264"
desktop_fps = 60
startup_probe = true
stream_ready_timeout_ms = 2000
picture_timeout_ms = 2000
pair_timeout_ms = 2000
terminate_grace_ms = 250
stderr_level = "warning"
max_nut_header_bytes = 1048576
max_encoded_picture_bytes = 134217728
max_encoded_pair_bytes = 268435456
quality_crf = 18
gop_pictures = 240
extra_x264_args = []
```

### FR-CONFIG-1

Use xrdp's existing typed configuration facilities. The spelling above is illustrative; the meaning and security boundary are normative.

### FR-CONFIG-2

The MVP adds `h264_encoder = "ffmpeg"`; its initial executable encoder is `libx264`. Startup probe failure makes that H.264 backend unavailable and codec order proceeds to the next entry. It does not silently invoke linked x264/OpenH264. The xrdp binary still has no compile-time FFmpeg dependency.

### FR-CONFIG-3

Configuration tokens are argv elements, never shell text. Only allowlisted libx264 options are accepted in the MVP. Structural arguments cannot be overridden.

### FR-CONFIG-4

Log the resolved executable path, encoder, selected capability/mode, coded dimensions, and safely escaped argv at debug level. Do not log unrelated environment data. Any externally-produced bytes (argv echoes, ffmpeg stderr) must be logged per NFR-SEC-8 — constant format string with the data as a `%s` argument, never as the format string itself.

### FR-CONFIG-5

Future Linux/Unix hardware profiles are separate follow-up work. They must not be enabled merely because `ffmpeg -encoders` lists a name; each requires end-to-end probe and MSTSC interoperability evidence.

---

## 8.13 Startup behavioral probe

### FR-PROBE-1: Timing

Run the exact executable/profile probe synchronously before `xrdp_mm_egfx_caps_advertise()` commits an AVC444 capability set in `xrdp_egfx_send_capsconfirm()`. Probe results are held in process/session memory only for MVP.

### FR-PROBE-2: Input and command

Use the same structural command builder as the real child at the **actual session coded dimensions**. Submit at least four distinguishable NV12 pictures in main/auxiliary order. The probe child is always terminated and reaped afterward because its test pictures are not visible to the client and must not become reference pictures for the real stream. A separate 32×32 command remains useful for unit/CI fixtures, but it is not sufficient for the connection capability decision.

### FR-PROBE-3: Required evidence

The probe must verify:

1. child spawn succeeds;
2. a valid standard-NUT identifier, main header, stream header, and syncpoint/packet sequence are parsed;
3. one H.264 video stream exists;
4. exactly one packet is returned per picture;
5. PTS/order is monotonic and input-order preserving;
6. no picture is duplicated or dropped;
7. packet payloads are Annex B;
8. first packet has NUT key flag plus SPS, PPS, and IDR — with **exactly
   one SPS** (a duplicated parameter set blacks out strict decoders such
   as the macOS Windows App VideoToolbox path; amended 2026-07-26,
   FR-PROBE-6);
9. second packet contains VCL data and follows in the same stream;
10. four packets complete before the configured deadlines; and
11. the child terminates and is reaped without fd leaks.

### FR-PROBE-4: No persistent cache

Do not implement persistent disk caching or a new cross-process probe service in MVP. A connection process probes once before capability confirmation and reuses that in-memory result. A later xrdp process rechecks the exact command, avoiding stale **server readiness** after FFmpeg replacement, package update, permission change, or configuration edit.

Within one established connection, the client capability and server selection are immutable. Runtime process restart uses the already selected profile and bounded reset path rather than renegotiating GFX capabilities.

### FR-PROBE-5: Failure

Probe failure removes only the configured `ffmpeg` H.264 candidate. It does not fail the xrdp service. Existing codec order then proceeds to the next entry, normally RFX. A separately configured linked H.264 backend remains a different administrator-selected mode rather than an implicit fallback.

### FR-PROBE-6: Verify-only contract and outcome observability (2026-07-26)

The probe is a **verifier of administrator-declared policy, never a policy
discoverer**. The in-band parameter-set policy (`[avc444_ffmpeg]
dump_extra` in `gfx.toml`) is static per-deployment configuration, exactly
like `encoder_args`; the probe runs **once** per capability decision with
the declared value and either confirms it or removes the AVC candidate.

1. **No adaptation, no cascade.** A probe failure must never change the
   wire policy or re-run the probe with a different command line. Only a
   CONTENT reject (a parsed first packet that violates the declared header
   contract) is deterministic evidence about the encoder; a timeout, spawn
   failure or stream error is environmental. Rationale (T4, 2026-07-26):
   the earlier adaptive ladder ("probe pristine, retry with dump_extra on
   failure") could not distinguish the two, so a cold-GPU timeout on the
   pristine attempt followed by a warm retry would have enabled
   `dump_extra` on an in-band encoder — duplicated SPS/PPS, the exact
   strict-decoder black-screen class the ladder was built to prevent.
2. **Exactly-once parameter sets.** The first packet must carry exactly
   one SPS. `dump_extra = true` on an encoder that already repeats headers
   in-band is refused (CONTENT reject: duplicates); `dump_extra = false`
   on an extradata-only encoder is refused (CONTENT reject: missing). The
   runtime first-packet validator enforces the same bound.
3. **Observable verdicts.** Every probe outcome is classified and logged:
   `OK / BAD_CONFIG / SPAWN_FAIL / TIMEOUT / STREAM_ERROR /
   CONTENT_REJECT`, with the failing-check description, packet count,
   elapsed time, the child's stderr (bounded, sanitized) and the child's
   exit status. A probe failure must be diagnosable from the log alone —
   no live-box shim or off-box replay required (observability debt from
   the 2026-07-22 and 2026-07-26 T4 incidents).

---

## 9. Non-functional requirements

## 9.1 Portability

### NFR-PORT-1

Target Linux and supported Unix-like xrdp/xorgxrdp server environments with normal POSIX process and pipe facilities:

- `pipe()`/`pipe2()` or xrdp wrappers;
- `fcntl()`;
- `poll()`;
- `posix_spawn()` or `fork()`/`execve()`;
- `read()`/`write()`;
- `waitpid()`;
- signals; and
- monotonic clocks.

### NFR-PORT-2

Linux-specific acceleration APIs such as DMA-BUF, VAAPI, QSV device integration, `eventfd`, `epoll`, or systemd APIs are not mandatory for MVP.

### NFR-PORT-3

The MVP requires a stock FFmpeg binary containing `libx264`, rawvideo input, the NUT muxer, and `h264_mp4toannexb`. xrdp itself does not link FFmpeg or x264.

---

## 9.2 Performance

Two full NV12 views require approximately three bytes per desktop pixel per update.

| Resolution | Pair size | 30 updates/s | 60 updates/s |
|---|---:|---:|---:|
| 1280×720 | 2.76 MB | 82.9 MB/s | 165.9 MB/s |
| 1920×1080 | 6.22 MB | 186.6 MB/s | 373.2 MB/s |
| 2560×1440 | 11.06 MB | 331.8 MB/s | 663.6 MB/s |
| 3840×2160 | 24.88 MB | 746.5 MB/s | 1.49 GB/s |
| 5120×2880 | 44.24 MB | 1.33 GB/s | 2.65 GB/s |

A conventional pipe entails userspace-to-kernel and kernel-to-userspace copying, approximately doubling IPC copy traffic. Conversion writes and encoder reads/uploads add further memory traffic.

### NFR-PERF-1

These costs are accepted for the portability-first backend. Failure to sustain a target rate on a selected encoder is not an xrdp correctness failure when bounded-backpressure behavior works.

### NFR-PERF-2

No unnecessary intermediate full-frame YUV444 buffer should be created. Convert the full-chroma capture directly into the two persistent NV12 views.

### NFR-PERF-3

The MVP converter reconstructs both complete views on every submitted update. Dirty-region conversion is a later optimization; pipe transport is full-picture in either case.

### NFR-PERF-4

Avoid per-update process creation, command parsing, heap churn proportional to raw frame size, and unbounded buffer growth.

---

## 9.3 Latency

### NFR-LAT-1

The primary latency metric is age of the displayed desktop state, not preservation of every intermediate update.

### NFR-LAT-2

Do not queue multiple stale unsubmitted pairs. Prefer latest-state coalescing.

### NFR-LAT-3

Output buffering must be minimized through:

- one persistent child;
- no B-frames;
- passthrough frame timing;
- low-delay encoder profile;
- NUT streaming output;
- packet flushing;
- concurrent pipe draining;
- bounded operation deadlines.

### NFR-LAT-4

No fixed latency promise is made for arbitrary hardware profiles. The probe and metrics must make buffering visible.

---

## 9.4 Security

### NFR-SEC-1

Never invoke a shell.

### NFR-SEC-2

Validate and canonicalize executable paths according to administrator policy. Prefer absolute paths.

### NFR-SEC-3

Close unintended file descriptors in the child and use close-on-exec in the parent.

### NFR-SEC-4

Use a minimal controlled environment. Do not pass session secrets through argv or environment.

### NFR-SEC-5

Treat FFmpeg stdout, stderr, and NUT metadata as untrusted bounded input.

### NFR-SEC-6

Limit:

- header length;
- metadata count and string length;
- packet length;
- stderr buffering;
- parser recursion/state;
- per-pair execution time;
- restart frequency.

### NFR-SEC-7

Do not run the child with more privileges than xrdp already has. Hardware-device permissions are administrator configuration.

### NFR-SEC-8

Untrusted FFmpeg stderr and any NUT-metadata-derived string must be logged **only via a constant format string** with the external data passed as a `%s` argument (e.g. `LOG(LOG_LEVEL_WARNING, "ffmpeg: %s", line)`); externally-produced bytes must **never** be passed as the `LOG()` / `log_message()` format argument. This matches xrdp's real logging API — `LOG(level, ...)` expands to `log_message()`, declared `printflike(2,3)` in `common/log.h`, so the first variadic argument is the printf format string — and satisfies CLAUDE.md rule 3 ("never pass client data as a format string"). Strip or escape control characters (including NUL and newlines) before logging.

---

## 9.5 Reliability and observability

### NFR-REL-1

Every child must be reaped. No zombie processes.

### NFR-REL-2

Session teardown must terminate and reap the single FFmpeg child before encoder object destruction completes.

### NFR-REL-3

Child stderr must be drained continuously and emitted through rate-limited xrdp logging.

### NFR-REL-4

Log state transitions with generation, surface ID, PID, dimensions, encoder/profile, and failure cause.

### NFR-REL-5

Repeated restart loops must be rate-limited. After a configured threshold, disable the backend and terminate/fail the GFX or session path cleanly. Mid-connection codec switching is not part of MVP.

---

## 10. Detailed ownership model

| Resource | Owner | Release point |
|---|---|---|
| XRGB capture mapping | Existing xorgxrdp/xrdp capture path | Existing completed-item/acknowledgement lifecycle |
| `XRDP_ENC_DATA` GFX work item | Existing xrdp encoder worker | Existing destructor/completion lifecycle |
| Persistent main NV12 view | Single external AVC444 handle | Resize, reset, or handle deletion |
| Persistent auxiliary NV12 view | Single external AVC444 handle | Resize, reset, or handle deletion |
| Raw bytes after `write()` | Kernel/FFmpeg | Consumed by child; parent may reuse view only after the entire corresponding raw picture is written |
| Ordered picture tag | External handle pending FIFO | Matching NUT packet associated or generation discarded |
| NUT parser buffer | External handle | Parsed/compacted or child reset |
| Encoded main packet | Pair assembly owned by handle | RDPGFX output object takes ownership or generation fails |
| Encoded auxiliary packet | Pair assembly owned by handle | RDPGFX output object takes ownership or generation fails |
| FFmpeg child/fds | External handle | Resize, reset, failure, or session teardown |
| RDPGFX compressed output | Existing `XRDP_ENC_DATA_DONE` path | Existing send/destructor lifecycle |

The xrdp encoder worker is the only thread allowed to mutate the handle in MVP. This removes the need for a second internal encoder queue or child-control protocol.

---

## 11. Proposed C interfaces

These are design sketches, not final ABI.

```c
struct xrdp_ffmpeg_avc444_config
{
    const char *path;                  /* absolute ffmpeg path */
    int desktop_fps;
    int stream_ready_timeout_ms;
    int picture_timeout_ms;
    int pair_timeout_ms;
    int terminate_grace_ms;
    size_t max_nut_header_bytes;
    size_t max_encoded_picture_bytes;
    size_t max_encoded_pair_bytes;
    int quality_crf;
    int gop_pictures;
    struct list *allowed_extra_x264_args;
};

struct xrdp_avc444_encoded_picture
{
    uint8_t *data;
    size_t bytes;
    int nut_keyframe;
    uint64_t pts;
};

struct xrdp_avc444_encoded_pair
{
    uint64_t generation;
    uint64_t desktop_sequence;
    struct xrdp_avc444_encoded_picture main;
    struct xrdp_avc444_encoded_picture auxiliary;
};

void *
xrdp_encoder_ffmpeg_avc444_create(
    const struct xrdp_ffmpeg_avc444_config *config,
    int surface_id,
    int actual_width,
    int actual_height);

int
xrdp_encoder_ffmpeg_avc444_encode_pair(
    void *handle,
    const uint8_t *xrgb,
    int xrgb_stride,
    int actual_width,
    int actual_height,
    const struct xrdp_egfx_rect *rects,
    int rect_count,
    uint64_t desktop_sequence,
    struct xrdp_avc444_encoded_pair *result);

int
xrdp_encoder_ffmpeg_avc444_restart(
    void *handle,
    int actual_width,
    int actual_height,
    enum xrdp_encoder_reset_reason reason);

void
xrdp_encoder_ffmpeg_avc444_delete(void *handle);
```

The handle computes 16-aligned coded dimensions internally. A distinct pair-returning interface is preferred over forcing AVC444 into the current one-buffer `xrdp_encoder_h264_encode()` signature. Resize should normally delete and recreate the handle; the restart operation is shown only as a possible internal lifecycle helper and must still kill/reap the old child.

---

## 12. Coded dimensions and padding

The visible RDP surface may have arbitrary dimensions. The H.264 coded picture may not.

### FR-DIM-1: Mandatory 16-pixel alignment

MS-RDPEGFX requires the width and height of each AVC420 H.264 bitstream—including each AVC444 substream—to be aligned to a multiple of 16:

```c
coded_width  = (actual_width  + 15) & ~15;
coded_height = (actual_height + 15) & ~15;
```

This is a protocol requirement, not an encoder-profile preference.

### FR-DIM-2: Arbitrary visible resize

No alignment restriction is imposed on the RDP window or desktop. The AVC420 metablock region mask crops display/update regions to actual coordinates. For a full reset, the visible region is `[0, 0, actual_width, actual_height]` while the encoded NV12 pictures use coded dimensions.

### FR-DIM-3: Padding contents

Initialize all padded pixels. Replicate the final visible row and column into right/bottom padding for both synthetic views. Never expose or encode uninitialized bytes.

### FR-DIM-4: Resize behavior

Every actual width/height change kills and recreates the FFmpeg child, even when the rounded coded dimensions remain unchanged. The new generation starts with a fully reconstructed padded `LC=0` pair.

### FR-DIM-5: Encoder restrictions

MVP does not negotiate profile-declared alignment beyond 16. The pre-confirm probe uses the actual 16-aligned session geometry; if the exact libx264 command cannot open or produce compliant output at that size, the external candidate is removed before codec-order selection. A later unexpected real-child spawn failure fails GFX after bounded retry. Hardware encoders requiring larger alignment are deferred.

---

## 13. Failure handling matrix

| Failure | Required action |
|---|---|
| Probe spawn/command failure | Remove external AVC444 candidate before capability confirmation |
| Real-child spawn failure after capability confirmation | Bounded retry; then fail/reset GFX or the session; no codec switch in MVP |
| FFmpeg exits before NUT stream-ready | Log bounded stderr; kill/reap; fail generation |
| Invalid NUT header/syncpoint/CRC | Kill/reap child; discard generation; increment parser failure |
| Broken input pipe or stdout EOF | Kill/reap; discard all pending tags and half-pair state |
| Stderr flood | Continue draining; rate-limit retained/logged bytes |
| Main or auxiliary deadline exceeded | Kill/reap; discard half-pair; never continue stream |
| Packet exceeds hard safety ceiling | Kill/reap; reject generation; never truncate |
| Packet count not 1:1 | Probe reject or runtime generation failure |
| Reordered/non-monotonic output | Probe reject or runtime generation failure |
| First packet lacks SPS/PPS/IDR | Probe reject or runtime reset failure |
| Resize | Kill/reap old child unconditionally; create new generation |
| Topology becomes multi-monitor | Kill/reap child and fail/restart the GFX connection path; no encoder remapping or mid-stream codec switch |
| Repeated runtime failures | Disable external AVC444 for the connection and terminate/fail GFX or the session; no mid-stream codec switch in MVP |
| Encoder-object teardown | Cancel poll loop, close fds, terminate/reap child, then complete worker teardown |

No runtime failure permits sending one member of a pair, skipping an encoded picture, or splicing a new child into the old H.264 stream.

---

## 14. Compatibility strategy

### 14.1 MVP FFmpeg support contract

The exact configured binary is supported only when the behavioral probe passes. Version strings and `-encoders` output are diagnostic, not sufficient evidence.

**Deployment prerequisite (build configuration).** `libx264` is only present in a **GPL-enabled** ffmpeg build (`--enable-gpl --enable-libx264`); LGPL-only or minimal distribution packages (e.g. some RHEL/rpmfusion-free-less or hardened builds) omit it, and the MVP command then fails at encoder-open. This is a packaging/deployment prerequisite to document, not merely a runtime-probe outcome. (Debian's stock `ffmpeg`, tested here, is GPL-enabled with `libx264`.)

Required executable features:

- rawvideo demuxer;
- NV12 input;
- `libx264` encoder;
- NUT muxer with normal syncpoints;
- `-write_index 0`;
- passthrough/no-drop timing;
- no B-picture reordering;
- `h264_mp4toannexb`; and
- libx264 `repeat-headers=1` support.

The MVP profile is:

```text
encoder = libx264
args = -preset ultrafast -tune zerolatency -crf 18 -g 240
structural = -bf 0 -x264-params repeat-headers=1
```

Quality and GOP values are configurable; structural behavior is not.

### 14.2 Future Linux/Unix hardware profiles

QSV, VAAPI, NVENC, and other encoders are future work. A later profile must demonstrate:

- the same two-picture/single-stream ordering;
- standard NUT packetization;
- one packet per picture;
- Annex-B payloads with usable reset parameter sets;
- no reordering;
- bounded startup and steady-state delay; and
- MSTSC AVC444 interoperability.

Only after a real profile fails should the shared implementation add mechanisms such as NUT-extradata parameter-set synthesis or hardware-specific reset control.

---

## 15. Testing requirements

## 15.1 Unit tests

### Capability classification

FR-CAP-2 is pure input→output logic (capability version + flags → AVC444-v1 / AVC420 / none) and per CLAUDE.md rule 5 requires table-driven unit tests independent of a live client:

- `RDPGFX_CAPVERSION_8` → no AVC candidate;
- v8.1 with `AVC420_ENABLED` → AVC420 only; v8.1 without it → none;
- v10.0 with `AVC_DISABLED` clear → AVC444 v1; any v10.x with `AVC_DISABLED` set → none;
- v10.1 (reserved-only capset) → not eligible for the v1-only MVP;
- v10.2–v10.7 with `AVC_DISABLED` clear → AVC444 v1;
- highest-supported-version tie-break when several eligible sets of the same mode are advertised;
- `AVC_THINCLIENT` treated as a preference, not a prerequisite.

### Converter and color

- exact full-range BT.709 matrix vectors, including clamp boundaries;
- canonical AVC444 v1 two-view vectors;
- solid colors and color bars;
- luma-only and chroma-only source changes;
- red/blue one-pixel text edges;
- odd visible dimensions;
- proof that every submitted update reconstructs the complete views regardless of dirty-rectangle shape;
- 16-pixel coded padding and edge replication;
- full reset after partial history.

### NUT demuxer

- normal standard-syncpoint streams generated by stock FFmpeg;
- fragmented one-byte reads and multiple packets per read;
- repeated headers and syncpoints;
- valid/invalid CRCs;
- oversized variable-length integers;
- malformed frame-code tables;
- wrong stream count/codec;
- packet/header safety ceilings;
- truncated EOF at every field boundary;
- fuzz corpus and regression minimization.

Do not include no-syncpoint/PIPE fixtures in MVP.

### H.264 adapter

- Annex-B start-code scanning;
- NUT extradata parsing/bounds without synthesis;
- first-packet SPS/PPS/IDR detection;
- auxiliary VCL detection;
- absence/presence of AUD;
- malformed/truncated NAL streams;
- key flag disagreement;
- packet size ceilings.

### Process state machine

- partial raw writes;
- stdout/stderr output while input blocks;
- child early exit;
- stream-ready, picture, and pair timeout;
- SIGTERM/SIGKILL escalation;
- resize in every state;
- topology change to multiple monitors;
- xrdp termination in every state;
- no zombies or fd leaks.

## 15.2 Executable behavior tests

Required MVP environment:

- stock Linux/Unix distribution FFmpeg containing `libx264`;
- standard NUT output;
- actual-session-geometry pre-confirm probes, plus 32×32 parser/command fixtures and real 1080p tests;
- arbitrary visible dimensions such as 1919×1079 with 1920×1088 coded frames;
- rapid resize sequences;
- repeated connect/disconnect;
- deliberately throttled child to verify bounded backpressure.

The local PRD review used Debian FFmpeg 7.1.5. Four NV12 input pictures produced four ordered NUT packets. `h264_mp4toannexb` produced Annex-B packet payloads but did not put SPS/PPS in the first packet until `-x264-params repeat-headers=1` was supplied. This behavior is captured as a regression fixture/command, not treated as a universal version guarantee.

## 15.3 MSTSC interoperability

Minimum target:

- Windows 10 / Windows Server 2016-generation `mstsc.exe` advertising an eligible RDP 10 AVC444 v1 capability set.

Also test current Windows 10/11 MSTSC versions available to the project. Capability content, not reported OS identity, determines selection.

Test content:

- grayscale and ClearType text;
- red text on black and blue text on white;
- terminal/browser scrolling;
- window movement;
- video playback;
- static idle desktop;
- arbitrary window resize;
- initial multi-monitor codec-order fallback before capability confirmation; and
- post-confirm topology-change failure without a mid-session codec switch.

## 15.4 Packet capture verification

Verify:

- selected capability version and flags;
- codec order decision and explicit `AVC444` log;
- `WireToSurface1` codec ID `0x000E`;
- `LC=0`;
- exact first-substream length;
- two valid AVC420 metablocks;
- both coded dimensions aligned to 16;
- region rectangles cropped to actual dimensions;
- main then auxiliary H.264 order;
- first generation packet contains SPS/PPS/IDR;
- reset generation after every resize; and
- no stale old-generation output.

---

## 16. Acceptance criteria

The MVP is complete when all of the following are true:

1. xrdp builds without FFmpeg development headers/libraries and without linked x264/OpenH264.
2. An administrator configures an absolute stock FFmpeg path containing `libx264`.
3. The behavioral probe completes before AVC444 capability confirmation.
4. Codec order remains authoritative; `RFX` can still preempt H.264 when configured earlier.
5. Eligible RDP 10.0 or 10.2–10.7 capabilities are classified for AVC444 v1; v8.1 is AVC420-only; v10.1 is not misused as AVC444 v1. With `h264_encoder = "ffmpeg"`, only the AVC444-v1 class is selectable.
6. A Windows Server 2016-era or newer eligible MSTSC displays codec ID `0x000E` AVC444 output.
7. XRGB8888 capture is converted with the exact full-range BT.709 matrix.
8. Arbitrary visible dimensions work while both H.264 coded dimensions are multiples of 16.
9. Every logical update is one main and one auxiliary picture through the same persistent FFmpeg stream.
10. Child stdout is standard NUT with normal syncpoints; xrdp never relies on `read()` boundaries.
11. The first main packet of every generation contains Annex-B SPS, PPS, and IDR; `repeat-headers=1` is enforced.
12. Every resize kills and reaps the child before new-size input, then sends a full `LC=0` reset pair.
13. Child failure, malformed NUT, timeout, pipe break, and output-size violation cannot deadlock xrdp or emit a half-pair.
14. Queueing is bounded to committed work plus at most the newest unsubmitted state.
15. Encoded buffers grow best-effort under independent hard safety ceilings and do not reuse the legacy GFX compressed-size limit.
16. Single-monitor behavior passes; an initial multi-monitor topology causes normal pre-confirm codec-order fallback, while a post-confirm transition fails/restarts GFX without switching codecs.
17. The NUT parser passes unit tests, provenance-checked fixtures, and fuzzing.
18. No shell invocation, zombie process, unintended fd inheritance, or stale-generation output exists; the FFmpeg child is spawned with a minimal controlled environment carrying no session secrets in argv or environment (NFR-SEC-4).
19. No non-target platform-specific runtime or documentation path remains in the MVP PRD.
20. Capability classification (FR-CAP-2) passes table-driven unit tests independent of a live client.
21. Backpressure/health metrics (FR-BP-7) are exposed and validated under the throttled-child test — at minimum restart count, timeout count, parser errors, and coalesced/dropped-before-submit counts.

---

## 17. Implementation plan and pull-request decomposition

### PR 1: Capability policy, build guards, and single-monitor gate

- Add explicit AVC modes and separate AVC444/AVC420 candidates.
- Implement the exact capability table and codec-order interaction.
- Exclude version 10.1 from v1 selection.
- Gate external AVC444 on one monitor and successful pre-confirm probe.
- Refactor x264/OpenH264-only compile guards around common GFX AVC code.

### PR 2: XRGB8888 capture mode

- Add `CC_GFX_AVC444`.
- Allocate/transport XRGB8888 capture with stride and actual dimensions.
- Preserve existing capture acknowledgement.
- Kill/recreate state on every visible resize.

### PR 3: Color conversion, AVC444 views, and 16-aligned padding

- Implement exact full-range BT.709 integer conversion.
- Implement Microsoft AVC444 v1 main/Chroma420 mapping.
- Add persistent NV12 views, complete-view reconstruction, and edge-replicated padding.
- Reconstruct both complete views on every submitted update.
- Add specification-derived vectors.

### PR 4: Secure FFmpeg process runner and pre-confirm probe

- Implement argv builder, fd mapping, nonblocking `poll()`, stderr drainage, deadlines, termination/reaping.
- Target stock `libx264` only.
- Run the four-picture behavioral probe at the actual 16-aligned session geometry before capability confirmation, then terminate/reap that probe child before starting the real stream.

### PR 5: Standard NUT demuxer

- Implement only standard syncpoints with `-write_index 0`.
- Add independent format notes, provenance-recorded fixtures, bounds, CRC handling, and fuzz target.
- Do not implement experimental PIPE/no-syncpoint mode.

### PR 6: H.264 validation

- Require Annex B, one packet per picture, monotonic FIFO order.
- Enforce `repeat-headers=1` and check first packet SPS/PPS/IDR.
- Parse/bound NUT extradata but do not synthesize parameter sets.

### PR 7: Pair backend and output-size safety

- Connect converter → raw pipe → NUT demuxer.
- Add pair API, ordered tags, dynamic output buffers, and independent safety ceilings.
- Add bounded backpressure and metrics.

### PR 8: RDPGFX AVC444 v1 serializer

- Emit `LC=0`, two AVC420 substreams, and codec ID `0x000E`.
- Apply actual-dimension region masks over 16-aligned coded pictures.
- Reuse `gfx_send_done()` completion path.

### PR 9: Resize/reset/failure hardening and MSTSC acceptance

- Enforce kill/reap on every resize and every stream discontinuity.
- Add failure thresholds and clean GFX/session failure behavior without mid-stream codec switching.
- Complete Windows Server 2016-generation MSTSC and current MSTSC tests.

### Later PRs

- AVC444v2.
- `LC=1`/`LC=2` deferred chroma.
- Independent main/chroma damage.
- Multi-monitor ownership.
- Linux/Unix hardware FFmpeg profiles.
- Parameter-set synthesis from NUT extradata only when required by a demonstrated profile.
- Optional shared-memory/libavcodec performance tier.

---

### Clean-room upstream port — locked decisions and slice plan

*Moved from BACKLOG 2026-07-28 (persistent decisions belong here). The task itself stays in BACKLOG as an open item.*

Transition from the dev branch to a reviewable upstream PR against `devel`.
The dev branch stays as-is (history + scaffold); the PR is rebuilt clean.

#### Owner decisions (locked)
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

#### Base the clean-room branch on `origin/devel`, not local `devel`
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

#### Excluded from the PR (dev-branch scaffold, keep in dev branch only)
`PR-demo/**`, `tests/xrdp/avc444/repro_mbparity/**`,
`tests/xrdp/avc444/FINDINGS_*.md`, `repro_*.py`, `tools/gen_isoluma.py`,
`PRD.md`, `BACKLOG.md`, `CLAUDE.md`, `*_config.md`, `scripts/build_dev_deb.sh`,
`dist/` debs, and all untracked scratch (burr/partialGreen PNGs, `tester_key`,
`xrdp-PR.tar`, `iptables.rules`, `*.Po`, …). Add a `.gitignore` hygiene pass.
**Keep** `tests/xrdp/avc444/PROVENANCE.md` (the NUT independent-implementation
/ licensing attestation) — fold it into the NUT slice and the PR cover letter;
maintainers will ask.

#### Divergence risk: none textual, one semantic touchpoint to verify
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

#### Acceptance
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

### Commit reorganization plan (clean-room slices) — DRAFT (2026-07-17)

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

#### Slice-order amendment: latent upstream multimon fix FIRST (2026-07-25, owner directive)

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

## 18. Review path used for this PRD

### Review pass 1: Architecture consistency

Confirmed:

- two AVC444 pictures must use one H.264 encoder/decoder stream;
- one persistent FFmpeg child is therefore required;
- raw pipe boundaries are fixed by NV12 picture size;
- encoded boundaries require a container demuxer;
- NUT demuxing is not H.264 decoding;
- full-frame process copies are an accepted portability tradeoff.

### Review pass 2: Current xrdp/xorgxrdp `devel` mapping

Reviewed current raw source for:

- `xrdp_mm_egfx_caps_advertise()` sorting capability sets and applying configured codec order;
- its current single `best_h264_index` and generic `XRDP_EGFX_H264` result;
- x264/OpenH264 compile guards surrounding common GFX H.264 behavior;
- encoder-worker ownership and `gfx_send_done()` completion;
- `gfx_wiretosurface1()` and existing H.264 handle slots;
- current NV12 AVC420 capture; and
- existing 32-bit xorgxrdp capture handling.

Refinement: the implementation must split AVC444-v1 and AVC420 candidates, preserve codec-order iteration, extend the existing `h264_encoder` selector with `ffmpeg`, and gate MVP on `monitorCount <= 1`.

### Review pass 3: Microsoft protocol review

Confirmed:

- v8.1 advertises AVC420 with `AVC420_ENABLED`;
- v10.0 and v10.2–v10.7 with `AVC_DISABLED` clear imply YUV444 capability;
- v10.1 carries a reserved-only capset (no AVC flags) and is not accepted as a v1 substitute;
- only the selected `CAPS_CONFIRM` set applies to the connection;
- AVC444 contains two AVC420-form substreams encoded by one H.264 encoder;
- full-range BT.709 is normative; and
- each AVC420 H.264 coded width and height must be a multiple of 16 and is cropped by the region mask.

Refinement: arbitrary visible resize is preserved through coded padding; “no alignment” is not protocol-correct.

### Review pass 4: FFmpeg/NUT executable behavior

Using installed Debian FFmpeg 7.1.5 with `libx264` and four synthetic 32×32 NV12 pictures:

- standard `-f nut -write_index 0` produced one ordered packet per picture;
- packet data used Annex-B start codes with `h264_mp4toannexb`;
- SPS/PPS remained in NUT extradata and were absent from the first packet with the bitstream filter alone;
- adding `-x264-params repeat-headers=1` put SPS/PPS in the first key packet before the IDR; and
- packet flags/timestamps remained ordered with `-bf 0` and zero-latency settings.

Refinement: `repeat-headers=1` and direct SPS/PPS/IDR packet validation are mandatory for MVP; extradata synthesis is deferred.

### Review pass 5: NUT subset and licensing boundary

Confirmed:

- FFmpeg documents normal NUT syncpoints as low-overhead and recommends them over `syncpoints=none`;
- `-write_index 0` is suitable for endless streaming without removing syncpoints;
- FFmpeg's NUT implementation is LGPL-2.1-or-later; and
- a clean independently implemented bounded parser with generated fixtures avoids copying source implementation details.

Refinement: remove all experimental PIPE/no-syncpoint code and tests from MVP.

### Review pass 6: Lifecycle, limits, and topology

Walked through:

- partial input write;
- stdout/stderr backpressure;
- delayed output;
- half-pair failure;
- size ceiling;
- timeout;
- resize with and without changed rounded coded dimensions;
- child exit;
- session teardown; and
- multi-monitor advertisement/topology change.

Refinement: the legacy compressed limit is not reused, every visible resize creates a new generation, both complete AVC444 views are reconstructed from XRGB for every submitted MVP update, and MVP is explicitly single-monitor.

### Review pass 7: Final cross-section consistency and artifact lint

Rechecked the complete document for:

- one persistent FFmpeg child and one H.264 stream for both AVC444 pictures;
- no accidental claim that the complete xrdp deployment contains only two processes;
- no mid-session codec fallback after capability confirmation;
- initial multi-monitor codec-order fallback versus post-confirm topology failure;
- actual-session-geometry probing and mandatory disposal of the probe child;
- full-view reconstruction rather than dirty-only conversion in MVP;
- standard NUT syncpoints only;
- first-packet SPS/PPS/IDR validation with `repeat-headers=1`;
- 16-aligned coded dimensions with arbitrary visible dimensions; and
- Linux/Unix-only platform scope.

The Markdown was parsed with Pandoc, code-fence counts were checked, headings were checked for duplicates, and stale platform/open-question markers were searched before publishing `PRD_v2.md`.

### Review pass 8: Automated multi-source re-verification (2026-07-13)

A fan-out verification cross-checked every concrete claim against ground truth on this working tree (commit `4d61d13b`), with each change-proposing finding adversarially re-derived before acceptance:

- **Codebase** — all Section 6 / Section 11 seams confirmed against `/work` and `/workUpdateXorgXrdp`, with real file:line: `xrdp_encoder.c` (`xrdp_encoder_create`/`delete`, `gfx_send_done`, `gfx_wiretosurface1`, `process_enc_h264` dummy, `codec_handle_h264_gfx[16]`, `CC_GFX_A2`/`XRDP_nv12_709fr`), the `#if defined(XRDP_X264) || defined(XRDP_OPENH264)` guards, `xrdp_encoder_x264.c` reconstruction, and `xrdp_mm.c` (`init_libh264_loaded`, `xrdp_mm_egfx_caps_advertise`, `best_h264_index`). Corrections: named the real serializer `xrdp_egfx_wire_to_surface1()`; documented that `init_libh264_loaded()` keys on the `XRDP_H264` umbrella macro (`xrdp/xrdp.h:41-43`), not `XRDP_X264`; identified the current GFX capture as `XRDP_nv12_709fr` (BT.709 full-range); and surfaced the pre-existing unimplemented `XRDP_yuv444*_709fr` constants the new capture mode should reuse.
- **FFmpeg/NUT** — the exact FR-PROC-5 argv was re-run end-to-end on stock Debian `ffmpeg 7.1.5` (GPL, `libx264`): valid single-stream NUT, in-band Annex-B SPS/PPS/IDR with `repeat-headers=1`. Added the GPL-build (`--enable-libx264`) deployment prerequisite and the `yuvj420p` full-range tag note.
- **MS-RDPEGFX** — codec IDs (AVC420 `0x000B`, AVC444 `0x000E`, AVC444v2 `0x000F`), capability constants/flags, the full-range BT.709 matrix, the two-view mapping, LC semantics, and 16-pixel alignment all match the spec. Corrections: renamed the info word to `avc420EncodedBitstreamInfo` with named subfields (`cbAvc420EncodedBitstream1`/`LC`, MS-RDPEGFX 2.2.4.5); softened the v10.1 "implies AVC444v2" wording to "reserved-only capset."
- **Consistency/security** — added format-string-safety (NFR-SEC-8), explicit NUT integer-overflow discipline (FR-NUT-7), and closed FR↔acceptance/test gaps for capability classification (§15.1) and metrics (§16). No design-level contradictions were found.

---

## 19. Required pre-implementation code review

Immediately before implementation, pin immutable xrdp and xorgxrdp commit IDs and refresh these exact seams:

1. `xrdp_mm_egfx_caps_advertise()` capability arrays, sorting, codec-order representation, and the point where `CAPS_CONFIRM` is sent;
2. how/when initial monitor count is known before capability confirmation;
3. `XRDP_ENC_DATA` GFX command layout and its `codec_id`, dimensions, rectangles, and surface identity;
4. enqueue/dequeue points for `fifo_to_proc` and `fifo_processed`;
5. the exact xorgxrdp acknowledgement permitting capture-buffer reuse;
6. resize ordering among capture reallocation, surface recreation, queued work, and `xrdp_encoder` lifetime;
7. compile guards for GFX H.264 code and definitions of `XRDP_H264`, `XRDP_X264`, and `XRDP_OPENH264`;
8. codec constants for AVC420/AVC444 and available XRGB/YUV format constants;
9. typed `gfx.toml` parsing and existing `H.264`/`RFX` order spelling; and
10. output-buffer ownership in `xrdp_egfx_wire_to_surface1()` and `XRDP_ENC_DATA_DONE`.

The implementation PR must include a seam-review note with old/new function names and line references. Any codebase drift updates this PRD's mapping rather than adding an undocumented queue, copy, or fallback.

---

## 20. Resolved design questions

### 20.1 Capability policy and codec order

**Resolution:** Classify AVC444 v1 for eligible v10.0 and v10.2–v10.7 sets with AVC enabled; classify AVC420 for v8.1 only when `AVC420_ENABLED`; exclude v10.1 (its reserved-only capset carries no AVC flags and is treated as AVC444v2 territory, not a v1 substitute). Preserve configured codec order and extend `h264_encoder` with `ffmpeg`. In MVP, `ffmpeg` supplies AVC444 v1 only and never silently falls back to a linked encoder.

### 20.2 Capture format

**Resolution:** XRGB8888. It is clear, full-chroma, and compatible with existing 32-bit capture handling. Prototype conversion compute is accepted.

### 20.3 Color conversion

**Resolution:** MS-RDPEGFX full-range BT.709 using the exact integer equations in section 3.3.8.3.1. Do not start with a configurable colorspace matrix.

### 20.4 Coded alignment

**Resolution:** Visible dimensions remain arbitrary, but H.264 coded width and height are always rounded up to multiples of 16 as required by MS-RDPEGFX. Region masks crop to actual dimensions. There is no additional profile alignment in MVP.

### 20.5 NUT subset

**Resolution:** Standard NUT with normal syncpoints and `-write_index 0` only. Experimental `syncpoints=none` / PIPE mode is deferred and absent from parser/tests.

### 20.6 Parameter sets

**Resolution:** `h264_mp4toannexb` alone is insufficient in the tested mundane libx264/NUT path. MVP adds `-x264-params repeat-headers=1` and requires SPS/PPS/IDR in the first packet. It parses but does not synthesize from NUT extradata. Revisit only for a demonstrated later hardware profile.

### 20.7 Reset keyframes

**Resolution:** No runtime force-IDR control. Child creation is the reset mechanism. Bounded timeouts and first-packet SPS/PPS/IDR checks determine success.

### 20.8 Probe and fallback timing

**Resolution:** Probe once in the connection process before capability confirmation, without persistent disk/cross-process caching. The client capability is not what changes; the server's ability to honor AVC444 can change between processes after executable/configuration/permission updates. Runtime failure uses bounded restart and then GFX/session failure, not capability mutation or mid-stream codec fallback.

### 20.9 Compressed limits

**Resolution:** Do not apply the current GFX maximum compressed-byte setting per subframe or pair. Allocate best-effort dynamically under independent hard parser/allocation safety ceilings; never truncate encoded output.

### 20.10 Multi-monitor topology

**Resolution:** One logical display only, represented by current `monitorCount` 0 or 1. Initial multi-monitor makes the external AVC444 candidate unavailable; a later transition to multi-monitor kills/reaps the child and fails/restarts GFX rather than switching codecs or remapping contexts.

### 20.11 NUT licensing and test vectors

**Resolution:** NUT is a publicly documented transport rather than a proprietary FFmpeg-only interface. FFmpeg publishes its implementation under LGPL-2.1-or-later. xrdp executes, rather than links, the separately installed encoder binary. Implement the demuxer independently from public format documentation, do not copy FFmpeg parser source/tests, and generate provenance-recorded fixtures with stock FFmpeg. Maintainer/legal review remains authoritative.

### 20.12 Server platform scope

**Resolution:** Linux and supported Unix-like xrdp/xorgxrdp servers only. Non-target platform-specific encoder discussion is removed from MVP.

---

## 21. Risks and mitigations

| Risk | Mitigation |
|---|---|
| NUT parser complexity | Standard-syncpoint-only subset, independent design note, provenance fixtures, fuzzing, strict bounds |
| Parser licensing concern | No copied FFmpeg parser code/tests; public format implementation; maintainer/legal review |
| Annex-B packet lacks reset headers | Enforce libx264 `repeat-headers=1`; probe/runtime SPS/PPS/IDR checks |
| FFmpeg buffers or reorders | `-bf 0`, zero-latency profile, one-to-one packet probe, bounded deadlines |
| Encoder cannot sustain 2× picture rate | Not a correctness promise; bounded pre-submit coalescing and metrics |
| Pipe throughput at high resolution | Accepted MVP tradeoff; no hidden deep pipe queue; future optimization tier |
| Half-pair failure corrupts stream | Kill/reap child; discard complete generation; never continue |
| Resize race | Every visible resize stops old work, kills/reaps child, discards old output, sends full reset pair |
| Misaligned arbitrary window size | 16-aligned coded buffers plus actual-dimension region masks |
| Color mismatch | Exact MS full-range BT.709 equations and MSTSC color/text vectors |
| Output allocation abuse | Dynamic buffers under independent 128 MiB packet / 256 MiB pair default safety caps |
| Runtime child fails after capability confirmation | Bounded restart, failure threshold, then clean GFX/session failure; no mid-stream codec switch |
| Multi-monitor use | Offer external AVC444 only when current `monitorCount <= 1`; reject/fail topology transition tests |
| FFmpeg CLI drift | Exact behavioral probe using shared command builder |
| User arguments break invariants | Allowlisted token arrays; structural argv owned by xrdp |
| Process deadlock/leak | Nonblocking three-fd poll loop, deterministic close/kill/waitpid tests |
| Codebase changes before implementation | Pin commits and include seam-review note in each PR |

---

## 22. Source and specification references

### xrdp and xorgxrdp `devel`

- xrdp encoder implementation:  
  https://raw.githubusercontent.com/neutrinolabs/xrdp/refs/heads/devel/xrdp/xrdp_encoder.c
- xrdp encoder structures:  
  https://raw.githubusercontent.com/neutrinolabs/xrdp/refs/heads/devel/xrdp/xrdp_encoder.h
- current x264 backend:  
  https://raw.githubusercontent.com/neutrinolabs/xrdp/refs/heads/devel/xrdp/xrdp_encoder_x264.c
- GFX capability selection:  
  https://raw.githubusercontent.com/neutrinolabs/xrdp/refs/heads/devel/xrdp/xrdp_mm.c
- codec and pixel-format constants:  
  https://raw.githubusercontent.com/neutrinolabs/xrdp/refs/heads/devel/common/xrdp_constants.h
- xorgxrdp capture/shared-memory path:  
  https://raw.githubusercontent.com/neutrinolabs/xorgxrdp/refs/heads/devel/module/rdpClientCon.c
- GFX configuration:  
  https://raw.githubusercontent.com/neutrinolabs/xrdp/refs/heads/devel/xrdp/gfx.toml

### Microsoft MS-RDPEGFX

- Versioning and capability negotiation:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/31c6e2b1-335b-4a75-9454-bb2309958c21
- Capability version 8.1 / AVC420 flag:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/487e57cc-cd16-44c4-add8-60b84bf6d9e4
- Capability version 10 / AVC444 implication:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/d1899912-2b84-4e0d-9e6d-da0fd25d14bc
- Capability version 10.1:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/5985e67e-4080-49a7-85e3-eb3ba0653ff6
- Capability version 10.3 / AVC flags:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/fef125c5-60be-43af-8ad1-2158761f4b32
- AVC420 stream and 16-pixel coded alignment:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/5f12c20e-2ea1-4ad1-a2a0-019ee3893731
- AVC444 two-substream structure:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/844018a5-d717-4bc9-bddb-8b4d6be5dd3f
- Full-range BT.709 color conversion:  
  https://learn.microsoft.com/en-us/openspecs/windows_protocols/ms-rdpegfx/954d7546-6873-4466-95c8-20a7569c43e5
- Windows Server 2016-generation AVC444 context:  
  https://techcommunity.microsoft.com/blog/microsoft-security-blog/remote-desktop-protocol-rdp-10-avch-264-improvements-in-windows-10-and-windows-s/249588
- `AVC444ModePreferred` platform support floor:  
  https://learn.microsoft.com/en-us/windows/win32/termserv/win32-tsclientsetting

### FFmpeg and NUT

- FFmpeg formats documentation / NUT syncpoint and `write_index` options:  
  https://ffmpeg.org/ffmpeg-formats.html
- FFmpeg H.264 bitstream-filter documentation:  
  https://ffmpeg.org/ffmpeg-bitstream-filters.html
- NUT format documentation:  
  https://ffmpeg.org/nut.html
- FFmpeg NUT definitions and LGPL notice:  
  https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/nut.h
- FFmpeg NUT muxer and LGPL notice:  
  https://raw.githubusercontent.com/FFmpeg/FFmpeg/master/libavformat/nutenc.c

### Local executable review record

- FFmpeg tested: Debian `7.1.5-0+deb13u1` (libavcodec 61.19.101), configured `--enable-gpl --enable-libx264`.
- Test geometry: four 32×32 NV12 pictures at a nominal coded-picture rate of 120 fps. This validates the command/parser assumptions only; connection admission still requires the specified probe at the actual session coded dimensions.
- Result without repeated headers: four ordered NUT packets; first packet Annex B but no SPS/PPS in packet payload; SPS/PPS present as NUT extradata.
- Result with `-x264-params repeat-headers=1`: first key packet contained Annex-B SPS, PPS, and IDR.
- 2026-07-13 re-verification: the exact FR-PROC-5 argv was re-run end-to-end on `7.1.5-0+deb13u1` (raw NV12 in via `pipe:3`, NUT out via `pipe:1`). It exits 0, emits a valid single-H.264-stream NUT container, and with `repeat-headers=1` carries SPS(7)/PPS(8)/IDR(5) in-band as Annex B in the first packet — matching the 7.1.x behavior above. `-f nut`, `-write_index 0`, `-fps_mode passthrough`, `-nostdin`, `h264_mp4toannexb`, and the `nv12` pixel format were all accepted.

---

## 23. Decision record

| Decision | Status |
|---|---|
| Use an unmodified stock FFmpeg executable | Accepted |
| MVP executable encoder is `libx264` | Accepted |
| No compile-time FFmpeg/x264 dependency for external backend | Required |
| Preserve existing codec-order semantics | Required |
| Extend `h264_encoder` with `ffmpeg`; that backend supplies AVC444 v1 only | Accepted |
| Eligible AVC444 v1 sets are v10.0 and v10.2–v10.7 with AVC enabled | Required |
| Version 10.1 is not used for AVC444 v1 | Required |
| Capture XRGB8888 | Accepted |
| Use exact full-range BT.709 conversion | Required |
| One logical display (`monitorCount` 0 or 1) / one surface / one FFmpeg child | Accepted MVP scope |
| Encode main and auxiliary through the same stream | Required |
| Serialize complete reconstructed NV12 pictures over a pipe | Accepted |
| Use standard NUT syncpoints with `-write_index 0` | Required |
| Do not implement experimental NUT PIPE/no-syncpoint mode | Accepted |
| Implement in-tree NUT demuxer, not H.264 decoder | Required |
| Clean-room parser and provenance-generated fixtures | Required process |
| Use `h264_mp4toannexb` plus `repeat-headers=1` | Required MVP behavior |
| Do not synthesize SPS/PPS from extradata in MVP | Accepted |
| Coded dimensions aligned to 16; visible dimensions arbitrary | Required |
| Kill/reap child on every resize | Required |
| No runtime force-IDR RPC; use restart and timeout checks | Accepted |
| Probe before capability confirmation; no persistent cache | Required MVP behavior |
| Legacy GFX compressed limit does not cap AVC444 subframes/pair | Accepted |
| Dynamic best-effort output buffers retain hard safety ceilings | Required |
| Encoder throughput is not guaranteed | Accepted |
| AVC444v2, deferred chroma, multi-monitor, and hardware profiles | Deferred |

---

## 24. Final implementation principle

The backend is a portability adapter, not an encoder implementation.

xrdp owns:

- RDP capability negotiation;
- full-chroma capture contract;
- complete AVC444 view construction for every submitted MVP update;
- update ordering;
- process lifecycle;
- raw-picture submission;
- NUT demuxing;
- H.264 stream validation;
- AVC444 wire framing;
- resize/reset correctness;
- bounded latency policy.

The user's FFmpeg installation owns:

- software or hardware encoder selection;
- driver/device integration;
- H.264 compression;
- rate control;
- encoder-specific performance.

Correctness ends at maintaining a valid, ordered, resettable AVC444 stream. Real-time throughput remains a property of the selected binary, profile, hardware, and workload.

## 25. Delivered — implementation history

Completed work (moved here from BACKLOG, which tracks only upcoming items).
Detailed root-cause writeups live under `tests/xrdp/avc444/`.

- **AVC444 v1 MVP over external stock ffmpeg.** Capability classification,
  full-range BT.709 converter + MS-RDPEGFX two-view construction, bounded NUT
  demux, H.264 Annex-B validation, secure fork/execve process runner + probe,
  `RFX_AVC444_BITMAP_STREAM` (LC=0) serializer. Deployed and validated on-box;
  no compile-time FFmpeg dependency (FR-CAP-0). Pipelined runner (one desktop
  update of latency) — see `avc444/FINDINGS_ffmpeg_latency.md`.

- **encoder_args verbatim passthrough.** Replaced the typed per-flag config with
  a single verbatim `[avc444_ffmpeg] encoder_args` array between the fixed NV12
  input and Annex-B/NUT output contracts, so hardware encoders are expressible
  with no xrdp change. Built-in default reproduces the historic libx264 argv.

- **AVC444 v2 (ChromaV2, codec id 0x000F) + auto-negotiation.** Fixed the
  magenta/purple burr on saturated colored text (a property of AVC444 v1 chroma
  reconstruction, not an xrdp defect). Auto-negotiated per client (v2 for
  capability v10.1+, else v1). mstsc-verified. See
  `avc444/FINDINGS_magenta_burr.md`.

- **AVC444 resize "comb" burr — chroma_align flag (live-verified, 2026-07-15).**
  The ChromaV2 aux U|V split is at `coded_width/2`, but clients derive it from
  the surface width with different rounding — mstsc `round_up_32`, FreeRDP
  `round_up_16` — so an odd coded-macroblock-count width read V 16px displaced
  (period-2 comb + right-edge strip, ~1/2 of widths, mstsc-only). Added
  `gfx.toml [avc444_ffmpeg] chroma_align = 16|32` (default 32) setting the coded
  width alignment; converter and runner are plumbed to agree. Confirmed clean on
  mstsc across multiple widths. Reproducer + writeup:
  `avc444/repro_mbparity/` (`FINDINGS_mstsc_split.md`).

- **Metablock region-rect origin even-alignment.** `out_RFX_AVC420_METABLOCK`
  rounds emitted rect origins down to even (chroma grid) — a separate real
  correctness fix for an odd-origin chroma parity fringe. Kept on the working
  branch with its unit test.

- **Debug tap + offline harness.** Env-gated `XRDP_AVC444_DUMP` dumps per-frame
  main/aux Annex-B H.264 + converter NV12 + meta; plus a FreeRDP-faithful
  offline decoder. The tooling that found and verified the chroma-split fix.

- **Hardware H.264 via VAAPI (live-verified, 2026-07-15).** No xrdp code change —
  the encoder_args passthrough already expresses it. `gfx.toml [avc444_ffmpeg]`
  with `-vaapi_device /dev/dri/renderD128 -vf format=nv12,hwupload -c:v
  h264_vaapi …` drives GPU H.264. Because the AVC444 input is already-decoded
  raw NV12, the GPU-*encode* path uses `hwupload` (all post-`-i`, so
  expressible), not `-hwaccel` decode. ffmpeg is forked from the xrdp connection
  worker, which runs as **root** in standard xrdp (manual and Debian/systemd,
  no `User=`), so it opens the `root:root 0660` render node with no change — the
  session-user permission concern applies only to a hardened non-root xrdp.
  Validated end-to-end on this box: the real runner+probe pass with VAAPI at
  256x256 and 1792x1152; live mstsc AVC444-v2 session shows the child running
  `h264_vaapi` with `drm-driver: amdgpu` and multi-second `drm-engine-enc` GPU
  encode-engine time (hardware, not software). Recipe documented in
  `docs/man/gfx.toml.5.in`.

- **AVC420 (single YUV420 view, codec id 0x000B) over the ffmpeg IPC backend
  (offscreen-verified, 2026-07-15).** Lets a deployment serve non-AVC444 clients
  (and drop the linked x264/OpenH264 library) through the same external ffmpeg
  child. AVC420 is AVC444 minus the aux view: the converter gains a `main_only`
  mode (2x2-averaged main chroma, no aux packing), the runner gains
  `encode_single` (one NV12 picture in, one Annex-B picture out; same pump/NUT/
  validator/pipeline as the pair path), and a new `gfx_wiretosurface1_avc420`
  emits one `RFX_AVC420_METABLOCK` + one sub-stream (no LC word) with codec id
  0x000B. The whole ffmpeg contract — input NV12, Annex-B/NUT output, verbatim
  `encoder_args` (incl. VAAPI) — is unchanged, so hardware encode applies to 420
  too. Because the RDP client elects the codec (mstsc has no 420/444 knob and
  always offers AVC444), a `gfx.toml [avc444_ffmpeg] avc_mode = "auto"|"444"|
  "420"` selector (default auto) forces AVC420 for a fixed client, effective on
  reconnect (no restart). Negotiation prefers AVC444 and falls back to AVC420.
  Verified offscreen through the full live path (xrdp + libx264 + real xfreerdp3
  3.15): default connect negotiates AVC444 v2, `/gfx:AVC420` negotiates
  "Matched H264/AVC420 (ffmpeg) mode" and decodes correctly; a 6x zoom of
  saturated colored text shows AVC420's expected softer (half-resolution) chroma
  edges versus AVC444's crisp ones, with identical luma. Unit tests:
  `test_avc444_main_only_420`, `test_ffmpeg_encode_single`.

- **AVC444/AVC420 tail-frame withholding — root cause corrected + last-resort
  guard (A/B-verified, 2026-07-16).** Field symptom: the last typed character was
  not shown until unrelated damage (another keystroke, a tooltip, continuous
  glxgears) pushed it out. **Corrected root cause** (measured on-box, superseding
  an earlier fftools-scheduler hypothesis): the withhold is a property of the
  **encoder pipeline DEPTH**, not the pipe or the scheduler. Feeding an encoder
  frames with stdin held open and counting emitted vs. withheld pictures
  (`PR-demo/ffmpeg_pipeline_depth_probe.py`): `h264_vaapi
  -async_depth N` withholds **N−1** frames on the tested GPU, and `libx264`
  frame-threading withholds its whole thread window. End-to-end A/B through real
  xrdp→FreeRDP with a *fresh login* (not just reconnect — config binds at login;
  colour sequence ending RED, then idle): `async_depth 2` withholds (client shows
  the prior colour); `tail_flush=true` at `async_depth 2` delivers.
  **RESOLVED (2026-07-17) — true root cause: pipelined-runner content/region
  desync; fix: synchronous encode.** A live mstsc repro on the same GPU at
  `-async_depth 1` disproved the pure-depth story and the forensic chain
  (per-frame `XRDP_GFX_TRACE`: send/ack, damage-region bbox, and a
  submitted-vs-returned sequence trace with capture centre-luma) pinned the
  real mechanism: the runner *returned the oldest completed pair* while the
  caller built the AVC metablock from the *current* frame's damage rects.
  After any slow first frame (VAAPI driver warmup) or deep pipeline, it went
  **permanently one-behind** — trace showed `returned_seq = submitted_seq − 1`
  on every frame — i.e. frame N−1's pixels shipped under frame N's region.
  mstsc honours region rects strictly: it decoded and **acked** each frame
  (`decoded` counter advanced) but blitted stale full-screen content, revealing
  the newest picture only inside later small damage rects — exactly the field
  screenshot (stuck `n=2` red screen; hovering a tooltip painted only that
  rect green, from the already-decoded newer surface). FreeRDP masked the bug
  by presenting the whole decoded surface, which is why every xfreerdp A/B
  read "delivered"; the tail-flush masked it by draining the pipe and
  re-emitting full-surface. **Fix** (`xrdp_encoder_ffmpeg.c`):
  `encode_pair()`/`encode_single()` now wait synchronously (bounded by
  `pair/picture_timeout_ms`) for exactly the submitted picture and verify
  `desktop_sequence`; mismatch or timeout fails loudly and restarts the
  encoder. Verified: keystroke-driven colour test (r/g/b/w) shows the correct
  colour on every keypress, `submitted_seq == returned_seq` with `inflight=0`
  from frame 0, no timeouts (VAAPI `-async_depth 1` returns each picture in
  ~3–9 ms). Requires a zero-latency encoder pipeline (shipped defaults); a
  too-deep pipeline now errors loudly instead of silently desyncing. The
  `tail_flush` knob (default off) remains as a defensive backstop but is
  normally a no-op (nothing left in flight). Diagnostics kept, all gated by
  `XRDP_GFX_TRACE=1`: per-frame send/ack, damage bbox, seq/luma enc trace
  (`xrdp_mm.c`, `xrdp_encoder.c`); keystroke harness in
  `PR-demo/smoke_gate/` (`colorkey.sh`); the tail_flush A/B harness was removed 2026-07-28.

  **ADDENDUM (2026-07-17, same day) — second root cause: ffmpeg
  stream-analysis hold; fix: `-probesize` = one frame.** The synchronous
  encode, failing loudly as designed, exposed why the pipeline was ever
  primed behind: with the declared input rate above ~100 fps (`-framerate
  120`), ffmpeg's `avformat_find_stream_info()` distrusts the timebase and
  buffers input for rate estimation up to the default 5 MB `probesize` —
  a **resolution-dependent** number of pictures (≈1.6 at 1920×1088, ≈4.2 at
  1024×768, hundreds at small sizes) emitted only once the byte window is
  crossed. Consequences, all reproduced standalone with the exact child
  argv: at 1920×1088 one AVC444 pair (6.2 MB) crosses the window at once,
  so every 1920-class test passed; at 1024×768 (mstsc default) a pair is
  2.4 MB, the first output never comes, the synchronous encode times out
  per frame and the session is an unusable respawn loop. This startup hold
  — not encoder pipelining — is what originally primed the pipelined
  runner behind, arming the content/region desync above. **An earlier
  "wedged GPU VCN engine" diagnosis is retracted**: the probes that
  "proved" it replicated the session argv and were measuring this hold
  (misread as a hung engine); probes with a low declared fps pass on the
  same GPU. Fix (`build_argv()`): cap `-probesize` at exactly one NV12
  frame (the declared `-framerate` makes rate estimation unnecessary);
  first packet then arrives in ~90 ms (VAAPI warmup) at every size tested,
  320×240 through 2560×1440. Guards: the previously env-gated real-ffmpeg
  unit tests run on-box (`XRDP_TEST_FFMPEG_PATH`), the stale
  `encode_single` one-behind expectation now asserts the synchronous
  contract, and the deploy smoke gate runs at both 1920×1080 and 1024×768
  (the resolution-dependence is exactly what a single-size gate misses).

- **2026-07-22 — NVENC/global-header probe failure on Nvidia T4; fix:
  chain `dump_extra` into the injected bitstream filter.** First deploy on
  a foreign box (x86 + T4, Ubuntu, ffmpeg 8.0.1): every login fell back to
  RFX with `ffmpeg probe FAILED`, while the identical argv run by hand
  encoded 4/4 frames cleanly. Forensics (an argv+stderr-logging shim at
  the `gfx.toml` `path`, plus feeding the box's captured NUT bytes through
  the real demuxer and validators off-box): the NUT muxer is
  global-header, so `h264_nvenc` — which has **no** in-band repeat option
  — emitted SPS/PPS in extradata only; the probe's reset-keyframe check
  (`main_reset_ok`: SPS+PPS+IDR in-band) correctly rejected a stream real
  clients could not have decoded. libx264 only ever passed because the
  default args force `repeat-headers=1`; `-flags:v -global_header` cannot
  override a muxer that demands global headers, and a user-supplied
  `-bsf:v` is overridden by the injected one — so no config-only fix
  exists. Fix (`build_argv()`, this dev branch; ported same day by
  folding into clean-room slice 7 — never a separate fix commit — with
  the branch history rewritten: slice 7 is now `04e43ee2`, tip
  `c74a09e7`, and `git diff` old-tip→new-tip is exactly the two-file
  fix): inject `-bsf:v dump_extra,h264_mp4toannexb`, reinserting the extradata
  parameter sets ahead of every keyframe for any encoder (duplicates are
  legal/identical when the encoder already repeats). Guard: gated
  real-ffmpeg regression test modelling a global-header-only encoder
  (libx264 minus `repeat-headers=1`), red without the fix, green with it,
  on both ffmpeg 7.1 and 8.1. Follow-up in `BACKLOG.md`: the probe
  discards child stderr — log it (`log_child_line`) so the next such
  failure names itself. **Validated live same day (owner-confirmed):**
  with the fixed build the T4 login negotiates the ffmpeg path, the
  encoder child persists for the session, display is correct, and
  `nvidia-smi` lists the session's `/usr/bin/ffmpeg` as a GPU compute
  process (~200 MiB) — first confirmed NVENC hardware-encode session;
  backend swap from VAAPI was config-only as designed. Additional owner
  coverage (same day, orthogonal to this fix): small session sizes OK,
  and chroma fringe empirically absent across multiple small
  width/height sessions on region-strict rendering (the probesize-hold
  and metablock-alignment defect classes, respectively).

- **2026-07-23 — the unconditional `dump_extra` was itself a regression;
  fix: adaptive (probe pristine first, retry only on missing headers).**
  The 2026-07-22 fix chained `dump_extra` for *every* encoder. That
  DUPLICATES the parameter sets on encoders that already repeat them
  in-band (libx264 `repeat-headers=1`, h264_vaapi packed headers): the
  first keyframe packet then carries SPS=2/PPS=2. Lenient decoders
  (xfreerdp, mstsc) tolerate it; the **macOS Windows App renders a black
  screen** (near-black + top-edge noise). Found because the Mac blacked
  every H.264 config while RFX rendered, then **bisected on identical
  hardware/config/ffmpeg/client**: `ff5890aa` (last build before the
  fix) renders on the Mac, `71179f67` (the dump_extra commit) is black.
  The earlier "no-op on in-band encoders" clearance was **measured
  through the raw `-f h264` muxer, not the live NUT flow** — wrong
  instrument, false acquittal; the duplication only appears in the NUT
  path the server actually uses. Fix: the connect-time probe runs
  pristine (no `dump_extra`) first and only retries with it when the
  reset-keyframe check fails (extradata-only encoders, e.g. h264_nvenc);
  the decision rides into the session child. Exactly one SPS/PPS copy
  per keyframe in both branches. Guard: the global-header test now
  asserts the full ladder (pristine probe FAILS on the headerless
  stream, dump_extra retry passes) plus a new test asserting SPS count
  == 1 in the first packet for BOTH branches (67/67, ffmpeg 7.1 + 8.1).
  Live-validated on the dev box (VAAPI + libx264 in-band both render on
  the Mac under fresh-login bracket discipline; the T4/NVENC dump_extra
  path was already owner-verified rendering). **Process lessons
  recorded:** (1) validate encoder wire changes through the *shipped*
  muxer, never a stand-in; (2) codec A/B on a live client requires
  fresh-login brackets — a persistent Xorg session survives xrdp
  restart and a black baseline voids everything measured after it
  (a `reset_420.sh` bracketing harness, REMOVED 2026-07-28 — superseded by one fresh container per arm). **Clean-branch caveat:** the
  clean branch still carries the *blanket* dump_extra (slice 7,
  `c74a09e7`); the slice-7 fold must be re-done with the adaptive form
  before any upstream push — tracked in `BACKLOG.md`.

- **2026-07-23 — known limitation (not chased): the `dump_extra` branch
  itself mis-renders on the macOS Windows App when the source encoder is
  libx264-without-repeat-headers.** Under bracket discipline, a
  fresh-login run of that config (single SPS/PPS per keyframe, ladder
  correctly engaged) still blacked the Mac — while the same dump_extra
  branch renders from NVENC on the T4, and this exact config never
  worked on ANY prior build (old builds failed the probe → RFX). So
  this is a gap in a corner of the *new* capability, not a regression,
  and its only real-world occupant (NVENC) is validated. Deliberately
  not chased: no shipped default or runbook recipe uses a
  headerless-x264 encoder; the probe now logs a WARNING steering configs
  toward in-band-header encoders. Structural suspect for the follow-up:
  x264 zerolatency emits 2 IDR slices vs NVENC's 1 (BACKLOG).

- **2026-07-26 — the adaptive dump_extra ladder was replaced by static
  gfx.toml configuration + a verify-once probe (FR-PROBE-6), after a T4
  cold-boot heisenbug.** First connections after the T4 instance booted
  fell back to RFX: both ladder attempts burned the full 4 s probe
  deadline (GPU up 01:07, failures 01:15/01:18). Forensics: a warm probe
  at 01:39 passed in 2.2 s through the same daemon, and an offline replay
  of the captured nvenc bytes through the real NUT parser + validators
  passed every check — the failure was cold CUDA/NVENC first-init
  latency, not content. Root design flaw: the probe returned one bit, so
  the ladder treated TIMEOUT as if it were CONTENT evidence and flipped
  `use_dump_extra` on either; the latent wrong-bit hazard (cold pristine
  timeout + warm dump_extra retry ⇒ duplicated SPS/PPS ⇒ strict-decoder
  black) was the same failure class `7927efa7` had been built to prevent.
  Owner directive: header policy is per-box admin configuration (like
  `encoder_args`) — `[avc444_ffmpeg] dump_extra = true|false`; the probe
  verifies the declaration (exactly-one-SPS, bidirectional: missing AND
  duplicated headers are both CONTENT rejects with actionable messages)
  and never adapts. Observability debt paid in the same change: outcome
  classes, child stderr, child exit status, elapsed time all logged
  (2026-07-22 item "probe must log child stderr" folded in). Forensics
  during diagnosis also disproved environment suspects measured live:
  ffmpeg+nvenc passed in ~1.4 s as the xrdp user under the unit's
  `SystemCallFilter=@system-service` seccomp sandbox, ruling out
  permissions/sandbox and leaving cold-init timing as the only
  consistent cause. The probe deadline itself was NOT widened (per the
  strict-honesty rule that widening timeouts masks symptoms): a cold
  boot now degrades one connection with a self-explaining TIMEOUT log
  line and recovers on reconnect, and the policy bit cannot be
  mis-learned. Tested NVENC block recorded in `xrdp/gfx.toml` and
  `man 5 gfx.toml` (Tesla T4, driver 580.159.03, Ubuntu ffmpeg 8.0.1).

## 26. Related work and differentiation

Moved to `PR-demo/UPSTREAM_GAP_ANALYSIS.md` — a rewritten, evidence-first
survey of the upstream neutrinolabs/xrdp and xorgxrdp issues, PRs and
discussions (fetched 2026-07-22) establishing the gap this work fills,
plus anticipated review objections and the open items to raise during PR
review (real-ffmpeg CI tests, latency benchmark).
