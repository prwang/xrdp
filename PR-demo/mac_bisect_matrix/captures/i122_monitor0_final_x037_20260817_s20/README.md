# #122 one-active/one-idle arm

This is the retained selected-monitor arm for #122.
`TEXTFLOOD_MONITOR=0` waited for the two-output RandR layout and rendered only
the 2560x1440 monitor at origin 0,0. Surface 0 carried 1043 damage events;
surface 1 carried only its initial fill. The paired decision record is
`docs/experiments/122-one-active-monitor.md`.

The exact image, packages, configuration, modelines, target identity,
certificate, trace sufficiency and distributions are archived in this
directory. The payload margin was 14.90x.
