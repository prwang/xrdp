# CLAUDE.md

Guidance for AI agents and human contributors working in this repository.
All durable rules and "memory" for this project live here, in-tree and committed
— not in opaque/external plan or memory stores.

## Project

**xrdp** — an open-source RDP (Remote Desktop Protocol) server.

- The **xrdp** front-end accepts RDP connections, renders the login screen, and
  negotiates the session with the client.
- **xrdp-sesman** authenticates the user and asks a per-session **sesexec**
  process to launch the backend X server (Xorg/xorgxrdp, Xvnc, or X11rdp).

## Repository structure

- `xrdp/`              — RDP front-end server (login window, session client side; e.g. `xrdp_mm.c`, `xrdp_login_wnd.c`)
- `libxrdp/`           — RDP protocol library used by the front-end
- `sesman/`            — session manager (xrdp-sesman)
  - `sesman/libsesman/` — config parsing (`sesman.ini`) and shared sesman code
  - `sesman/sesexec/`   — per-session executor; builds argv and starts the X server backend
  - `sesman/chansrv/`   — channel server: clipboard, drive/audio/file redirection (handles untrusted client channel data)
  - `sesman/tools/`     — sesman CLI tools (e.g. `sesrun.c`, a second caller of the SCP create-session API)
- `libipm/`            — inter-process messaging and protocols:
  SCP (xrdp ⇄ sesman), EICP (sesman ⇄ sesexec), ERCP, CCP
- `common/`            — shared utilities: `xrdp_client_info.h`, `list`, logging (`log.{c,h}`), `g_*` OS-call wrappers (`os_calls.{c,h}`)
- `libpainter/`, `librfxcodec/` — graphics/codec libraries (git **submodules** — see Build & test)
- `vnc/`, `neutrinordp/` — proxy backend modules; `xup/` — xorgxrdp transport module
- `third_party/`       — vendored code (e.g. `tomlc99` TOML parser); do not restyle/refactor
- `tests/`             — unit tests (Check framework): `tests/xrdp`, `tests/libipm`, `tests/common`, `tests/libxrdp`
- `docs/`, `instfiles/`, `genkeymap/`, `keygen/`, `fontutils/`, `tools/` — docs, install files, helpers
- Other module dirs exist (`xrdpapi/`, `xrdpvr/`, `mc/`, `waitforx/`, `xrdp_accel_assist/`, `vrplayer/`, `m4/`, `pkgconfig/`, `scripts/`).
  See README.md's "Directory Structure" for the complete, authoritative tree.
- `BACKLOG.md`         — transparent, in-tree task backlog for in-flight work

## Build & test

- Submodules: `librfxcodec` and `libpainter` are git submodules — run
  `git submodule update --init` (or clone `--recursive`) before building.
- Bootstrap & build: `./bootstrap && ./configure [opts] && make`
- Run tests: `make check` (Check framework; TAP output)
- Format: `astyle --options=astyle_config.as "*.c"` before committing (see `coding_style.md`)
- CI (`.github/workflows/build.yml`) runs build, `make check`, astyle (pinned
  3.4.14), and cppcheck on every PR — style and tests are hard gates. PRs target
  `devel`; there is no `CONTRIBUTING.md`.

## Coding rules

1. **Controlled scope.** Change only what the active `BACKLOG.md` item requires.
   No drive-by refactors, renames, or reformatting of untouched code.
2. **No functional regression.** Existing behavior must be preserved when a new
   feature/field/flag is absent, invalid, or disabled. New protocol fields and
   messages must be backward-compatible and default to current behavior.
3. **No security regression.** Treat all client-supplied data as untrusted:
   validate numeric bounds; reject zero/negative/extreme values; avoid integer
   overflow and buffer overflows when formatting; never pass client data as a
   format string. Do not modify auth/PAM/session-ownership/identity paths without
   explicit sign-off (security-critical; see `SECURITY.md`). Do not read or
   execute user-controlled shell/dotfile config to derive behavior.
4. **Follow existing patterns.** Reuse existing helpers — `LOG(LOG_LEVEL_*, ...)`
   logging (levels in `common/log.h`; `LOG_DEVEL` is compiled out of release
   builds), `g_*` OS-call wrappers in `common/os_calls.{c,h}`, `list_*`, and
   libipm serialization (`libipm_msg_out_simple_send` / `libipm_msg_in_parse`
   format strings). Match surrounding naming and idiom.
5. **Tests required.** Add/extend unit tests for new pure logic (e.g. a new
   encode/decode or geometry calculation) and for new message serialization.
   Keep tests deterministic.
6. **Style.** Follow `coding_style.md`: 4-space indent, no tabs, Allman braces,
   ≤80 cols, lowercase_with_underscores (UPPERCASE preprocessor constants),
   `/* */` comments only (never `//`), newline before the function name in
   definitions, one declaration per line. Run astyle. Aim for C/C++ compatibility.

## Cooperation rule

- Work is tracked **transparently in `BACKLOG.md`**, committed in-tree.
- Before coding, ensure the task exists in `BACKLOG.md` with clear scope and
  acceptance criteria; update its status (`TODO` / `IN PROGRESS` / `BLOCKED` /
  `DONE`) as you go.
- Commit `BACKLOG.md` / `CLAUDE.md` updates alongside the related code so the
  rationale and scope stay reviewable in git history.
- Make small, reviewable commits, each scoped to one backlog item. Do not commit
  or push unless asked; when asked, branch off `devel` (never commit directly to
  `devel`).
- Surface any scope/security/regression concern in `BACKLOG.md` rather than
  silently expanding scope.

## Strict honesty rule

A red result must stay red until the thing that failed is fixed and proven.
Concretely:

- **Never swap the component under test to turn a failing check green.**
  Substituting a different encoder/codec/config and reporting the suite as
  passing validates the substitute, not the fix under test.
- **Never mask a failure with a fallback.** Automatic degradation paths
  (fall back to another codec, retry-and-hide, widen a timeout to make the
  symptom rare) convert loud failures into silent ones and destroy the
  forensic signal needed for root cause. Fallbacks in *shipped* behavior
  require explicit owner sign-off, recorded in `BACKLOG.md`.
- **Report state changes that alter what a test means.** If the environment
  or config differs from what the owner believes is deployed, say so first,
  in plain words, before any green result is claimed.
- **Severe violation example (2026-07-17), do not repeat.** While validating
  the AVC444 synchronous-encode fix, the GPU VAAPI path started failing and
  the live rig was switched to software libx264 to obtain a passing smoke
  run, and an automatic RFX fallback was queued in the backlog — i.e. the
  failing hardware path was replaced *and* a masking mechanism was proposed
  while the actual encoder under test remained broken and unproven. The
  failure was even misdiagnosed as environment ("wedged GPU VCN engine");
  root-causing it instead of masking it found a real, deterministic code
  bug (resolution-dependent ffmpeg probesize hold — PRD §25 addendum).
  Correct handling: keep the failing config in place, report "VAAPI broken,
  cause unknown, fix not validated", capture forensics, and root-cause on
  the real path. The backlog fallback item was withdrawn (see `BACKLOG.md`).

## Demo & reproduction scaffolding

- **`PR-demo/`** holds box-specific reproduction harnesses (and their committed
  reference result images) that show a feature working end-to-end — e.g. the
  AVC444-vs-AVC420 visual A/B. Check these in **so they survive container
  restarts** and so a PR can show reproducible results, rather than leaving them
  in `/tmp`. They may hardcode this dev box (session display, `tester`,
  `127.0.0.1:3389`); document the assumptions and make the knobs env-overridable.
- Portable, dependency-light helpers (no live session/GPU needed) belong in
  `tools/`, not `PR-demo/`. The CI regression backstop is always an in-tree unit
  test under `tests/`; `PR-demo/` is a visual aid, never a substitute for it.
- **Smoke-gate every handoff.** Never hand the live box to a human tester
  without running `PR-demo/smoke_gate/smoke.sh` against the exact deployed
  binary *and* config, as the LAST step after the final install/restart. A
  test that passed before the last deployment step counts for nothing, and a
  single-configuration pass proves only that configuration: the smoke gate
  runs multiple session sizes because a real encoder bug (ffmpeg probesize
  hold) passed every 1920×1080 run while freezing every 1024×768 login.
- This is our own **dev branch**. The upstream PR against `devel` needs a
  separate clean-room pass — reviewable commit slices plus written rationale —
  and does **not** necessarily carry `PR-demo/` as-is; treat that folder as the
  reproducibility scaffold, not part of the final slicing.

## Deployment

- **Deploy from clean dev `.deb`s, never by hand-copying binaries.** Even on
  this dev box, the installed server must come from a package built from the
  committed branch (`dpkg-buildpackage`/`make deb`-style flow → `apt install`
  / `dpkg -i` the resulting `.deb`), not from copying `xrdp/.libs/xrdp` or any
  other build-tree artifact over `/usr/sbin`. Manual copies have already caused
  real incidents (a libtool wrapper shipped in place of the real ELF; a binary
  that drifted from the committed source), and they leave no record of *what*
  is deployed. A package pins the exact commit, installs every component
  consistently, and is what the owner will `apt install` to test onscreen.
- The smoke gate (above) still runs as the LAST step, against the package-
  installed binary + config — a package that was never smoke-gated post-install
  counts for nothing.

### T4 test-box deployments (owner directive, 2026-07-26)

The T4 box (EC2, Cascade Lake + Tesla T4/NVENC) is the representative
low-to-average old-CPU target: proving smooth 4K there is a headline PR
selling point, so its numbers are evidence, not just debugging.

- **After every deb build touching the encoder/conversion path**, run
  `tools/avc444_pack_bench.c` remotely on the T4 (scp the `-O2` binary; the
  bench carries verbatim copies of the shipped loops — keep them in sync)
  and RECORD the ms/frame results in `BACKLOG.md`/`PRD.md` alongside the
  deployed commit hashes. Perf history on the reference CPU is part of the
  deploy record.
- **Tear down stale Xorg sessions on the T4 BEFORE handing over for
  onscreen testing** (kill the session Xorg; verify sesman logs the clean
  session finish). A surviving session keeps the PREVIOUS xorgxrdp module
  loaded: if the xup contract version happens to match, reconnecting pairs
  silently and the owner unknowingly tests the old code — a state change
  that invalidates the test (strict-honesty rule). If the contract version
  changed, login fails with a mismatch complaint instead.
