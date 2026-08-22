# #103 — encoder input-pipe size and the container host limit

Recorded 2026-08-08 through 2026-08-09. The implemented portion moved from
`BACKLOG.md` on 2026-08-16; the remaining host and architecture questions stay
open there.

## Finding

The ffmpeg child input pipe was silently left at 8192 bytes in this
unprivileged Incus environment when a one-megabyte `F_SETPIPE_SZ` request was
refused. A 13.82 MB picture then crossed the pipe in 1688 write/read turns.
The limitation belonged to the host-user pipe-page accounting and the
container's lack of `CAP_SYS_RESOURCE` in the initial user namespace, not to
a normal bare-metal xrdp deployment.

The size sweep corrected the initial requirement. Performance is flat from
64 KiB through 1 MiB for this workload; the cost rises below 64 KiB. One
megabyte is a request and the kernel's common unprivileged ceiling, not the
minimum requirement and not a universal maximum.

## Implemented server and harness contract

The dev runner in `xrdp/xrdp_encoder_ffmpeg.c` now negotiates down from the
wish to the 64 KiB requirement, reads the granted size with `F_GETPIPE_SZ`,
and reports `PIPE_TOO_SMALL` at warning level with the requested and granted
sizes and the resulting writes per picture. xrdp does not modify host
sysctls.

The fleet certification and gate refuse a run whose real encoder received a
pipe below the requirement. An explicit override may collect diagnostics but
marks the result invalid; it does not suppress the warning.

The mechanism, sweep, commands and surviving raw outputs are under
`PR-demo/mac_bisect_matrix/captures/i103_pipe_handover_20260808/`. The later
before/after fleet pair used the invalid xorgxrdp per-frame logger and was
deleted by #121; its frame periods are not quotable.

## Still open elsewhere

The host sysctl may not be persistent across reboot, baselines used for a new
comparison must be re-established under the current host state, and replacing
the stream copy with shared-memory input would be a separate architecture
decision. None of those changes the implemented 64 KiB fail-loud contract.
