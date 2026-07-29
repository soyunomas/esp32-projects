#!/usr/bin/env python3

import importlib.util
import pathlib
import unittest

MODULE_PATH = (
    pathlib.Path(__file__).resolve().parents[2] / "tools" / "capture_jsonl.py"
)
SPEC = importlib.util.spec_from_file_location("capture_jsonl", MODULE_PATH)
assert SPEC is not None and SPEC.loader is not None
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class CaptureJsonlTests(unittest.TestCase):
    def test_accepts_probe_record(self) -> None:
        record = MODULE.parse_telemetry_line(
            b'{"schema":"wifi_ap_scan/v1","type":"scan","scan_id":1}\r\n'
        )
        self.assertIsNotNone(record)
        self.assertEqual(record["scan_id"], 1)

    def test_rejects_logs_invalid_json_and_other_schema(self) -> None:
        self.assertIsNone(MODULE.parse_telemetry_line(b"I (20) boot: log"))
        self.assertIsNone(MODULE.parse_telemetry_line(b"{broken"))
        self.assertIsNone(
            MODULE.parse_telemetry_line(
                b'{"schema":"another/v1","type":"scan"}'
            )
        )

    def test_scan_bundle_collector_accepts_only_complete_scans(self) -> None:
        collector = MODULE.ScanBundleCollector()
        ap = {"schema": MODULE.SCHEMA, "type": "ap", "scan_id": 7}
        scan = {
            "schema": MODULE.SCHEMA,
            "type": "scan",
            "scan_id": 7,
            "emitted_aps": 1,
        }
        self.assertEqual(collector.accept(ap), [])
        self.assertEqual(collector.accept(scan), [ap, scan])
        self.assertEqual(collector.incomplete_scans, 0)

        incomplete = {
            "schema": MODULE.SCHEMA,
            "type": "scan",
            "scan_id": 8,
            "emitted_aps": 2,
        }
        self.assertEqual(collector.accept(incomplete), [])
        self.assertEqual(collector.incomplete_scans, 1)


if __name__ == "__main__":
    unittest.main()
