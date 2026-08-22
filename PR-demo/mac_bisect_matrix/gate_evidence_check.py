#!/usr/bin/env python3
"""Deterministic identity and trace-presence checks for e_gate_run.sh."""

import argparse


def check_identity(requested_arm, selected_pod, selected_arm,
                   dialed_pod, dialed_arm):
    errors = []
    if selected_arm != requested_arm:
        errors.append("requested arm %s selected pod %s labelled %s" %
                      (requested_arm, selected_pod, selected_arm))
    if dialed_arm != requested_arm:
        errors.append("dialled endpoint belongs to arm %s, not %s" %
                      (dialed_arm, requested_arm))
    if dialed_pod != selected_pod:
        errors.append("dialled endpoint reaches pod %s but logs would be "
                      "collected from %s" % (dialed_pod, selected_pod))
    if errors:
        raise ValueError("; ".join(errors))


def minimum_records(seconds):
    if seconds < 1:
        raise ValueError("run duration must be positive")
    return max(30, seconds * 5)


def check_records(seconds, records):
    required = minimum_records(seconds)
    if records < required:
        raise ValueError("only %d classed trace records for a %ds run; "
                         "need at least %d" %
                         (records, seconds, required))
    return required


def main():
    parser = argparse.ArgumentParser()
    subparsers = parser.add_subparsers(dest="command", required=True)
    identity = subparsers.add_parser("identity")
    identity.add_argument("--requested-arm", required=True)
    identity.add_argument("--selected-pod", required=True)
    identity.add_argument("--selected-arm", required=True)
    identity.add_argument("--dialed-pod", required=True)
    identity.add_argument("--dialed-arm", required=True)
    records = subparsers.add_parser("records")
    records.add_argument("--seconds", type=int, required=True)
    records.add_argument("--records", type=int, required=True)
    args = parser.parse_args()
    try:
        if args.command == "identity":
            check_identity(args.requested_arm, args.selected_pod,
                           args.selected_arm, args.dialed_pod,
                           args.dialed_arm)
            print("identity OK: arm=%s pod=%s" %
                  (args.requested_arm, args.selected_pod))
        else:
            required = check_records(args.seconds, args.records)
            print("trace presence OK: %d records (minimum %d for %ds)" %
                  (args.records, required, args.seconds))
    except ValueError as exc:
        parser.exit(1, "evidence check: %s\n" % exc)


if __name__ == "__main__":
    main()
