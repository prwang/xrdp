# Ubuntu 24.04 frontier reproduction

This is the required unmodified-frontier control for the CPU auxiliary-leaf
failure. The server was Ubuntu 24.04.4 LTS with FFmpeg 6.1.1 and Ubuntu's
libx264 0.164 package. `server-identity.txt` pins both repository packages;
xrdp is the pre-repair `4cf5063e05d3` package and xorgxrdp is
`c190343ff28a`.

The single client monitor used the historical 2560x1440 modeline recorded in
`client-modeline.txt`. FreeRDP 3.15.0 connected to `127.0.0.1:40055` as the
matrix probe account with `/size:2560x1440 /gfx:AVC444 /cert:ignore`. The
password came from the root-only matrix credential and is not retained.
`deployed-gfx.toml` is the exact server configuration.

## Result

The old, single-child capability probe reported OK in 55 ms and xrdp selected
AVC444v2. After login, the real path repeatedly created its main and
forced-IDR auxiliary children, rejected the auxiliary-to-leaf conversion and
deleted both children. In the 5.3-second presentation window, `xrdp.log`
contains 128 live child spawns, 63 completed rewrite failures and the next
pair already started. No encoded pair was published.

`freerdp-presentation.png` is therefore black apart from the client cursor.
This is a reproduction, not a passing result. The old binary had neither a
typed rejection reason nor the bounded forensic capture added by the repair.
