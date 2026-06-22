# Start Xorg sessions at the client monitor's DPI

Fixes #3473

## Problem

xrdp already computes the client monitor's physical DPI and uses it to scale the
**login screen** (you can see it in the log: *"Login screen monitor height is N
pixels over M mm (D DPI)"*). But that value is never passed to xrdp-sesman, so
the **Xorg/xorgxrdp desktop** that follows always starts at the default 96 DPI.

The result is the inconsistency reported in #3473: on a HiDPI client the login
screen is scaled correctly, then the desktop appears with everything too small.
The DPI the client supplies is available at connect time but is dropped on the
floor before the X server is launched.

## What this changes

Propagate the already-computed client DPI from the xrdp front-end through
sesman and sesexec, and launch Xorg with `-dpi <client_dpi>` so the desktop
matches the login screen.

Data flow (all new code is pure data plumbing — no auth/PAM/identity changes):

```
xrdp_mm (compute+validate) → SCP → sesman → EICP → sesexec → Xorg argv "-dpi N"
```

- **`common/xrdp_client_info.{c,h}`** — new pure helper
  `xrdp_client_info_calculate_dpi(height_px, height_mm)` (the existing
  login-screen formula, extracted and made unit-testable, with overflow/
  underflow guards) plus `xrdp_client_info_dpi_valid_for_session()` enforcing a
  50–400 range. The login screen keeps using the **raw** value exactly as
  before — bounds are applied only on the new session path, so font scaling is
  unchanged.
- **`xrdp/xrdp_login_wnd.c`** — uses the helper; also guards an inverted-rectangle
  unsigned underflow in the height calculation.
- **`xrdp/xrdp_mm.c`** — for Xorg sessions, computes and range-checks the DPI and
  sends it in the SCP create-session request. Out-of-range/absent → sends 0.
- **SCP / EICP** (`libipm/scp.*`, `libipm/eicp.*`) — the create-session request
  gains one trailing DPI field; it is **re-validated on every receive** (sesman
  must not trust the value from xrdp; defense in depth). `0` means "no DPI".
- **`sesman/sesexec/session.c`** — appends `-dpi` `<n>` as two integer-only argv
  tokens (never a shell string) when the DPI is valid **and** the admin has not
  already set `-dpi` in `sesman.ini [Xorg]` (exact-token check; the admin value
  always wins, never a duplicate `-dpi`).
- Xvnc and other non-Xorg backends are untouched.

## Behavior / compatibility

- **No regression when DPI is absent/invalid/out-of-range:** no `-dpi` is
  appended and the session starts exactly as today (default 96 DPI). No
  `-dpi 0`, `-dpi 1`, or `-dpi 10000` can ever be emitted.
- **Admin override wins:** an existing `-dpi` in `[Xorg]` params is detected and
  the client value is skipped, so there is never a duplicate `-dpi`.
- **Wire-format change (please note):** the SCP and EICP create-session messages
  gain a trailing field, so `LIBIPM_VERSION` is bumped **2 → 3**. libipm has no
  optional-field mechanism, so this is a breaking change by construction. xrdp,
  xrdp-sesman, xrdp-sesexec and sesrun ship and must be upgraded together; a
  mixed-version peer is rejected cleanly by the existing version check
  (fail-closed: failed session create, never a misparse or crash). The new field
  is appended at the end of the format strings to keep the change minimal.

## Design note: physical DPI

The DPI is derived from the client's **physical monitor size**
(TS_MONITOR_ATTRIBUTES, in mm) — the *same* computation xrdp already trusts for
the login screen. This deliberately makes the desktop consistent with the login
screen, which is the concrete defect in #3473. Using the RDP desktop *scale
factor* instead (e.g. 96×1.5) is a reasonable alternative policy; it's
intentionally out of scope here and could be added later behind a config knob
without changing this plumbing. See FAQ for the trade-off.

## Testing

- **Unit tests, pure helper** (`tests/common/test_xrdp_client_info.c`, 15 cases):
  the DPI formula incl. the truncation cases, zero/overflow/underflow → invalid,
  and the 50–400 range boundaries (PRD-style cases, e.g. 2160px/392mm → 139).
- **Semantic round-trip tests** (`tests/libipm/test_libipm_recv_calls.c`, 4
  cases): the SCP and EICP create-session messages are sent and received over
  the loopback link; a valid DPI survives intact (with the other fields
  unaffected by the appended field), and out-of-range/absent values are rejected
  to 0 on receive. These are value-based, not raw-byte assertions, so they don't
  become tripwires for future field additions.
- `make check` green across all suites (libcommon, libipm, libxrdp, daemon).
- astyle (pinned 3.4.14) and cppcheck clean.
- Built and smoke-tested against an Xorg/xorgxrdp session.

## Notes for reviewers

- Integer-only end to end: the value is a bounded `unsigned short`/`q` field all
  the way to the argv token, exec'd via `g_execvp_list` (no shell) — argument
  injection is not possible.
- The DPI is consumed **after** the setuid/setgid privilege drop in
  `start_x_server`; ordering is unchanged.
- Happy to split the `LIBIPM_VERSION` bump discussion out if you'd prefer a
  different compatibility strategy.
