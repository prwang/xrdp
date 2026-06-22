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
- `libipm/`            — inter-process messaging and protocols:
  SCP (xrdp ⇄ sesman), EICP (sesman ⇄ sesexec), ERCP, CCP
- `common/`            — shared utilities (`xrdp_client_info.h`, `list`, logging, `g_*` OS-call wrappers)
- `libpainter/`, `librfxcodec/` — graphics/codec libraries
- `vnc/`, `xup/`, `neutrinordp/` — backend channel modules
- `tests/`             — unit tests (Check framework): `tests/xrdp`, `tests/libipm`, `tests/common`, `tests/libxrdp`
- `docs/`, `instfiles/`, `genkeymap/`, `keygen/`, `fontutils/`, `tools/` — docs, install files, helpers
- `BACKLOG.md`         — transparent, in-tree task backlog for in-flight work

## Build & test

- Bootstrap & build: `./bootstrap && ./configure [opts] && make`
- Run tests: `make check` (Check framework)
- Format: `astyle --options=astyle_config.as "*.c"` before committing (see `coding_style.md`)

## Coding rules

1. **Controlled scope.** Change only what the active `BACKLOG.md` item requires.
   No drive-by refactors, renames, or reformatting of untouched code.
2. **No functional regression.** Existing behavior must be preserved when a new
   feature/field/flag is absent, invalid, or disabled. New protocol fields and
   messages must be backward-compatible and default to current behavior.
3. **No security regression.** Treat all client-supplied data as untrusted:
   validate numeric bounds; reject zero/negative/extreme values; avoid buffer
   overflows when formatting. Never modify auth/PAM/session-ownership/identity
   paths. Never read user-controlled shell config.
4. **Follow existing patterns.** Reuse existing helpers — `LOG()` logging macros,
   `g_*` OS-call wrappers, `list_*`, libipm serialization (`libipm_msg_out_simple_send`
   / `libipm_msg_in_parse` format strings). Match surrounding naming and idiom.
5. **Tests required.** Add/extend unit tests for new pure logic (e.g. DPI calc)
   and for new message serialization. Keep tests deterministic.
6. **Style.** Follow `coding_style.md` (4-space indent, no tabs, Allman braces,
   ≤80 cols, lowercase_with_underscores); run astyle. Aim for C/C++ compatibility.

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
