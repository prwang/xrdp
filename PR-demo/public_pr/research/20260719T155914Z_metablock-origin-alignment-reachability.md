# RFX_AVC420 metablock even-alignment — reachability & justification

**Question this answers (for the PR):** is the region-rect origin
even-alignment a client-compatibility/correctness fix that belongs in the
shared metablock emitter — reachable on **both** the linked x264/OpenH264 GFX
path and the external stock-ffmpeg path — rather than something specific to the
ffmpeg backend?

**Answer: yes, proven at three levels (source, symbol, machine code).** The fix
must not be coupled to the ffmpeg path; it corrects the RFX_AVC420_METABLOCK
region-rect origins that every H.264 GFX caller emits.

Relevant commits on `avc444-ffmpeg-upstream`:
- `avc444: expose the RFX_AVC420 metablock emitter outside the codec guard`
  (moves `out_RFX_AVC420_METABLOCK` out of `#if defined(XRDP_X264)||
  defined(XRDP_OPENH264)`, external linkage; **no behaviour change**).
- `avc444: even-align RFX_AVC420 metablock region-rect origins` (adds
  `rect.left/top &= ~1`; applies to **every** caller).

---

## 1. Source-level reachability

A single emitter, `out_RFX_AVC420_METABLOCK()` (`xrdp/xrdp_encoder.c`), is
called by three serializers, in two independently-compiled families:

| Caller | Compiled when | Path |
|---|---|---|
| `gfx_wiretosurface1_avc420()` | always | external ffmpeg, plain AVC420 (0x000B) |
| `gfx_wiretosurface1_avc444()` | always | external ffmpeg, AVC444 v1/v2 (main **and** aux view) |
| `gfx_wiretosurface1()` | `#if defined(XRDP_X264) \|\| defined(XRDP_OPENH264)` | **pre-existing** linked-library H.264 GFX |

The emitter itself is deliberately **outside** the `XRDP_X264/OPENH264` guard so
the guarded linked-library caller and the always-compiled ffmpeg callers resolve
the same function. The `rect.left &= ~1; rect.top &= ~1;` origin alignment lives
inside that one function, so it is applied identically no matter which serializer
produced the region rects.

Odd origins are not exotic: the emitter expands each damage rect by 1px
(`rects[i].x1 - dst->x1 - 1`), so any damage region starting on an even source
column yields an **odd** metablock origin — the common case on interactive
updates. Region-strict decoders (mstsc / mstscax / RD Client) index odd chroma
columns/rows relative to that origin, so an odd origin flips chroma parity and
fringes the region's top/left edge.

> Note: this is the **region-rect origin** parity fix. It is independent of the
> separate `chroma_align` (coded-**width** 16/32) knob, which governs where the
> ChromaV2 U|V split falls. The A/B below targets the origin-parity fix.

## 2. Symbol-level proof (nm)

Built `--enable-x264` so the guarded caller is actually compiled:

```
$ nm xrdp/xrdp_encoder.o | grep out_RFX_AVC420_METABLOCK
0000000000000fc0 T out_RFX_AVC420_METABLOCK      <- global (T), not static
$ nm xrdp/xrdp_encoder.o | grep gfx_wiretosurface1
... t gfx_wiretosurface1_avc420    (ffmpeg AVC420)
... t gfx_wiretosurface1_avc444    (ffmpeg AVC444)
... t gfx_wiretosurface1           (linked-library H.264, under XRDP_X264 guard)
```

The `--enable-x264` link **succeeds with no undefined reference** — that link is
the proof that the guarded linked-library caller resolves the relocated,
now-global emitter.

## 3. Machine-code proof (objdump)

The alignment is compiled into the one shared emitter, so it executes for every
caller:

```
$ objdump -d xrdp/xrdp_encoder.o   # within <out_RFX_AVC420_METABLOCK>
10d1:  83 e1 fe   and $0xfffffffe,%ecx   <- rect.left &= ~1
10e2:  83 e2 fe   and $0xfffffffe,%edx   <- rect.top  &= ~1
```

`make check` = 63/63 with x264 linked **and** with the default (ffmpeg-only,
no linked codec) build.

---

## 4. How to build each path (this box)

Both configurations build and test clean.

```
# external-ffmpeg path (default / shipped config — links no H.264 library)
./configure --prefix=/usr --sysconfdir=/etc --localstatedir=/var \
    --libdir=/usr/lib/x86_64-linux-gnu \
    --with-systemdsystemunitdir=/lib/systemd/system
make

# linked-library H.264 path (adds the guarded gfx_wiretosurface1 caller)
sudo apt-get install -y libx264-dev
./configure ... --enable-x264       # or --enable-openh264
make
```

## 5. How to reach each path at runtime (gfx.toml)

- **External-ffmpeg path:** set `[avc444_ffmpeg]` (path + avc_mode + encoder_args)
  in `gfx.toml`; the client negotiates AVC444/AVC420 and the ffmpeg serializers
  emit the metablock.
- **Linked-library path:** build `--enable-x264`, leave `[avc444_ffmpeg]`
  disabled, and let `gfx.toml` select the in-tree H.264 GFX codec; the guarded
  `gfx_wiretosurface1` emits the metablock.

Config binds at **fresh login** (logoff→login), not TCP reconnect.

## 6. Runtime A/B (mstsc / rdcman.exe screenshot proof)

Goal: show a region-strict client wedges/fringes with odd metablock origins
(pre-fix) and renders correctly with even origins (post-fix), on **both** paths.
Use a session/window that produces odd-origin damage (a high-contrast vertical
or horizontal edge redrawn at an odd column/row; the odd-width login-screen
regions are a reliable trigger).

**Pre-fix ("before") arm — two equivalent ways to get odd origins:**
- Linked path: build **`origin/devel`** (8812646d) `--enable-x264`. Its emitter
  is the original static, non-even-aligned version → odd origins as shipped.
- Either path: on this branch, revert the two alignment lines for the "before"
  capture:
  ```
  # in out_RFX_AVC420_METABLOCK(), delete:
  rect.left &= ~1;
  rect.top  &= ~1;
  ```

**Post-fix ("after") arm:** this branch as-is.

**Procedure (per path, per arm):**
1. Build + install the arm's binary; restart xrdp; **fresh login**.
2. Connect with `mstsc.exe` (or `rdcman.exe`) — a region-strict decoder.
3. Drive an update whose region origin is odd (high-contrast edge on an
   odd-width region).
4. Capture: **before** = magenta/teal fringe on the region's top/left edge (and,
   under the historical pipelined path, a stuck region); **after** = clean edge.
5. Cross-check with FreeRDP/`xfreerdp` — it presents the whole decoded surface
   and so does **not** show the fringe; that lenient behaviour is why this bug
   needs a region-strict client to see, and is worth stating in the PR.

**Expected result table for the PR:**

| Path | Arm | mstsc/rdcman | xfreerdp |
|---|---|---|---|
| linked x264 | before (odd origin) | fringe on top/left edge | clean (lenient) |
| linked x264 | after (even origin) | clean | clean |
| ffmpeg | before (odd origin) | fringe on top/left edge | clean (lenient) |
| ffmpeg | after (even origin) | clean | clean |

Identical before/after behaviour on both paths is the justification that the fix
belongs in the shared emitter and is a client-compatibility correctness fix, not
an ffmpeg-specific workaround.
