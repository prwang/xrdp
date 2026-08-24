# Ubuntu 24.04 repaired pre-confirmation rejection

This reruns the affected operator arguments on the repaired server, on the
same Ubuntu 24.04.4 / FFmpeg 6.1.1 / libx264 0.164 arm and historical
2560x1440 modeline as the frontier control. The explicit arguments in
`deployed-gfx.toml` deliberately omit `cabac=1`; they are not the repaired
built-in default.

FreeRDP 3.15.0 connected to `127.0.0.1:40055` as the matrix probe account with
`/size:2560x1440 /gfx:AVC444 /cert:ignore`. The root-only password is not
retained. `server-identity.txt` pins the candidate image, xrdp binary and
xorgxrdp binary used by this replay.

## Result

Before capability confirmation, the repaired probe created exactly the two
children that an AVC444 leaf session would use, encoded one pair and invoked
the production auxiliary-to-leaf transform. It rejected the pair once as
`ENTROPY_UNSUPPORTED`. Both encoded streams are H.264 Constrained Baseline at
2560x1440; the manifest independently records `main_entropy_cabac=0` and
`aux_entropy_cabac=0`.

The server retained the untouched main access unit, untouched auxiliary
access unit and manifest in a mode-0700 directory with mode-0600 files. The
on-server modes are in `forensics-inventory.txt`; `kubectl cp` changed the
archived copies' local modes only. The manifest stores exact argv as
lowercase hex so whitespace and metacharacters remain unambiguous without
making configured text executable.

The AVC candidate was removed before selection and RFX was then selected
according to the configured codec order. `freerdp-presentation.png` shows the
banner rendered through that pre-confirmation choice. This is not a runtime
codec fallback and is not evidence that the rejected AVC stream rendered; it
proves the incompatible configuration is now refused before it can produce a
black AVC session.
