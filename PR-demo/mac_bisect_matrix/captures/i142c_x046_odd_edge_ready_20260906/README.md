# Port 40062 readiness after host pipe correction

2026-09-06: the live pod reports the restored soft pipe quota of 262144
pages, hard limit 0 and per-pipe maximum 1048576 bytes. This read verifies
the active values, not persistence across host reboots.

The replacement pod is xrdp-x046-88845dc4d-x6cs5. Its installed xrdp remains
c729a50889a2 and its producer remains baf9658c397d; the gfx configuration
hash matches the initial deployment. See ready-pod.json and ready-identity.txt.
The initial failed certificate and smoke remain in the preceding capture;
pre-recreate files here preserve the old pod state and traces.

The subsequent three-second certificate at 15:19:12 UTC passed the encoder
pipe guard, all six auxiliary-leaf wire checks, and decoding all four captured
pictures without black frames. See final-certification.txt and its output.
Its oracle client deliberately terminates at the end of capture; that EOF
is not a Windows odd-edge reproduction.

The final rendered smoke passed at 1920x1080 and 1024x768: each has eight
correct key transitions, zero lag, edge fidelity 1.000 and zero encoder
restart/sequence errors. The per-size outputs retain each observation.
The probe session is logged off and its autostart marker is removed.
The final handoff is copied into the tester home as
.turn_draft_20260906-152225.md.
No Windows test has been performed in this turn. The arm retains the intentional
odd-edge overflow; successful even-size gates do not confirm that hypothesis.
Ports 40060 and 40061 were not modified in this turn.
