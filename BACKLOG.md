# BACKLOG

Transparent, in-tree task backlog. One item per reviewable unit of work.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.

Only **upcoming** work lives here. Completed work is recorded in `PRD.md` §25
("Delivered"), with detailed root-cause writeups under `tests/xrdp/avc444/`.

See `CLAUDE.md` for the rules; `build_config.md` / `dev_config.md` /
`normal_config.md` for the build, package and test-env procedures.

---

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

## Graceful degradation on persistent encoder failure — TODO (2026-07-17)

**Problem.** When the external ffmpeg encoder persistently fails mid-session
(e.g. a wedged GPU VAAPI/VCN engine: encode returns nothing until EOF, vainfo
hangs), the synchronous AVC444 path times out per frame, tears down and
respawns the encoder in a loop, and the user sees a black/frozen desktop. The
session-setup probe only guards *connect time*; there is no *mid-session*
fallback. Observed live: GPU wedged between two test runs, every generation
timed out ("submitted pair not returned within 2000 ms"), qterminal never
painted.

**Acceptance criteria.**
- After N consecutive encode timeouts/errors (small, e.g. 3), stop retrying
  H.264, log one clear line, and degrade the session to a working codec (RFX
  path) rather than looping black.
- Degradation must not corrupt the GFX pipeline (client re-negotiation or
  surface reset as required), and must be covered by a unit test for the
  failure-counting logic.
- No behavior change when the encoder is healthy.
