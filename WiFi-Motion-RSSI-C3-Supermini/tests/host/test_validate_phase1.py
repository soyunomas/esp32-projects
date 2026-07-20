import importlib.util
import tempfile
import unittest
from pathlib import Path


SCRIPT = Path(__file__).parents[2] / "tools" / "validate_phase1.py"
SPEC = importlib.util.spec_from_file_location("validate_phase1", SCRIPT)
assert SPEC and SPEC.loader
validate_phase1 = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(validate_phase1)


HEADER = (
    "uptime_ms,sample_ok,rssi_dbm,read_error,rssi_changed,"
    "query_interval_ms,rssi_change_interval_ms,rssi_unchanged_ms,"
    "baseline,baseline_stddev,score,threshold,state,transition,calibrated,"
    "queries,samples_ok,read_errors,schedule_misses,repeated_samples,"
    "rssi_changes,disconnects,reconnects,channel,bssid"
)


def row(uptime: int, ok: int = 1, repeated: int = 0, changes: int = 1) -> str:
    return (
        f"{uptime},{ok},-60,,1,100,100,0,0.1,0.1,0.2,0.5,idle,0,1,"
        f"1,1,0,0,{repeated},{changes},0,0,6,00:11:22:33:44:55"
    )


class ValidatePhase1Tests(unittest.TestCase):
    def load(self, contents: str):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "capture.log"
            path.write_text(contents, encoding="utf-8")
            return validate_phase1.load_rows(path)

    def test_ignores_monitor_logs_and_accepts_fifteen_minutes(self):
        rows = self.load(
            "I (10) boot: log\n" + HEADER + "\n" + row(1000) + "\n"
            + "W (20) wifi: log\n" + row(901000, repeated=1, changes=2) + "\n"
        )
        _, failures = validate_phase1.validate(rows)
        self.assertEqual([], failures)

    def test_rejects_short_capture_and_reset(self):
        rows = self.load(HEADER + "\n" + row(1000) + "\n" + row(500) + "\n")
        _, failures = validate_phase1.validate(rows)
        self.assertTrue(any("15 minutos" in failure for failure in failures))
        self.assertTrue(any("reinicios" in failure for failure in failures))


if __name__ == "__main__":
    unittest.main()
