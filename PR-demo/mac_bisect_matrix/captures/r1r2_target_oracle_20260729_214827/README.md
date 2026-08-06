# CONTAMINATED RUN — kept as the evidence that found a harness bug

This directory is not a measurement. It is where the teardown defect was
discovered, and it is kept rather than deleted because the raw log is what
proves the diagnosis.

`r1_slot_recon.sh` tore the client down with
`pkill -f "<client>.*:<port>"`. The port is passed to the client through
the environment (`/args-from:env:RDPARGS`) and never appears in argv, so
that pattern matched nothing: the rendering client from the 19:36 run
survived and kept the session alive **for 2.2 hours**. The Xorg session
log — which outlives any single client — accumulated the whole time, so
this run's `grep R1SLOT` returned **49 723 records spanning 7 980 s**
instead of one 60 s window, and the report printed a per-monitor rate
computed over all of it.

The rate timeline over those 7 980 s is the useful part, and it is what
answered "client or server?":

    t+0 .. t+7890 s   ~6.1 sends/s   rendering xfreerdp3 attached
    t+7920, t+7950 s  19.0, 19.6     oracle client attached
    t+7980 s          0.13           no client attached

Fixes now in the harness: mark the pod's session log before connecting and
extract only lines after the mark; kill the client by process group
(`setsid` makes its pid the pgid) and warn loudly if the group survives.
The clean A/B is `../ab_render_215222/` and `../ab_oracle_215330/`.

The R1 and R2 verdicts in `VERDICT.txt` here are unaffected in substance
(slot pinning and the shmem floor do not depend on the window), but the
send counts and rates in it are meaningless — read the clean runs instead.

(Kept trimmed: the 5 MB session log was dropped and `r1slot.txt` gzipped —
the 49 723 records and their timestamps are all that the diagnosis needs.)
