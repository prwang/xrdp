#!/usr/bin/env python3

import unittest

from gate_evidence_check import check_identity, check_records


class GateEvidenceCheckTests(unittest.TestCase):

    def test_positive_identity_and_record_count(self):
        check_identity("x040", "x040-pod", "x040", "x040-pod", "x040")
        self.assertEqual(check_records(20, 100), 100)

    def test_wrong_target_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "dialled endpoint"):
            check_identity("x040", "x040-pod", "x040",
                           "x041-pod", "x041")

    def test_empty_trace_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "only 0"):
            check_records(20, 0)

    def test_too_short_trace_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "need at least 100"):
            check_records(20, 99)


if __name__ == "__main__":
    unittest.main()
