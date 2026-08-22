# lib — shared and target-specific PR-demo helpers

- `capture_codec.sh` — waits for/asserts the negotiated GFX codec of the
  current session from the xrdp log (harnesses source this to fail fast on
  a config mismatch instead of validating the wrong codec).
- `show_img.sh`      — displays an image fullscreen in the tester session
  (wrapper used by the visual A/B scripts).
- `windows_rdp/rdc_burr_sweep.ps1` — Windows-client helper which resizes an
  existing RDCMan or mstsc window and captures each width for the AVC444
  resize-comb investigation. It runs on the client, not this Linux box.
- `incus_user_container/start_test_env.sh` plus `iptables.rules` — historical
  2026-06-23 bootstrap for the original Incus test container. It starts xrdp
  and restores the container-local SSH firewall. The current remapped
  perf/tracefs setup is not encoded here; inspect and adapt the fixed gateway
  and paths before reuse.
- `incus_user_container/tester_ed25519.pub` — the public SSH key used by that
  historical container helper. It contains no private key material.

Helpers in target subdirectories are deliberately environment-specific. Their
presence here is an inventory and provenance guarantee, not a claim that the
old target configuration is still current.
