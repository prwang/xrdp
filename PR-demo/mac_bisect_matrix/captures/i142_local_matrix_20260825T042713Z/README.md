# #142 local numerical replay — rejected shakedown

No timing leg ran and no performance number from this directory is usable.

The first generated profile used the clean-room software encoder's default
auxiliary-leaf topology. The live encoder started, produced 44,563,040 bytes
in the bounded certification window, decoded all 396 pictures with zero black
frames and logged no undersized input pipe. The deployment certifier still
failed because its wire audit is specifically an LTR-chain audit: it required
LT0/LT1 list modification, self-marking and one interleaved frame-number chain
from a topology which deliberately uses a non-reference intra auxiliary leaf.
That is an instrument/configuration mismatch, not a passing certificate and
not evidence that the leaf stream is non-conforming.

The retained `dense-w1.cert.FAILED` is the exact red output. The matrix was
stopped before A1 rather than bypassing certification. The replacement replay
uses the already specified #125B/#142 LTR-chain baseline for all four cells,
which is the topology this certifier independently audits. The leaf topology
remains covered by its exact production probe, unit tests and x040's two-size
oracle smoke; those checks are not relabelled as an LTR certificate.

## 2026-08-25 correction after the replacement profile failed

The proposed LTR replacement above was not a valid conclusion from this run.
It changed a shipped-default topology solely to satisfy an LTR-specific
instrument. The attempted replacement was stopped as another red shakedown.
The certifier now has an independent auxiliary-leaf gate which checks the
actual default contract, so the retained replay returns to the original
`aux_ltr_chain=false` profile. This correction changes no result above: this
directory still contains no timing leg and supplies no performance number.
