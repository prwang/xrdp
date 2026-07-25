# lib — shared helpers for the PR-demo harnesses

- `capture_codec.sh` — waits for/asserts the negotiated GFX codec of the
  current session from the xrdp log (harnesses source this to fail fast on
  a config mismatch instead of validating the wrong codec).
- `show_img.sh`      — displays an image fullscreen in the tester session
  (wrapper used by the visual A/B scripts).
