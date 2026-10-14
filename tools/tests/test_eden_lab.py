# SPDX-License-Identifier: GPL-3.0-or-later
"""Host-only Eden lab safety tests; no device or Android SDK required."""
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import eden_lab


def request(operation="probe", **kwargs):
    return {"protocol_version": 1, "run_id": "a" * 32,
            "operation": operation, **kwargs}


class ProfileValidationTests(unittest.TestCase):
    def test_read_only_request(self):
        self.assertEqual(eden_lab.validate_request(request()), request())

    def test_roundtrip_explicit_false_and_empty(self):
        data = request("sandbox-roundtrip", config={
            "measurement_tag": "", "aggregate_telemetry": False})
        self.assertIs(eden_lab.validate_request(data), data)

    def test_reject_unknown_command_and_keys(self):
        for item in [request("launch"), dict(request(), arbitrary=True),
                     request("sandbox-roundtrip"),
                     request("probe", config={})]:
            with self.subTest(item=item), self.assertRaises(eden_lab.LabError):
                eden_lab.validate_request(item)

    def test_strict_types(self):
        bad = [
            dict(request(), protocol_version=True),
            dict(request(), run_id="A" * 32),
            request("sandbox-roundtrip", config={
                "measurement_tag": "", "aggregate_telemetry": "false"}),
            request("sandbox-roundtrip", config={
                "measurement_tag": "é", "aggregate_telemetry": True}),
            request("sandbox-roundtrip", config={
                "measurement_tag": "a" * 65, "aggregate_telemetry": True}),
            request("sandbox-roundtrip", config={
                "measurement_tag": "", "aggregate_telemetry": True, "extra": 1}),
        ]
        for item in bad:
            with self.subTest(item=item), self.assertRaises(eden_lab.LabError):
                eden_lab.validate_request(item)

    def test_reject_serial_injection(self):
        for serial in ["", "device; rm -rf /", "a\nb", "spaces allowed"]:
            with self.assertRaises(eden_lab.LabError):
                eden_lab.Device(serial)


class InstrumentationTests(unittest.TestCase):
    def test_parse_success(self):
        answer = {"protocol_version": 1, "run_id": "a" * 32,
                  "status": "complete", "read_only": True}
        line = "INSTRUMENTATION_STATUS: stream=LAB_RESULT=" + json.dumps(answer)
        self.assertEqual(eden_lab.parse_result(line, "a" * 32), answer)

    def test_reject_duplicate_or_incomplete_results(self):
        good = json.dumps({"protocol_version": 1, "run_id": "a" * 32,
                           "status": "complete"})
        for output in ["", "LAB_RESULT=" + good + "\nLAB_RESULT=" + good,
                       "LAB_RESULT=" + good.replace("a" * 32, "b" * 32),
                       'LAB_RESULT={"run_id":"a"}']:
            with self.subTest(output=output), self.assertRaises(eden_lab.LabError):
                eden_lab.parse_result(output, "a" * 32)

    def test_staging_only_allowed_request(self):
        dev = eden_lab.Device("device-123")
        calls = []

        def invoke(*args, data=None, **kwargs):
            calls.append((args, data))
            if "instrument" in args:
                return "LAB_RESULT=" + json.dumps({
                    "protocol_version": 1, "run_id": "a" * 32,
                    "status": "complete"})
            return ""

        with mock.patch.object(dev, "call", side_effect=invoke):
            result = dev.bridge(request())
        self.assertEqual(result["status"], "complete")
        self.assertEqual(len(calls), 3)
        self.assertIn("shell", calls[0][0])
        self.assertIn("run-as", calls[0][0])
        self.assertEqual(json.loads(calls[0][1]), request())
        self.assertIn("instrument", calls[1][0])
        self.assertIn("rm", calls[2][0])

    def test_lease_conflict(self):
        dev1, dev2 = eden_lab.Device("device-a"), eden_lab.Device("device-a")
        with dev1.lease():
            with self.assertRaises(eden_lab.LabError):
                with dev2.lease():
                    pass

    def test_cli_requires_write_authorization_without_adb(self):
        with mock.patch.object(eden_lab.Device, "info", return_value={}):
            with mock.patch.object(eden_lab.Device, "assert_idle_debug"):
                with mock.patch.object(eden_lab.Device, "bridge") as bridge:
                    self.assertEqual(eden_lab.main(["--serial", "a",
                        "sandbox-roundtrip"]), 2)
                    bridge.assert_not_called()


if __name__ == "__main__":
    unittest.main()
