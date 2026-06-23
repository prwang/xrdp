# PRD: Propagate RDP Client DPI to Xorg/xorgxrdp Sessions

## 1. Title

Dynamic client DPI propagation from `xrdp` login/session negotiation to `xrdp-sesman` Xorg startup.

## 2. Problem Statement

`xrdp` detects the RDP client's monitor physical size and computes a correct DPI for the login screen, but this DPI is not propagated to the Xorg/xorgxrdp session started by `xrdp-sesman`.

Observed behavior:

```text
/var/log/xrdp.log:
Login screen monitor height is 2160 pixels over 392 mm (139 DPI)
```

But after login, inside the Xorg/xorgxrdp desktop session:

```text
$ ps -ef | grep '[X]org'
/usr/lib/xorg/Xorg :11 -auth .Xauthority -config xrdp/xorg.conf -noreset -nolisten tcp -logfile .xorgxrdp.%s.log

$ xdpyinfo | grep resolution
resolution: 96x96 dots per inch

$ xrandr --verbose
rdp0 connected 3840x2160+0+0 ... 0mm x 0mm

$ xrdb -query | grep dpi
Xft.dpi: 96
```

This causes HiDPI RDP clients to get a correctly scaled `xrdp` login screen but an unscaled 96-DPI desktop session.

The user-visible failure is that the login screen appears at the expected scale, while the actual XFCE/Xorg session is too small.

## 3. Goal

When an RDP client provides usable monitor DPI or physical monitor-size metadata, `xrdp` should propagate that information to the Xorg/xorgxrdp session so that the desktop session starts with the corresponding DPI.

For example, if the RDP client monitor is reported as:

```text
height = 2160 px
physical height = 392 mm
computed vertical DPI ~= 139
```

then `xrdp-sesman` should start the Xorg backend in a way that results in:

```text
$ xdpyinfo | grep resolution
resolution: 139x139 dots per inch
```

or an equivalent Xorg/xorgxrdp state where the X server and desktop environment receive the client-derived DPI instead of defaulting to 96 DPI.

## 4. Non-Goals

This patch must not:

1. Hard-code a DPI such as `139`, `144`, or `150`.
2. Require users to modify `startwm.sh`.
3. Require users to modify `~/.xsession`, `~/.xinitrc`, XFCE config, GNOME config, or desktop-specific startup files.
4. Require parsing `/var/log/xrdp.log`.
5. Depend on a specific desktop environment such as XFCE.
6. Change RDP listener binding behavior.
7. Change authentication, PAM, or session authorization behavior.
8. Attempt to solve per-monitor mixed-DPI scaling fully in this patch.

> **Acknowledged consequence of items 3 & 5.** This patch sets only the X server
> *core* DPI (via `-dpi`) and deliberately does not touch desktop config. GUI
> toolkits (GTK/Qt) take their font/UI scale from `Xft.dpi` / XSETTINGS
> `/Xft/DPI`, **not** the core DPI, so visible scaling happens only when the
> desktop honors the core DPI — i.e. runs in **auto-DPI mode** (e.g. XFCE
> `Xft/DPI = -1`). A desktop that pins a fixed toolkit DPI (e.g. XFCE's default
> `Xft/DPI = 96`) overrides `-dpi` and fonts will not scale. Making every DE
> honor the core DPI is out of scope; sesman instead logs a one-line reminder
> (see §11) whenever it applies a client DPI, so the admin knows to switch the
> desktop to auto-DPI if scaling does not appear.

## 5. Background

Currently, the `xrdp` front-end process already receives enough monitor metadata to log:

```text
Login screen monitor height is 2160 pixels over 392 mm (139 DPI)
```

This proves the RDP client monitor metadata is available before login and is used by the login screen.

However, `xrdp-sesman` later starts Xorg with static arguments from `sesman.ini`:

```text
/usr/lib/xorg/Xorg :11 -auth .Xauthority -config xrdp/xorg.conf -noreset -nolisten tcp -logfile .xorgxrdp.%s.log
```

There is no dynamic `-dpi <client_dpi>` argument.

The Xorg/xorgxrdp RandR output also exposes:

```text
rdp0 connected 3840x2160+0+0 ... 0mm x 0mm
```

As a result, the X server defaults to 96 DPI.

## 6. Desired Product Behavior

### 6.1 Single-monitor client with valid physical dimensions

If the RDP client reports valid pixel dimensions and valid physical dimensions, compute DPI from the monitor metadata and pass it into the Xorg session.

Example:

```text
height_px = 2160
height_mm = 392
dpi = round(2160 * 25.4 / 392) = 140
```

The observed existing log rounds this case as approximately `139 DPI`; the implementation should use the same calculation path or produce a consistent nearby integer.

Expected result after login:

```text
pgrep -a Xorg
```

should include:

```text
-dpi 139
```

or:

```text
-dpi 140
```

depending on the existing rounding behavior.

And:

```text
xdpyinfo | grep resolution
```

should report the propagated DPI instead of `96x96`.

### 6.2 Client with missing or invalid physical dimensions

If physical width/height is missing, zero, nonsensical, or outside a sane range, preserve current behavior.

Expected fallback:

```text
96 DPI
```

unless an existing config default says otherwise.

### 6.3 96-DPI client

If the client reports normal 96-DPI geometry, the Xorg session should remain approximately 96 DPI.

The patch must not make normal-DPI clients larger.

### 6.4 Multi-monitor clients

For this patch, Xorg receives one global DPI through `-dpi`.

The MVP behavior should be:

1. Prefer the primary monitor if the RDP monitor layout identifies one.
2. Otherwise use the first monitor in the client monitor layout.
3. If that monitor has invalid physical dimensions, scan for the first monitor with valid physical dimensions.
4. If none are valid, fall back to current behavior.

Do not attempt full mixed-DPI per-monitor scaling in this patch.

## 7. Proposed Design

### 7.1 Add a session DPI field to the login/session handoff

Find the code path where the `xrdp` front-end creates or requests a session from `xrdp-sesman`.

Extend the session creation/request data structure to include an optional integer DPI field, for example:

```c
int client_dpi;
int client_dpi_valid;
```

or equivalent project-style fields.

The DPI value should be computed from the same monitor metadata currently used to log login-screen DPI.

The new field must be optional and backward-compatible.

### 7.2 Compute DPI in the front-end where monitor metadata is known

The front-end already logs monitor height and physical height. Reuse that source of truth.

Suggested helper behavior:

```c
bool
xrdp_monitor_layout_get_effective_dpi(
    const struct monitor_layout *layout,
    int *dpi_out);
```

Rules:

1. Use monitor pixel height and physical height in millimeters.
2. Compute vertical DPI as:

```text
dpi = round(height_px * 25.4 / height_mm)
```

3. Accept only sane values, for example:

```text
50 <= dpi <= 400
```

4. If invalid, return false and preserve existing behavior.
5. Avoid integer overflow.
6. Log the selected DPI at debug or info level.

The exact function and struct names must follow the repository’s existing style.

### 7.3 Pass DPI to `xrdp-sesman`

Extend the protocol/message between `xrdp` and `xrdp-sesman` to carry the optional DPI.

Requirements:

1. Preserve compatibility with older clients/tools where possible.
2. Do not break existing session creation.
3. If the field is absent or invalid, `sesman` must behave exactly as before.
4. Add logging in `sesman` when a client DPI is received.

Example desired log:

```text
[INFO ] Client requested Xorg session DPI: 139
```

### 7.4 Append `-dpi <value>` to Xorg argv in `xrdp-sesman`

When starting an Xorg/xorgxrdp backend session:

Current:

```text
/usr/lib/xorg/Xorg :11 -auth .Xauthority -config xrdp/xorg.conf -noreset -nolisten tcp -logfile .xorgxrdp.%s.log
```

Desired when client DPI is valid:

```text
/usr/lib/xorg/Xorg :11 -auth .Xauthority -dpi 139 -config xrdp/xorg.conf -noreset -nolisten tcp -logfile .xorgxrdp.%s.log
```

Implementation rule:

1. Add `-dpi <client_dpi>` only for Xorg/xorgxrdp sessions.
2. Do not add it for Xvnc, X11rdp, chansrv, or unrelated backends unless those paths already support equivalent DPI semantics and tests are added.
3. Do not duplicate `-dpi` if the administrator already configured `-dpi` in `sesman.ini`.

### 7.5 Admin override behavior

If `sesman.ini [Xorg]` already contains a static `-dpi` argument, preserve the admin’s explicit configuration.

Example:

```ini
[Xorg]
param=/usr/lib/xorg/Xorg
param=-dpi
param=144
param=-config
param=xrdp/xorg.conf
...
```

In that case, do not append a second `-dpi`.

Log:

```text
[INFO ] Xorg -dpi already configured; client DPI propagation skipped
```

This prevents surprising behavior for existing deployments.

### 7.6 Optional config knob

Add an optional config setting if maintainers prefer explicit control.

Suggested setting:

```ini
[Sessions]
UseClientDPI=true
```

Default recommendation:

```ini
UseClientDPI=true
```

If maintainers prefer conservative compatibility:

```ini
UseClientDPI=false
```

and document the feature.

Behavior:

```text
UseClientDPI=true:
    If valid client DPI exists and no explicit Xorg -dpi is configured, append -dpi <client_dpi>.

UseClientDPI=false:
    Preserve current behavior.
```

Do not block the core patch on this knob unless maintainers request it.

## 8. User Stories

### 8.1 HiDPI Windows client

As a user connecting from a 3840x2160 Windows RDP client with approximately 140 DPI physical monitor metadata, I want my Xorg/XFCE session to start at approximately 140 DPI so that the desktop scale matches the already-correct xrdp login screen.

Acceptance:

```text
xrdp.log shows client monitor DPI around 139/140
xrdp-sesman.log shows Xorg started with -dpi 139/140
xdpyinfo reports 139x139 or 140x140
```

### 8.2 Normal-DPI client

As a user connecting from a normal 96-DPI client, I want the desktop session to remain normal-sized.

Acceptance:

```text
xdpyinfo reports approximately 96x96
```

### 8.3 Invalid client monitor metadata

As an administrator, I want clients with invalid or missing physical monitor dimensions to preserve existing behavior rather than causing bad scaling.

Acceptance:

```text
No -dpi is appended
or fallback remains 96
No crash
No login failure
```

### 8.4 Explicit admin `-dpi`

As an administrator who explicitly configured `-dpi` in `sesman.ini`, I want that config to remain authoritative.

Acceptance:

```text
Configured -dpi remains unchanged
No duplicate -dpi appears in the Xorg argv
```

## 9. Acceptance Criteria

### 9.1 Functional acceptance

Given an RDP client where `xrdp.log` reports:

```text
Login screen monitor height is 2160 pixels over 392 mm (139 DPI)
```

After login:

```bash
sudo grep -E 'Starting X server' /var/log/xrdp-sesman.log | tail -1
```

must show:

```text
-dpi 139
```

or a consistent rounded nearby value.

Inside the session:

```bash
xdpyinfo | grep resolution
```

must report:

```text
resolution: 139x139 dots per inch
```

or a consistent rounded nearby value.

### 9.2 Regression acceptance

For a 96-DPI client, inside the session:

```bash
xdpyinfo | grep resolution
```

must remain approximately:

```text
resolution: 96x96 dots per inch
```

### 9.3 Invalid-data acceptance

If the client physical monitor size is zero or missing, `xrdp-sesman` must not append a bad DPI such as:

```text
-dpi 0
-dpi 1
-dpi 10000
```

The session must still start.

### 9.4 Existing-config acceptance

If `[Xorg]` in `sesman.ini` already includes:

```ini
param=-dpi
param=144
```

then the generated Xorg command must not include another `-dpi`.

### 9.5 Non-Xorg acceptance

Xvnc and other non-Xorg backends must behave as before unless explicitly updated and tested.

## 10. Test Plan

### 10.1 Unit tests

Add tests for the DPI calculation helper.

Cases:

```text
height_px=2160, height_mm=392 -> 139 or 140 depending on project rounding convention
height_px=1440, height_mm=392 -> 93
height_px=1080, height_mm=286 -> 96
height_px=2160, height_mm=0 -> invalid
height_px=0, height_mm=392 -> invalid
height_px=2160, height_mm=-1 -> invalid
dpi < 50 -> invalid
dpi > 400 -> invalid
```

### 10.2 Argument-building tests

Add tests for Xorg argv construction if the repo has test coverage for sesman argument generation.

Cases:

1. Valid client DPI and no configured `-dpi`:

```text
argv contains -dpi <client_dpi>
```

2. Valid client DPI and existing configured `-dpi`:

```text
argv contains exactly one -dpi
configured value wins
```

3. Invalid client DPI:

```text
argv does not contain generated -dpi
```

4. Non-Xorg backend:

```text
argv unchanged
```

### 10.3 Manual test: HiDPI client

Server:

```bash
sudo systemctl restart xrdp
sudo tail -f /var/log/xrdp.log /var/log/xrdp-sesman.log
```

Client:

```text
Connect from Windows mstsc on a 3840x2160 monitor with HiDPI scaling.
```

Validate:

```bash
sudo grep -E 'Login screen monitor height|Starting X server' /var/log/xrdp.log /var/log/xrdp-sesman.log
pgrep -a Xorg
xdpyinfo | grep resolution
xrandr --verbose | sed -n '/ connected/,/^$/p'
```

Expected:

```text
xrdp login logs client DPI around 139/140
Xorg argv includes -dpi 139/140
xdpyinfo reports 139/140 DPI
```

### 10.4 Manual test: 96-DPI client

Connect from a normal-DPI client.

Validate:

```bash
xdpyinfo | grep resolution
```

Expected:

```text
approximately 96x96 DPI
```

### 10.5 Manual test: explicit admin override

Configure:

```ini
[Xorg]
param=/usr/lib/xorg/Xorg
param=-dpi
param=144
...
```

Reconnect.

Validate:

```bash
pgrep -a Xorg
```

Expected:

```text
contains exactly one -dpi 144
does not append client-derived -dpi
```

## 11. Logging Requirements

Add clear but non-noisy logging.

Suggested front-end log:

```text
[INFO ] Client monitor height is 2160 pixels over 392 mm (139 DPI)
```

Existing log may already provide this.

Suggested sesman logs:

```text
[INFO ] Received client DPI for Xorg session: 139
[INFO ] Starting X server on display 11: /usr/lib/xorg/Xorg :11 ... -dpi 139 ...
```

When the client DPI is applied, sesman also logs a reminder that the desktop
must be in auto-DPI mode for it to take visible effect (see Non-Goals):

```text
[INFO ] ... starting Xorg with client DPI 139. This sets the X server core DPI
        only; GUI toolkits (GTK/Qt) follow it only when the desktop's font DPI is
        automatic. If fonts do not scale, set the desktop to auto DPI (e.g. XFCE
        Xft/DPI = -1).
```

If skipped:

```text
[DEBUG] No valid client DPI supplied; using existing Xorg arguments
```

If admin override exists:

```text
[INFO ] Xorg -dpi already configured; skipping client DPI injection
```

## 12. Security Considerations

Client-provided DPI is not security-sensitive, but it is client-controlled input.

The implementation must:

1. Validate numeric bounds.
2. Reject zero, negative, and extreme values.
3. Avoid buffer overflows when formatting `-dpi`.
4. Avoid changing auth, PAM, session ownership, or user identity handling.
5. Avoid reading user-controlled shell config to compute DPI.

Recommended sane range:

```text
50 <= dpi <= 400
```

If outside range, ignore and preserve existing behavior.

## 13. Compatibility Considerations

This patch should preserve existing behavior when:

1. Client DPI is unavailable.
2. Client monitor physical dimensions are invalid.
3. Admin already configured `-dpi`.
4. Session backend is not Xorg/xorgxrdp.
5. Feature flag is disabled, if a feature flag is added.

The patch should not require xorgxrdp changes for the MVP if using the Xorg `-dpi` argument.

A future xorgxrdp patch could also expose RandR physical monitor dimensions instead of `0mm x 0mm`, but that is out of scope for this MVP.

## 14. Implementation Notes for Codex

Codex should inspect the repository before editing. Do not assume exact filenames.

Likely areas to inspect:

```text
xrdp front-end monitor layout / login screen DPI calculation
session creation request from xrdp to xrdp-sesman
scp or sesman communication structs/messages
sesman session startup data structures
Xorg backend argument construction
sesman.ini parsing for [Xorg] param= lines
tests for session startup or config parsing
```

Search terms:

```text
"Login screen monitor height"
"default_dpi"
"fv1_select"
"monitor height"
"physical"
"mm"
"Starting X server"
"param="
"Xorg"
"scp"
"sesman"
```

Implementation should reuse existing coding style, logging macros, memory allocation helpers, and config parsing conventions.

## 15. Proposed Patch Shape

### Phase 1: Find source of login-screen DPI

Locate the existing code that emits:

```text
Login screen monitor height is ... pixels over ... mm (... DPI)
```

Refactor the calculation into a reusable helper if needed.

Output:

```c
bool client_dpi_valid;
int client_dpi;
```

### Phase 2: Add DPI to session request

Extend the session-start request path from `xrdp` to `xrdp-sesman` to include optional DPI.

Preserve compatibility and initialize defaults.

### Phase 3: Store DPI in sesman session parameters

Add the field to the sesman-side session/session-start structure.

Validate bounds again in sesman.

### Phase 4: Inject Xorg `-dpi`

When building Xorg argv:

```text
if backend == Xorg
and client_dpi_valid
and no admin-configured -dpi already exists
then append:
    -dpi
    <client_dpi>
```

### Phase 5: Add tests and docs

Update relevant documentation/manpage if config behavior changes.

If a config knob is added, document it.

## 16. Definition of Done

The patch is done when:

1. HiDPI client DPI visible in `xrdp.log` is propagated to the Xorg session.
2. `xrdp-sesman.log` shows Xorg launched with `-dpi <client_dpi>`.
3. `xdpyinfo` inside the session reports the client-derived DPI.
4. 96-DPI clients remain 96 DPI.
5. Invalid monitor metadata falls back safely.
6. Existing explicit `-dpi` config is respected.
7. Tests cover DPI calculation and Xorg argv behavior.
8. No desktop-environment-specific startup script changes are required.

## 17. Example Before/After

### Before

```text
xrdp.log:
Login screen monitor height is 2160 pixels over 392 mm (139 DPI)

xrdp-sesman.log:
Starting X server on display 11:
  /usr/lib/xorg/Xorg :11 -auth .Xauthority -config xrdp/xorg.conf -noreset -nolisten tcp -logfile .xorgxrdp.%s.log

inside session:
xdpyinfo -> 96x96 DPI
xrandr -> rdp0 ... 0mm x 0mm
```

### After

```text
xrdp.log:
Login screen monitor height is 2160 pixels over 392 mm (139 DPI)

xrdp-sesman.log:
Received client DPI for Xorg session: 139
Starting X server on display 11:
  /usr/lib/xorg/Xorg :11 -auth .Xauthority -dpi 139 -config xrdp/xorg.conf -noreset -nolisten tcp -logfile .xorgxrdp.%s.log

inside session:
xdpyinfo -> 139x139 DPI
```

## 18. Open Questions for Maintainers

1. Should the default behavior be enabled by default or behind a config flag?
2. Should the DPI source prefer physical DPI or RDP desktop scale factor if both are available?
3. For multi-monitor layouts, should global Xorg DPI use the primary monitor, first monitor, largest monitor, or average DPI?
4. Should xorgxrdp also be updated to expose non-zero RandR physical dimensions?
5. Should this apply only to Xorg/xorgxrdp sessions or also to Xvnc where supported?

## 19. Recommended MVP Decision

For the first upstreamable patch:

1. Use physical monitor DPI already calculated by the `xrdp` front-end.
2. Propagate one integer DPI to `xrdp-sesman`.
3. Apply only to Xorg/xorgxrdp sessions.
4. Respect existing explicit `-dpi` config.
5. Fall back to current behavior if metadata is invalid.
6. Do not attempt mixed-DPI per-monitor support.

This solves the observed regression without introducing desktop-environment-specific behavior or brittle local workarounds.

