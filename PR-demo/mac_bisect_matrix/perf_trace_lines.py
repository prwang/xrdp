#!/usr/bin/env python3
"""perf_trace_lines.py -- render a perf-ring trace as the log lines it replaced.

BACKLOG #61h. The per-frame trace records used to be `LOG(LOG_LEVEL_INFO,
"GFX_TRACE ...")` / `"ACK_TRACE ..."` lines in xrdp.log -- log.c's
unbuffered write under a global mutex, ~12 of them per frame on the xrdp
main thread, on the very path whose latency #61f exists to cut. They now
go to `common/perf_trace`'s ring, which the sink thread writes.

This is NOT another instrument. It is the format adapter between the one
sink we have and the analyses that already exist (e_gate_run.sh's E5
parser, i61f_delivery_chain.py, e52_flood_analyze.py, e7_drag_sweep.sh).
Rewriting each of those to read the ring would have been four chances to
get a parser subtly wrong on the same data; rendering the ring back into
the shape they already read is one.

The ring record is

    <monotonic_ns> <tid> <tag> <a> <b> <c> <d> <e> <f>

and the sink's first line is

    # perfbase mono_ns <M> real_ns <R>

which is what puts the records on the wall clock without fitting
anything: the two clocks are read back to back at sink open.

One field is gone for good: the damage record's `first=(x,y)-(x,y)`
rect. The payload is six fields and the bounding box takes all six. No
reader parsed `first=` (checked across PR-demo/ and tools/ before it was
dropped), and the bbox is what every geometry check uses.

THE RING FILE IS THE WHOLE PROCESS'S LIFE, NOT ONE RUN. The sink opens
it once per xrdp process and appends until that process dies, so a pod
that has served three runs has all three in one file. xrdp.log is
windowed by an explicit mark for exactly this reason (`Mark the FILE,
not just a line count`, 2026-07-31) and the ring needs the same
treatment -- without it a 60 s run reports its records spread over the
pod's whole uptime. Measured 2026-08-01, and it is not subtle: a 60 s
run on a pod that had been up 45 min reported "sends: 2629 over
2713.5 s", mean 1032.5 ms against a p50 of 25.0 ms. The p50 was right
and the mean was an artefact of two idle gaps between runs.

--since/--until take UNIX epoch seconds and keep only records inside
that window. The gate passes the run's own start and end.

Usage: perf_trace_lines.py [--since EPOCH] [--until EPOCH] <perf-file> [...]
"""
import sys
import datetime

# tag -> (line prefix, format string over the six payload fields).
# Each entry reproduces the log line that tag replaced, byte for byte,
# so an analysis written against the old capture reads the new one.
TAGS = {
    "msgin":     ("ACK_TRACE", "msgin id={a} bytes={b} us={us}"),
    "submit":    ("ACK_TRACE", "submit id={a} mon={b} us={us}"),
    "absorb":    ("ACK_TRACE", "absorb id={a} mon={b} us={us}"),
    "egress":    ("ACK_TRACE", "egress id={a} shown={b} us={us}"),
    "ackregion": ("ACK_TRACE",
                  "ack id={a} kind=region egress={b} absorbed={c} us={us}"),
    "ackslot":   ("ACK_TRACE",
                  "ack id={a} kind=slot egress={b} absorbed={c} us={us}"),
    "cliack":    ("GFX_TRACE",
                  "ack frame_id={a} queue_depth={b} decoded={c} "
                  "id_server={d} ack_off={e}"),
    "send":      ("GFX_TRACE",
                  "send bytes={a} last={b} frame_id={c} id_server={d} "
                  "id_client={e} fif={f}"),
    "dmg":       ("GFX_TRACE",
                  "avc dmg surface={a} num_rects={b} bbox=({c},{d})-({e},{f})"),
    "enc":       ("GFX_TRACE",
                  "enc submitted_seq={a} returned_seq={b} rv={rv} "
                  "inflight={d} centerY={e}"),
    "batch":     ("GFX_TRACE",
                  "batch cycle={a} set_n={b} monitors_armed={c} "
                  "kids_armed={d} max_kids={e} rv={f}"),
}


def render(path, out, since=None, until=None):
    base_mono = base_real = None
    kept = skipped = dropped = 0
    for line in open(path, errors="replace"):
        if line.startswith("# perfbase"):
            f = line.split()
            base_mono, base_real = int(f[3]), int(f[5])
            continue
        f = line.split()
        if len(f) != 9:
            continue
        tag = f[2]
        spec = TAGS.get(tag)
        if spec is None:
            skipped += 1          # a stage bracket: not a log line, ignore
            continue
        if base_mono is None:
            sys.exit("%s has no `# perfbase` line -- the records cannot be "
                     "placed on the wall clock, and guessing the offset is "
                     "exactly what the base line exists to prevent" % path)
        ns = int(f[0])
        a, b, c, d, e, g = (int(x) for x in f[3:9])
        real_ns = base_real + (ns - base_mono)
        # window BEFORE formatting: a record from another run of the same
        # pod is not this run's evidence
        if since is not None and real_ns < since * 1000000000:
            dropped += 1
            continue
        if until is not None and real_ns > until * 1000000000:
            dropped += 1
            continue
        # integer seconds + integer remainder: real_ns is ~1.8e18, and
        # a float cannot hold that to nanosecond precision, so dividing
        # first would quantise the very jitter these records exist to
        # show
        stamp = (datetime.datetime.fromtimestamp(real_ns // 1000000000,
                                                 datetime.timezone.utc)
                 + datetime.timedelta(
                     microseconds=(real_ns % 1000000000) // 1000))
        body = spec[1].format(a=a, b=b, c=c, d=d, e=e, f=g,
                              us=ns // 1000,
                              rv="READY" if c else "PENDING")
        out.write("[%s+0000] [INFO ] %s %s\n"
                  % (stamp.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3],
                     spec[0], body))
        kept += 1
    return kept, skipped, dropped


def main():
    args = sys.argv[1:]
    since = until = None
    paths = []
    i = 0
    while i < len(args):
        if args[i] == "--since":
            since = float(args[i + 1])
            i += 2
        elif args[i] == "--until":
            until = float(args[i + 1])
            i += 2
        else:
            paths.append(args[i])
            i += 1
    if not paths:
        sys.exit(__doc__)
    kept = skipped = dropped = 0
    for path in paths:
        k, s, d = render(path, sys.stdout, since, until)
        kept += k
        skipped += s
        dropped += d
    sys.stderr.write("perf_trace_lines: %d records rendered, %d stage "
                     "brackets left in the ring file, %d outside the run "
                     "window\n" % (kept, skipped, dropped))
    if kept == 0 and dropped > 0:
        sys.stderr.write("perf_trace_lines: EVERY record fell outside the "
                         "window -- the run window and the ring disagree; "
                         "this is a harness fault, not an idle session\n")


main()
