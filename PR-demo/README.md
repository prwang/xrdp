# PR-demo — inventory of demos, onscreen probes and harnesses

Everything here is **box-specific scaffolding**: it needs a live session, a
client, or a GPU, and it is committed so it survives container restarts and
so a PR can point at reproducible results. Portable, dependency-light
helpers live in `tools/` instead, and the CI backstop is always a unit test
under `tests/` — nothing in this folder substitutes for one.

**Start here.** The table below is the index. It exists because it did not:
on 2026-08-08 the owner asked for "the app with a colour box at the left
corner and a reference palette next to it" and it took several rounds to
find `mac_bisect_matrix/chroma_probe.py`, which is an onscreen instrument
filed among two dozen offline log analysers. If you add something a human
looks at, add a row.

---

## Onscreen probes — a human watches these through a real client

Four things, and they answer four different questions. Picking the wrong
one costs a testing session.

| probe | the question it answers | what is on screen | launch |
|---|---|---|---|
| **`mac_bisect_matrix/chroma_probe.py`** | Are the AVC444 main and auxiliary streams paired correctly, and if not, by how many frames are they apart? | Fast clock (numeral 1..8 at 1 Hz beside a patch of that palette hue) and the 8-swatch reference palette at the top left; a slow clock stepping once per 8 s; named colour bars carrying 1 px red/blue stripe pairs; a never-repainted static zone bottom left; a bouncing block bottom right. Numerals are pure luma, timed patches are equiluminant so they are pure chroma — the lag is read straight off the screen. | `chroma-probe` (fullscreen, Escape quits). Needs `python3-tk`. Autostart entry: `mac_bisect_matrix/chroma-probe.desktop` |
| **`smoke_gate/colorkey_x11.c`** | Is flow control withholding, dropping or reordering frames, and is motion smooth? | Solid colour keys; a 1 px red/blue column pattern; the eight-corner colour cycle with step, cycle and frame count drawn on it; a white block sliding by PIXELS on blue. | `colorkey_x11` — keys `r g b w e c s`, `q` quits |
| **`smoke_gate/colorkey.sh`** | The same, but as the gate's scriptable payload rather than for the eye. | The same modes drawn through a terminal, so it can only address character cells — the sliding block hops ~24 px. Prefer `colorkey_x11` for judging motion by eye. | driven by `smoke_gate/keytest.sh`; standalone `colorkey.sh` |
| **`win2022_ground_truth/chroma_strip_anim.c`** | Is chroma running at 4:4:4 or has it fallen back to 4:2:0? | Four scrolling bands: naive red/blue 1 px (the control, survives 4:2:0), isoluminant red/cyan at 1 px and 2 px, isoluminant checkerboard. Any wash to flat grey is 4:2:0. | `chroma_strip_anim [seconds] [fps]` (defaults 120 30) |

`smoke_gate/codescroll10.sh` is a fixed 10 Hz code scroll — a steady,
realistic payload rather than a diagnostic pattern.

## Payloads — machine-driven load, not for the eye

* **`textflood/`** — the throughput payload. Renders an ANSI code corpus
  with cairo **in its own process** and hands X one finished image per
  frame over MIT-SHM, because profiling showed an xterm scroll spends
  44.9 % of the session Xorg's core drawing itself against 13.8 % for the
  whole xrdp capture — i.e. the old payload measured the X server, not us.
  `ring_recon.c` reconstructs what was actually shown;
  `textflood.desktop` starts it through the permitted login-time autostart
  lifecycle for an XFCE measurement session.
* **`oracle_client/`** — the **timing** client: with `FREERDP_ORACLE_DUMP=1`
  it saves encoded payloads and acknowledges *before* decoding, so the rate
  measured server-side is the server plus network with no client decode in
  it. **Renders nothing and proves no fidelity** — never smoke-gate against
  it.
* **`mac_bisect_matrix/gen_code_corpus.py`**, `code_corpus.ansi` — the
  corpus the scroll payloads draw.

## Harnesses — they drive a client and assert something

* **`smoke_gate/`** — the mandatory pre-handoff gate. `smoke.sh` runs the
  colour-key test at **two** session sizes (1920×1080 and 1024×768, both
  required: the ffmpeg probesize hold passed every 1080p run and froze
  every 1024×768 login) and asserts zero encoder restarts. `keytest.sh` is
  the workhorse — it arms the versioned XDG-autostart payload before a fresh
  `xfreerdp3` login on its own Xvfb, presses keys through RDP, screenshots the
  client framebuffer and asserts the colour. It logs off only whole sessions.
* **`mac_bisect_matrix/`** — the containerised per-arm fleet. Every variant
  is its own k3s pod with its own pinned deb and `gfx.toml`, all up at
  once on `127.0.0.1:400xx`, so the host install is never mutated for a
  bisect. Also holds the capture archive (`captures/`), the arm
  certificates, `e_gate_run.sh`, `build_and_deploy.sh`, and the per-item
  analysers (`i61e_*`, `i79_*`, `i80_*`, `i91_encode_overlap.py`, …).
  `Containerfile.ubuntu2404`, `k8s/x039.yaml` and
  `i125c_ubuntu2404_deploy.sh` define the one Ubuntu 24.04 / FFmpeg 6 CPU
  compatibility arm on `127.0.0.1:40055`; its deliberately incompatible
  CAVLC configuration verifies rejection before AVC selection.
  `deploy_x040_cleanroom.sh` and `k8s/x040.yaml` define the paired clean-room
  interactive candidate on `127.0.0.1:40056`, using a regular XFCE desktop
  and the indexed visual payloads.
* **`multimon_offline/`** — drives a real two-monitor `xfreerdp /multimon`
  login and asserts the server took the multi-monitor AVC444 path.
  `setup_monitors.sh` verifies each output's **active pixel geometry**, not
  just its mode name.
* **`multimon_burr/`** — self-driven reproduction of the dual-monitor
  drag-ghost defect (thin stale 1–3 px lines when dragging across screens).
* **`t4_profile/`** — everything for the T4 reference box: install, measure,
  the uprobe scripts for the X-server side, and its `gfx.toml` variants.
  The profile switcher is indexed under `lib/t4/` and installed on that box as
  `~/xrdp-profile`.
* **`bench/`** — encoder and conversion throughput measurements backing
  `RESULTS.md`; no live session needed.
* **`mac_bisect_matrix/i92_sparse_aux_ab.sh`** + `i92_sparse_aux_analyze.py`
  — the BACKLOG #92 A/B: does dropping the AVC444 chroma view in motion
  buy anything? ONE arm, two configurations of its gfx.toml
  (`chroma_refresh_ms` 0 against 1000), interleaved off/on/off/on so
  host drift is bracketed rather than folded in. The analyser checks
  that the feature APPLIED before it will print a rate, and reports the
  chroma guarantee's worst gap as a red result in its own right.
* **`mac_bisect_matrix/i125b_t4_matrix.sh`** + `i125b_analyze.py` — the
  repeated T4 numerical matrix for dense versus sparse chroma at one- and
  two-frame wire windows. It runs the eight named 20-second legs through the
  oracle client, verifies the treatment from the perf trace before comparing
  rates, closes command/byte and latency accounting, and leaves the T4 on the
  sparse two-frame interactive profile.
* **`mac_bisect_matrix/x264_keyint_probe.c`** — asks libx264 what
  keyframe interval xrdp's linked-library H.264 path actually gets,
  under the presets `gfx.toml` ships. It never sets `i_keyint_max`, so
  the answer is the library default; measured 250. Exists because the
  ffmpeg path's scheduled refresh interval should be compared against
  what xrdp has always done, not against a number from memory.
* **`lib/`** — shared and target-specific helpers, indexed by `lib/README.md`.
  The shared codec/display helpers live at its root; Windows-client and legacy
  Incus-container helpers live in named target subdirectories.

## Evidence and prose

* **`RESULTS.md`** — paper-style benchmarks with measured numbers.
* **`BREAKING_CHANGE_credit_frontier.md`** — what changes for an operator
  or upstream reviewer when `[avc444_ffmpeg] eager_slot_ack` defaults on,
  the measurements behind it, its cost of one more frame in flight, and how
  to get the previous behaviour back.
* **`INTERACTIVE_ARM.md`** — how to reach the interactive XFCE arm from a
  UWP or macOS client and what to check.
* **`UPSTREAM_GAP_ANALYSIS.md`** — what still separates this branch from an
  upstream-ready PR.
* **`win2022_ground_truth/`** — captures and C# probes from a stock Windows
  Server 2022 RDP host, used as ground truth for what a conforming AVC444
  stream looks like.
* **`mac_windows_app/`** — screenshots from the macOS Windows App
  (VideoToolbox decoder), which exposed decoder strictness the Linux and
  Windows clients tolerated — the AVC444v2 LC framing blackout.
* **`media_evidences/`** — the curated visual history of four client bugs,
  grouped by bug rather than capture run. `media_evidences/README.md` records
  each bug → failed direction → fix sequence and why each of the ten retained
  artifacts adds information; `SHA256SUMS` pins the bytes.
* **`public_pr/`** — BACKLOG #300's reviewer-facing documentation workspace
  and indexed historical research inputs. It remains a draft until the final
  clean-room pair exists.
* **`results/`** — the committed reference outputs of the demo below.

---

# The demo itself — AVC444 vs AVC420 visual evidence

Renders an **iso-luminant** test pattern (coloured detail that lives only
in chroma, at matched BT.709 luma), displays it full-screen on a live xrdp
session, and captures it decoded through `xfreerdp3` in each GFX mode.

Why iso-luminant: AVC420 keeps full-resolution **luma** and half-resolution
**chroma**. Bright-on-black text is a luma edge, so it survives 420 and hides
the difference. Matching luma forces the detail into chroma alone, where 4:2:0
must lose it and 4:4:4 keeps it.

Pieces: `../tools/gen_isoluma.py` (the portable pattern generator, pure
PIL/numpy), `run_demo.sh` (generate → display → capture → compare),
`lib/show_img.sh`, `lib/capture_codec.sh`, and `results/`.

## Results (reference outputs in `results/`)
- `cmp_bars1px.png` — **the decisive frame.** 1px magenta|green stripes at equal
  luma: AVC444 keeps every stripe; AVC420 collapses to flat gray.
- `cmp_isotext.png` — equal-luma pink-on-teal text: AVC444 crisp, AVC420 blurs
  the fine strokes into the background.
- `iso444.png` / `iso420.png` — full decoded frames. The "white on black"
  control band stays crisp on both, proving the difference is chroma-specific.

The offline CI regression that locks down the same mechanism (420 main chroma
flattens; 444 aux retains it) with no display or client is
`tests/xrdp/test_avc444_convert.c::test_avc420_isoluminant_chroma_loss`.

## Assumptions (this dev box)
This harness is **environment-specific by design** and is committed so it
survives container restarts. It expects:
- xrdp running and serving the external-ffmpeg backend on `127.0.0.1:3389`;
- a passwordless RDP user (`tester`) whose session Xorg is `:10` with
  `XAUTHORITY=/var/run/xrdp/1000/Xauthority`;
- tools on PATH: `xfreerdp3`, `Xvfb`, `xdotool`, `ffmpeg`, `ffplay`, `python3`
  with `PIL`+`numpy`, DejaVu fonts.

Override the defaults via environment variables (`SESSION_DISPLAY`,
`SESSION_XAUTH`, `SESSION_USER`, `CLIENT_DISPLAY`, `RDP_HOST`, `RDP_USER`,
`GEOM`). Use a 32-multiple `GEOM` width to avoid the AVC444 resize comb.

To force AVC420 for a client that has no 420/444 knob (e.g. mstsc), set
`avc_mode = "420"` under `[avc444_ffmpeg]` in `gfx.toml` and reconnect;
`xfreerdp3` can instead select it directly with `/gfx:AVC420`.

## Run
```
bash PR-demo/run_demo.sh
```
Leaves xrdp running; tears down the offscreen Xvfb/ffplay/xfreerdp it started.

> Note: this lives on our dev branch. The upstream PR needs a separate
> clean-room pass (reviewable commit slices + prose); this folder is the
> reproducibility scaffold, not part of that slicing.
