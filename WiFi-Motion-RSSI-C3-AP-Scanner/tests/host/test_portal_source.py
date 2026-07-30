#!/usr/bin/env python3
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PORTAL = (
    ROOT / "components" / "maintenance_portal" / "maintenance_portal.c"
).read_text(encoding="utf-8")
MAIN = (ROOT / "main" / "app_main.c").read_text(encoding="utf-8")
KCONFIG = (ROOT / "main" / "Kconfig.projbuild").read_text(encoding="utf-8")
SDK_DEFAULTS = (ROOT / "sdkconfig.defaults").read_text(encoding="utf-8")


class PortalSourceTests(unittest.TestCase):
    def test_live_chart_and_status_endpoint_are_present(self):
        self.assertIn("id=motion_chart", PORTAL)
        self.assertIn('fetch(\'/status\'', PORTAL)
        self.assertIn('.uri = "/status"', PORTAL)
        self.assertIn("MOVIMIENTO DETECTADO", PORTAL)

    def test_captive_requests_redirect_to_root(self):
        self.assertIn("captive_redirect_handler", PORTAL)
        self.assertIn('.uri = "/*"', PORTAL)
        self.assertIn('"302 Found"', PORTAL)
        self.assertIn('"Location", "/"', PORTAL)

    def test_captive_login_uses_a_first_party_session(self):
        self.assertIn("Accede para ver la detección", PORTAL)
        self.assertIn('.uri = "/login"', PORTAL)
        self.assertIn("motion_session=", PORTAL)
        self.assertIn("HttpOnly; SameSite=Strict", PORTAL)
        self.assertIn("request_authenticated(request)", PORTAL)

    def test_portal_runs_while_detector_keeps_scanning(self):
        self.assertIn("maintenance_portal_update_detector(", MAIN)
        self.assertIn("maintenance_portal_start(", MAIN)
        self.assertNotIn(
            "if (maintenance_portal_active()) {", MAIN
        )

    def test_adaptive_threshold_confirmation_is_visible(self):
        self.assertIn("trigger_count", PORTAL)
        self.assertIn("trigger_required", PORTAL)
        self.assertIn("Confirmación", PORTAL)
        self.assertIn("result->trigger_score_x100", MAIN)

    def test_delayed_calibration_has_countdown_and_progress(self):
        self.assertIn("id=calibration_delay", PORTAL)
        self.assertIn("Salir y calibrar", PORTAL)
        self.assertIn('.uri = "/calibrate"', PORTAL)
        self.assertIn("calibration_remaining_seconds", PORTAL)
        self.assertIn("calibration_completed_scans", PORTAL)
        self.assertIn("maintenance_portal_take_calibration_request()", MAIN)
        self.assertIn("runtime_config.calibration_scans", MAIN)

    def test_detection_settings_are_simple_closed_profiles(self):
        for label in (
            "Sensitivity",
            "Motion confirmation",
            "Detection speed",
            "Motion state duration",
            "Calibration duration",
        ):
            self.assertIn(label, PORTAL)
        self.assertIn("sensitivity_label:'Sensibilidad'", PORTAL)
        self.assertIn("confirmation_label:'Confirmación de movimiento'", PORTAL)
        self.assertIn("speed_label:'Velocidad de detección'", PORTAL)
        for value in ("value=15", "value=25", "value=40"):
            self.assertIn(value, PORTAL)
        self.assertNotIn("name=release_score", PORTAL)
        self.assertNotIn("name=adaptive_sigma", PORTAL)
        self.assertNotIn("name=passive_dwell", PORTAL)

    def test_configuration_actions_have_distinct_reference_behavior(self):
        self.assertIn("id=save_config", PORTAL)
        self.assertIn("configuration", PORTAL)
        self.assertIn("Save and recalibrate", PORTAL)
        self.assertIn("Restore detection settings", PORTAL)
        self.assertIn("save_config:'Guardar ", PORTAL)
        self.assertIn("save_recalibrate:'Guardar y recalibrar'", PORTAL)
        self.assertIn('.uri = "/restore-detection"', PORTAL)
        restore = PORTAL[
            PORTAL.index("static esp_err_t restore_detection_handler"):
            PORTAL.index("static esp_err_t apply_handler")
        ]
        self.assertNotIn("reference_store_erase", restore)

    def test_runtime_uses_persisted_detection_values_and_derived_thresholds(self):
        self.assertIn("runtime_config.trigger_score_x100", MAIN)
        self.assertIn("runtime_config.trigger_consecutive", MAIN)
        self.assertIn("runtime_config.inter_scan_delay_ms", MAIN)
        self.assertIn("probe_config_release_score_x100(", MAIN)
        self.assertIn("probe_config_clear_consecutive(", MAIN)

    def test_effective_fresh_install_defaults_are_one_and_twenty_five(self):
        self.assertIn(
            "CONFIG_PROBE_DETECTOR_TRIGGER_CONSECUTIVE=1", SDK_DEFAULTS
        )
        self.assertIn("CONFIG_PROBE_CALIBRATION_SCANS=25", SDK_DEFAULTS)
        self.assertIn(
            'config PROBE_DETECTOR_TRIGGER_CONSECUTIVE\n'
            '    int "Consecutive high scores required for motion"\n'
            "    range 1 3\n"
            "    default 1",
            KCONFIG,
        )

    def test_history_clock_downloads_and_bilingual_ui_are_present(self):
        self.assertIn('.uri = "/clock"', PORTAL)
        self.assertIn('.uri = "/events"', PORTAL)
        self.assertIn('.uri = "/events.json"', PORTAL)
        self.assertIn('.uri = "/events.csv"', PORTAL)
        self.assertIn("MOTION_HISTORY_CAPACITY", PORTAL)
        self.assertIn("id=history_rows", PORTAL)
        self.assertIn("type=datetime-local", PORTAL)
        self.assertIn("href='/?lang=en'", PORTAL)
        self.assertIn("href='/?lang=es'", PORTAL)
        self.assertIn("const LANG='%s'", PORTAL)
        self.assertIn("request_spanish(request)", PORTAL)
        self.assertIn(
            'config PROBE_CALIBRATION_SCANS\n'
            '    int "Full-channel scans used for reference calibration"\n'
            "    range 15 40\n"
            "    default 25",
            KCONFIG,
        )


if __name__ == "__main__":
    unittest.main()
