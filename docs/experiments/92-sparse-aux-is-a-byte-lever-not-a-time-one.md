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

## 2026-08-23 correction

The implementation anchor above did not prove the screen eventually returned
to 4:4:4. It only proved which *arriving* frames included auxiliary work. Real
Windows-client evidence in #125 showed that an LC=1 update replaces its damage
region with 4:2:0 and can remain there when application damage stops. The old
implicit assumption that the client preserves older chroma is retracted.

#125 adds a trailing-edge timer: after a main-only update remains the newest
frame for `chroma_idle_ms`, xrdp requests one current full-screen capture from
xorgxrdp. The resulting main-plus-auxiliary frame restores static regions
without retaining or copying borrowed capture pages on every moving frame.
The deterministic display model and timer tests live in
`test_avc444_convert.c` and `test_avc444_chroma_due.c`. Representative-client
replay remains open in #125.
