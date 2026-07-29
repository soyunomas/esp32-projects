#!/usr/bin/env python3
import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[2]
PORTAL = (
    ROOT / "components" / "maintenance_portal" / "maintenance_portal.c"
).read_text(encoding="utf-8")
MAIN = (ROOT / "main" / "app_main.c").read_text(encoding="utf-8")


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


if __name__ == "__main__":
    unittest.main()
