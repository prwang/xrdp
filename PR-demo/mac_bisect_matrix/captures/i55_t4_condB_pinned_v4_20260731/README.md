# VOID for gate purposes — black frame persists with deferred map

textflood's window map was deferred until frame 0 was rendered; the black
frame STILL appeared (picture 64), falsifying the textflood-map
hypothesis: the black state belongs to the desktop during the payload
wait. The wait was reverted (see e52_payload.sh comment); v5 is clean.
The deferred map is kept — it is correct regardless.
