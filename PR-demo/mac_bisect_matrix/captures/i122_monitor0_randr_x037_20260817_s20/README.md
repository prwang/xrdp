# #122 failed early-RandR selector attempt

RED intervention: RandR was queried while xorgxrdp still exposed one
6400x2400 union output. The session log shows the real two-output layout
arriving about 2.49 seconds later. The payload consequently covered both
surfaces, and no timing from this directory supports a one-active-monitor
claim.
