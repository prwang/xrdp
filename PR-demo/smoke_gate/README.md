# smoke_gate — mandatory pre-handoff deploy gate

The LAST step after every install/restart of xrdp on this box, BEFORE handing
the rig to a human tester (see CLAUDE.md "Smoke-gate every handoff"). A test
that passed before the last deployment step counts for nothing.

- `smoke.sh` — the gate. Runs the keystroke-colour test at TWO session sizes
  (1920x1080 and 1024x768 — both mandatory: a real encoder bug, the ffmpeg
  probesize hold, passed every 1920x1080 run while freezing every 1024x768
  login) and asserts zero encoder restarts in the xrdp log during the run.
  Exit 0 + "SMOKE PASS" = safe to hand over.
- `keytest.sh` — the workhorse: fresh xfreerdp3 login on Xvfb :99, launches a
  fullscreen `colorkey.sh` in the tester session, presses r/g/b/w twice each
  through the RDP client, screenshots the CLIENT framebuffer after each press
  and asserts the centre shows that key's colour ("LAG" = withheld/stale
  frame). Env knobs: KEYTEST_SIZE, KEYTEST_CLIENT_SIZE, KEYTEST_USER,
  KEYTEST_HOST, KEYTEST_CLIENT_DISPLAY.
- `colorkey.sh` — the in-session colour-key app (one keypress = one damage =
  one GFX frame, tagged with a running count so a withheld tail frame is
  unambiguous).

Box assumptions (env-overridable): tester account, empty password, xrdp on
127.0.0.1:3389, Xvfb on :99.
