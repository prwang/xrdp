"""Reproduce the captured odd-edge transition using the existing trace reader."""
from pathlib import Path
import sys

CAPTURE = Path(__file__).resolve().parent
sys.path.insert(0, str(CAPTURE.parents[1]))
from i142_wire_transition import load, tx_description

records = load(str(CAPTURE / "var/log/xrdp-perf/xrdp.1557"))
sends = [r for r in records if r["event"] == "wire_tx"]
for event in ("wire_tx", "wire_rx"):
    sequence = [r["seq"] for r in records if r["event"] == event]
    if sequence != list(range(1, len(sequence) + 1)):
        raise ValueError("Missing, duplicated or unordered " + event)
    print(event, "continuous records:", len(sequence))

writes = [r for r in sends if r["cmd"] == 1 and r.get("codec") == 15]
overflow = [r for r in writes if
            r["region_x2"] > r["x2"] or r["region_y2"] > r["y2"]]
print("Overflowing AVC444 surface writes:", [r["seq"] for r in overflow])
acks = [r for r in records if r["event"] == "wire_ack"]
print("Frame ACKs after first overflowing write:",
      [r for r in acks if r["mono_ns"] > overflow[0]["mono_ns"]])
capsets = [[(r["version"], r["flags"]) for r in records
            if r["event"] == "wire_cap" and r["caps_seq"] == n]
           for n in (1, 2)]
print("Initial and replacement capability sets identical:",
      bool(capsets[0]) and capsets[0] == capsets[1])
print("Transaction tail, including both completed frame and next STARTFRAME:")
for record in records:
    if record["event"] == "wire_tx" and record["seq"] >= 588:
        print(tx_description(record))
    elif (record["event"] in ("wire_ack", "wire_caps_begin") and
          record["mono_ns"] >= sends[587]["mono_ns"]):
        print(record)
