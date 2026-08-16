# #96 — Moving capture packing across the xup boundary withdrawn

## Historical proposal

The item proposed handing xrdp a stable raw XRGB snapshot and moving AVC view
packing from the session Xorg thread to the encoder side.

## Withdrawal — 2026-08-16

This was an architectural response to #95's optional speed target, not part of
the dev frontier or the normative port. It changes xup ownership, memory
lifetime, synchronization and security review across both repositories. With
#95 withdrawn and no PRD gate requiring the change, #96 is scope expansion and
is withdrawn. Any future design must begin as a separate paired specification
after #142.
