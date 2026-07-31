# VOID — session ran at 2560x1440, not 4K (client-rig statefulness bug)

The dev box's leftover :94 dummy X server was adopted by the harness; a
RandR mode NAMED 3840x2160R had been created against the default
2560x1440 modeline, setup_monitors' count-only check printed "OK", the
oracle client clamped the session to its real 2560x1440 screen, and this
run measured a 3.69 Mpx workload labelled 4K (mean 125.2 ms is BIMODAL —
p50 58 — and comparable to nothing). Kept as the record; fixes:
setup_monitors.sh active-geometry check, e_gate_run.sh stateless client
X server, CLAUDE.md "Client-rig statelessness".
