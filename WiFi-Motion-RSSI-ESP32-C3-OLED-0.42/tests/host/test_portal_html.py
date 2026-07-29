import ast
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
PORTAL_SOURCE = PROJECT_ROOT / "components" / "config_portal" / "config_portal.c"
AUTH_SOURCE = PROJECT_ROOT / "components" / "portal_auth" / "portal_auth.c"


def embedded_index_html() -> str:
    source = PORTAL_SOURCE.read_text(encoding="utf-8")
    declaration = source.split("static const char INDEX_HTML[] =", 1)[1]
    block = declaration.split(";\n\nstatic cJSON", 1)[0]
    literals = re.findall(r'"(?:\\.|[^"\\])*"', block)
    return "".join(ast.literal_eval(value) for value in literals)


class PortalHtmlTests(unittest.TestCase):
    def test_embedded_javascript_has_valid_syntax(self):
        node = shutil.which("node")
        if node is None:
            self.skipTest("node is not installed")
        html = embedded_index_html()
        script = html.split("<script>", 1)[1].split("</script>", 1)[0]
        with tempfile.NamedTemporaryFile(
            mode="w", suffix=".js", encoding="utf-8"
        ) as temporary:
            temporary.write(script)
            temporary.flush()
            result = subprocess.run(
                [node, "--check", temporary.name],
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
            )
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_javascript_does_not_depend_on_named_window_properties(self):
        html = embedded_index_html()
        script = html.split("<script>", 1)[1].split("</script>", 1)[0]
        self.assertIn("document.getElementById", script)
        self.assertNotRegex(script, r"(?<![\w.])import\.onclick")
        self.assertNotRegex(script, r"(?<![\w.])status\.textContent")

    def test_interface_is_bilingual_with_english_default(self):
        html = embedded_index_html()
        self.assertIn("<html lang=en>", html)
        self.assertIn("id=lang_en", html)
        self.assertIn("id=lang_es", html)
        self.assertIn(">English</button>", html)
        self.assertIn(">Español</button>", html)
        self.assertIn("Administrator access", html)
        self.assertIn("Acceso de administrador", html)
        self.assertIn("localStorage.getItem('motion_language')", html)
        self.assertIn("localStorage.setItem('motion_language',language)", html)
        self.assertNotIn("navigator.language", html)
        self.assertIn("document.documentElement.lang=language", html)
        self.assertIn("language==='es'?'es-ES':'en-GB'", html)

        dictionaries = re.search(
            r"const T=\{en:\{(.*?)\},es:\{(.*?)\}\};function loadLanguage",
            html,
            re.DOTALL,
        )
        self.assertIsNotNone(dictionaries)
        key_pattern = re.compile(r"(?:^|,)([a-zA-Z_]\w*):")
        english_keys = set(key_pattern.findall(dictionaries.group(1)))
        spanish_keys = set(key_pattern.findall(dictionaries.group(2)))
        self.assertEqual(english_keys, spanish_keys)

        referenced_keys = set(re.findall(r"data-i18n=([a-zA-Z_]\w*)", html))
        referenced_keys.update(re.findall(r"data-i18n-aria=([a-zA-Z_]\w*)", html))
        referenced_keys.update(
            re.findall(r"data-i18n-placeholder=([a-zA-Z_]\w*)", html)
        )
        referenced_keys.update(re.findall(r"data-help-key=([a-zA-Z_]\w*)", html))
        referenced_keys.update(re.findall(r"data-aria-key=([a-zA-Z_]\w*)", html))
        self.assertFalse(referenced_keys - english_keys)

    def test_portal_is_mobile_friendly_and_touch_accessible(self):
        html = embedded_index_html()
        self.assertIn("width=device-width,initial-scale=1", html)
        self.assertIn("*{box-sizing:border-box}", html)
        self.assertIn("@media(max-width:700px)", html)
        self.assertIn("@media(max-width:380px)", html)
        self.assertIn("min-height:44px", html)
        self.assertIn("touch-action:manipulation", html)
        self.assertIn(".chart-controls{grid-template-columns:1fr}", html)
        self.assertIn(".wifi-networks button{align-items:flex-start;flex-direction:column", html)
        self.assertIn(".save-bar{position:static", html)
        self.assertIn("w=Math.max(240,box.width)", html)

    def test_motion_indicator_has_accessible_states(self):
        html = embedded_index_html()
        self.assertIn("id=motion_card", html)
        self.assertIn("aria-live=polite", html)
        self.assertIn("MOVIMIENTO DETECTADO", html)
        for state in ("motion", "idle", "calibrating", "error"):
            self.assertIn(f".motion-card.{state}", html)

    def test_chart_has_time_axis_and_detection_markers(self):
        html = embedded_index_html()
        self.assertIn("id=motion_chart", html)
        self.assertIn("HISTORY_MS=120000", html)
        self.assertIn("toLocaleTimeString", html)
        self.assertIn("hit=motion&&!lastMotion", html)
        self.assertIn("detecciones visibles", html)
        self.assertIn("Score CSI", html)
        self.assertIn("csi_detector", html)
        self.assertIn("csiHit=csiMotion&&!lastCsiMotion", html)

    def test_chart_series_can_be_toggled_accessibly(self):
        html = embedded_index_html()
        self.assertIn("data-i18n-aria=visible_chart_elements", html)
        for toggle in (
            "show_rssi_score",
            "show_rssi_threshold",
            "show_csi_score",
            "show_csi_threshold",
            "show_rssi_hits",
            "show_csi_hits",
        ):
            self.assertIn(f"id={toggle} type=checkbox", html)
        self.assertIn("ui[id].onchange=drawChart", html)

    def test_chart_elements_have_contextual_help(self):
        html = embedded_index_html()
        self.assertEqual(html.count("class=help type=button"), 10)
        self.assertEqual(html.count("data-help-key="), 10)
        self.assertEqual(html.count("data-aria-key="), 10)
        self.assertIn("id=chart_help", html)
        self.assertIn("b.onfocus=show", html)

    def test_detector_configuration_has_explicit_save_feedback(self):
        html = embedded_index_html()
        self.assertIn("id=save_config type=submit", html)
        self.assertIn("Save Wi-Fi and detector; restart", html)
        self.assertIn("Save Telegram settings", html)
        self.assertIn("saved_reconnect", html)
        self.assertIn("pending_changes", html)
        self.assertIn("ui.save_config.disabled=true", html)
        self.assertIn("id=config_save_status", html)
        self.assertIn("data-target=config_help", html)
        self.assertIn("id=detection_source", html)
        self.assertIn("data-i18n=rssi_only", html)
        self.assertIn("data-i18n=csi_only", html)
        self.assertIn("data-i18n=rssi_or_csi", html)
        self.assertNotIn("CSI (experimental)", html)
        self.assertNotIn("CSI sombra", html)
        self.assertIn("d.source=ui.detection_source.value", html)
        self.assertIn("schema_version:4", html)
        self.assertIn("r.selected_state", html)

    def test_manual_calibration_has_a_delayed_device_request(self):
        html = embedded_index_html()
        self.assertIn("id=calibration_delay", html)
        for delay in (10, 20, 30, 60):
            self.assertIn(f"value={delay}", html)
        self.assertIn("id=start_calibration", html)
        self.assertIn("/api/calibrate", html)
        self.assertIn("calibration_remaining_ms", html)
        self.assertIn("aria-live=polite", html)

    def test_admin_login_and_password_change_are_accessible(self):
        html = embedded_index_html()
        for element in (
            "id=login_panel",
            "id=login_form",
            "id=login_username",
            "id=login_password",
            "id=logout",
            "id=password_form",
            "id=current_admin_password",
            "id=new_admin_password",
        ):
            self.assertIn(element, html)
        self.assertIn("/api/login", html)
        self.assertIn("/api/session", html)
        self.assertIn("/api/logout", html)
        self.assertIn("/api/password", html)
        self.assertIn("default_password", html)
        self.assertIn("autocomplete=current-password", html)
        self.assertIn("autocomplete=new-password", html)

    def test_browser_keeps_session_in_httponly_cookie(self):
        html = embedded_index_html()
        self.assertEqual(html.count("localStorage.getItem("), 1)
        self.assertEqual(html.count("localStorage.setItem("), 1)
        self.assertNotIn("sessionStorage", html)
        self.assertNotIn("Authorization", html)
        self.assertIn("authFetch('/api/diagnostics'", html)
        source = PORTAL_SOURCE.read_text(encoding="utf-8")
        self.assertIn("HttpOnly; SameSite=Strict", source)
        self.assertIn('"Cache-Control", "no-store"', source)

    def test_password_is_salted_and_stretched_in_persistent_storage(self):
        source = AUTH_SOURCE.read_text(encoding="utf-8")
        self.assertIn("AUTH_PBKDF2_ITERATIONS 20000U", source)
        self.assertIn("esp_fill_random(candidate.salt", source)
        self.assertIn("mbedtls_pkcs5_pbkdf2_hmac_ext", source)
        self.assertIn("mbedtls_ct_memcmp", source)
        self.assertIn("AUTH_FAILURE_LIMIT 5U", source)
        self.assertNotRegex(source, r"char\s+password\s*\[")

    def test_wifi_scan_selector_has_safe_accessible_states(self):
        html = embedded_index_html()
        for element in (
            "id=scan_wifi",
            "id=wifi_scan_status",
            "id=wifi_networks",
            "id=wifi_password_status",
        ):
            self.assertIn(element, html)
        self.assertIn("data-i18n-aria=wifi_networks_aria", html)
        self.assertIn("/api/wifi/scan", html)
        self.assertIn("method:'POST'", html)
        self.assertIn("document.createElement('button')", html)
        self.assertIn("b.disabled=!n.supported", html)
        self.assertIn("b.setAttribute('aria-pressed'", html)
        self.assertIn("meta.textContent=n.rssi+' dBm", html)
        self.assertIn("selectedNetworkSecure", html)
        self.assertIn("tr('new_network_password')", html)

    def test_wifi_scan_is_async_and_guards_detector_inputs(self):
        portal_source = PORTAL_SOURCE.read_text(encoding="utf-8")
        main_source = (PROJECT_ROOT / "main" / "app_main.c").read_text(
            encoding="utf-8"
        )
        self.assertIn("esp_wifi_scan_start(NULL, false)", portal_source)
        self.assertIn("WIFI_EVENT_SCAN_DONE", portal_source)
        self.assertIn("WIFI_SCAN_MAX_RESULTS 20U", portal_source)
        self.assertIn("WIFI_SCAN_DETECTOR_GUARD_MS 1000LL", portal_source)
        self.assertIn("config_portal_wifi_scan_active(timestamp_ms)", main_source)
        self.assertIn("!detector_inputs_paused", main_source)

    def test_recovery_mode_has_captive_dns_and_http_redirect(self):
        main_source = (PROJECT_ROOT / "main" / "app_main.c").read_text(
            encoding="utf-8"
        )
        portal_source = PORTAL_SOURCE.read_text(encoding="utf-8")
        dns_source = (
            PROJECT_ROOT / "components" / "captive_dns" / "captive_dns.c"
        ).read_text(encoding="utf-8")
        self.assertIn("captive_dns_start(wifi_ap_netif)", main_source)
        self.assertIn("RECOVERY_BUTTON_HOLD_MS 5000U", main_source)
        self.assertIn("RECOVERY_BUTTON_REQUESTED", main_source)
        self.assertIn("force_recovery", main_source)
        self.assertIn("uri_match_fn = httpd_uri_match_wildcard", portal_source)
        self.assertIn('.uri = "/*"', portal_source)
        self.assertIn("current_provisioning_mode", portal_source)
        self.assertIn("SOCK_DGRAM", dns_source)
        self.assertIn("DNS_PORT 53", dns_source)

    def test_float_configuration_values_are_aligned_to_input_steps(self):
        html = embedded_index_html()
        self.assertIn("function setNumberInput(input,value)", html)
        self.assertIn("Math.round(Number(value)/step)*step", html)
        self.assertIn("setNumberInput(ui[k],d[k])", html)
        node = shutil.which("node")
        if node is None:
            self.skipTest("node is not installed")
        function_source = re.search(
            r"function setNumberInput\(input,value\)\{[^}]+\}", html
        ).group(0)
        javascript = (
            function_source
            + ";let minimum={step:'.01',value:''},baseline={step:'.0001',value:''};"
            + "setNumberInput(minimum,0.310000002384186);"
            + "setNumberInput(baseline,0.0098999999463558);"
            + "console.log(minimum.value+'|'+baseline.value);"
        )
        result = subprocess.run(
            [node, "-e", javascript],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            check=False,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.strip(), "0.31|0.0099")

    def test_telegram_configuration_never_reads_the_token_back(self):
        html = embedded_index_html()
        for element in (
            "id=telegram_form",
            "id=telegram_enabled",
            "id=telegram_token",
            "id=telegram_clear_token",
            "id=telegram_chat_id",
            "id=save_telegram",
            "id=test_telegram",
            "id=telegram_status",
        ):
            self.assertIn(element, html)
        self.assertIn("/api/telegram/test", html)
        self.assertIn("t.token_set", html)
        self.assertIn("ui.telegram_token.value=''", html)
        portal_source = PORTAL_SOURCE.read_text(encoding="utf-8")
        telegram_json = portal_source.split("static cJSON *telegram_json", 1)[1]
        telegram_json = telegram_json.split("static esp_err_t", 1)[0]
        self.assertNotIn('"token"', telegram_json)

    def test_telegram_transport_uses_validated_https_and_a_queue(self):
        source = (
            PROJECT_ROOT
            / "components"
            / "telegram_notifier"
            / "telegram_notifier.c"
        ).read_text(encoding="utf-8")
        self.assertIn('"https://api.telegram.org/bot%s/sendMessage"', source)
        self.assertIn("esp_crt_bundle_attach", source)
        self.assertIn("disable_auto_redirect = true", source)
        self.assertIn("xQueueCreate", source)
        self.assertIn("xQueueSend(notification_queue, &item, 0)", source)
        self.assertIn("TELEGRAM_COOLDOWN_MS 30000LL", source)
        self.assertNotIn("http://api.telegram.org", source)


if __name__ == "__main__":
    unittest.main()
