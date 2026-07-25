# mac_windows_app — macOS "Windows App" client evidence

Reference screenshots from the macOS Windows App (Apple VideoToolbox
decoder) sessions; kept because that client exposed decoder-strictness
issues the Linux/Windows clients tolerated (AVC444v2 LC framing blackout,
fixed by the luma-first LC=1/LC=2 pairing — see BACKLOG).

- `avc444v2_blackout_2026-07-23.png` — the pre-fix blackout as shipped to
  the owner's Mac.
