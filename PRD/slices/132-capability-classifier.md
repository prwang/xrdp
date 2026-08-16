# Slice #132 — Pure AVC capability classification

## Commit boundary

This xrdp-only commit adds a pure classifier. It does not probe an encoder,
write a capability set or modify live connection state.

Target files are `xrdp/xrdp_avc444_caps.c`,
`xrdp/xrdp_avc444_caps.h`, and `tests/xrdp/test_avc444_caps.c`.
Register source and suite in `xrdp/Makefile.am`,
`tests/xrdp/Makefile.am`, `tests/xrdp/test_xrdp.h` and
`tests/xrdp/test_xrdp_main.c`.

## Requirements

* S132-R1: input is the parsed RDPGFX capability version/flags and a requested
  server mode; output is one of unavailable, AVC420, AVC444v1 or AVC444v2 plus
  the exact codec ID. The function shall have no side effect.
* S132-R2: RDPGFX v8 has no AVC path. v8.1 permits AVC420 only when its
  enable flag is present. v10.0 permits AVC444v1 unless AVC is disabled.
  v10.1 signals AVC444v2 but is not a v1 capset. v10.2 through v10.7 permit
  v1 and signal v2 unless AVC is disabled. Unknown versions are rejected.
* S132-R3: forced `444` shall choose v2 when the client signals it and
  otherwise v1 when available; forced `444v1` shall never select v2; forced
  `420` shall require AVC420 support. Auto shall choose the highest configured
  compatible mode, not invent client support.
* S132-R4: global AVC disable or backend-unavailable input shall return
  unavailable. The classifier shall not fall back to another server codec.
* S132-R5: no symbol in this slice shall make the new backend selectable.

## Required tests and gate

The `Avc444Caps` suite shall retain seven table-driven cases covering v8,
v8.1, v10.0, excluded v10.1, v10.2–v10.7, unknown versions and v2 support,
including disabled/forced subrows. Run
`CK_RUN_SUITE=Avc444Caps tests/xrdp/test_xrdp`, then the README gate.
