# #122 failed Xinerama selector attempt

RED intervention: `/etc/textflood_monitor` was `0`, but the payload header
reported `target=monitor-0 origin=0,0 6400x2400`; Xorgxrdp exposes the desktop
as one Xinerama screen. Both surfaces carried equal damage. No timing from
this directory supports a one-active-monitor claim.
