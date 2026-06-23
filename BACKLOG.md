# BACKLOG.md

Transparent, in-tree task backlog. One feature/fix per section.
Status values: `TODO` / `IN PROGRESS` / `BLOCKED` / `DONE`.
See `CLAUDE.md` for coding, scope, security, and cooperation rules.

---

## DPI-1: Propagate RDP client DPI to Xorg/xorgxrdp sessions

- **Status:** IN PROGRESS (implementation + headless tests + **BEFORE**
  interactive baseline DONE; **AFTER** interactive verification on localhost:3389
  in progress — see dev_config.md (scenarios); headless checks + build/deploy in
  build_config.md)
- **Source:** `PRD.md`
- **Owner:** (unassigned)

### Progress log

- **Done:** end-to-end plumbing implemented exactly per the Fix scope below.
  - Pure helper `xrdp_client_info_calculate_dpi()` + range predicate
    `xrdp_client_info_dpi_valid_for_session()` (bounds 50..400) added to
    `common/xrdp_client_info.{c,h}` (new `.c`, wired into `common/Makefile.am`).
  - Login screen keeps the **raw** DPI (no font-scaling regression); bounds
    apply only on the session path.
  - DPI field appended at END of SCP `"yqqysss"`→`"yqqysssq"` and EICP
    `"iyqqysss"`→`"iyqqysssq"`; re-validated on every receive (defense in depth).
  - `LIBIPM_VERSION` 2→3; test canary `tests/libipm/test_libipm.h` updated to 3.
  - sesexec emits `-dpi <n>` as two integer-only argv tokens, only when valid
    AND no admin `-dpi` already present (exact-token compare); admin wins.
  - Unit tests added: `tests/common/test_xrdp_client_info.c` (15 cases, PRD
    §10.1) for the pure helper, plus 4 SCP/EICP create-session **semantic
    round-trip** tests in `tests/libipm/test_libipm_recv_calls.c` (send→recv
    over the loopback link, asserting the DPI value survives and that
    out-of-range/absent values are rejected to 0; non-DPI fields verified
    intact). `make check` green: libcommon 172, libipm 39, libxrdp 13,
    memtest 1, XRDP daemon 26. astyle (pinned 3.4.14) clean; no new cppcheck.
  - BEFORE baseline: stock-commit `.deb` (`0.10.80+git34a48901382e`, pre-DPI)
    was installed for the BEFORE regression run. After BEFORE was captured, the
    DPI-1 (AFTER) `.deb` was built from the feature branch HEAD and installed for
    AFTER testing.
  - **BEFORE interactive verification DONE (2026-06-23, localhost:3389, mstsc
    HiDPI):** confirmed the #3473 regression on the pre-DPI deb — the login
    screen computes 139 DPI (2160 px / 392 mm) but the Xorg session launches with
    **no `-dpi`** and `xdpyinfo` reports `96x96`. Normal / invalid-metadata paths
    start cleanly. This is the regression control for the AFTER comparison.
  - Environment prereqs surfaced during interactive bring-up (NOT DPI-related;
    see build_config.md Part X / dev_config.md §1): the Xorg wrapper must allow
    non-console users, and a **version-matched xorgxrdp** is required — distro
    0.10.2 is incompatible with the dev branch's display-socket naming
    (upstream `c4727ad8`). A matched `xorgxrdp-dev 0.10.80` was built + installed.
  - **AFTER interactive HiDPI verified (2026-06-23):** with the DPI-1 deb, the
    Xorg session launches `-dpi 139`, `xdpyinfo` reports `139x139`, sesman logs
    `Received client DPI for Xorg session: 139`. **Font scaling finding:** `-dpi`
    sets only the X *core* DPI; GTK/Qt scale from `Xft.dpi`/XSETTINGS `/Xft/DPI`,
    so fonts scale **only when the desktop is in auto-DPI mode** (XFCE
    `Xft/DPI = -1`). The XFCE default pins `/Xft/DPI = 96`, which overrides
    `-dpi` — verified by flipping it to `-1` (fonts then scaled).
  - **Scope acknowledged (per PRD Non-Goals 3 & 5):** DPI-1 propagates the core
    DPI only; setting the toolkit `Xft.dpi`/XSETTINGS is per-user, DE-specific
    config and is out of scope (would mean "fix every WM"). Added a sesman
    one-line log reminder (`sesman/sesexec/session.c`) advising auto-DPI when a
    client DPI is applied; documented in PRD §4/§11, FAQ Q14, PR.md.
- **Pending:** finish AFTER interactive matrix (HiDPI scaling DONE): 96-DPI
  client stays ~96; invalid metadata ⇒ no `-dpi`, session starts; admin `-dpi`
  in `sesman.ini [Xorg]` still wins (exactly one `-dpi`).

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
- Behavior-compatible: absent/invalid DPI ⇒ exactly current behavior (96 DPI).
  NOTE: this is *behavioral* compatibility, not wire compatibility. libipm has no
  optional-field mechanism — every field is type-tagged and parsed positionally,
  so adding the DPI field to the SCP/EICP messages is a breaking wire change.
  xrdp + sesman + sesexec + sesrun ship together and must upgrade in lockstep; a
  mixed-version peer fails the parse cleanly (fail-closed: failed session create,
  never a misparse/crash — verified `libipm/libipm_recv.c:584-590`). Append the
  new field at the END of the format strings and bump `LIBIPM_VERSION` so a
  mismatch is rejected with a clear error.
- Respect admin-configured `-dpi` in `sesman.ini [Xorg]`; never emit a duplicate
  `-dpi`. Detect via an exact-string token compare over `g_cfg->xorg_params`;
  never parse the client value into that decision.
- Xorg/xorgxrdp only (not Xvnc / X11rdp).
- Validate bounds: `50 <= dpi <= 400`; reject zero/negative/extreme. Guard the
  arithmetic against integer overflow (`unsigned int` math at
  `xrdp_login_wnd.c:739`) and against `height_pixels = bottom - top + 1`
  unsigned underflow when `bottom < top`; reject `height_mm == 0`.

### Fix scope — changed source files (end-to-end data flow)

**Front-end: compute the DPI and send it**
- `xrdp/xrdp_login_wnd.c` — extract the inline DPI calculation currently inside
  `xrdp_login_wnd_get_monitor_dpi()` (formula at `:739`) into a small, reusable,
  unit-testable helper. NOTE: the current function returns the **raw** DPI with
  NO `50..400` bound and no overflow guard (it only feeds font scaling today); the
  extracted helper must become the single source of truth for bounds + overflow
  and return an "invalid" sentinel (the existing `0` return maps cleanly to
  "absent/invalid ⇒ don't inject").
- `common/xrdp_client_info.{h,c}` *(or a small dedicated common util)* — host the
  pure `height_px,height_mm -> validated dpi` helper so it is testable
  independently of `struct xrdp_wm` (the surrounding monitor-selection logic stays
  in `xrdp_login_wnd.c`). *(decide exact location; if a new common source file is
  added, update `common/Makefile.am`.)*
- `xrdp/xrdp.h` — update declaration(s) ONLY if the existing helper signature
  changes (not needed if the pure helper lives in a common header).
- `xrdp/xrdp_mm.c` — `xrdp_mm_create_session()`: obtain the client DPI (via the
  helper using `self->wm`, which it already references) and pass it to
  `scp_send_create_session_request()`.

**SCP protocol (xrdp ⇄ sesman)**
- `libipm/scp.h` — add the DPI parameter to `scp_send_create_session_request()`
  and `scp_get_create_session_request()`.
- `libipm/scp.c` — extend the serialization format string (currently `"yqqysss"`,
  send `:428` / get `:458`) with the DPI field appended at the END; re-validate
  `50..400` on receive (sesman must not trust xrdp's value — defense in depth).
- `sesman/tools/sesrun.c` — **second caller** of `scp_send_create_session_request`
  (`:507`). MUST update the call (pass a "no DPI" sentinel). **Compile blocker if
  omitted.**
- `libipm/libipm_private.h` — bump `LIBIPM_VERSION` (`:37`) since the wire format
  changes; this is the graceful mismatch-rejection mechanism.

**sesman: forward the DPI**
- `sesman/scp_process.c` — `process_create_session_request()` (`:432`): receive
  the DPI (`:453`), log it (e.g. *"Received client DPI for Xorg session: N"*,
  fixed `%d` format), and forward it via `eicp_send_create_session_request()`
  (`:535`).

**EICP protocol (sesman ⇄ sesexec)**
- `libipm/eicp.h` — add the DPI parameter to the create-session request send/get.
- `libipm/eicp.c` — extend the serialization format string (currently
  `"iyqqysss"`, send `:288` / get `:321`) with the DPI field appended at the END;
  re-validate `50..400` on receive.

**sesexec: apply the DPI to the Xorg argv**
- `sesman/sesexec/session.h` — add a DPI field (e.g. `int dpi;`, with validity
  semantics) to `struct session_parameters`.
- `sesman/sesexec/eicp_server.c` — `handle_create_session_request()`: parse the
  DPI into `sp`.
- `sesman/sesexec/session.c` — `prepare_xorg_xserver_params()` (`:353`): append
  `-dpi <value>` only when the DPI is valid AND the configured `[Xorg]` params do
  not already contain `-dpi`; log the decision. Format with
  `g_snprintf(buf, sizeof(buf), "%d", dpi)` + `list_add_strdup` (mirror the
  existing width/height handling) — integer-only, never a combined `"-dpi N"`
  token, never via any shell. Defensively re-check `50..400` here before emitting.
  Do **not** modify `prepare_xvnc_xserver_params()`.

**No change needed (stated to preempt the question)**
- Session reuse/reconnect matching `session_list_get_bydata()`
  (`sesman/scp_process.c:474`, `sesman/session_list.c:186`) intentionally does
  NOT include DPI in its match key, and should not — on reconnect the Xorg server
  is already running with `-dpi` baked in at launch. DPI matters only at *create*
  time, so `session_list.h` / `struct session_item` need no DPI field.

### Security must-do (from security review)

- The X server is launched via `g_execvp_list()` (argv, no shell —
  `session.c:757`); keep DPI integer-only so argument injection is impossible.
- `env_set_user()` (the `setuid`/`setgid` drop) is the FIRST call in
  `start_x_server` (`session.c:681-683`), before `prepare_xorg_xserver_params`
  (`:720`). Do not reorder — DPI must be consumed post-drop, as the user.
- Plumb DPI as pure data only; do NOT gate or branch any login/authorization
  logic on it (no changes to auth/PAM/ownership/identity).
- Log DPI only as a `%d` value with a fixed format string; never as a format arg.

**Tests**
- `tests/xrdp/` (or `tests/common/`) — unit tests for the DPI calc helper.
  Cases (PRD §10.1): `2160/392 -> 139/140`, `1440/392 -> 93`, `1080/286 -> 96`,
  `2160/0 -> invalid`, `0/392 -> invalid`, `2160/-1 -> invalid`, `dpi<50` /
  `dpi>400 -> invalid`.
- `tests/libipm/` — **write** create-session send/recv tests (none exist today)
  covering the new SCP/EICP DPI field.
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
