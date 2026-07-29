#!/usr/bin/env python3

import importlib.util
import pathlib
import unittest

MODULE_PATH = (
    pathlib.Path(__file__).resolve().parents[2]
    / "tools"
    / "summarize_capture.py"
)
SPEC = importlib.util.spec_from_file_location("summarize_capture", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class SummarizeCaptureTests(unittest.TestCase):
    def test_robust_reference_statistics(self) -> None:
        records = [
            {
                "schema": MODULE.SCHEMA,
                "type": "ap",
                "scan_id": 1,
                "ssid": "A",
                "bssid": "00:00:00:00:00:01",
                "channel": 1,
                "rssi": -50,
            },
            {
                "schema": MODULE.SCHEMA,
                "type": "scan",
                "scan_id": 1,
                "duration_ms": 100,
                "free_heap": 1000,
                "minimum_free_heap": 900,
                "dropped_events": 0,
                "truncated": False,
            },
            {
                "schema": MODULE.SCHEMA,
                "type": "ap",
                "scan_id": 2,
                "ssid": "A",
                "bssid": "00:00:00:00:00:01",
                "channel": 1,
                "rssi": -54,
            },
            {
                "schema": MODULE.SCHEMA,
                "type": "scan",
                "scan_id": 2,
                "duration_ms": 102,
                "free_heap": 996,
                "minimum_free_heap": 900,
                "dropped_events": 0,
                "truncated": False,
            },
        ]

        result = MODULE.summarize(records)
        self.assertEqual(result["scan_count"], 2)
        self.assertEqual(result["duration_ms"]["median"], 101.0)
        reference = result["references"][0]
        self.assertEqual(reference["presence_ratio"], 1.0)
        self.assertEqual(reference["rssi_median"], -52.0)
        self.assertEqual(reference["rssi_mad"], 2.0)
        self.assertEqual(reference["adjacent_rssi_change"]["samples"], 1)
        self.assertEqual(reference["adjacent_rssi_change"]["median_abs"], 4.0)
        self.assertEqual(reference["adjacent_rssi_change"]["p90_abs"], 4.0)
        self.assertEqual(reference["adjacent_rssi_change"]["max_abs"], 4)
        self.assertEqual(reference["half_comparison"]["first_median"], -50.0)
        self.assertEqual(reference["half_comparison"]["second_median"], -54.0)
        self.assertEqual(reference["half_comparison"]["median_shift"], -4.0)

    def test_adjacent_change_ignores_missing_scan(self) -> None:
        records = [
            {
                "schema": MODULE.SCHEMA,
                "type": "ap",
                "scan_id": 1,
                "bssid": "00:00:00:00:00:01",
                "rssi": -50,
            },
            {"schema": MODULE.SCHEMA, "type": "scan", "scan_id": 1},
            {"schema": MODULE.SCHEMA, "type": "scan", "scan_id": 2},
            {
                "schema": MODULE.SCHEMA,
                "type": "ap",
                "scan_id": 3,
                "bssid": "00:00:00:00:00:01",
                "rssi": -60,
            },
            {"schema": MODULE.SCHEMA, "type": "scan", "scan_id": 3},
        ]

        reference = MODULE.summarize(records)["references"][0]
        self.assertEqual(reference["adjacent_rssi_change"]["samples"], 0)
        self.assertIsNone(
            reference["adjacent_rssi_change"]["median_abs"]
        )

    def test_empty_capture(self) -> None:
        result = MODULE.summarize([])
        self.assertEqual(result["scan_count"], 0)
        self.assertEqual(result["references"], [])
        self.assertIsNone(result["duration_ms"]["median"])


if __name__ == "__main__":
    unittest.main()
