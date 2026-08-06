# VOID — the session ran at 1024x768, not 4K

This capture is **not a result**. It was intended as the BACKLOG #64
single-monitor control at 3840x2160. The client presented 3840x2160
correctly (`client-monitors.txt`: `Monitors: 1 / DUMMY0 3840/1016x2160`),
but the SESSION was served at 1024x768: all **6685** damage records read
`bbox=(0,0)-(1024,768)`.

Cause: without `/multimon`, xfreerdp does not take session geometry from
the client's monitor layout — it asks for its own default 1024x768.
`/size` is mandatory in the single-monitor path. Fixed in
`e_gate_run.sh` (the `E_MONITORS=1` branch now passes `/size:$E_SIZE`,
defaulted from `E_MODE0`), and the VERDICT now prints the session's
actual geometry from the damage extents and warns below 2 Mpx, so this
cannot pass silently again.

Any comparison of this run's 26.3 ms/send against the 2-monitor 87.0 ms
would be 0.79 Mpx against 12.9 Mpx — a 16x pixel difference — and is
meaningless (scientific quality gate, check 5).

Kept only as the worked example behind that guard.
