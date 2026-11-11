"""Host tests for HIL result integrity; no serial devices are opened."""
import contextlib
import io
import os
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, os.path.dirname(__file__))
import hil_test_suite as hil


class HarnessTests(unittest.TestCase):
    def test_missing_device_is_skipped_without_discovery(self):
        with patch.dict(os.environ, {}, clear=True), patch.object(sys, "argv", ["hil"]), \
             patch.object(hil, "find_bus_pirate_port", side_effect=AssertionError("unexpected discovery")):
            self.assertEqual(hil.main(), 77)

    def test_explicit_mock_is_labeled(self):
        output = io.StringIO()
        with patch.object(sys, "argv", ["hil", "--mock", "--cycles", "0", "--soak", "0"]), \
             patch.object(hil.time, "sleep"), contextlib.redirect_stdout(output):
            with self.assertRaises(SystemExit) as result:
                hil.main()
        self.assertEqual(result.exception.code, 0)
        self.assertIn("MOCK HARNESS Result: ALL PASS", output.getvalue())

    def test_wrong_mode_prompt_is_failure(self):
        harness = hil.HilHarness(mock=True)
        harness.send_cmd = lambda *args, **kwargs: "HiZ> "
        self.assertFalse(harness.run_mode_cycling(2))

    def test_health_requires_numeric_fields_and_stack_margin(self):
        port = hil.MockSerialPort()
        port.write(b"health")
        response = port.read(4096).decode()
        self.assertGreater(hil.HilHarness.health_sample(response)["core1"][0], 0)
        with self.assertRaises(ValueError):
            hil.HilHarness.health_sample("Health OK > ")
        with self.assertRaises(ValueError):
            hil.HilHarness.health_sample(response.replace("stack free: 2048", "stack free: 0"))

    def test_soak_detects_stalled_counters(self):
        harness = hil.HilHarness(mock=True)
        port = hil.MockSerialPort()
        port.write(b"health")
        response = port.read(4096).decode()
        harness.send_cmd = lambda *args, **kwargs: response
        with patch.object(hil.time, "monotonic", side_effect=[0, 0, 1]), patch.object(hil.time, "sleep"):
            self.assertFalse(harness.run_soak(2))


if __name__ == "__main__":
    unittest.main()
