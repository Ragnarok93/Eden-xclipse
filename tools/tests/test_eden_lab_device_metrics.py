# SPDX-License-Identifier: GPL-3.0-or-later
"""Mock ADB read-only data collection and unavailable sensor interpretation."""
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from eden_lab_device_metrics import capture, metric


class DeviceStub:
    serial = "SM-S731U-readonly"

    def __init__(self):
        self.requests = []

    def call(self, *args, **kwargs):
        self.requests.append((args, kwargs))
        return {
            ("get-state",): "device\n",
            ("shell", "pidof", "dev.eden.eden_emulator.debug"): "1999\n",
            ("shell", "dumpsys", "meminfo", "dev.eden.eden_emulator.debug"):
                "TOTAL PSS: 123,456    TOTAL RSS: 987,654\n",
            ("shell", "dumpsys", "thermalservice"):
                "Thermal Status: 3\n",
            ("shell", "dumpsys", "battery"):
                "  level: 79\n  temperature: 353\n",
            ("shell", "cat", "/proc/uptime"): "904.56 120.0\n",
        }[args]


class ReadOnlyTelemetryTests(unittest.TestCase):
    def test_metrics_label_unsupported_gpu_sensor(self):
        device = DeviceStub()
        result = capture(device)
        self.assertEqual(result["package_pss_kib"]["value"], 123456)
        # Some Android versions print TOTAL RSS on the same line, and that
        # layout must be explicitly handled (or marked unavailable).
        self.assertEqual(result["battery_temperature_c"]["value"], 35.3)
        self.assertEqual(result["thermal_severity_android"]["value"], 3)
        self.assertEqual(result["battery_level_pct"]["value"], 79)
        self.assertEqual(result["device_uptime_seconds"]["value"], 904.56)
        self.assertFalse(result["gpu_die_temperature_c"]["available"])
        self.assertTrue(result["package_process_present"])
        self.assertFalse(result["collector_has_mutated_device"])
        for args, _ in device.requests:
            self.assertEqual(args[0], "get-state" if len(args) == 1 else "shell")
            self.assertNotIn("rm", args)
            self.assertNotIn("am", args)
            self.assertNotIn("settings", args)

    def test_missing_or_invalid_metric_remains_unavailable(self):
        for raw in ("", "permission denied", "Thermal Status: -1",
                    "Thermal Status: 999"):
            self.assertFalse(metric(raw, r"Thermal Status:\s*([0-9]+)",
                                    max_value=6)["available"])

    def test_host_clock_and_android_clock_are_not_conflated(self):
        result = capture(DeviceStub())
        self.assertTrue(result["host_monotonic_start_ns"] <=
                        result["host_monotonic_end_ns"])
        self.assertIn("not Android elapsedRealtimeNanos",
                      result["clock_domain_note"])


if __name__ == "__main__":
    unittest.main()
