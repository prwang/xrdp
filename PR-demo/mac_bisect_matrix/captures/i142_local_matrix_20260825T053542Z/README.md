# Retracted #142 local matrix attempt

The runner printed `PASS`, but this attempt contributes no throughput result.
Its rate calculation divided the intervals between the first and last active
terminal frame rather than dividing the terminal-frame count by the complete
20-second measurement window. D1 delivered 538 frames and D2 delivered 592
frames in equal windows, yet the discarded calculation reported both near
30 frames/s. The counts and the stated rates therefore did not close.

The underlying boundary fault was a blind eight-second delay starting at
client launch. Fresh pods could spend part of that delay establishing the RDP
session, leaving cold-start inactivity inside the measurement window, while a
later profile repetition reused an established environment. The replacement
runner removes the payload stamp file before connection, waits for 60 frames
from that new session, stops polling, and only then opens the measurement
window. Its analyzer uses terminal-frame count divided by the exact recorded
duration; active-frame intervals remain distributions and are not reused as
the throughput denominator.

The trace still proves useful schema and accounting properties, but none of
this attempt's timing effects is accepted. The complete corrected matrix is a
new capture rather than a reinterpretation of these files.
