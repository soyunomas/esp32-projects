import contextlib
import csv
import io
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest

TOOLS = Path(__file__).resolve().parents[2] / "tools"
sys.path.insert(0, str(TOOLS))
import experiment  # noqa: E402


RAW_FIELDS = [
    "uptime_ms",
    "sample_ok",
    "rssi_dbm",
    "rssi_changed",
    "algorithm",
    "baseline_mode",
    "profile",
    "baseline",
    "state",
    "calibrated",
    "csi_temporal_delta",
]


def write_raw(path: Path, count: int = 180) -> None:
    with path.open("w", newline="", encoding="utf-8") as destination:
        writer = csv.DictWriter(destination, fieldnames=RAW_FIELDS)
        writer.writeheader()
        for index in range(count):
            truth_motion = 145 <= index < 160
            false_motion = 170 <= index < 172
            active = (147 <= index < 162) or false_motion
            rssi = -50
            if truth_motion:
                rssi += (index % 5) * 3 - 6
            writer.writerow({
                "uptime_ms": 1000 + index * 100,
                "sample_ok": 1,
                "rssi_dbm": rssi,
                "rssi_changed": int(index > 0 and rssi != -50),
                "algorithm": "mean_absolute_difference",
                "baseline_mode": "mean_stddev",
                "profile": "balanced",
                "baseline": f"{0.1 + index / 10000:.4f}",
                "state": "motion" if active else (
                    "idle" if index >= 20 else "calibrating"
                ),
                "calibrated": int(index >= 20),
                "csi_temporal_delta": abs(rssi + 50) / 10,
            })


class ExperimentTests(unittest.TestCase):
    def test_serial_header_matches_the_expected_telemetry_schema(self):
        header = ",".join(experiment.TELEMETRY_FIELDS) + "\n"
        parsed, rows = experiment.telemetry_rows([
            (header, 0),
            (",".join("0" for _ in experiment.TELEMETRY_FIELDS) + "\n", 100),
        ])
        self.assertEqual(tuple(parsed), experiment.TELEMETRY_FIELDS)
        self.assertEqual(len(rows), 1)

    def capture(self, directory: Path) -> Path:
        raw = directory / "raw.log"
        output = directory / "session.csv"
        write_raw(raw)
        arguments = [
            "capture",
            "--input", str(raw),
            "--output", str(output),
            "--session-id", "session-a",
            "--split", "evaluation",
            "--scenario", "link_crossing",
            "--event", "cross-1:14.5:16.0",
            "--distance-m", "3.2",
            "--height-m", "1.1",
            "--orientation", "usb-up",
            "--placement", "shelf-a",
        ]
        with contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(experiment.main(arguments), 0)
        return output

    def test_capture_labels_and_manifest(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = self.capture(Path(temporary))
            rows = experiment.read_experiment(output)
            self.assertEqual(len(rows), 180)
            self.assertEqual(sum(row["truth_active"] == "1" for row in rows), 15)
            event = next(row for row in rows if row["truth_active"] == "1")
            self.assertEqual(event["event_start_ms"], "14500")
            self.assertEqual(event["event_end_ms"], "16000")
            manifest = json.loads(output.with_suffix(".json").read_text())
            self.assertFalse(manifest["contains_personal_data"])
            self.assertEqual(manifest["split"], "evaluation")
            self.assertEqual(len(manifest["rssi_sha256"]), 64)
            self.assertEqual(manifest["source"]["event_clock"],
                             "device_uptime")

    def test_live_rows_use_host_arrival_clock_for_labels(self):
        header = "uptime_ms,sample_ok,rssi_dbm,state,calibrated\n"
        lines = [
            (header, 0),
            ("1000,1,-50,idle,1\n", 0),
            ("50000,1,-42,active,1\n", 15000),
            ("50100,1,-50,idle,1\n", 22000),
        ]
        _, rows = experiment.telemetry_rows(lines)
        labeled = experiment.label_rows(
            rows, "live-a", "tuning", "link_crossing",
            [("cross-1", 14000, 20000)],
        )
        self.assertEqual([row["elapsed_ms"] for row in labeled],
                         ["0", "15000", "22000"])
        self.assertEqual([row["truth_active"] for row in labeled],
                         ["0", "1", "0"])

    def test_interactive_event_commands_record_exact_boundaries(self):
        events = []
        active = {}
        self.assertIn("event_started", experiment.apply_event_command(
            "event-start cross-1", 1234, events, active
        ))
        self.assertIn("event_finished", experiment.apply_event_command(
            "event-end cross-1", 4567, events, active
        ))
        self.assertEqual(events, [("cross-1", 1234, 4567)])
        self.assertEqual(active, {})
        with self.assertRaises(ValueError):
            experiment.apply_event_command(
                "event-end missing", 5000, events, active
            )
        self.assertTrue(experiment.capture_stop_requested("capture-stop", {}))
        self.assertFalse(experiment.capture_stop_requested("event-start x", {}))
        with self.assertRaises(ValueError):
            experiment.capture_stop_requested("capture-stop", {"x": 1})

    def test_physical_marker_events_use_persistent_state(self):
        rows = [
            {"_capture_elapsed_ms": "0", "marker_event_id": "0",
             "marker_active": "0"},
            {"_capture_elapsed_ms": "1200", "marker_event_id": "1",
             "marker_active": "1"},
            {"_capture_elapsed_ms": "2200", "marker_event_id": "1",
             "marker_active": "1"},
            {"_capture_elapsed_ms": "4500", "marker_event_id": "1",
             "marker_active": "0"},
        ]
        self.assertEqual(experiment.marker_events_from_rows(rows), [
            ("boot-1", 1200, 4500),
        ])
        with self.assertRaises(ValueError):
            experiment.marker_events_from_rows(rows[:-1])

    def test_metrics_and_replay_comparison(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = self.capture(Path(temporary))
            rows = experiment.read_experiment(output)
            metrics = experiment.evaluate_rows(rows)
            self.assertEqual(metrics["events"], 1)
            self.assertEqual(metrics["detected_events"], 1)
            self.assertEqual(metrics["activation_latency_median_ms"], 200)
            self.assertEqual(metrics["release_latency_median_ms"], 200)
            self.assertEqual(metrics["false_activations"], 1)

            binary = os.environ.get("REPLAY_BINARY")
            if not binary:
                self.skipTest("REPLAY_BINARY is not configured")
            report = Path(temporary) / "comparison.json"
            with contextlib.redirect_stdout(io.StringIO()):
                result = experiment.main([
                    "compare",
                    "--input", str(output),
                    "--split", "evaluation",
                    "--replay-binary", binary,
                    "--output", str(report),
                ])
            self.assertEqual(result, 0)
            comparison = json.loads(report.read_text())
            self.assertEqual(len(comparison["combinations"]), 30)
            self.assertEqual(comparison["sample_field"], "rssi_dbm")

            csi_report = Path(temporary) / "csi-comparison.json"
            self.assertEqual(experiment.main([
                "compare",
                "--input", str(output),
                "--sample-field", "csi_temporal_delta",
                "--replay-binary", binary,
                "--output", str(csi_report),
            ]), 0)
            csi_comparison = json.loads(csi_report.read_text())
            self.assertEqual(csi_comparison["sample_field"],
                             "csi_temporal_delta")
            self.assertTrue(all(
                item["sample_field"] == "csi_temporal_delta"
                for item in csi_comparison["combinations"]
            ))

            override_report = Path(temporary) / "override-comparison.json"
            self.assertEqual(experiment.main([
                "compare",
                "--input", str(output),
                "--sample-field", "csi_temporal_delta",
                "--window", "10",
                "--sigma", "2.5",
                "--minimum-threshold", "0.01",
                "--replay-binary", binary,
                "--output", str(override_report),
            ]), 0)
            self.assertEqual(
                json.loads(override_report.read_text())["detector_overrides"],
                {"window": 10, "sigma": 2.5, "minimum_threshold": 0.01},
            )

    def test_evaluate_creates_output_directory(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            output = self.capture(root)
            report = root / "new" / "nested" / "evaluation.json"
            self.assertEqual(experiment.main([
                "evaluate",
                "--input", str(output),
                "--split", "evaluation",
                "--output", str(report),
            ]), 0)
            self.assertEqual(json.loads(report.read_text())["selected_split"],
                             "evaluation")


if __name__ == "__main__":
    unittest.main()
