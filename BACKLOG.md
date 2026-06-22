# BACKLOG.md

Transparent, in-tree task backlog. One feature/fix per section.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.
See `CLAUDE.md` for coding, scope, security, and cooperation rules.

---

## DPI-1: Propagate RDP client DPI to Xorg/xorgxrdp sessions

- **Status:** TODO
- **Source:** `PRD.md`
- **Owner:** (unassigned)

### Context

xrdp already computes the client monitor's physical DPI for the login screen
(logged as *"Login screen monitor height is N pixels over M mm (D DPI)"*), but
this value is never passed to xrdp-sesman. As a result the Xorg/xorgxrdp desktop
session always starts at the default 96 DPI, so HiDPI clients get a correctly
scaled login screen but a too-small desktop. Goal: when the client reports valid
physical monitor dimensions, start Xorg with `-dpi <client_dpi>` so the desktop
matches.

### Constraints (also see CLAUDE.md)

- No hard-coded DPI — compute from client monitor metadata.
- No changes to `startwm.sh`, `~/.xsession`, `~/.xinitrc`, or DE-specific config.
- Backward compatible: absent/invalid DPI ⇒ exactly current behavior (96 DPI).
- Respect admin-configured `-dpi` in `sesman.ini [Xorg]`; never emit a duplicate `-dpi`.
- Xorg/xorgxrdp only (not Xvnc / X11rdp).
- Validate bounds: `50 <= dpi <= 400`; reject zero/negative/extreme; no overflow.

### Fix scope — changed source files (end-to-end data flow)

**Front-end: compute the DPI and send it**
- `xrdp/xrdp_login_wnd.c` — extract the inline DPI calculation currently inside
  `xrdp_login_wnd_get_monitor_dpi()` into a small, reusable, unit-testable helper
  (input: monitor pixel height + physical height in mm; output: validated DPI).
- `common/xrdp_client_info.{h,c}` *(or a small dedicated common util)* — host the
  pure `height_px,height_mm -> dpi` helper with bounds validation so it is
  testable independently of `struct xrdp_wm`. *(decide exact location; if a new
  common source file is added, update `common/Makefile.am`.)*
- `xrdp/xrdp.h` — update declaration(s) if the helper signature changes.
- `xrdp/xrdp_mm.c` — `xrdp_mm_create_session()`: obtain the client DPI (via the
  helper using `self->wm`) and pass it to `scp_send_create_session_request()`.

**SCP protocol (xrdp ⇄ sesman)**
- `libipm/scp.h` — add the DPI parameter to `scp_send_create_session_request()`
  and `scp_get_create_session_request()`.
- `libipm/scp.c` — extend the serialization format string (currently `"yqqysss"`)
  to carry the DPI field; re-validate bounds on receive.

**sesman: forward the DPI**
- `sesman/scp_process.c` — `process_create_session_request()`: receive the DPI,
  log it (e.g. *"Received client DPI for Xorg session: N"*), and forward it via
  `eicp_send_create_session_request()`.

**EICP protocol (sesman ⇄ sesexec)**
- `libipm/eicp.h` — add the DPI parameter to the create-session request send/get.
- `libipm/eicp.c` — extend the serialization format string (currently `"iyqqysss"`).

**sesexec: apply the DPI to the Xorg argv**
- `sesman/sesexec/session.h` — add a DPI field (e.g. `int dpi;`, with validity
  semantics) to `struct session_parameters`.
- `sesman/sesexec/eicp_server.c` — `handle_create_session_request()`: parse the
  DPI into `sp`.
- `sesman/sesexec/session.c` — `prepare_xorg_xserver_params()`: append `-dpi
  <value>` only when the DPI is valid AND the configured `[Xorg]` params do not
  already contain `-dpi`; log the decision. Do **not** modify
  `prepare_xvnc_xserver_params()`.

**Tests**
- `tests/xrdp/` (or `tests/common/`) — unit tests for the DPI calc helper.
  Cases (PRD §10.1): `2160/392 -> 139/140`, `1440/392 -> 93`, `1080/286 -> 96`,
  `2160/0 -> invalid`, `0/392 -> invalid`, `2160/-1 -> invalid`, `dpi<50` /
  `dpi>400 -> invalid`.
- `tests/libipm/` — extend send/recv tests for the new SCP/EICP DPI field.
- If feasible, an argv-construction test asserting `-dpi` injection and the
  admin-override skip.

### Acceptance criteria (PRD §9)

- HiDPI client ⇒ Xorg launched with `-dpi <client_dpi>`; `xdpyinfo` reports it.
- 96-DPI client ⇒ stays approximately 96 DPI.
- Invalid/missing metadata ⇒ no `-dpi` appended; session still starts (no
  `-dpi 0` / `-dpi 1` / `-dpi 10000`).
- Admin `-dpi` already in `sesman.ini [Xorg]` ⇒ exactly one `-dpi`, admin value wins.
- Xvnc and other non-Xorg backends unchanged.

---

## DPI-2 (optional): `UseClientDPI` config knob

- **Status:** TODO — do **not** block DPI-1 on this.
- **Source:** `PRD.md` §7.6

Add an optional `[Sessions] UseClientDPI` (default `true`) to gate client-DPI
injection for conservative deployments.

- Files: `sesman/libsesman/sesman_config.{c,h}`, the shipped `sesman.ini`, and
  the `sesman.ini` manpage under `docs/`.
- Behavior: `true` ⇒ inject DPI when valid and no admin `-dpi`; `false` ⇒
  preserve current behavior.
