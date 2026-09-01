#!/usr/bin/env python3
"""Report the graphics transaction immediately before capability replacement."""

import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from perf_trace_records import read_records


COMMANDS = {
    1: "write pixels to surface",
    9: "create surface",
    10: "delete surface",
    11: "start frame",
    12: "end frame",
    13: "confirm capabilities",
    14: "reset graphics",
    15: "map surface to output",
    16: "client frame acknowledgement",
    18: "client capability advertisement",
}


def trace_files(path):
    if os.path.isfile(path):
        return [path]
    result = []
    for root, _, names in os.walk(path):
        for name in sorted(names):
            if (name.startswith("enc.") or
                    name.startswith("xrdp-perf.") or
                    name.startswith("xrdp.")):
                result.append(os.path.join(root, name))
    if not result:
        raise ValueError("%s contains no perf-trace files" % path)
    return result


def load(path):
    records = []
    for filename in trace_files(path):
        records.extend(read_records(filename))
    records.sort(key=lambda record: (record["pid"], record["mono_ns"]))
    bad = [record for record in records
           if record["event"] in ("perfdrop", "perfformat")]
    if bad:
        raise ValueError("%s has dropped or malformed trace records: %r" %
                         (path, bad))
    return records


def tx_description(record):
    command = record["cmd"]
    fields = ["tx %d" % record["seq"],
              COMMANDS.get(command, "command %d" % command)]
    for key in ("frame", "surface", "codec", "lc", "x1", "y1", "x2",
                "y2", "regions", "payload_bytes", "pdu_bytes",
                "wire_bytes", "segments", "head", "next", "tail",
                "width", "height", "monitors", "result"):
        if key in record and record[key] != -1:
            fields.append("%s=%s" % (key, record[key]))
    return "  " + " ".join(fields)


def report_process(label, pid, records):
    sends = [record for record in records if record["event"] == "wire_tx"]
    acks = [record for record in records if record["event"] == "wire_ack"]
    caps = [record for record in records
            if record["event"] == "wire_caps_begin"]
    print("%s: process %d sent %d graphics commands, received %d frame "
          "acknowledgements and %d capability advertisements." %
          (label, pid, len(sends), len(acks), len(caps)))
    replacements = [record for record in caps if record["caps_seq"] > 1]
    if not replacements:
        print("  No replacement capability advertisement was observed.")
        return
    for cap in replacements:
        last_tx = cap["last_tx"]
        preceding = next((record for record in reversed(sends)
                          if record["seq"] == last_tx), None)
        if preceding is None:
            raise ValueError("%s pid %d capability %d names missing tx %d" %
                             (label, pid, cap["caps_seq"], last_tx))
        frame = preceding.get("frame", 0)
        cumulative_ack = max((record["frame"] for record in acks
                              if record["mono_ns"] < cap["mono_ns"]),
                             default=-1)
        delta_ms = (cap["mono_ns"] - preceding["mono_ns"]) / 1_000_000.0
        print("  Replacement advertisement %d followed tx %d by %.3f ms; "
              "that transaction names frame %d and the latest client "
              "acknowledgement was frame %d." %
              (cap["caps_seq"], last_tx, delta_ms, frame, cumulative_ack))
        frame_sends = [record for record in sends
                       if record.get("frame") == frame and
                       record["seq"] <= last_tx]
        for record in frame_sends[-8:]:
            print(tx_description(record))
        capsets = [record for record in records
                   if record["event"] == "wire_cap" and
                   record["caps_seq"] == cap["caps_seq"]]
        print("  Client advertised %d sets: %s" %
              (len(capsets), ", ".join(
                  "version=%u flags=%d" %
                  (record["version"], record["flags"])
                  for record in capsets)))


def report(label, path):
    records = load(path)
    by_pid = {}
    for record in records:
        if record["event"].startswith("wire_"):
            by_pid.setdefault(record["pid"], []).append(record)
    if not by_pid:
        raise ValueError("%s has no graphics wire records" % path)
    for pid, process_records in sorted(by_pid.items()):
        report_process(label, pid, process_records)


def main():
    parser = argparse.ArgumentParser(
        description="Correlate an unacknowledged graphics frame with the "
                    "next client capability advertisement")
    parser.add_argument(
        "traces", nargs="+", metavar="LABEL=PATH",
        help="label and perf-trace file or directory for one server arm")
    args = parser.parse_args()
    for item in args.traces:
        if "=" not in item:
            parser.error("trace arguments must be LABEL=PATH")
        label, path = item.split("=", 1)
        report(label, path)


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        print("RED: %s" % error, file=sys.stderr)
        sys.exit(1)
