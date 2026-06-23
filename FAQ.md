# FAQ.md — anticipated maintainer questions for the DPI PR

Anticipated maintainer questions for the #3473 DPI fix, with detailed answers.
Each entry is a likely objection and a straight answer.

---

### Q1. Why physical DPI from the monitor size, not the RDP desktop *scale factor*?

This is the most likely pushback (the #3473 reporter expected `96 × 1.5 = 144`
for 150% scaling, but the physical-size calc gives a higher value on a denser
panel, because it reflects true pixel density rather than the scale preference).

**Answer.** Two different things:
- **Physical DPI** = the panel's true pixel density (from TS_MONITOR_ATTRIBUTES
  physical size in mm). Makes one on-screen inch equal one real inch.
- **Scale factor** = the user's Windows magnification preference.

We chose physical DPI deliberately because:
1. It is the **exact value xrdp already computes and trusts for the login
   screen**. The concrete bug in #3473 is that the login screen is scaled right
   and the desktop is not — so matching the desktop to the login screen's own
   DPI is the narrowest, most consistent fix. We reuse the existing formula
   rather than introduce a second, divergent notion of "DPI".
2. It is objective and well-defined; scale factor is a client-side policy that
   maps onto X DPI only by convention.

Scale-factor-driven DPI is a legitimate *alternative*, but it's a policy change
with its own surprises (it ignores actual panel density; two clients at the same
scale on different panels get the same DPI). The plumbing here is agnostic — the
*source* value is chosen in one place (`xrdp_mm.c` via the helper input), so a
scale-factor mode or an admin toggle can be added later without touching the
protocol or sesexec. Happy to add that as a follow-up if you prefer it.

### Q2. You bumped `LIBIPM_VERSION` and changed the SCP/EICP wire format — isn't that a breaking change?

Yes, and it's unavoidable: libipm fields are positional and type-tagged with no
optional-field mechanism, so adding any field is a wire break. We handled it the
safe way:
- New field **appended at the end** of the format strings (minimal change).
- `LIBIPM_VERSION` bumped 2 → 3 so a version mismatch is rejected by the existing
  header check **before** the body is parsed — fail-closed, no misparse, no
  crash. A mixed-version peer just gets a failed session-create with a clear log.
- xrdp / sesman / sesexec / sesrun are built and shipped together, so in any
  real install they're in lockstep.

If you'd rather sequence the version bump differently (or batch it with other
protocol work), say so — it's isolated to `libipm_private.h` + the test canary.

### Q3. Why validate the DPI at *every* layer? Looks redundant.

Defense in depth, and each layer has a different trust boundary:
- `xrdp_mm.c` validates before sending (don't send garbage).
- `scp.c` / `eicp.c` re-validate on receive — **sesman/sesexec must not trust a
  value from xrdp** (or anything that speaks SCP). Out-of-range → 0.
- `session.c` re-checks immediately before building the argv (the value becomes
  a process argument; treat it as untrusted at the point of use).

All client-derived data is untrusted per the project's security rules; the cost
is three integer comparisons. The value reaching Xorg is always in 50–400.

### Q4. Why the 50–400 bounds? Aren't they arbitrary?

They bracket every realistic display (≈50 DPI for huge low-density panels up to
≈400 for extreme HiDPI) while rejecting nonsense (0, 1, 10000) that would make
Xorg misbehave or fail to start. They're named constants
(`XRDP_SESSION_DPI_MIN/MAX`) so they're easy to adjust. Note the bounds apply
**only** to the value injected into the session — the login screen still uses the
raw computed value, so this introduces no behavior change there.

### Q5. Did you change the login-screen DPI / font scaling?

No. `xrdp_login_wnd_get_monitor_dpi()` still returns the **raw, unbounded** DPI
exactly as before; the extracted helper produces a byte-identical result for the
inputs the login path uses. The only login-path change is a guard against an
inverted-rectangle unsigned underflow (`bottom < top`), which previously could
produce a garbage value. Bounds live solely on the new session path.

### Q6. Is this safe — a client value ending up as an X server argument?

- The value is a bounded integer the whole way (`unsigned short` / libipm `q`),
  never a string, so it can't carry metacharacters or extra tokens.
- It's emitted as **two separate argv tokens** (`"-dpi"`, `"139"`) via
  `list_add_strdup`, then exec'd with `g_execvp_list` — **no shell**. There is no
  `"-dpi N"` concatenation and no interpolation. Argument injection is not
  possible.
- It is consumed **after** the setuid/setgid drop (`env_set_user` is still first
  in `start_x_server`); ordering is untouched.
- Logged only as `%d` with a fixed format string (no format-string risk).
- No auth/PAM/session-ownership/identity code is touched.

### Q7. Why Xorg only? What about Xvnc / X11rdp?

`-dpi` is consumed by `prepare_xorg_xserver_params()`; Xvnc takes DPI
differently and the issue is specifically about Xorg/xorgxrdp. `Xvnc`'s param
builder is untouched, so Xvnc behavior is unchanged. A Xvnc equivalent could be
a separate, smaller change later.

### Q8. Why a new `common/xrdp_client_info.c` instead of putting the helper in the front-end?

The helper is a pure function with two consumers: the front-end (compute) and
libipm scp/eicp (re-validate). Placing it in `common/` keeps it (a) testable in
`tests/common` independent of `struct xrdp_wm`, and (b) linkable by every process
that needs the range check. `xrdp_client_info.h` was already the public header
for client-display data, so the declarations sit naturally there.

### Q9. The test says `1080px / 286mm → 95`, but that's "96 DPI" hardware. Bug?

No — integer truncation. `1080×127 / (286×5) = 95.9 → 95`. This matches the
existing login-screen calculation (same formula), so it's consistent, and 95 is
within the "≈96" acceptance. We did **not** add rounding because that would also
shift the login-screen value (e.g. 139→140) and is out of scope for this fix. If
you want banker's/round-half-up, it's a one-line change we can make with sign-off
since it affects the login screen too.

### Q10. Why no `make check` test for the exact bytes of the create-session message?

By repo convention, the libipm tests byte-assert the **generic type layer** (one
synthetic catch-all message) and the `q` type is already covered there. No
individual application message is byte-asserted, and doing so would be a tripwire
that breaks on every future field append. Instead we added **semantic
round-trip** tests (send→recv, assert values and the receive-side clamp), which
cover the new field without coupling to byte layout.

### Q11. What if upstream changed `LIBIPM_VERSION` before this merges?

Rebase and re-bump to whatever is current + the field still appended last; update
the duplicated value in `tests/libipm/test_libipm.h` (it's an intentional canary
— the comment there says so). The conflict is mechanical and confined to two
lines plus the format strings.

### Q12. Can the admin still force a specific DPI?

Yes — put `-dpi <n>` in `sesman.ini [Xorg]` params. We detect an exact `-dpi`
token and skip the client value entirely (logged), so the admin value always
wins and there's never a duplicate `-dpi` on the command line.

### Q13. Why is the diff "just plumbing" across so many files?

The value has to cross two process boundaries (xrdp→sesman, sesman→sesexec), and
each hop has a send side, a receive side, and a header. That's inherent to the
SCP/EICP design; each file's change is small (a parameter + a format-string
field + a validation). The actual logic lives in one place
(`common/xrdp_client_info.c`).

### Q14. Does `-dpi` actually scale the desktop fonts? Why not set `Xft.dpi`?

`-dpi` sets the X server **core** DPI (what `xdpyinfo` reports). GTK/Qt toolkits
take their font/UI scale from `Xft.dpi` / XSETTINGS `/Xft/DPI`, **not** the core
DPI — so visible scaling happens only when the desktop runs in **auto-DPI mode**
(it honors the core DPI, e.g. XFCE `Xft/DPI = -1`). A desktop that pins a fixed
toolkit DPI (e.g. XFCE's default `Xft/DPI = 96`) overrides `-dpi`, and fonts stay
at 96 even though `xdpyinfo` shows the new value.

We deliberately do **not** set `Xft.dpi`/XSETTINGS, because that is per-user,
desktop-specific session config — explicitly out of scope (PRD Non-Goals 3 & 5:
no `.xsession`/XFCE/GNOME config changes, no DE dependency). Trying to set it
correctly for every DE (XFCE, GNOME, KDE, bare WMs, each with its own settings
daemon that may re-override) is the "fix every WM" rabbit hole this PR avoids.

So the scope is: **propagate the DPI to the X server; let the desktop honor it.**
Setting the toolkit DPI could be a separate, opt-in follow-up.
