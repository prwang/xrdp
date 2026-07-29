# End-to-end run: RENDERING client at 2560×1440 + 3840×2400

`R1_OUT=... r1r2_target_geometry.sh 60`, arm-q on a freshly rolled pod,
distro `xfreerdp3` decoding and presenting. Half of the A/B in
`../ab_oracle_215330/` (same pod, same geometry, run immediately after).

**5.94 sends/s = 2.97 frame-pairs/s per monitor.** Send-gap p10/p50/p90 =
7 / 103 / 331 ms, mean 169 ms — a long, irregular tail. 149 of 338 sends
were issued at outstanding depth 2.

R1 and R2 verdicts are unchanged from the earlier runs (`VERDICT.txt`).
This run also validates the harness fix: the window is extracted from a
log mark taken before connect, and the client is killed by process group,
so the numbers are this run's and nothing else's.
