# Ubuntu 24.04 post-confirmation failure replay

This is the terminal-failure half of the repair. It uses valid CABAC
auxiliary-leaf arguments plus `-frames:v 5`, as retained in
`deployed-gfx.toml`. The low-level probe consumes four frames and the exact
topology probe uses a separate child pair, so both probes pass. The persistent
post-login children then exit after their fifth encoded picture.

The client and server environment is the same one-monitor 2560x1440 Ubuntu
24.04.4 arm described by the paired frontier and repaired-rejection captures.
`server-identity.txt` pins the exact candidate image and binaries.

## Result

The log contains four FFmpeg spawns total: two topology-probe children and
two live children. At 23:10:54.482 UTC the first live encode failure retained
one mode-0600 manifest; at 23:10:54.488 xrdp latched the backend fatal state
and closed the connection without retry or codec fallback. There is one
forensic record, one terminal log line and no later child spawn.

`freerdp-render.log` labels the disconnect `ERRINFO_LOGOFF_BY_USER
[0x0001000C]`. That does not contradict xrdp's internal
`ERRINFO_SERVER_DWM_CRASH` latch. FreeRDP 3.15 client mode defaults error-info
PDU support off; xrdp therefore cannot transmit its detailed reason and
FreeRDP maps the subsequent user-requested disconnect ultimatum to its generic
logoff status. The packed `0x0001000C` is FreeRDP's error class plus that
status, not a second server retry or fallback.

No access units exist to retain for a child that has already exited, so this
failure record contains the exact FFmpeg version, dimensions and argv. A
rewrite rejection, where encoded access units do exist, retains both units as
shown by the repaired-rejection capture.
