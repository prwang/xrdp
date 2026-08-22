# #92 -- sparse auxiliary-view implementation anchor

This record replaces the instrumented fleet timing and byte narrative on
2026-08-17. The affected captures used xorgxrdp's synchronous per-frame
`ACK_TRACE cap` logger and were deleted under the instrument-on-path rule.
Their timing, overlap and sampled byte totals are not quotable.

The retained result is the implementation contract. Chroma selection uses
configured time bounds and frame timestamps, never pixels. When chroma is not
due, the auxiliary child is neither fed nor armed, and the next auxiliary
picture retains its independent reference chain. Deterministic tests cover
the first-frame-at-or-after refresh guarantee, non-dividing frame intervals,
settle behavior, LC=1/LC=2 serialization and independent intra cadence.

BACKLOG #125 owns identified real-client rendering and a new admissible byte
or rate qualification if those live claims remain in the handoff. See
`docs/experiments/121-evidence-admissibility-cleanup.md`.
