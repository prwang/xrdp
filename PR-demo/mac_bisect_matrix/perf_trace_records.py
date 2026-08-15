#!/usr/bin/env python3
"""Generic reader for common/perf_trace's named key/value records."""

import re


KEY = re.compile(r"^[a-z][a-z0-9_]*$")
STATIC_VALUE = re.compile(r"^[A-Za-z][A-Za-z0-9_]*$")


def parse_line(line, path="<stream>", line_number=0):
    """Return one named record, or None for a blank line.

    The producer accepts integer payloads and static token values only. A
    malformed or incompatible line is a failed trace, not an event to skip.
    """
    line = line.rstrip("\n")
    if not line:
        return None
    record = {}
    for token in line.split(" "):
        if "=" not in token:
            raise ValueError("%s:%d: token is not key=value: %r" %
                             (path, line_number, token))
        key, value = token.split("=", 1)
        if not KEY.fullmatch(key) or not value or key in record:
            raise ValueError("%s:%d: invalid or duplicate key %r" %
                             (path, line_number, key))
        try:
            record[key] = int(value)
        except ValueError:
            if not STATIC_VALUE.fullmatch(value):
                raise ValueError("%s:%d: %s has invalid token value %r" %
                                 (path, line_number, key, value))
            record[key] = value
    for key in ("schema", "mono_ns", "pid", "tid", "event"):
        if key not in record:
            raise ValueError("%s:%d: missing common key %s" %
                             (path, line_number, key))
    if record["schema"] != 1:
        raise ValueError("%s:%d: unsupported schema %s" %
                         (path, line_number, record["schema"]))
    return record


def read_records(path):
    """Read a complete trace. Malformed lines terminate the analysis."""
    records = []
    with open(path, errors="replace") as stream:
        for line_number, line in enumerate(stream, 1):
            record = parse_line(line, path, line_number)
            if record is not None:
                records.append(record)
    return records
