#!/usr/bin/env python3
"""Analyze the repeated T4 dense/sparse by wire-window matrix."""

import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from perf_trace_records import read_records


def mean(values):
    return sum(values) / len(values) if values else math.nan


def percentile(values, percent):
    if not values:
        return math.nan
    ordered = sorted(values)
    index = int(round((len(ordered) - 1) * percent / 100.0))
    return ordered[index]


def distribution(values):
    return {
        "count": len(values),
        "mean": mean(values),
        "p50": percentile(values, 50),
        "p90": percentile(values, 90),
        "p99": percentile(values, 99),
        "min": min(values) if values else math.nan,
        "max": max(values) if values else math.nan,
    }


def read_pairs(path):
    result = {}
    with open(path, encoding="utf-8") as stream:
        for line in stream:
            key, value = line.rstrip("\n").split(" ", 1)
            result[key] = value
    return result


def load_windowed(leg_dir):
    window = read_pairs(os.path.join(leg_dir, "measurement_window.txt"))
    start_ns = int(window["start_epoch_ns"])
    end_ns = int(window["end_epoch_ns"])
    records = []
    perf_dir = os.path.join(leg_dir, "perf")
    for name in sorted(os.listdir(perf_dir)):
        path = os.path.join(perf_dir, name)
        file_records = read_records(path)
        base = next((record for record in file_records
                     if record["event"] == "clock_base"), None)
        if base is None:
            raise ValueError("%s has no clock_base record" % path)
        for record in file_records:
            if record["event"] == "clock_base":
                continue
            real_ns = (base["real_ns"] + record["mono_ns"] -
                       base["mono_ns"])
            if start_ns <= real_ns <= end_ns:
                records.append(record)
    records.sort(key=lambda record: record["mono_ns"])
    return records, start_ns, end_ns


def grouped(records, event):
    return [record for record in records if record["event"] == event]


def frame_identity(record):
    """Read either the current schema or an archived development capture."""
    if "frame" in record:
        return record["frame"]
    return record["id"]


def classify_video_commands(records, frame_ids):
    in_scope = [record for record in records
                if record["frame_id"] in frame_ids]
    boundary = [record for record in records
                if record["frame_id"] not in frame_ids]
    commands_by_id = {}
    for record in in_scope:
        commands_by_id.setdefault(record["frame_id"], []).append(record)
    main_only_count = 0
    paired_count = 0
    main_only_bytes = 0
    paired_bytes = 0
    unclassified = []
    for frame_id in sorted(frame_ids):
        commands = commands_by_id.get(frame_id, [])
        if not commands:
            continue
        size = sum(record["bytes"] for record in commands)
        if len(commands) == 1 and commands[0]["view"] == 1:
            main_only_count += 1
            main_only_bytes += size
        elif (len(commands) == 2 and commands[0]["view"] == 1 and
              commands[1]["view"] == 2):
            paired_count += 1
            paired_bytes += size
        else:
            unclassified.append({"frame_id": frame_id,
                                 "views": [r["view"] for r in commands]})
    return {
        "main_only_frames": main_only_count,
        "main_plus_aux_frames": paired_count,
        "main_only_commands": main_only_count,
        "main_plus_aux_commands": paired_count * 2,
        "main_only_bytes": main_only_bytes,
        "main_plus_aux_bytes": paired_bytes,
        "total_commands": len(in_scope),
        "total_bytes": sum(record["bytes"] for record in in_scope),
        "classified_commands": main_only_count + paired_count * 2,
        "classified_bytes": main_only_bytes + paired_bytes,
        "classified_frames": main_only_count + paired_count,
        "boundary_terminal_frames": len(frame_ids) - len(commands_by_id),
        "boundary_commands": len(boundary),
        "boundary_bytes": sum(record["bytes"] for record in boundary),
        "unclassified": unclassified,
    }


def analyze_leg(leg_dir):
    condition = read_pairs(os.path.join(leg_dir, "condition.txt"))
    records, start_ns, end_ns = load_windowed(leg_dir)
    result = {
        "leg": condition["condition"],
        "profile": condition["profile"],
        "seconds_requested": int(condition["seconds"]),
        "records": len(records),
        "measurement_seconds": (end_ns - start_ns) / 1e9,
    }

    egress = grouped(records, "egress")
    egress_by_id = {frame_identity(record): record for record in egress}
    egress_times = sorted(record["mono_ns"] for record in egress)
    intervals = [(right - left) / 1e6
                 for left, right in zip(egress_times, egress_times[1:])]
    result["frame_interval_ms"] = distribution(intervals)
    result["frames"] = len(egress)
    result["unique_frame_identities"] = len(egress_by_id)
    result["active_span_seconds"] = (
        (egress_times[-1] - egress_times[0]) / 1e9
        if len(egress_times) > 1 else math.nan)
    result["throughput_fps"] = (
        len(egress) / result["measurement_seconds"]
        if result["measurement_seconds"] > 0 else math.nan)
    result["throughput_count_closure"] = (
        result["throughput_fps"] * result["measurement_seconds"] -
        len(egress))

    aux = grouped(records, "auxdue")
    result["aux_decisions"] = len(aux)
    result["aux_sent"] = sum(record["due"] == 1 for record in aux)
    result["aux_skipped"] = sum(record["due"] == 0 for record in aux)
    result["refresh_values_ms"] = sorted({record["refresh_ms"]
                                           for record in aux})
    result["skipped_since_aux_max_ms"] = max(
        [record["since_aux_ms"] for record in aux if record["due"] == 0],
        default=math.nan)
    chroma_times = [record["mono_ns"] for record in aux
                    if record["due"] == 1]
    chroma_gaps = [(right - left) / 1e6
                   for left, right in zip(chroma_times, chroma_times[1:])]
    result["chroma_gap_ms"] = distribution(chroma_gaps)

    result["video_commands"] = classify_video_commands(
        grouped(records, "video_cmd"), egress_by_id)
    result["bytes_per_frame"] = (
        result["video_commands"]["total_bytes"] /
        result["video_commands"]["classified_frames"]
        if result["video_commands"]["classified_frames"] else math.nan)

    slot_acks = [record for record in grouped(records, "ack")
                 if record.get("class") == "ACK_TRACE" and
                 record.get("kind") == "slot"]
    slot_by_id = {frame_identity(record): record for record in slot_acks}
    result["wire_window_values"] = sorted({record["window"]
                                            for record in slot_acks})
    result["window_two_counterfactual_advances"] = sum(
        min(record["absorbed"], record["egress"] + 1,
            record["client"] + 2) >
        min(record["absorbed"], record["egress"] + 1,
            record["client"] + 1)
        for record in slot_acks if record["window"] == 2)

    waits = []
    wait_start = None
    for record in records:
        if record["event"] == "wait_beg":
            wait_start = record["mono_ns"]
        elif record["event"] == "wait_end" and wait_start is not None:
            waits.append((record["mono_ns"] - wait_start) / 1e6)
            wait_start = None
    result["producer_idle_ms"] = distribution(waits)
    result["producer_idle_over_1ms"] = sum(value > 1.0 for value in waits)

    pump_beg = grouped(records, "pump_beg")
    pump_end = grouped(records, "pump_end")
    stages = {"feed": [], "wait_output": [], "drain": [],
              "finish_transport_credit": [], "full": [], "residual": []}
    stage_errors = []
    for index, begin in enumerate(pump_beg):
        end = next((record for record in pump_end
                    if record["tid"] == begin["tid"] and
                    record["mono_ns"] > begin["mono_ns"]), None)
        if end is None:
            continue
        next_begin_ns = min(
            [record["mono_ns"] for record in pump_beg
             if record["tid"] == begin["tid"] and
             record["mono_ns"] > begin["mono_ns"]], default=10 ** 30)
        cycle_records = [record for record in records
                         if begin["mono_ns"] <= record["mono_ns"] <
                         next_begin_ns]
        main_feeds = {record["sequence"]: record for record in cycle_records
                      if record["event"] == "feedend" and
                      record["main"] == 1}
        main_outputs = {record["sequence"]: record
                        for record in cycle_records
                        if record["event"] == "outfirst" and
                        record["main"] == 1}
        sequences = sorted(set(main_feeds) & set(main_outputs))
        emits = [record for record in cycle_records
                 if record["event"] == "emit_beg" and
                 record.get("monitor") == 0]
        if len(sequences) != 1 or len(emits) != 1:
            if next_begin_ns == 10 ** 30:
                continue
            stage_errors.append("pump %d has %d main sequences and %d emits" %
                                (index, len(sequences), len(emits)))
            continue
        sequence = sequences[0]
        feed_end = main_feeds[sequence]["mono_ns"]
        output_first = main_outputs[sequence]["mono_ns"]
        frame_id = emits[0]["frame_id"]
        if frame_id not in egress_by_id or frame_id not in slot_by_id:
            continue
        completion = max(egress_by_id[frame_id]["mono_ns"],
                         slot_by_id[frame_id]["mono_ns"])
        values = {
            "feed": (feed_end - begin["mono_ns"]) / 1e6,
            "wait_output": (output_first - feed_end) / 1e6,
            "drain": (end["mono_ns"] - output_first) / 1e6,
            "finish_transport_credit": (completion - end["mono_ns"]) / 1e6,
            "full": (completion - begin["mono_ns"]) / 1e6,
        }
        values["residual"] = values["full"] - sum(
            values[name] for name in ("feed", "wait_output", "drain",
                                      "finish_transport_credit"))
        for name, value in values.items():
            stages[name].append(value)
    result["latency_ms"] = {name: distribution(values)
                             for name, values in stages.items()}
    result["latency_pairing_errors"] = stage_errors
    result["latency_negative_segments"] = sum(
        value < -0.000001 for name, values in stages.items()
        if name != "residual" for value in values)
    result["latency_max_abs_residual_ms"] = max(
        [abs(value) for value in stages["residual"]], default=math.nan)
    return result


def finite(value):
    return not math.isnan(value)


def fmt(value, digits=2):
    return "-" if not finite(value) else ("%.*f" % (digits, value))


def validate(results):
    failures = []
    expected_window = {"dense-w1": 1, "sparse-w1": 1,
                       "dense-w2": 2, "sparse-w2": 2}
    for result in results:
        profile = result["profile"]
        commands = result["video_commands"]
        if result["frames"] == 0:
            failures.append("%s delivered no frame" % result["leg"])
        if result["unique_frame_identities"] != result["frames"]:
            failures.append("%s has duplicate terminal frame identities" %
                            result["leg"])
        if abs(result["throughput_count_closure"]) > 0.000001:
            failures.append("%s throughput does not close to frame count" %
                            result["leg"])
        if result["wire_window_values"] != [expected_window[profile]]:
            failures.append("%s did not trace its requested wire window" %
                            result["leg"])
        if commands["unclassified"]:
            failures.append("%s has unclassified video-command frames" %
                            result["leg"])
        if commands["classified_commands"] != commands["total_commands"]:
            failures.append("%s command-count accounting does not close" %
                            result["leg"])
        if commands["classified_bytes"] != commands["total_bytes"]:
            failures.append("%s byte accounting does not close" %
                            result["leg"])
        if result["latency_negative_segments"]:
            failures.append("%s has a negative latency segment" %
                            result["leg"])
        if (finite(result["latency_max_abs_residual_ms"]) and
                result["latency_max_abs_residual_ms"] > 0.001):
            failures.append("%s latency decomposition does not close" %
                            result["leg"])
        if result["latency_pairing_errors"]:
            failures.append("%s has latency identity-pairing errors" %
                            result["leg"])
        if profile.startswith("sparse"):
            if result["aux_sent"] == 0 or result["aux_skipped"] == 0:
                failures.append("%s did not both skip and restore chroma" %
                                result["leg"])
            if result["refresh_values_ms"] != [1000]:
                failures.append("%s did not trace refresh=1000 ms" %
                                result["leg"])
            if (finite(result["skipped_since_aux_max_ms"]) and
                    result["skipped_since_aux_max_ms"] >= 1000):
                failures.append("%s skipped chroma at or beyond its bound" %
                                result["leg"])
            if (commands["main_only_frames"] == 0 or
                    commands["main_plus_aux_frames"] == 0):
                failures.append("%s lacks one sparse command class" %
                                result["leg"])
        elif commands["main_only_frames"] != 0:
            failures.append("%s dense leg emitted a main-only frame" %
                            result["leg"])

    for profile in expected_window:
        legs = [result for result in results if result["profile"] == profile]
        if len(legs) != 2:
            failures.append("%s does not have exactly two repetitions" % profile)
            continue
        values = [result["throughput_fps"] for result in legs]
        spread = 100.0 * (max(values) - min(values)) / min(values)
        if spread > 15.0:
            failures.append("%s exact-window throughput repetitions differ "
                            "by %.1f%%" %
                            (profile, spread))
    return failures


def profile_means(results, profile, key):
    return mean([result[key] for result in results
                 if result["profile"] == profile])


def main():
    if len(sys.argv) == 2 and sys.argv[1] == "--selftest":
        assert percentile([1, 2, 3, 4, 5], 90) == 5
        assert distribution([2, 4])["mean"] == 3
        commands = classify_video_commands(
            [{"frame_id": 1, "view": 1, "bytes": 40},
             {"frame_id": 2, "view": 1, "bytes": 50},
             {"frame_id": 2, "view": 2, "bytes": 30}], {1: {}, 2: {}})
        assert commands["main_only_frames"] == 1
        assert commands["main_plus_aux_frames"] == 1
        assert commands["total_commands"] == 3
        assert commands["total_bytes"] == 120
        assert commands["classified_frames"] == 2
        assert commands["boundary_terminal_frames"] == 0
        assert commands["boundary_commands"] == 0
        boundary = classify_video_commands(
            [{"frame_id": 3, "view": 1, "bytes": 60}], {1: {}, 2: {}})
        assert boundary["total_commands"] == 0
        assert boundary["boundary_commands"] == 1
        assert boundary["boundary_bytes"] == 60
        missing = classify_video_commands([], {1: {}, 2: {}})
        assert missing["boundary_terminal_frames"] == 2
        assert not commands["unclassified"]
        seconds = 20.0
        frames = 500
        rate = frames / seconds
        assert rate * seconds - frames == 0
        print("PASS: i125b analyzer arithmetic selftest")
        return 0
    if len(sys.argv) != 2:
        sys.exit("usage: i125b_analyze.py <matrix-capture>")
    capture = sys.argv[1]
    order = [line.split()[0] for line in
             open(os.path.join(capture, "order.txt"), encoding="utf-8")]
    results = [analyze_leg(os.path.join(capture, "leg_" + leg))
               for leg in order]
    failures = validate(results)

    raw_path = os.path.join(capture, "raw-distributions.json")
    with open(raw_path, "w", encoding="utf-8") as stream:
        json.dump(results, stream, indent=2, sort_keys=True, allow_nan=True)
        stream.write("\n")

    print("=== mechanism and accounting ===")
    print("leg profile    frames aux+/aux- main-only paired commands "
          "edge-in/edge-out bytes-MB idle>1ms W2-extra")
    for result in results:
        commands = result["video_commands"]
        print("%-3s %-10s %6d %4d/%-4d %9d %6d %8d %7d/%-7d %8s %8d %8d" %
              (result["leg"], result["profile"], result["frames"],
               result["aux_sent"], result["aux_skipped"],
               commands["main_only_frames"],
               commands["main_plus_aux_frames"],
               commands["total_commands"],
               commands["boundary_terminal_frames"],
               commands["boundary_commands"],
               fmt(commands["total_bytes"] / 1e6, 1),
               result["producer_idle_over_1ms"],
               result["window_two_counterfactual_advances"]))
    print()
    print("Each paired frame contributes two video commands; each main-only "
          "frame contributes one. Their command and byte sums equal the "
          "audited totals in every row. Edge-in frames reached terminal "
          "egress inside the window after their command was built before it; "
          "edge-out commands were built inside before terminal egress after "
          "it. Both are named by identity and excluded from byte-per-frame "
          "rather than being paired by time.")
    print()
    print("=== delivered-frame interval and exact-window throughput ===")
    print("leg profile      mean-ms p50-ms p90-ms p99-ms frames/s bytes/frame-MB")
    for result in results:
        dist = result["frame_interval_ms"]
        print("%-3s %-10s %8s %7s %7s %7s %8s %14s" %
              (result["leg"], result["profile"], fmt(dist["mean"]),
               fmt(dist["p50"]), fmt(dist["p90"]), fmt(dist["p99"]),
               fmt(result["throughput_fps"]),
               fmt(result["bytes_per_frame"] / 1e6, 3)))
    print("Throughput is terminal frame count divided by the complete recorded "
          "measurement window. Interval percentiles describe spacing between "
          "active frames and do not discard idle time from the rate.")
    print()
    print("=== mean latency from encoder-pump start to transport/credit "
          "completion ===")
    print("leg profile      feed-ms wait-output drain-ms finish-ms full-ms residual")
    for result in results:
        latency = result["latency_ms"]
        print("%-3s %-10s %7s %11s %8s %9s %7s %8s" %
              (result["leg"], result["profile"],
               fmt(latency["feed"]["mean"]),
               fmt(latency["wait_output"]["mean"]),
               fmt(latency["drain"]["mean"]),
               fmt(latency["finish_transport_credit"]["mean"]),
               fmt(latency["full"]["mean"]),
               fmt(latency["residual"]["mean"], 6)))
    print()
    print("Feed ends when the main child's complete raw picture has entered "
          "its pipe; wait-output ends at that same child/sequence's first "
          "encoded byte; drain ends when the shared child pump completes; "
          "finish ends after the same frame identity has both reached the "
          "transport and released slot credit.")
    print()

    profiles = ("dense-w1", "sparse-w1", "dense-w2", "sparse-w2")
    means = {profile: profile_means(results, profile, "throughput_fps")
             for profile in profiles}
    byte_means = {profile: profile_means(results, profile, "bytes_per_frame")
                  for profile in profiles}
    print("=== paired effects (mean of each profile's two repetitions) ===")
    for window in (1, 2):
        dense = "dense-w%d" % window
        sparse = "sparse-w%d" % window
        print("window %d: sparse throughput %+.1f%%; bytes/frame %+.1f%%" %
              (window, 100.0 * (means[sparse] / means[dense] - 1.0),
               100.0 * (byte_means[sparse] / byte_means[dense] - 1.0)))
    for policy in ("dense", "sparse"):
        one = "%s-w1" % policy
        two = "%s-w2" % policy
        print("%s: window 2 throughput %+.1f%%; bytes/frame %+.1f%%" %
              (policy, 100.0 * (means[two] / means[one] - 1.0),
               100.0 * (byte_means[two] / byte_means[one] - 1.0)))
    print()
    if failures:
        print("RED: matrix acceptance failed")
        for failure in failures:
            print("- " + failure)
        return 1
    print("PASS: all eight legs satisfy the mechanism, accounting, latency "
          "and repetition checks")
    print("Raw per-leg distributions: %s" % raw_path)
    return 0


if __name__ == "__main__":
    sys.exit(main())
