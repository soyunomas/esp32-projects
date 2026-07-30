#include "maintenance_portal.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_netif.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mbedtls/base64.h"
#include "motion_history.h"
#include "probe_config.h"
#include "reference_store.h"
#include "captive_dns.h"

#define PORTAL_BODY_SIZE 3072U
#define PORTAL_PAGE_SIZE 49152U
#define PORTAL_MAX_NETWORKS 16U
#define PORTAL_MAX_CHOICES (PORTAL_MAX_NETWORKS + PROBE_CONFIG_MAX_SSIDS)

static atomic_bool portal_active;
static httpd_handle_t server;
static SemaphoreHandle_t network_mutex;
static SemaphoreHandle_t history_mutex;
typedef struct {
    uint8_t ssid[32];
    uint8_t ssid_length;
    int8_t rssi;
    uint8_t channel;
    uint8_t bssid_count;
} portal_network_t;
static portal_network_t networks[PORTAL_MAX_NETWORKS];
static size_t network_count;
static char portal_page[PORTAL_PAGE_SIZE];
static char portal_options[8192];
static char portal_selected[1024];
static char portal_body[PORTAL_BODY_SIZE];
static char runtime_username[PROBE_CONFIG_ADMIN_USER_MAX_LENGTH + 1U];
static char runtime_password[PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH + 1U];
static char csrf_token[17];
static char session_token[33];
static atomic_uint runtime_scan_id;
static atomic_uint runtime_state = MULTIREF_STATE_CALIBRATING;
static atomic_uint runtime_score_x100;
static atomic_uint runtime_trigger_x100;
static atomic_uint runtime_coverage_permille;
static atomic_uint runtime_observed_references;
static atomic_uint runtime_reference_count;
static atomic_uint runtime_trigger_count;
static atomic_uint runtime_trigger_required;
static atomic_llong runtime_updated_ms;
static atomic_llong calibration_deadline_ms;
static atomic_bool calibration_running;
static atomic_uint calibration_completed_scans;
static atomic_uint calibration_target_scans;
static motion_history_t motion_history;
static motion_clock_t motion_clock;
static motion_history_event_t
    history_snapshot[MOTION_HISTORY_CAPACITY];

void maintenance_portal_update_detector(
    uint32_t scan_id,
    const multiref_result_t *result)
{
    if (result == NULL) {
        return;
    }
    atomic_store(&runtime_scan_id, scan_id);
    atomic_store(&runtime_state, (unsigned)result->state);
    atomic_store(&runtime_score_x100, result->score_x100);
    atomic_store(&runtime_trigger_x100, result->trigger_score_x100);
    atomic_store(&runtime_coverage_permille, result->coverage_permille);
    atomic_store(&runtime_observed_references,
                 result->observed_references);
    atomic_store(&runtime_reference_count, result->reference_count);
    atomic_store(&runtime_trigger_count, result->trigger_count);
    atomic_store(&runtime_trigger_required, result->trigger_required);
    atomic_store(&runtime_updated_ms, esp_timer_get_time() / 1000);
    if (history_mutex != NULL &&
        xSemaphoreTake(history_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        const int64_t now_ms = esp_timer_get_time() / 1000;
        if (result->state == MULTIREF_STATE_MOTION) {
            if (result->state_changed) {
                motion_history_start(
                    &motion_history,
                    scan_id,
                    now_ms,
                    result->score_x100,
                    result->trigger_score_x100,
                    result->coverage_permille);
            }
            motion_history_update(&motion_history,
                                  scan_id,
                                  result->score_x100,
                                  result->coverage_permille);
        } else if (result->state_changed &&
                   result->previous_state == MULTIREF_STATE_MOTION) {
            motion_history_finish(&motion_history, scan_id, now_ms);
        }
        xSemaphoreGive(history_mutex);
    }
}

bool maintenance_portal_take_calibration_request(void)
{
    long long deadline = atomic_load(&calibration_deadline_ms);
    const long long now = esp_timer_get_time() / 1000;
    return deadline > 0 && now >= deadline &&
           atomic_compare_exchange_strong(
               &calibration_deadline_ms, &deadline, 0);
}

void maintenance_portal_update_calibration(
    bool running, uint16_t completed_scans, uint16_t target_scans)
{
    atomic_store(&calibration_running, running);
    atomic_store(&calibration_completed_scans, completed_scans);
    atomic_store(&calibration_target_scans, target_scans);
    if (running) {
        atomic_store(&runtime_state, MULTIREF_STATE_CALIBRATING);
        atomic_store(&runtime_score_x100, 0U);
        atomic_store(&runtime_trigger_count, 0U);
        atomic_store(&runtime_updated_ms, esp_timer_get_time() / 1000);
    }
}

static bool constant_time_equal(const char *left,
                                size_t left_length,
                                const char *right,
                                size_t right_length)
{
    const size_t maximum =
        left_length > right_length ? left_length : right_length;
    unsigned difference = (unsigned)(left_length ^ right_length);
    for (size_t index = 0U; index < maximum; ++index) {
        const unsigned left_byte =
            index < left_length ? (unsigned char)left[index] : 0U;
        const unsigned right_byte =
            index < right_length ? (unsigned char)right[index] : 0U;
        difference |= left_byte ^ right_byte;
    }
    return difference == 0U;
}

static bool cookie_authenticated(httpd_req_t *request)
{
    const size_t cookie_length =
        httpd_req_get_hdr_value_len(request, "Cookie");
    char cookie[192];
    if (cookie_length == 0U || cookie_length >= sizeof(cookie) ||
        httpd_req_get_hdr_value_str(
            request, "Cookie", cookie, sizeof(cookie)) != ESP_OK) {
        return false;
    }
    const char *cursor = cookie;
    const char prefix[] = "motion_session=";
    while ((cursor = strstr(cursor, prefix)) != NULL) {
        if ((cursor == cookie || cursor[-1] == ' ' || cursor[-1] == ';')) {
            const char *value = cursor + sizeof(prefix) - 1U;
            const size_t length = strcspn(value, "; ");
            return constant_time_equal(value,
                                       length,
                                       session_token,
                                       strlen(session_token));
        }
        cursor += sizeof(prefix) - 1U;
    }
    return false;
}

static bool basic_authenticated(httpd_req_t *request)
{
    const size_t header_length =
        httpd_req_get_hdr_value_len(request, "Authorization");
    char header[160];
    if (header_length == 0U || header_length >= sizeof(header) ||
        httpd_req_get_hdr_value_str(
            request, "Authorization", header, sizeof(header)) != ESP_OK ||
        strncmp(header, "Basic ", 6U) != 0) {
        return false;
    }
    unsigned char decoded[80];
    size_t decoded_length = 0U;
    if (mbedtls_base64_decode(
            decoded,
            sizeof(decoded),
            &decoded_length,
            (const unsigned char *)header + 6U,
            strlen(header + 6U)) != 0) {
        decoded_length = 0U;
    }
    char expected[PROBE_CONFIG_ADMIN_USER_MAX_LENGTH +
                  PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH + 2U];
    const int expected_length = snprintf(
        expected, sizeof(expected), "%s:%s",
        runtime_username, runtime_password);
    if (expected_length < 0 ||
        !constant_time_equal((const char *)decoded,
                             decoded_length,
                             expected,
                             (size_t)expected_length)) {
        return false;
    }
    return true;
}

static bool request_authenticated(httpd_req_t *request)
{
    return cookie_authenticated(request) || basic_authenticated(request);
}

static bool authenticated(httpd_req_t *request)
{
    if (request_authenticated(request)) {
        return true;
    }
    httpd_resp_set_status(request, "401 Unauthorized");
    (void)httpd_resp_sendstr(request, "Sesion no valida");
    return false;
}

static size_t html_escape(char *output,
                          size_t capacity,
                          const uint8_t *input,
                          size_t length)
{
    size_t written = 0U;
    for (size_t index = 0U; index < length; ++index) {
        const char *replacement = NULL;
        switch (input[index]) {
        case '&': replacement = "&amp;"; break;
        case '"': replacement = "&quot;"; break;
        case '<': replacement = "&lt;"; break;
        case '>': replacement = "&gt;"; break;
        default: break;
        }
        const char fallback[2] = {(char)input[index], '\0'};
        const char *text =
            replacement != NULL ? replacement : fallback;
        const size_t text_length = strlen(text);
        if (written + text_length < capacity) {
            memcpy(output + written, text, text_length);
        }
        written += text_length;
    }
    if (capacity > 0U) {
        output[written < capacity ? written : capacity - 1U] = '\0';
    }
    return written;
}

static void rebuild_networks(const wifi_ap_record_t *records,
                             size_t record_count)
{
    if (records == NULL || network_mutex == NULL ||
        xSemaphoreTake(network_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    network_count = 0U;
    memset(networks, 0, sizeof(networks));
    for (size_t record_index = 0U;
         record_index < record_count;
         ++record_index) {
        const wifi_ap_record_t *record = &records[record_index];
        const size_t ssid_length =
            strnlen((const char *)record->ssid, sizeof(record->ssid));
        if (ssid_length == 0U) {
            continue;
        }
        size_t network_index = 0U;
        for (; network_index < network_count; ++network_index) {
            if (networks[network_index].ssid_length == ssid_length &&
                memcmp(networks[network_index].ssid,
                       record->ssid,
                       ssid_length) == 0) {
                if (record->rssi > networks[network_index].rssi) {
                    networks[network_index].rssi = record->rssi;
                    networks[network_index].channel = record->primary;
                }
                if (networks[network_index].bssid_count < UINT8_MAX) {
                    networks[network_index].bssid_count++;
                }
                break;
            }
        }
        if (network_index == network_count &&
            network_count < PORTAL_MAX_NETWORKS) {
            portal_network_t *network = &networks[network_count++];
            memcpy(network->ssid, record->ssid, ssid_length);
            network->ssid_length = (uint8_t)ssid_length;
            network->rssi = record->rssi;
            network->channel = record->primary;
            network->bssid_count = 1U;
        }
    }
    xSemaphoreGive(network_mutex);
}

void maintenance_portal_update_networks(
    const wifi_ap_record_t *records, size_t record_count)
{
    rebuild_networks(records, record_count);
}

static bool hex_value(char character, uint8_t *value)
{
    if (character >= '0' && character <= '9') {
        *value = (uint8_t)(character - '0');
        return true;
    }
    if (character >= 'A' && character <= 'F') {
        *value = (uint8_t)(character - 'A' + 10);
        return true;
    }
    if (character >= 'a' && character <= 'f') {
        *value = (uint8_t)(character - 'a' + 10);
        return true;
    }
    return false;
}

static bool url_decode(char *text)
{
    size_t read = 0U;
    size_t write = 0U;
    while (text[read] != '\0') {
        if (text[read] == '+') {
            text[write++] = ' ';
            read++;
        } else if (text[read] == '%') {
            uint8_t high;
            uint8_t low;
            if (text[read + 1U] == '\0' ||
                text[read + 2U] == '\0' ||
                !hex_value(text[read + 1U], &high) ||
                !hex_value(text[read + 2U], &low)) {
                return false;
            }
            text[write++] = (char)((high << 4U) | low);
            read += 3U;
        } else {
            text[write++] = text[read++];
        }
    }
    text[write] = '\0';
    return true;
}

static bool append_text(char *output,
                        size_t capacity,
                        size_t *length,
                        const char *text)
{
    const size_t text_length = strlen(text);
    if (*length + text_length >= capacity) {
        return false;
    }
    memcpy(output + *length, text, text_length);
    *length += text_length;
    output[*length] = '\0';
    return true;
}

static bool append_json_string(char *output,
                               size_t capacity,
                               size_t *output_length,
                               const uint8_t *input,
                               size_t input_length)
{
    if (!append_text(output, capacity, output_length, "\"")) {
        return false;
    }
    for (size_t index = 0U; index < input_length; ++index) {
        char escaped[8];
        if (input[index] == '"' || input[index] == '\\') {
            (void)snprintf(
                escaped, sizeof(escaped), "\\%c", input[index]);
        } else if (input[index] < 0x20U) {
            (void)snprintf(
                escaped, sizeof(escaped), "\\u%04x", input[index]);
        } else {
            escaped[0] = (char)input[index];
            escaped[1] = '\0';
        }
        if (!append_text(output, capacity, output_length, escaped)) {
            return false;
        }
    }
    return append_text(output, capacity, output_length, "\"");
}

static bool build_network_json_unlocked(void)
{
    size_t length = 0U;
    portal_options[0] = '\0';
    if (!append_text(
            portal_options, sizeof(portal_options), &length, "[")) {
        return false;
    }
    for (size_t index = 0U; index < network_count; ++index) {
        if ((index > 0U &&
             !append_text(portal_options,
                          sizeof(portal_options),
                          &length,
                          ",")) ||
            !append_text(portal_options,
                         sizeof(portal_options),
                         &length,
                         "{\"ssid\":") ||
            !append_json_string(portal_options,
                                sizeof(portal_options),
                                &length,
                                networks[index].ssid,
                                networks[index].ssid_length)) {
            return false;
        }
        char details[96];
        (void)snprintf(details,
                       sizeof(details),
                       ",\"channel\":%u,\"rssi\":%d,\"aps\":%u}",
                       networks[index].channel,
                       networks[index].rssi,
                       networks[index].bssid_count);
        if (!append_text(portal_options,
                         sizeof(portal_options),
                         &length,
                         details)) {
            return false;
        }
    }
    return append_text(
        portal_options, sizeof(portal_options), &length, "]");
}

static bool build_network_json(void)
{
    if (network_mutex == NULL ||
        xSemaphoreTake(network_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return false;
    }
    const bool result = build_network_json_unlocked();
    xSemaphoreGive(network_mutex);
    return result;
}

static bool build_selected_json(const probe_config_blob_t *config)
{
    size_t length = 0U;
    portal_selected[0] = '\0';
    if (!append_text(
            portal_selected, sizeof(portal_selected), &length, "[")) {
        return false;
    }
    for (uint8_t index = 0U; index < config->ssid_count; ++index) {
        if ((index > 0U &&
             !append_text(portal_selected,
                          sizeof(portal_selected),
                          &length,
                          ",")) ||
            !append_json_string(portal_selected,
                                sizeof(portal_selected),
                                &length,
                                config->ssids[index].bytes,
                                config->ssids[index].length)) {
            return false;
        }
    }
    return append_text(
        portal_selected, sizeof(portal_selected), &length, "]");
}

static const char *selected_u16(uint16_t actual, uint16_t option)
{
    return actual == option ? "selected" : "";
}

static const char *selected_u8(uint8_t actual, uint8_t option)
{
    return actual == option ? "selected" : "";
}

static bool request_spanish(httpd_req_t *request)
{
    char query[48] = "";
    char language[8] = "";
    return httpd_req_get_url_query_str(
               request, query, sizeof(query)) == ESP_OK &&
           httpd_query_key_value(
               query, "lang", language, sizeof(language)) == ESP_OK &&
           strcmp(language, "es") == 0;
}

static esp_err_t login_page_handler(httpd_req_t *request,
                                    bool spanish)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    const int length = snprintf(
        portal_page,
        sizeof(portal_page),
        "<!doctype html><html lang=%s><meta name=viewport "
        "content='width=device-width,initial-scale=1'><title>Motion C3</title>"
        "<style>*{box-sizing:border-box}body{font:19px system-ui;margin:0;"
        "min-height:100vh;display:grid;place-items:center;padding:20px;"
        "background:#0b1118;color:#f5f7fa}.login{width:min(100%%,440px);"
        "background:#172331;padding:26px;border-radius:16px;box-shadow:"
        "0 12px 40px #0008}h1{margin-top:0}label{display:block;margin:18px 0}"
        "input{display:block;width:100%%;font:inherit;padding:13px;margin-top:"
        "7px;border:2px solid #789;border-radius:9px}button{width:100%%;font:"
        "bold 20px system-ui;padding:14px;background:#00a86b;color:white;"
        "border:0;border-radius:9px}.hint{color:#b9c8d8}</style>"
        "<form class=login method=post action='/login?lang=%s'>"
        "<p><a href='/?lang=en'>English</a> · "
        "<a href='/?lang=es'>Español</a></p><h1>WiFi Motion C3</h1>"
        "<p>%s</p>"
        "<label>%s<input name=username value=admin maxlength=16 "
        "autocomplete=username required></label><label>%s<input "
        "name=password type=password maxlength=32 autocomplete=current-password "
        "required autofocus></label><button>%s</button>"
        "<p class=hint>%s</p></form></html>",
        spanish ? "es" : "en",
        spanish ? "es" : "en",
        spanish
            ? "Accede para ver la detección, el historial y la configuración."
            : "Sign in to view detection, history and configuration.",
        spanish ? "Usuario" : "Username",
        spanish ? "Contraseña" : "Password",
        spanish ? "Entrar" : "Sign in",
        spanish ? "Acceso inicial: admin / admin"
                : "Initial access: admin / admin");
    if (length < 0 || (size_t)length >= sizeof(portal_page)) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "login page error");
    }
    return httpd_resp_send(request, portal_page, length);
}

static esp_err_t page_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return login_page_handler(request, request_spanish(request));
    }
    probe_config_blob_t config;
    if (probe_config_load(&config) != PROBE_CONFIG_OK) {
        probe_config_set_defaults(&config);
    }
    const bool manual = config.mode == PROBE_CONFIG_MODE_MANUAL;
    if (!build_network_json() || !build_selected_json(&config)) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "page data error");
    }
    char escaped_user[80];
    (void)html_escape(
        escaped_user,
        sizeof(escaped_user),
        (const uint8_t *)config.admin_username,
        strlen(config.admin_username));
    const int length = snprintf(
        portal_page,
        sizeof(portal_page),
        "<!doctype html><html lang=en><meta name=viewport "
        "content='width=device-width,initial-scale=1'><title>Motion C3</title>"
        "<style>*{box-sizing:border-box}body{font:18px system-ui;max-width:"
        "760px;margin:20px auto;padding:16px;background:#0b1118;color:#f5f7fa}"
        "h1{font-size:30px;margin:0 0 18px}"
        "h2{font-size:22px;margin-top:28px}.card{background:#172331;"
        "padding:18px;border-radius:14px}.mode{display:block;margin:16px 0}"
        ".detector{display:flex;align-items:center;gap:16px;padding:20px;"
        "border-radius:14px;background:#6b7280;box-shadow:0 5px 18px #0005}"
        ".detector.idle{background:#087f5b}.detector.motion{background:#c92a2a}"
        ".detector.wait{background:#a15c00}.detector.bad{background:#4b5563}"
        ".dot{width:30px;height:30px;border:5px solid white;border-radius:50%%;"
        "background:currentColor;flex:none}.detector.motion .dot{animation:pulse "
        ".7s ease-in-out infinite alternate}@keyframes pulse{to{transform:"
        "scale(1.3);opacity:.65}}.detector strong{display:block;font-size:24px}"
        ".detail{margin-top:4px;color:#fff}.chart{background:#f8fafc;color:"
        "#18212b;padding:16px;border-radius:14px;margin:14px 0}.chart h2{"
        "margin:0 0 10px}.chart-wrap{height:clamp(240px,62vw,310px)}"
        "#motion_chart{display:block;width:100%%;height:100%%}.legend{display:"
        "flex;flex-wrap:wrap;gap:14px;font-size:15px;margin-bottom:8px}.key{"
        "display:inline-flex;align-items:center;gap:7px}.swatch{width:25px;"
        "height:4px;background:#0072b2}.swatch.threshold{background:repeating-"
        "linear-gradient(90deg,#e69f00 0 7px,transparent 7px 11px)}.swatch.hit"
        "{background:#d55e00}.chart-note{color:#4b5563;font-size:15px;margin:"
        "8px 0 0}"
        ".net{display:flex;gap:12px;align-items:center;background:#243447;"
        "padding:13px;margin:10px 0;border-radius:10px}.net span{min-width:0;"
        "flex:1}.net b{display:block;"
        "overflow-wrap:anywhere}.net small{display:block;color:#b9c8d8;"
        "margin-top:4px}input[type=text],input[type=password],input[type=number],"
        "select"
        "{font-size:18px;"
        "width:100%%;padding:12px;margin-top:6px;box-sizing:border-box}"
        "button{font-size:20px;padding:14px 22px;background:#00a86b;"
        "color:white;border:0;border-radius:9px}.add,.remove{margin:0;"
        "font-size:17px;padding:10px 14px;white-space:nowrap}.remove{"
        "background:#b83b4b}.toolbar{display:flex;align-items:center;gap:14px}"
        ".primary{margin-top:22px}.calibrate{margin-top:14px;background:#b45309}"
        ".calibration-row{display:flex;gap:14px;align-items:end;flex-wrap:wrap}"
        ".calibration-row label{flex:1;min-width:190px}.calibration-row button{"
        "flex:1}.empty{padding:16px;background:#243447;"
        "border-radius:10px;color:#b9c8d8}"
        ".lang{display:flex;justify-content:flex-end;gap:10px;margin-bottom:14px}"
        ".lang a,.downloads a{color:#7dd3fc}.table-wrap{overflow:auto}"
        "table{width:100%%;border-collapse:collapse;font-size:15px}"
        "th,td{text-align:left;padding:9px;border-bottom:1px solid #405268;"
        "white-space:nowrap}.downloads{display:flex;gap:18px;flex-wrap:wrap}"
        ".hint{color:#b9c8d8}.warn{color:#ffd166}@media(prefers-reduced-motion:"
        "reduce){.detector.motion .dot{animation:none}}</style><body>"
        "<nav class=lang aria-label=Language><a href='/?lang=en'>English</a>"
        "<span aria-hidden=true>·</span><a href='/?lang=es'>Español</a></nav>"
        "<h1>WiFi Motion C3</h1>"
        "<section id=detector class='detector wait' role=status aria-live=polite>"
        "<span class=dot aria-hidden=true></span><div><strong id=motion_label>"
        "Waiting for data</strong><div id=motion_detail class=detail>"
        "The detector is starting.</div></div></section>"
        "<section class=chart><h2 id=activity_title>Live activity</h2>"
        "<div class=legend><span class=key><i class=swatch></i><span "
        "id=legend_score>Change score</span></span><span class=key><i "
        "class='swatch threshold'></i><span id=legend_threshold>Threshold</span>"
        "</span><span class=key><i class='swatch hit'></i><span "
        "id=legend_detection>Detection</span></span></div>"
        "<div class=chart-wrap><canvas id=motion_chart role=img aria-label="
        "'Timeline of score, threshold and detections'></canvas></div>"
        "<p id=chart_note class=chart-note>Waiting for samples…</p></section>"
        "<section class=card><h2 id=clock_title>Date and time</h2>"
        "<p id=clock_help>Set the local date and time after each restart. "
        "The clock and history remain in memory until the next restart.</p>"
        "<div class=calibration-row><label><span id=clock_label>Local date and "
        "time</span><input id=clock_input type=datetime-local step=1></label>"
        "<button id=set_clock type=button>Set date and time</button></div>"
        "<p id=clock_status class=hint role=status aria-live=polite>"
        "Time is not configured.</p></section>"
        "<section class=card><h2 id=history_title>Motion history</h2>"
        "<p id=history_help>Up to 128 detections from this session are kept "
        "in memory.</p><div class=table-wrap><table><thead><tr>"
        "<th id=history_time>Date and time</th><th id=history_duration>Duration"
        "</th><th id=history_peak>Peak score</th><th id=history_threshold>"
        "Threshold</th><th id=history_coverage>Coverage</th><th id=history_state>"
        "Status</th></tr></thead><tbody id=history_rows></tbody></table></div>"
        "<p id=history_empty class=empty>No detections recorded yet.</p>"
        "<p class=downloads><a id=download_json href=/events.json>Download JSON"
        "</a><a id=download_csv href=/events.csv>Download CSV</a></p></section>"
        "<section class=card><h2 id=calibration_title>Empty-room calibration</h2>"
        "<p id=calibration_help>The detector waits while you leave, then scans "
        "the networks again and learns the empty environment.</p>"
        "<div class=calibration-row><label><span id=leave_time_label>Time to "
        "leave (seconds)</span>"
        "<input id=calibration_delay type=number min=5 max=300 value=20 "
        "inputmode=numeric></label><button id=calibrate class=calibrate "
        "type=button>Leave and calibrate</button></div>"
        "<p id=calibration_status class=hint role=status aria-live=polite>"
        "No calibration is scheduled.</p></section>"
        "<form class=card method=post action=/save><h2 id=config_title>"
        "Configuration</h2>"
        "<input id=csrf type=hidden name=csrf value=%s>"
        "<label class=mode><input type=radio name=mode value=auto %s> "
        "<span id=automatic_mode><b>Automatic</b> — use the best visible "
        "networks</span></label>"
        "<label class=mode><input id=manual type=radio name=mode value=manual %s> "
        "<span id=manual_mode><b>Choose networks</b> — select up to 8</span>"
        "</label><h2 id=search_title>Find networks</h2><div class=toolbar>"
        "<button id=scan type=button>Find networks</button>"
        "<span id=status aria-live=polite></span></div>"
        "<div id=results></div>"
        "<h2><span id=chosen_title>Selected networks</span> "
        "<span id=count></span></h2>"
        "<div id=chosen></div><div id=inputs></div>"
        "<p id=network_hint class=hint>Press <b>+ Add</b> on each reference "
        "network. The device never joins them or needs their password.</p>"
        "<h2 id=detection_settings_title>Detection settings</h2>"
        "<p id=detection_settings_help class=hint>They apply after restart and "
        "do not need recalibration. Changing selected networks does.</p>"
        "<label><span id=sensitivity_label>Sensitivity</span><select "
        "name=trigger_score><option id=sensitivity_sensitive value=200 %s>"
        "Sensitive (2.00)</option><option id=sensitivity_balanced value=250 %s>"
        "Balanced (2.50)</option><option id=sensitivity_conservative value=350 %s>"
        "Conservative (3.50)</option></select></label>"
        "<label><span id=confirmation_label>Motion confirmation</span><select "
        "name=trigger_consecutive><option id=confirmation_1 value=1 %s>1 reading"
        "</option><option id=confirmation_2 value=2 %s>2 readings</option>"
        "<option id=confirmation_3 value=3 %s>3 readings</option></select></label>"
        "<label><span id=speed_label>Detection speed</span><select name=scan_delay>"
        "<option id=speed_fast value=250 %s>Fast</option><option id=speed_normal "
        "value=500 %s>Normal</option><option id=speed_slow value=1000 %s>Slow"
        "</option></select></label>"
        "<label><span id=motion_duration_label>Motion state duration</span><select "
        "name=motion_duration><option id=duration_2 value=2 %s>2 seconds</option>"
        "<option id=duration_4 value=4 %s>4 seconds</option><option id=duration_8 "
        "value=8 %s>8 seconds</option></select></label>"
        "<label><span id=calibration_duration_label>Calibration duration</span>"
        "<select name=calibration_scans><option id=calibration_15 value=15 %s>"
        "Fast · 15 scans</option><option id=calibration_25 value=25 %s>Normal · "
        "25 scans</option><option id=calibration_40 value=40 %s>Precise · 40 "
        "scans</option></select></label>"
        "<p id=calibration_warning class=warn>A shorter calibration finishes "
        "sooner but may choose less stable references. It applies to initial "
        "and manual calibration.</p>"
        "<h2 id=access_title>Configuration access</h2>"
        "<label><span id=username_label>Username</span><input type=text "
        "name=admin_user maxlength=%u "
        "required value=\"%s\"></label>"
        "<label><span id=password_label>New password</span><input type=password "
        "name=admin_pass minlength=4 maxlength=%u "
        "placeholder='Leave blank to keep the current password'></label>"
        "<p id=password_warning class=warn>The initial admin/admin credentials "
        "are weak; change them.</p><div class=toolbar>"
        "<button id=save_config class=primary name=action value=save>Save "
        "configuration"
        "</button><button class='primary calibrate' name=action "
        "value=recalibrate id=save_recalibrate>Save and recalibrate</button>"
        "</div></form>"
        "<form class=card method=post action=/restore-detection>"
        "<input type=hidden name=csrf value=%s><h2 id=defaults_title>Defaults</h2>"
        "<p id=defaults_help class=hint>Only restores these detection settings. "
        "Credentials, selected networks and calibrated references are kept.</p>"
        "<button id=restore_detection class=calibrate>Restore detection settings"
        "</button></form>"
        "<script>const LANG='%s',tr=(en,es)=>LANG==='es'?es:en;"
        "document.documentElement.lang=LANG;const ES={motion_label:'Esperando "
        "datos',motion_detail:'El detector está iniciándose.',activity_title:"
        "'Actividad en tiempo real',legend_score:'Score de cambio',"
        "legend_threshold:'Umbral',legend_detection:'Detección',chart_note:"
        "'Esperando muestras…',clock_title:'Fecha y hora',clock_help:'Configura "
        "la fecha y hora local después de cada reinicio. El reloj y el historial "
        "se conservan en memoria hasta el siguiente reinicio.',clock_label:"
        "'Fecha y hora local',set_clock:'Configurar fecha y hora',clock_status:"
        "'La hora no está configurada.',history_title:'Historial de detecciones',"
        "history_help:'Se conservan en memoria hasta 128 detecciones de esta "
        "sesión.',history_time:'Fecha y hora',history_duration:'Duración',"
        "history_peak:'Pico de score',history_threshold:'Umbral',history_coverage:"
        "'Cobertura',history_state:'Estado',history_empty:'Todavía no hay "
        "detecciones registradas.',download_json:'Descargar JSON',download_csv:"
        "'Descargar CSV',calibration_title:'Calibración sin presencia',"
        "calibration_help:'El detector espera mientras sales; después busca de "
        "nuevo las redes y aprende el ambiente vacío.',leave_time_label:'Tiempo "
        "para salir (segundos)',calibrate:'Salir y calibrar',calibration_status:"
        "'No hay ninguna calibración programada.',config_title:'Configuración',"
        "search_title:'Buscar redes',scan:'Buscar redes',chosen_title:'Redes "
        "elegidas',detection_settings_title:'Ajustes de detección',"
        "detection_settings_help:'Se aplican al reiniciar y no necesitan "
        "recalibración. Cambiar las redes elegidas sí requiere recalibrar.',"
        "sensitivity_label:'Sensibilidad',sensitivity_sensitive:'Sensible (2,00)',"
        "sensitivity_balanced:'Equilibrado (2,50)',sensitivity_conservative:"
        "'Conservador (3,50)',confirmation_label:'Confirmación de movimiento',"
        "confirmation_1:'1 lectura',confirmation_2:'2 lecturas',confirmation_3:"
        "'3 lecturas',speed_label:'Velocidad de detección',speed_fast:'Rápida',"
        "speed_normal:'Normal',speed_slow:'Lenta',motion_duration_label:'Duración "
        "del estado de movimiento',duration_2:'2 segundos',duration_4:'4 segundos',"
        "duration_8:'8 segundos',calibration_duration_label:'Duración de la "
        "calibración',calibration_15:'Rápida · 15 escaneos',calibration_25:"
        "'Normal · 25 escaneos',calibration_40:'Precisa · 40 escaneos',"
        "calibration_warning:'Una calibración corta termina antes, pero puede "
        "elegir referencias menos estables. Se aplica a la calibración inicial "
        "y manual.',access_title:'Acceso a esta configuración',username_label:"
        "'Usuario',password_label:'Nueva contraseña',password_warning:'El valor "
        "inicial admin/admin es débil; conviene cambiarlo.',save_config:'Guardar "
        "configuración',save_recalibrate:'Guardar y recalibrar',defaults_title:"
        "'Valores iniciales',defaults_help:'Solo restaura estos ajustes. Conserva "
        "credenciales, redes elegidas y referencias calibradas.',restore_detection:"
        "'Restaurar ajustes de detección'};if(LANG==='es'){for(const[k,v]of "
        "Object.entries(ES)){const e=document.getElementById(k);if(e)e.textContent="
        "v}document.querySelector('[name=admin_pass]').placeholder='Dejar vacía "
        "para conservar';document.getElementById('automatic_mode').innerHTML="
        "'<b>Automático</b> — usa las mejores redes visibles';document.getElementById"
        "('manual_mode').innerHTML='<b>Elegir redes</b> — selecciona hasta 8';"
        "document.getElementById('network_hint').innerHTML='Pulsa <b>+ Añadir</b> "
        "en cada red de referencia. No hace falta conectarse ni conocer su clave.'}"
        "let networks=%s,selected=%s;"
        "const R=document.querySelector('#results'),C=document.querySelector("
        "'#chosen'),I=document.querySelector('#inputs'),N=document.querySelector("
        "'#count'),S=document.querySelector('#status'),D=document.querySelector("
        "'#detector'),L=document.querySelector('#motion_label'),M=document."
        "querySelector('#motion_detail'),V=document.querySelector('#motion_chart'),"
        "G=document.querySelector('#chart_note'),CB=document.querySelector("
        "'#calibrate'),CD=document.querySelector('#calibration_delay'),CS="
        "document.querySelector('#calibration_status'),CI=document.querySelector("
        "'#clock_input'),CT=document.querySelector('#clock_status'),HR=document."
        "querySelector('#history_rows'),HE=document.querySelector('#history_empty');"
        "const HISTORY=120000,points=[];let lastScan=0,lastMotion=false,hits=0;"
        "function stateView(d){if(!d.live)return[tr('Detector offline','Sin "
        "conexión con el detector'),'bad'];if(d.state==='MOTION')return[tr("
        "'MOTION DETECTED','MOVIMIENTO DETECTADO'),'motion'];if(d.state==='IDLE')"
        "return[tr('No motion','Sin movimiento'),'idle'];if(d.state==='CALIBRATING'"
        "||d.state==='WARMUP')return[tr('Calibrating','Calibrando'),'wait'];return"
        "[tr('Insufficient coverage','Sin cobertura suficiente'),'bad']}"
        "function renderStatus(d){const v=stateView(d),motion=d.state==='MOTION'"
        "&&d.live;D.className='detector '+v[1];L.textContent=v[0];M.textContent="
        "`${tr('Score','Score')} ${(d.score_x100/100).toFixed(2)} · ${tr("
        "'Threshold','Umbral')} ${(d.trigger_x100/100"
        ").toFixed(2)} · ${tr('Coverage','Cobertura')} ${(d.coverage_permille/10).toFixed(0)}"
        "${String.fromCharCode(37)} · ${d.observed_references}/${d.reference_count}"
        " ${tr('references','referencias')}${d.state==='IDLE'&&d.trigger_count?"
        "` · ${tr('Confirmation','Confirmación')} "
        "${d.trigger_count}/${d.trigger_required}`:''}`;if(d.scan_id===lastScan)"
        "return;lastScan=d.scan_id;if("
        "motion&&!lastMotion)hits++;lastMotion=motion;const now=Date.now();points."
        "push({t:now,score:d.score_x100/100,threshold:d.trigger_x100/100,hit:"
        "motion&&points.length&&!points.at(-1).motion,motion});while(points.length"
        "&&points[0].t<now-HISTORY)points.shift();drawChart()}"
        "function drawChart(){const box=V.getBoundingClientRect(),dpr=Math.min("
        "devicePixelRatio||1,2),w=Math.max(260,box.width),h=Math.max(240,box."
        "height);V.width=Math.round(w*dpr);V.height=Math.round(h*dpr);const x=V."
        "getContext('2d');x.setTransform(dpr,0,0,dpr,0,0);x.clearRect(0,0,w,h);"
        "const p={l:43,r:12,t:12,b:34},pw=w-p.l-p.r,ph=h-p.t-p.b,now=Date.now(),"
        "start=now-HISTORY,max=Math.max(1,...points.flatMap(q=>[q.score,q."
        "threshold]))*1.15,xx=t=>p.l+Math.max(0,(t-start)/HISTORY)*pw,yy=n=>p.t"
        "+ph-Math.min(1,n/max)*ph;x.font='11px system-ui';x.strokeStyle="
        "'#d9dee5';x.fillStyle='#58616c';x.lineWidth=1;for(let i=0;i<=4;i++){"
        "let y=p.t+ph*i/4;x.beginPath();x.moveTo(p.l,y);x.lineTo(w-p.r,y);x."
        "stroke();x.fillText((max*(1-i/4)).toFixed(1),3,y+4);let px=p.l+pw*i/4,"
        "t=start+HISTORY*i/4;x.beginPath();x.moveTo(px,p.t);x.lineTo(px,p.t+ph);"
        "x.stroke();x.fillText(new Date(t).toLocaleTimeString('es-ES',{hour:"
        "'2-digit',minute:'2-digit',second:'2-digit'}),Math.max(p.l,Math.min("
        "px-24,w-62)),h-9)}for(const q of points)if(q.hit){let px=xx(q.t);x."
        "strokeStyle='#d55e00';x.lineWidth=3;x.setLineDash([]);x.beginPath();x."
        "moveTo(px,p.t);x.lineTo(px,p.t+ph);x.stroke()}function line(k,color,"
        "dash){x.strokeStyle=color;x.lineWidth=3;x.setLineDash(dash);x.beginPath"
        "();let first=true;for(const q of points){let px=xx(q.t),py=yy(q[k]);"
        "first?(x.moveTo(px,py),first=false):x.lineTo(px,py)}x.stroke();x."
        "setLineDash([])}line('threshold','#e69f00',[7,4]);line('score','#0072b2'"
        ",[]);G.textContent=points.length?`Ventana: 2 minutos · ${hits} "
        "${tr('detections since opening the page','detecciones desde que abriste "
        "la página')}`:tr('Waiting for samples…','Esperando muestras…')}"
        "function renderCalibration(d){if(d.calibration_state==='scheduled'){CS."
        "textContent=tr(`Leave now: calibration starts in ${d."
        "calibration_remaining_seconds} s.`,`Sal ahora: la calibración comienza "
        "en ${d.calibration_remaining_seconds} s.`);CB.disabled=false}else if(d."
        "calibration_state==='running'){CS.textContent=tr(`Calibrating empty room:"
        " ${d.calibration_completed_scans}/${d.calibration_target_scans} scans.`,"
        "`Calibrando ambiente vacío: ${d.calibration_completed_scans}/${d."
        "calibration_target_scans} escaneos.`);CB.disabled=true}else{CS.textContent="
        "tr('No calibration is scheduled.','No hay ninguna calibración "
        "programada.');CB.disabled=false}}"
        "async function poll(){try{const r=await fetch('/status',{cache:'no-store'"
        "});if(!r.ok)throw Error();const d=await r.json();renderStatus(d);"
        "renderCalibration(d)}catch(e){D."
        "className='detector bad';L.textContent=tr('Offline','Sin conexión');"
        "M.textContent=tr('No data is being received from the device.','No se "
        "reciben datos del dispositivo.')}setTimeout(poll,750)}"
        "addEventListener('resize',drawChart);"
        "function row(name,detail,action,label,klass){const d=document.createElement("
        "'div'),s=document.createElement('span'),b=document.createElement('b'),"
        "m=document.createElement('small'),x=document.createElement('button');"
        "d.className='net';b.textContent=name;m.textContent=detail;s.append(b,m);"
        "x.type='button';x.className=klass;x.textContent=label;x.onclick=action;"
        "d.append(s,x);return d}"
        "function draw(){R.replaceChildren();if(!networks.length){const e=document."
        "createElement('p');e.className='empty';e.textContent=tr('Press Find "
        "networks to refresh the list.','Pulsa Buscar redes para actualizar la "
        "lista.');R.append(e)}networks.forEach(n=>{const added=selected.includes("
        "n.ssid),detail=`${tr('Channel','Canal')} ${n.channel} · ${n.rssi} dBm · "
        "${n.aps} AP`;const x=row(n.ssid,detail,()=>add(n.ssid),added?tr('Added',"
        "'Añadida'):tr('+ Add','+ Añadir'),'add');x.lastChild.disabled=added;"
        "R.append(x)});C.replaceChildren"
        "();I.replaceChildren();N.textContent=`(${selected.length}/8)`;"
        "if(!selected.length){const e=document.createElement('p');e.className="
        "'empty';e.textContent=tr('No networks selected yet.','Todavía no has "
        "añadido ninguna red.');C.append(e)}selected.forEach((name,i)=>{C.append("
        "row(name,tr('Reference network','Red de referencia'),()=>{selected.splice"
        "(i,1);draw()},tr('Remove','Quitar'),'remove'));const h=document."
        "createElement('input');h.type='hidden';h.name='ssid'+i;h.value=name;"
        "I.append(h)})}"
        "function add(name){if(selected.includes(name))return;if(selected.length"
        ">=8){alert(tr('Maximum 8 networks','Máximo 8 redes'));return}selected."
        "push(name);document."
        "querySelector('#manual').checked=true;draw()}"
        "document.querySelector('#scan').onclick=async function(){this.disabled="
        "true;S.textContent=tr('Searching…','Buscando…');try{const r=await "
        "fetch('/networks',{"
        "method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'"
        "},body:'csrf='+encodeURIComponent(document.querySelector('#csrf').value)"
        "});if(!r.ok)throw Error();networks=await r.json();S.textContent=networks."
        "length?tr(`${networks.length} networks found`,`${networks.length} redes "
        "encontradas`):tr('No networks found','No se encontraron redes');draw()}"
        "catch(e){S.textContent=tr('Search failed. Try again.','No se pudo buscar."
        " Inténtalo otra vez.')}"
        "finally{this.disabled=false}};draw();poll();"
        "CB.onclick=async function(){const delay=Number(CD.value);if(!Number."
        "isInteger(delay)||delay<5||delay>300){CS.textContent=tr('Enter 5 to 300 "
        "seconds.','Indica entre 5 y 300 segundos.');return}this.disabled=true;"
        "CS.textContent=tr('Scheduling…','Programando…');"
        "try{const r=await fetch('/calibrate',{method:'POST',headers:{"
        "'Content-Type':'application/x-www-form-urlencoded'},body:'csrf='+"
        "encodeURIComponent(document.querySelector('#csrf').value)+'&delay_seconds"
        "='+delay});if(!r.ok)throw Error();renderCalibration(await r.json())}"
        "catch(e){CS.textContent=tr('Calibration could not be scheduled.','No se "
        "pudo programar la calibración.');this.disabled=false}};"
        "function localInputValue(d){const z=new Date(d.getTime()-d.getTimezoneOffset"
        "()*60000);return z.toISOString().slice(0,19)}CI.value=localInputValue(new "
        "Date());function renderHistory(d){CT.textContent=d.clock_configured?tr("
        "`Clock set: ${new Date(d.now_epoch_seconds*1000).toLocaleString()}`,`Reloj "
        "configurado: ${new Date(d.now_epoch_seconds*1000).toLocaleString()}`):tr("
        "'Time is not configured. Set it to date the detections.','La hora no está "
        "configurada. Ajústala para fechar las detecciones.');HR.replaceChildren();"
        "const events=[...d.events].reverse();HE.hidden=events.length>0;for(const "
        "e of events){const row=document.createElement('tr'),values=[e."
        "started_epoch_seconds===null?tr('Time not set','Hora sin configurar'):new "
        "Date(e.started_epoch_seconds*1000).toLocaleString(),`${(e.duration_ms/1000)"
        ".toFixed(1)} s`,(e.peak_score_x100/100).toFixed(2),(e.trigger_score_x100/"
        "100).toFixed(2),`${(e.coverage_permille/10).toFixed(0)}${String."
        "fromCharCode(37)}`,e.active?tr('Active','Activa'):tr('Completed','Finalizada"
        "')];for(const value of values){const cell=document.createElement('td');"
        "cell.textContent=value;row.append(cell)}HR.append(row)}}async function "
        "loadHistory(){try{const r=await fetch('/events',{cache:'no-store'});if(!r.ok)"
        "throw Error();renderHistory(await r.json())}catch(e){CT.textContent=tr("
        "'History unavailable.','Historial no disponible.')}}async function "
        "pollHistory(){await loadHistory();setTimeout(pollHistory,3000)}document."
        "querySelector('#set_clock').onclick=async function(){const "
        "date=new Date(CI.value);if(!CI.value||Number.isNaN(date.getTime())){CT."
        "textContent=tr('Enter a valid date and time.','Indica una fecha y hora "
        "válidas.');return}this.disabled=true;CT.textContent=tr('Setting clock…',"
        "'Configurando reloj…');try{const body='csrf='+encodeURIComponent(document."
        "querySelector('#csrf').value)+'&epoch_seconds='+Math.floor(date.getTime()/"
        "1000),r=await fetch('/clock',{method:'POST',headers:{'Content-Type':"
        "'application/x-www-form-urlencoded'},body});if(!r.ok)throw Error();await "
        "loadHistory()}catch(e){CT.textContent=tr('Clock could not be set.','No se "
        "pudo configurar el reloj.')}finally{this.disabled=false}};pollHistory();"
        "</script></body></html>",
        csrf_token,
        manual ? "" : "checked",
        manual ? "checked" : "",
        selected_u16(config.trigger_score_x100, 200U),
        selected_u16(config.trigger_score_x100, 250U),
        selected_u16(config.trigger_score_x100, 350U),
        selected_u8(config.trigger_consecutive, 1U),
        selected_u8(config.trigger_consecutive, 2U),
        selected_u8(config.trigger_consecutive, 3U),
        selected_u16(config.inter_scan_delay_ms, 250U),
        selected_u16(config.inter_scan_delay_ms, 500U),
        selected_u16(config.inter_scan_delay_ms, 1000U),
        selected_u8(config.motion_duration_seconds, 2U),
        selected_u8(config.motion_duration_seconds, 4U),
        selected_u8(config.motion_duration_seconds, 8U),
        selected_u16(config.calibration_scans, 15U),
        selected_u16(config.calibration_scans, 25U),
        selected_u16(config.calibration_scans, 40U),
        (unsigned)PROBE_CONFIG_ADMIN_USER_MAX_LENGTH,
        escaped_user,
        (unsigned)PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH,
        csrf_token,
        request_spanish(request) ? "es" : "en",
        portal_options,
        portal_selected);
    if (length < 0 || (size_t)length >= sizeof(portal_page)) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "page error");
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    httpd_resp_set_hdr(request, "Referrer-Policy", "no-referrer");
    httpd_resp_set_hdr(
        request,
        "Content-Security-Policy",
        "default-src 'self'; script-src 'unsafe-inline'; "
        "style-src 'unsafe-inline'; connect-src 'self'; "
        "img-src 'self' data:; frame-ancestors 'none'");
    return httpd_resp_send(request, portal_page, length);
}

static void restart_task(void *argument)
{
    (void)argument;
    vTaskDelay(pdMS_TO_TICKS(1200));
    esp_restart();
}

static bool read_request_body(httpd_req_t *request)
{
    if (request->content_len <= 0 ||
        request->content_len >= PORTAL_BODY_SIZE) {
        return false;
    }
    size_t received = 0U;
    while (received < (size_t)request->content_len) {
        const int chunk = httpd_req_recv(
            request,
            portal_body + received,
            (size_t)request->content_len - received);
        if (chunk <= 0) {
            return false;
        }
        received += (size_t)chunk;
    }
    portal_body[received] = '\0';
    return true;
}

static bool valid_csrf(const char *body)
{
    char submitted[sizeof(csrf_token)] = "";
    return httpd_query_key_value(
               body, "csrf", submitted, sizeof(submitted)) == ESP_OK &&
           constant_time_equal(submitted,
                               strlen(submitted),
                               csrf_token,
                               strlen(csrf_token));
}

static bool read_u16(const char *body,
                     const char *key,
                     uint16_t *value)
{
    char text[8] = "";
    if (httpd_query_key_value(body, key, text, sizeof(text)) != ESP_OK ||
        !url_decode(text) || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const unsigned long parsed = strtoul(text, &end, 10);
    if (end == text || *end != '\0' || parsed > UINT16_MAX) {
        return false;
    }
    *value = (uint16_t)parsed;
    return true;
}

static bool read_i64(const char *body,
                     const char *key,
                     int64_t *value)
{
    char text[24] = "";
    if (httpd_query_key_value(body, key, text, sizeof(text)) != ESP_OK ||
        !url_decode(text) || text[0] == '\0') {
        return false;
    }
    char *end = NULL;
    const long long parsed = strtoll(text, &end, 10);
    if (end == text || *end != '\0') {
        return false;
    }
    *value = (int64_t)parsed;
    return true;
}

static size_t copy_history(motion_clock_t *clock)
{
    if (history_mutex == NULL ||
        xSemaphoreTake(history_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return 0U;
    }
    const size_t count = motion_history_snapshot(
        &motion_history,
        history_snapshot,
        MOTION_HISTORY_CAPACITY);
    *clock = motion_clock;
    xSemaphoreGive(history_mutex);
    return count;
}

static esp_err_t clock_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    if (!read_request_body(request) || !valid_csrf(portal_body)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    int64_t epoch_seconds = 0;
    if (!read_i64(
            portal_body, "epoch_seconds", &epoch_seconds) ||
        history_mutex == NULL ||
        xSemaphoreTake(history_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid date and time");
    }
    const bool configured = motion_clock_set(
        &motion_clock,
        epoch_seconds,
        esp_timer_get_time() / 1000);
    xSemaphoreGive(history_mutex);
    if (!configured) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "date out of range");
    }
    char response[96];
    (void)snprintf(response,
                   sizeof(response),
                   "{\"configured\":true,\"epoch_seconds\":%" PRId64 "}",
                   epoch_seconds);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, response);
}

static esp_err_t events_json_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    motion_clock_t clock = {0};
    const size_t count = copy_history(&clock);
    int64_t now_epoch = 0;
    const int64_t now_ms = esp_timer_get_time() / 1000;
    (void)motion_clock_now(&clock, now_ms, &now_epoch);
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    if (strcmp(request->uri, "/events.json") == 0) {
        httpd_resp_set_hdr(
            request,
            "Content-Disposition",
            "attachment; filename=motion-events.json");
    }
    char header[160];
    (void)snprintf(
        header,
        sizeof(header),
        "{\"clock_configured\":%s,\"now_epoch_seconds\":%" PRId64
        ",\"capacity\":%u,\"events\":[",
        clock.configured ? "true" : "false",
        now_epoch,
        (unsigned)MOTION_HISTORY_CAPACITY);
    esp_err_t error =
        httpd_resp_send_chunk(request, header, HTTPD_RESP_USE_STRLEN);
    for (size_t index = 0U;
         error == ESP_OK && index < count;
         ++index) {
        const motion_history_event_t *event =
            &history_snapshot[index];
        int64_t started_epoch = 0;
        int64_t ended_epoch = 0;
        const bool has_start = motion_clock_at(
            &clock, event->started_monotonic_ms, &started_epoch);
        const bool has_end =
            !event->active &&
            motion_clock_at(
                &clock, event->ended_monotonic_ms, &ended_epoch);
        const int64_t duration_ms =
            (event->active ? now_ms : event->ended_monotonic_ms) -
            event->started_monotonic_ms;
        char start_text[24];
        char end_text[24];
        if (has_start) {
            (void)snprintf(start_text,
                           sizeof(start_text),
                           "%" PRId64,
                           started_epoch);
        } else {
            memcpy(start_text, "null", 5U);
        }
        if (has_end) {
            (void)snprintf(end_text,
                           sizeof(end_text),
                           "%" PRId64,
                           ended_epoch);
        } else {
            memcpy(end_text, "null", 5U);
        }
        char row[448];
        (void)snprintf(
            row,
            sizeof(row),
            "%s{\"id\":%" PRIu32 ",\"start_scan_id\":%" PRIu32
            ",\"end_scan_id\":%" PRIu32
            ",\"started_epoch_seconds\":%s"
            ",\"ended_epoch_seconds\":%s"
            ",\"duration_ms\":%" PRId64 ",\"active\":%s"
            ",\"start_score_x100\":%u,\"peak_score_x100\":%u"
            ",\"trigger_score_x100\":%u,\"coverage_permille\":%u}",
            index > 0U ? "," : "",
            event->id,
            event->start_scan_id,
            event->end_scan_id,
            start_text,
            end_text,
            duration_ms,
            event->active ? "true" : "false",
            event->start_score_x100,
            event->peak_score_x100,
            event->trigger_score_x100,
            event->coverage_permille);
        error = httpd_resp_send_chunk(
            request, row, HTTPD_RESP_USE_STRLEN);
    }
    if (error == ESP_OK) {
        error = httpd_resp_send_chunk(
            request, "]}", HTTPD_RESP_USE_STRLEN);
    }
    if (error == ESP_OK) {
        error = httpd_resp_send_chunk(request, NULL, 0U);
    }
    return error;
}

static esp_err_t events_csv_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    motion_clock_t clock = {0};
    const size_t count = copy_history(&clock);
    const int64_t now_ms = esp_timer_get_time() / 1000;
    httpd_resp_set_type(request, "text/csv; charset=utf-8");
    httpd_resp_set_hdr(
        request,
        "Content-Disposition",
        "attachment; filename=motion-events.csv");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    esp_err_t error = httpd_resp_send_chunk(
        request,
        "id,start_epoch_seconds,end_epoch_seconds,duration_ms,status,"
        "start_scan_id,end_scan_id,start_score,peak_score,trigger_score,"
        "coverage_percent\r\n",
        HTTPD_RESP_USE_STRLEN);
    for (size_t index = 0U;
         error == ESP_OK && index < count;
         ++index) {
        const motion_history_event_t *event =
            &history_snapshot[index];
        int64_t started_epoch = 0;
        int64_t ended_epoch = 0;
        const bool has_start = motion_clock_at(
            &clock, event->started_monotonic_ms, &started_epoch);
        const bool has_end =
            !event->active &&
            motion_clock_at(
                &clock, event->ended_monotonic_ms, &ended_epoch);
        const int64_t duration_ms =
            (event->active ? now_ms : event->ended_monotonic_ms) -
            event->started_monotonic_ms;
        char start_text[24] = "";
        char end_text[24] = "";
        if (has_start) {
            (void)snprintf(start_text,
                           sizeof(start_text),
                           "%" PRId64,
                           started_epoch);
        }
        if (has_end) {
            (void)snprintf(end_text,
                           sizeof(end_text),
                           "%" PRId64,
                           ended_epoch);
        }
        char row[256];
        (void)snprintf(
            row,
            sizeof(row),
            "%" PRIu32 ",%s,%s"
            ",%" PRId64 ",%s,%" PRIu32 ",%" PRIu32
            ",%.2f,%.2f,%.2f,%.1f\r\n",
            event->id,
            start_text,
            end_text,
            duration_ms,
            event->active ? "active" : "completed",
            event->start_scan_id,
            event->end_scan_id,
            event->start_score_x100 / 100.0,
            event->peak_score_x100 / 100.0,
            event->trigger_score_x100 / 100.0,
            event->coverage_permille / 10.0);
        error = httpd_resp_send_chunk(
            request, row, HTTPD_RESP_USE_STRLEN);
    }
    if (error == ESP_OK) {
        error = httpd_resp_send_chunk(request, NULL, 0U);
    }
    return error;
}

static bool network_selection_changed(
    const probe_config_blob_t *left,
    const probe_config_blob_t *right)
{
    return left->mode != right->mode ||
           left->ssid_count != right->ssid_count ||
           memcmp(left->ssids,
                  right->ssids,
                  sizeof(left->ssids)) != 0;
}

static esp_err_t login_handler(httpd_req_t *request)
{
    if (!read_request_body(request)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    char username[PROBE_CONFIG_ADMIN_USER_MAX_LENGTH * 3U + 1U] = "";
    char password[PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH * 3U + 1U] = "";
    if (httpd_query_key_value(portal_body,
                              "username",
                              username,
                              sizeof(username)) != ESP_OK ||
        httpd_query_key_value(portal_body,
                              "password",
                              password,
                              sizeof(password)) != ESP_OK ||
        !url_decode(username) || !url_decode(password) ||
        !constant_time_equal(username,
                             strlen(username),
                             runtime_username,
                             strlen(runtime_username)) ||
        !constant_time_equal(password,
                             strlen(password),
                             runtime_password,
                             strlen(runtime_password))) {
        httpd_resp_set_status(request, "403 Forbidden");
        httpd_resp_set_type(request, "text/html; charset=utf-8");
        return httpd_resp_sendstr(
            request,
            "<!doctype html><meta name=viewport "
            "content='width=device-width,initial-scale=1'>"
            "<h1>Credenciales incorrectas</h1>"
            "<p><a href=/>Volver a intentarlo</a></p>");
    }
    char cookie[96];
    (void)snprintf(cookie,
                   sizeof(cookie),
                   "motion_session=%s; Path=/; HttpOnly; SameSite=Strict",
                   session_token);
    httpd_resp_set_status(request, "303 See Other");
    httpd_resp_set_hdr(
        request,
        "Location",
        request_spanish(request) ? "/?lang=es" : "/?lang=en");
    httpd_resp_set_hdr(request, "Set-Cookie", cookie);
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Acceso correcto");
}

static esp_err_t status_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    const int64_t now_ms = esp_timer_get_time() / 1000;
    const int64_t updated_ms = atomic_load(&runtime_updated_ms);
    const int64_t age_ms =
        updated_ms > 0 && now_ms >= updated_ms ? now_ms - updated_ms : -1;
    const unsigned state = atomic_load(&runtime_state);
    const long long deadline_ms =
        atomic_load(&calibration_deadline_ms);
    const bool running = atomic_load(&calibration_running);
    const uint32_t remaining_seconds =
        deadline_ms > now_ms
            ? (uint32_t)((deadline_ms - now_ms + 999) / 1000)
            : 0U;
    const char *calibration_state =
        running ? "running" : deadline_ms > 0 ? "scheduled" : "idle";
    char response[640];
    const int length = snprintf(
        response,
        sizeof(response),
        "{\"scan_id\":%u,\"state\":\"%s\",\"score_x100\":%u,"
        "\"trigger_x100\":%u,\"coverage_permille\":%u,"
        "\"observed_references\":%u,\"reference_count\":%u,"
        "\"trigger_count\":%u,\"trigger_required\":%u,"
        "\"calibration_state\":\"%s\","
        "\"calibration_remaining_seconds\":%" PRIu32 ","
        "\"calibration_completed_scans\":%u,"
        "\"calibration_target_scans\":%u,"
        "\"updated_age_ms\":%" PRId64 ",\"live\":%s}",
        atomic_load(&runtime_scan_id),
        state <= MULTIREF_STATE_NO_DATA
            ? multiref_state_name((multiref_state_t)state)
            : "UNKNOWN",
        atomic_load(&runtime_score_x100),
        atomic_load(&runtime_trigger_x100),
        atomic_load(&runtime_coverage_permille),
        atomic_load(&runtime_observed_references),
        atomic_load(&runtime_reference_count),
        atomic_load(&runtime_trigger_count),
        atomic_load(&runtime_trigger_required),
        calibration_state,
        remaining_seconds,
        atomic_load(&calibration_completed_scans),
        atomic_load(&calibration_target_scans),
        age_ms,
        age_ms >= 0 && age_ms < 5000 ? "true" : "false");
    if (length < 0 || (size_t)length >= sizeof(response)) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "status error");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

static esp_err_t calibrate_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    if (!read_request_body(request) || !valid_csrf(portal_body)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    if (atomic_load(&calibration_running)) {
        httpd_resp_set_status(request, "409 Conflict");
        return httpd_resp_sendstr(
            request, "calibration already running");
    }
    char delay_text[8] = "";
    if (httpd_query_key_value(portal_body,
                              "delay_seconds",
                              delay_text,
                              sizeof(delay_text)) != ESP_OK) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "missing delay");
    }
    char *end = NULL;
    const unsigned long delay_seconds =
        strtoul(delay_text, &end, 10);
    if (end == delay_text || *end != '\0' ||
        delay_seconds < 5UL || delay_seconds > 300UL) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid delay");
    }
    atomic_store(
        &calibration_deadline_ms,
        esp_timer_get_time() / 1000 +
            (long long)delay_seconds * 1000LL);
    char response[192];
    const int length = snprintf(
        response,
        sizeof(response),
        "{\"calibration_state\":\"scheduled\","
        "\"calibration_remaining_seconds\":%lu,"
        "\"calibration_completed_scans\":0,"
        "\"calibration_target_scans\":%u}",
        delay_seconds,
        atomic_load(&calibration_target_scans));
    if (length < 0 || (size_t)length >= sizeof(response)) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "response error");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_send(request, response, length);
}

static esp_err_t captive_redirect_handler(httpd_req_t *request)
{
    httpd_resp_set_status(request, "302 Found");
    httpd_resp_set_hdr(request, "Location", "/");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, "Abriendo configuracion");
}

static esp_err_t networks_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    if (!read_request_body(request) || !valid_csrf(portal_body)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    if (!build_network_json()) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "result error");
    }
    httpd_resp_set_type(request, "application/json; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    return httpd_resp_sendstr(request, portal_options);
}

static esp_err_t save_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    if (!read_request_body(request) || !valid_csrf(portal_body)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid body");
    }
    char mode[12] = "";
    if (httpd_query_key_value(
            portal_body, "mode", mode, sizeof(mode)) != ESP_OK ||
        !url_decode(mode)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid mode");
    }
    probe_config_blob_t previous;
    if (probe_config_load(&previous) != PROBE_CONFIG_OK) {
        probe_config_set_defaults(&previous);
    }
    probe_config_blob_t config = previous;
    config.ssid_count = 0U;
    memset(config.ssids, 0, sizeof(config.ssids));
    if (strcmp(mode, "auto") == 0) {
        config.mode = PROBE_CONFIG_MODE_AUTOMATIC;
    } else if (strcmp(mode, "manual") == 0) {
        config.mode = PROBE_CONFIG_MODE_MANUAL;
        for (size_t index = 0U; index < PORTAL_MAX_CHOICES; ++index) {
            char key[12];
            char encoded[REFERENCE_SELECTOR_SSID_MAX_LENGTH * 3U + 1U] = "";
            (void)snprintf(key, sizeof(key), "ssid%u", (unsigned)index);
            if (httpd_query_key_value(
                    portal_body, key, encoded, sizeof(encoded)) != ESP_OK) {
                continue;
            }
            if (!url_decode(encoded)) {
                return httpd_resp_send_err(
                    request, HTTPD_400_BAD_REQUEST, "invalid SSID");
            }
            const size_t length = strlen(encoded);
            if (length == 0U) {
                continue;
            }
            if (length > REFERENCE_SELECTOR_SSID_MAX_LENGTH) {
                return httpd_resp_send_err(
                    request, HTTPD_400_BAD_REQUEST, "SSID too long");
            }
            if (config.ssid_count >= PROBE_CONFIG_MAX_SSIDS) {
                return httpd_resp_send_err(
                    request, HTTPD_400_BAD_REQUEST, "maximum 8 SSIDs");
            }
            reference_ssid_t *ssid =
                &config.ssids[config.ssid_count++];
            memcpy(ssid->bytes, encoded, length);
            ssid->length = (uint8_t)length;
        }
        if (config.ssid_count == 0U) {
            return httpd_resp_send_err(
                request, HTTPD_400_BAD_REQUEST, "SSID required");
        }
    } else {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "unknown mode");
    }
    uint16_t trigger_score = 0U;
    uint16_t trigger_consecutive = 0U;
    uint16_t scan_delay = 0U;
    uint16_t motion_duration = 0U;
    uint16_t calibration_scans = 0U;
    if (!read_u16(portal_body, "trigger_score", &trigger_score) ||
        !read_u16(portal_body,
                  "trigger_consecutive",
                  &trigger_consecutive) ||
        !read_u16(portal_body, "scan_delay", &scan_delay) ||
        !read_u16(portal_body, "motion_duration", &motion_duration) ||
        !read_u16(portal_body,
                  "calibration_scans",
                  &calibration_scans) ||
        (trigger_score != PROBE_CONFIG_TRIGGER_SCORE_SENSITIVE_X100 &&
         trigger_score != PROBE_CONFIG_TRIGGER_SCORE_BALANCED_X100 &&
         trigger_score !=
             PROBE_CONFIG_TRIGGER_SCORE_CONSERVATIVE_X100) ||
        trigger_consecutive < PROBE_CONFIG_TRIGGER_CONSECUTIVE_MIN ||
        trigger_consecutive > PROBE_CONFIG_TRIGGER_CONSECUTIVE_MAX ||
        (scan_delay != PROBE_CONFIG_SCAN_DELAY_FAST_MS &&
         scan_delay != PROBE_CONFIG_SCAN_DELAY_NORMAL_MS &&
         scan_delay != PROBE_CONFIG_SCAN_DELAY_SLOW_MS) ||
        (motion_duration != PROBE_CONFIG_MOTION_DURATION_SHORT_S &&
         motion_duration != PROBE_CONFIG_MOTION_DURATION_NORMAL_S &&
         motion_duration != PROBE_CONFIG_MOTION_DURATION_LONG_S) ||
        (calibration_scans != PROBE_CONFIG_CALIBRATION_FAST_SCANS &&
         calibration_scans != PROBE_CONFIG_CALIBRATION_NORMAL_SCANS &&
         calibration_scans !=
             PROBE_CONFIG_CALIBRATION_PRECISE_SCANS)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid detection settings");
    }
    config.trigger_score_x100 = trigger_score;
    config.trigger_consecutive = (uint8_t)trigger_consecutive;
    config.inter_scan_delay_ms = scan_delay;
    config.motion_duration_seconds = (uint8_t)motion_duration;
    config.calibration_scans = calibration_scans;
    char username[PROBE_CONFIG_ADMIN_USER_MAX_LENGTH * 3U + 1U] = "";
    char password[PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH * 3U + 1U] = "";
    if (httpd_query_key_value(
            portal_body, "admin_user", username, sizeof(username)) != ESP_OK ||
        !url_decode(username) || username[0] == '\0' ||
        strlen(username) > PROBE_CONFIG_ADMIN_USER_MAX_LENGTH) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid username");
    }
    (void)httpd_query_key_value(
        portal_body, "admin_pass", password, sizeof(password));
    if (!url_decode(password) ||
        strlen(password) > PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH ||
        (password[0] != '\0' && strlen(password) < 4U)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid password");
    }
    memcpy(config.admin_username, username, strlen(username) + 1U);
    const char *saved_password =
        password[0] == '\0' ? previous.admin_password : password;
    memcpy(config.admin_password,
           saved_password,
           strlen(saved_password) + 1U);
    char action[16] = "";
    if (httpd_query_key_value(
            portal_body, "action", action, sizeof(action)) != ESP_OK ||
        !url_decode(action) ||
        (strcmp(action, "save") != 0 &&
         strcmp(action, "recalibrate") != 0)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid action");
    }
    const bool recalibrate = strcmp(action, "recalibrate") == 0;
    if (!recalibrate && network_selection_changed(&previous, &config)) {
        return httpd_resp_send_err(
            request,
            HTTPD_400_BAD_REQUEST,
            "network changes require recalibration");
    }
    if (probe_config_save(&config) != PROBE_CONFIG_OK ||
        (recalibrate &&
         reference_store_erase() != REFERENCE_STORE_OK)) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    esp_err_t response = httpd_resp_send_chunk(
        request,
        "<!doctype html><html lang=es><meta name=viewport "
        "content='width=device-width,initial-scale=1'><style>"
        "body{font:20px system-ui;max-width:600px;margin:30px auto;"
        "padding:20px}button{font-size:21px;padding:14px}</style>"
        "<h1>Configuración guardada</h1>",
        HTTPD_RESP_USE_STRLEN);
    const char *saved_message =
        recalibrate
            ? "<p>Se han guardado los ajustes. Al continuar, el dispositivo "
              "reiniciará y recalibrará las redes elegidas.</p>"
            : "<p>Se han guardado los ajustes sin borrar redes ni referencias. "
              "Al continuar, el dispositivo reiniciará para aplicarlos.</p>";
    if (response == ESP_OK) {
        response = httpd_resp_send_chunk(
            request, saved_message, HTTPD_RESP_USE_STRLEN);
    }
    if (response == ESP_OK) {
        response = httpd_resp_send_chunk(
            request,
            "<form method=post action=/apply><input type=hidden name=csrf value=",
            HTTPD_RESP_USE_STRLEN);
    }
    if (response != ESP_OK) {
        return response;
    }
    response =
        httpd_resp_send_chunk(request, csrf_token, HTTPD_RESP_USE_STRLEN);
    if (response == ESP_OK) {
        response = httpd_resp_send_chunk(
            request,
            "><button>Aplicar y reiniciar</button></form></html>",
            HTTPD_RESP_USE_STRLEN);
    }
    if (response == ESP_OK) {
        response = httpd_resp_send_chunk(request, NULL, 0U);
    }
    return response;
}

static esp_err_t restore_detection_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    if (!read_request_body(request) || !valid_csrf(portal_body)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    probe_config_blob_t config;
    if (probe_config_load(&config) != PROBE_CONFIG_OK) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "load failed");
    }
    probe_config_blob_t defaults;
    probe_config_set_defaults(&defaults);
    config.trigger_score_x100 = defaults.trigger_score_x100;
    config.trigger_consecutive = defaults.trigger_consecutive;
    config.inter_scan_delay_ms = defaults.inter_scan_delay_ms;
    config.motion_duration_seconds = defaults.motion_duration_seconds;
    config.calibration_scans = defaults.calibration_scans;
    if (probe_config_save(&config) != PROBE_CONFIG_OK) {
        return httpd_resp_send_err(
            request, HTTPD_500_INTERNAL_SERVER_ERROR, "save failed");
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    const esp_err_t response = httpd_resp_sendstr(
        request,
        "<!doctype html><meta name=viewport content='width=device-width'>"
        "<h1>Ajustes restaurados</h1><p>Se conservaron credenciales, redes y "
        "referencias. El dispositivo se reiniciará para aplicar los valores "
        "iniciales.</p>");
    (void)xTaskCreate(
        restart_task, "portal_restart", 2048U, NULL, 4U, NULL);
    return response;
}

static esp_err_t apply_handler(httpd_req_t *request)
{
    if (!authenticated(request)) {
        return ESP_OK;
    }
    if (!read_request_body(request) || !valid_csrf(portal_body)) {
        return httpd_resp_send_err(
            request, HTTPD_400_BAD_REQUEST, "invalid request");
    }
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    const esp_err_t response = httpd_resp_sendstr(
        request,
        "<!doctype html><meta name=viewport content='width=device-width'>"
        "<h1>Reiniciando</h1><p>Ya puedes cerrar esta página.</p>");
    (void)xTaskCreate(
        restart_task, "portal_restart", 2048U, NULL, 4U, NULL);
    return response;
}

bool maintenance_portal_active(void)
{
    return atomic_load(&portal_active);
}

esp_err_t maintenance_portal_start(const char *ap_ssid,
                                   const char *ap_password)
{
    if (ap_ssid == NULL || ap_password == NULL ||
        strlen(ap_ssid) == 0U || strlen(ap_ssid) > 32U ||
        strlen(ap_password) < 8U || strlen(ap_password) > 63U) {
        return ESP_ERR_INVALID_ARG;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong(
            &portal_active, &expected, true)) {
        return ESP_OK;
    }
    network_mutex = xSemaphoreCreateMutex();
    history_mutex = xSemaphoreCreateMutex();
    if (network_mutex == NULL || history_mutex == NULL) {
        atomic_store(&portal_active, false);
        return ESP_ERR_NO_MEM;
    }
    motion_history_init(&motion_history);
    motion_clock_clear(&motion_clock);
    probe_config_blob_t config;
    if (probe_config_load(&config) != PROBE_CONFIG_OK) {
        probe_config_set_defaults(&config);
    }
    memcpy(runtime_username,
           config.admin_username,
           strlen(config.admin_username) + 1U);
    memcpy(runtime_password,
           config.admin_password,
           strlen(config.admin_password) + 1U);
    (void)snprintf(csrf_token,
                   sizeof(csrf_token),
                   "%08lx%08lx",
                   (unsigned long)esp_random(),
                   (unsigned long)esp_random());
    (void)snprintf(session_token,
                   sizeof(session_token),
                   "%08lx%08lx%08lx%08lx",
                   (unsigned long)esp_random(),
                   (unsigned long)esp_random(),
                   (unsigned long)esp_random(),
                   (unsigned long)esp_random());
    wifi_config_t wifi_config = {0};
    memcpy(wifi_config.ap.ssid, ap_ssid, strlen(ap_ssid));
    wifi_config.ap.ssid_len = (uint8_t)strlen(ap_ssid);
    memcpy(wifi_config.ap.password, ap_password, strlen(ap_password));
    wifi_config.ap.channel = 1U;
    wifi_config.ap.max_connection = 2U;
    wifi_config.ap.authmode = WIFI_AUTH_WPA2_PSK;
    esp_err_t error = esp_wifi_set_mode(WIFI_MODE_APSTA);
    if (error == ESP_OK) {
        error = esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    }
    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_uri_handlers = 16U;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    if (error == ESP_OK) {
        error = httpd_start(&server, &http_config);
    }
    if (error == ESP_OK) {
        const httpd_uri_t page = {
            .uri = "/", .method = HTTP_GET, .handler = page_handler,
        };
        const httpd_uri_t login = {
            .uri = "/login",
            .method = HTTP_POST,
            .handler = login_handler,
        };
        const httpd_uri_t save = {
            .uri = "/save", .method = HTTP_POST, .handler = save_handler,
        };
        const httpd_uri_t networks_uri = {
            .uri = "/networks",
            .method = HTTP_POST,
            .handler = networks_handler,
        };
        const httpd_uri_t status = {
            .uri = "/status",
            .method = HTTP_GET,
            .handler = status_handler,
        };
        const httpd_uri_t calibrate = {
            .uri = "/calibrate",
            .method = HTTP_POST,
            .handler = calibrate_handler,
        };
        const httpd_uri_t apply = {
            .uri = "/apply", .method = HTTP_POST, .handler = apply_handler,
        };
        const httpd_uri_t restore_detection = {
            .uri = "/restore-detection",
            .method = HTTP_POST,
            .handler = restore_detection_handler,
        };
        const httpd_uri_t clock_uri = {
            .uri = "/clock",
            .method = HTTP_POST,
            .handler = clock_handler,
        };
        const httpd_uri_t events = {
            .uri = "/events",
            .method = HTTP_GET,
            .handler = events_json_handler,
        };
        const httpd_uri_t events_json = {
            .uri = "/events.json",
            .method = HTTP_GET,
            .handler = events_json_handler,
        };
        const httpd_uri_t events_csv = {
            .uri = "/events.csv",
            .method = HTTP_GET,
            .handler = events_csv_handler,
        };
        const httpd_uri_t fallback = {
            .uri = "/*",
            .method = HTTP_GET,
            .handler = captive_redirect_handler,
        };
        error = httpd_register_uri_handler(server, &page);
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &login);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &save);
        }
        if (error == ESP_OK) {
            error =
                httpd_register_uri_handler(server, &networks_uri);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &status);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &calibrate);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &apply);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(
                server, &restore_detection);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &clock_uri);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &events);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &events_json);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &events_csv);
        }
        if (error == ESP_OK) {
            error = httpd_register_uri_handler(server, &fallback);
        }
    }
    if (error == ESP_OK) {
        error = captive_dns_start(
            esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"));
    }
    if (error != ESP_OK) {
        atomic_store(&portal_active, false);
    }
    return error;
}
