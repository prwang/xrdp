"""Offline report checks: expectations describe explicit transaction order."""
import contextlib
import io
import unittest

from i142_wire_transition import report_process, COMMANDS


class WireTransitionTests(unittest.TestCase):
    def test_reused_frame_id_does_not_join_login_and_desktop(self):
        records = [
            dict(event="wire_tx", seq=1, cmd=11, frame=1, mono_ns=1),
            dict(event="wire_tx", seq=2, cmd=12, frame=1, mono_ns=2),
            dict(event="wire_ack", frame=90, last_tx=2, mono_ns=3),
            dict(event="wire_ack", frame=1, last_tx=2, mono_ns=4),
            dict(event="wire_tx", seq=3, cmd=11, frame=1, mono_ns=5),
            dict(event="wire_tx", seq=4, cmd=12, frame=1, mono_ns=6),
            dict(event="wire_caps_begin", caps_seq=2, last_tx=4,
                 mono_ns=1000006),
        ]
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            report_process("fixture", 7, records)
        text = output.getvalue()
        self.assertIn("followed tx 4 by 1.000 ms", text)
        self.assertIn("Latest acknowledgement: frame 1, received after tx 2", text)
        self.assertIn("  tx 3 start frame", text)
        self.assertNotIn("  tx 1 start frame", text)

    def test_confirm_command_number(self):
        self.assertEqual(COMMANDS[19], "confirm capabilities")
        self.assertEqual(COMMANDS[13], "client frame acknowledgement")
        self.assertEqual(COMMANDS[8], "evict cache entry")


if __name__ == "__main__":
    unittest.main()
