# #142C two-arm wire-transition pre-interactive qualification

This is the final server state prepared for the same-client Windows resize
comparison. Port 40060 is canonical development; port 40061 is the
fault-preserving clean-room diagnostic. Both use Ubuntu 24.04, XFCE, the same
automatic dense AVC profile, a one-frame client window and the same bounded
logical-RDPGFX trace schema. Each trace has its own new host directory.

| arm | endpoint | xrdp | xorgxrdp | trace directory |
| --- | --- | --- | --- | --- |
| development | `127.0.0.1:40060` | `655639d8270c` | `baf9658c397d` | `/var/lib/xrdp-matrix/x044-wire-655639d8` |
| clean-room | `127.0.0.1:40061` | `50a974b6e56c` | `aca3c774cb8b` | `/var/lib/xrdp-matrix/x045-wire-50a974b6` |

Both pods reported zero container restarts after deployment. Their installed
package versions named the exact commits above, `XRDP_WIRE_TRACE=1` and the
perf sink path were present, and XFCE, Thunar and the container-safe Chromium
launcher were installed.

The trace-enabled inspector tests passed 220/220 on development and 193/193
on clean-room. The MapSurfaceToOutput check uses the production builder,
checks surface and coordinates and rejects every truncation. This gate was
added after the provisional deployment exposed that the inspector had omitted
the command's reserved two bytes.

Each exact image/config pair passed its three-second byte/decode certificate.
Each certificate trace contained 525 identified outbound graphics commands,
four inbound commands, one initial capability advertisement, one identified
MapSurfaceToOutput, zero unidentified sends and zero sink drops. The analyzer
reported no replacement advertisement in either short run, which is the
expected pre-trigger state.

The final rendered smoke ran after the final deployment. On each arm both
1920x1080 and 1024x768 produced all eight expected key transitions, no lag,
edge fidelity 1.000 and zero encoder restart/sequence errors.

The interactive result remains open. Use the same Windows client profile and
resize sequence on both endpoints. The comparison is not complete until the
clean-room replacement capability advertisement is reproduced and the
development control is exercised through at least the same resize count. Raw
trace files will be archived with that result; the live host directories are
not evidence by themselves.
