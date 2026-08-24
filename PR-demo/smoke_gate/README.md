# smoke_gate — mandatory pre-handoff deploy gate

The LAST step after every install/restart of xrdp on this box, BEFORE handing
the rig to a human tester (see CLAUDE.md "Smoke-gate every handoff"). A test
that passed before the last deployment step counts for nothing.

- `smoke.sh` — the gate. Runs the keystroke-colour test at TWO session sizes
  (1920x1080 and 1024x768 — both mandatory: a real encoder bug, the ffmpeg
  probesize hold, passed every 1920x1080 run while freezing every 1024x768
  login) and asserts zero encoder restarts in the xrdp log during the run.
  Exit 0 + "SMOKE PASS" = safe to hand over.
- `keytest.sh` — the workhorse: arms the versioned XDG autostart, makes a
  fresh xfreerdp3 login on Xvfb :98, presses r/g/b/w twice each
  through the RDP client, screenshots the CLIENT framebuffer after each press
  and asserts the centre shows that key's colour ("LAG" = withheld/stale
  frame). Env knobs: KEYTEST_SIZE, KEYTEST_CLIENT_SIZE, KEYTEST_USER,
  KEYTEST_HOST, KEYTEST_CLIENT_DISPLAY.
- `colorkey.sh` — the in-session colour-key app (one keypress = one damage =
  one GFX frame, tagged with a running count so a withheld tail frame is
  unambiguous). Keys: `r`/`g`/`b`/`w` flat colour, `e` red-blue chroma
  stripes, `q` quit — those four plus `e` are what `keytest.sh` drives. `c`
  and `s` are for INTERACTIVE validation from a real client (Windows UWP,
  macOS) and are never pressed by the gate: `c` cycles the screen through the
  eight RGB corners (black red green blue yellow magenta cyan white), one
  colour per frame, printing the colour name and frame count so a missing or
  out-of-order frame is nameable; `s` slides a solid block across a
  contrasting background, one whole-cell hop per frame, so stutter, jumps and
  tearing are visible by eye. Both animate until a key is pressed, and that
  key is then acted on. Rates and sizes: `CK_CYCLE_MS` (250), `CK_SLIDE_MS`
  (40), `CK_SLIDE_STEP` (4 cols), `CK_BLOCK_W`/`CK_BLOCK_H` (default: a tenth
  of the screen, capped at 40x20 cells).
- `colorkey-autostart.sh` and `xrdp-smoke-colorkey.desktop` — persistent but
  normally inert login-time wrapper. `keytest.sh` arms it with the requested
  geometry before login and removes the marker before logging off the whole
  session. SSH never launches, kills or supervises a GUI process in a live
  desktop.

Box assumptions (env-overridable): tester account, its credential in a
root-owned target-side file, xrdp on 127.0.0.1:3389 and local Xvfb on :98.
