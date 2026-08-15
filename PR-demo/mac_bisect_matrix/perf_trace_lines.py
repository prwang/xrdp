#!/usr/bin/env python3
"""Put named perf-trace records beside legacy wall-clock analyses.

The trace already contains the final event and field names. This adapter does
not know an AVC444 event schema and does not map positional payload slots. It
only uses the clock-base record to add the wall-clock log envelope expected by
older gate scripts, and passes every classed event's printed fields through.

Usage: perf_trace_lines.py [--since EPOCH] [--until EPOCH] <trace> [...]
"""

import datetime
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from perf_trace_records import read_records

COMMON_KEYS = {"schema", "mono_ns", "pid", "tid", "event", "class"}


def render(path, out, since=None, until=None):
    records = read_records(path)
    base = next((record for record in records
                 if record["event"] == "clock_base"), None)
    if base is None:
        raise ValueError("%s has no event=clock_base record; wall-clock "
                         "placement cannot be guessed" % path)
    base_mono = base["mono_ns"]
    base_real = base["real_ns"]
    kept = skipped = outside = 0
    for record in records:
        if record["event"] == "clock_base":
            continue
        trace_class = record.get("class")
        if trace_class is None:
            skipped += 1
            continue
        ns = record["mono_ns"]
        real_ns = base_real + (ns - base_mono)
        if since is not None and real_ns < since * 1000000000:
            outside += 1
            continue
        if until is not None and real_ns > until * 1000000000:
            outside += 1
            continue
        stamp = (datetime.datetime.fromtimestamp(real_ns // 1000000000,
                                                 datetime.timezone.utc)
                 + datetime.timedelta(
                     microseconds=(real_ns % 1000000000) // 1000))
        fields = ["%s=%s" % (key, value)
                  for key, value in record.items()
                  if key not in COMMON_KEYS]
        fields.append("us=%d" % (ns // 1000))
        out.write("[%s+0000] [INFO ] %s %s %s\n" %
                  (stamp.strftime("%Y-%m-%dT%H:%M:%S.%f")[:-3],
                   trace_class, record["event"], " ".join(fields)))
        kept += 1
    return kept, skipped, outside


def main():
    args = sys.argv[1:]
    since = until = None
    paths = []
    index = 0
    while index < len(args):
        if args[index] == "--since":
            since = float(args[index + 1])
            index += 2
        elif args[index] == "--until":
            until = float(args[index + 1])
            index += 2
        else:
            paths.append(args[index])
            index += 1
    if not paths:
        sys.exit(__doc__)
    kept = skipped = outside = 0
    try:
        for path in paths:
            count, stages, omitted = render(path, sys.stdout, since, until)
            kept += count
            skipped += stages
            outside += omitted
    except (OSError, ValueError) as exc:
        sys.exit("perf_trace_lines: %s" % exc)
    sys.stderr.write("perf_trace_lines: %d records rendered, %d unclassed "
                     "records left in the trace, %d outside the run window\n"
                     % (kept, skipped, outside))
    if kept == 0 and outside > 0:
        sys.stderr.write("perf_trace_lines: EVERY classed record fell outside "
                         "the window; this is a harness fault, not an idle "
                         "session\n")


if __name__ == "__main__":
    main()
