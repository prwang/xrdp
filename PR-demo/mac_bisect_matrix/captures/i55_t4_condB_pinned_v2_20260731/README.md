# VOID — same 2560x1440 client-geometry bug as v1, plus 1 black frame

A "wait for stable root" guard added to the payload (chasing what was
actually the v1 client-geometry bug) left the bare desktop on screen for
~4 s; the desktop passes through an all-black state and the oracle
black-frame check caught it (picture 68). Geometry still 2560x1440.
