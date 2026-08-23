# #124/#124B — credit-frontier client qualification

**Closed 2026-08-23.** This record owns the qualitative client replay for the
dense AVC444v2 development profile and the repair of its two interactive test
payloads. It makes no numerical throughput or latency claim.

## Configuration and clients

The server identity, Tesla T4/NVENC stack, Windows host `5Q77`, macOS host
`Signals-iMac`, and limits on identifying the exact client product versions
are recorded in `docs/experiments/123-t4-nvenc-compatibility.md`. The profile
used here was dense AVC444v2: `wire_window=1`, eager slot acknowledgement on,
and sparse chroma off.

The owner observed `colorkey_x11` correctly on both clients. After the payload
repair below, the Solarized Dark code-scroll and both textflood variants were
also correct on both clients under AVC444v2. These are client-visible
observations; server logs establish the selected codec and absence of encoder
faults, not image fidelity.

## #124B payload correction

The first replay exposed faults in the instrument rather than in the server:

* a requested live line rate was ignored outside strip mode;
* each source line was repeated to the right edge by default;
* `--frames N` destroyed the live window instead of holding its last frame;
* the code-scroll helper depended on terminal configuration for its background.

Commit `01c5e895` made natural one-copy lines the default, retained the
saturated repeat-to-edge workload behind an explicit option, made the live
line rate effective, held a limited live run until Escape or `q`, and made the
code-scroll helper paint Solarized base03 itself. Compiler warnings, offline
rendering equivalence, argument validation, the 5-lines/s cadence, and the
finite-frame hold/quit checks passed before the corrected helpers were
installed on the T4.

## Closure

Both corrected flood payloads were visually good on both clients in dense
AVC444v2 mode. #124 and #124B are therefore closed. The later sparse-profile
Color-A/Color-B defect is isolated to #125 and does not retract this dense
result.
