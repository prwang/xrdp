# Red #142 replay: the analyzer expected a different trace schema

This eight-leg run is retained as a red harness/implementation result and
supplies no performance number. All legs completed on xrdp `b53bead65628` and
xorgxrdp `aa03d860137d`, but analysis stopped on the first slot-credit record
with `KeyError: id`.

Inspection found that the clean-room trace uses `frame=` while the archived
development analyzer expected `id=`. More importantly, the safe formatter
rejects `%zu`, so the attempted `egress` and `video_cmd` records were absent;
the credit event also lacked the configured window and binding state required
to prove that window 2 exercised a state window 1 could not admit. The gate's
legacy line parser also ignored the new terminal `send` records. These are
measurement-contract defects, so none of the captured timing is admissible.

The owning clean-room slices were reopened and corrected before another run.
The replacement gate accepts both archived `id=` and current `frame=`
identities, consumes terminal `send` records, rejects unsupported trace
conversions, and requires the treatment/binding fields.
