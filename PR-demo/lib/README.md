# lib — shared and target-specific PR-demo helpers

- `capture_codec.sh` — waits for/asserts the negotiated GFX codec of the
  current session from the xrdp log (harnesses source this to fail fast on
  a config mismatch instead of validating the wrong codec).
- `show_img.sh`      — displays an image fullscreen in the tester session
  (wrapper used by the visual A/B scripts).
- `build_xorgxrdp_dev_deb.sh` — stages an already-built GLAMOR xorgxrdp tree
  as a commit-identified `xorgxrdp-dev` package without changing the live host
  installation.
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
- `t4/xrdp-profile` — installs one of the root-owned T4 frontier profiles as
  `gfx.toml`, restarts xrdp and reads the effective fields back. It refuses to
  change configuration while an `ubuntu` X11 session exists. The deployment
  installs it as `~/xrdp-profile` for the T4 login user.
- `t4/xrdp-benchmark-profile` — root-only selector for the dense/sparse by
  one-/two-frame-window numerical matrix. It installs a complete staged
  profile, refuses to alter a live X11 desktop, restarts xrdp and prints the
  effective settings.
- `t4/xrdp-perf-trace.conf` — service environment for the #125B matrix. It
  enables the compile-time trace ring with the filename prefix consumed by
  the gate harness; it does not enable the ordinary mutex-backed logger.

Helpers in target subdirectories are deliberately environment-specific. Their
presence here is an inventory and provenance guarantee, not a claim that the
old target configuration is still current.
