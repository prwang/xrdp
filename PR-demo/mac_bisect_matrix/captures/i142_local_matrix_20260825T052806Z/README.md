# Red #142 replay: unequal cold-start contamination

This run is retained as a red experimental-design result and supplies no
paired performance effect. The trace schema, command/byte accounting and
identity-paired latency decomposition all closed, but the two `sparse-w2`
repetitions differed by 71.0 percent.

The discrepancy is not an encoder distribution. `D1` was the first session
after a profile rollout and contains a 6138.1 ms desktop-startup gap; adjacent
`D2` reused the same pod/profile and has no gap above 56.8 ms. The steady-state
p50 intervals agree (33.39 and 32.92 ms). Every other rolled-out leg likewise
contains one 3.9--6.5 second startup gap, which pulls its mean away from its
33--36 ms steady-state mode. Consequently the printed sparse/window ratios at
the end of `analysis.txt` are inadmissible.

The replacement method gives every newly created whole session the same
bounded eight-second desktop warm-up before opening its retained 20-second
measurement window. The warm-up is outside trace analysis and does not change
the four treatments or the prescribed `A B C D D C B A` order.
