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
#include "probe_config.h"
#include "reference_store.h"
#include "captive_dns.h"

#define PORTAL_BODY_SIZE 3072U
#define PORTAL_PAGE_SIZE 24576U
#define PORTAL_MAX_NETWORKS 16U
#define PORTAL_MAX_CHOICES (PORTAL_MAX_NETWORKS + PROBE_CONFIG_MAX_SSIDS)

static atomic_bool portal_active;
static httpd_handle_t server;
static SemaphoreHandle_t network_mutex;
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

static esp_err_t login_page_handler(httpd_req_t *request)
{
    httpd_resp_set_type(request, "text/html; charset=utf-8");
    httpd_resp_set_hdr(request, "Cache-Control", "no-store");
    httpd_resp_set_hdr(request, "X-Content-Type-Options", "nosniff");
    return httpd_resp_sendstr(
        request,
        "<!doctype html><html lang=es><meta name=viewport "
        "content='width=device-width,initial-scale=1'><title>Motion C3</title>"
        "<style>*{box-sizing:border-box}body{font:19px system-ui;margin:0;"
        "min-height:100vh;display:grid;place-items:center;padding:20px;"
        "background:#0b1118;color:#f5f7fa}.login{width:min(100%,440px);"
        "background:#172331;padding:26px;border-radius:16px;box-shadow:"
        "0 12px 40px #0008}h1{margin-top:0}label{display:block;margin:18px 0}"
        "input{display:block;width:100%;font:inherit;padding:13px;margin-top:"
        "7px;border:2px solid #789;border-radius:9px}button{width:100%;font:"
        "bold 20px system-ui;padding:14px;background:#00a86b;color:white;"
        "border:0;border-radius:9px}.hint{color:#b9c8d8}</style>"
        "<form class=login method=post action=/login><h1>WiFi Motion C3</h1>"
        "<p>Accede para ver la detección, la gráfica y la configuración.</p>"
        "<label>Usuario<input name=username value=admin maxlength=16 "
        "autocomplete=username required></label><label>Contraseña<input "
        "name=password type=password maxlength=32 autocomplete=current-password "
        "required autofocus></label><button>Entrar</button>"
        "<p class=hint>Acceso inicial: admin / admin</p></form></html>");
}

static esp_err_t page_handler(httpd_req_t *request)
{
    if (!request_authenticated(request)) {
        return login_page_handler(request);
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
        "<!doctype html><html lang=es><meta name=viewport "
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
        "margin-top:4px}input[type=text],input[type=password],input[type=number]"
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
        ".hint{color:#b9c8d8}.warn{color:#ffd166}@media(prefers-reduced-motion:"
        "reduce){.detector.motion .dot{animation:none}}</style><body>"
        "<h1>WiFi Motion C3</h1>"
        "<section id=detector class='detector wait' role=status aria-live=polite>"
        "<span class=dot aria-hidden=true></span><div><strong id=motion_label>"
        "Esperando datos</strong><div id=motion_detail class=detail>"
        "El detector está iniciándose.</div></div></section>"
        "<section class=chart><h2>Actividad en tiempo real</h2>"
        "<div class=legend><span class=key><i class=swatch></i>Score de cambio"
        "</span><span class=key><i class='swatch threshold'></i>Umbral"
        "</span><span class=key><i class='swatch hit'></i>Detección</span></div>"
        "<div class=chart-wrap><canvas id=motion_chart role=img aria-label="
        "'Gráfica temporal del score, umbral y detecciones'></canvas></div>"
        "<p id=chart_note class=chart-note>Esperando muestras…</p></section>"
        "<section class=card><h2>Calibración sin presencia</h2>"
        "<p>El detector esperará el tiempo indicado para que puedas salir. "
        "Después buscará de nuevo las redes y aprenderá el ambiente vacío.</p>"
        "<div class=calibration-row><label>Tiempo para salir (segundos)"
        "<input id=calibration_delay type=number min=5 max=300 value=20 "
        "inputmode=numeric></label><button id=calibrate class=calibrate "
        "type=button>Salir y calibrar</button></div>"
        "<p id=calibration_status class=hint role=status aria-live=polite>"
        "No hay ninguna calibración programada.</p></section>"
        "<form class=card method=post action=/save><h2>Configuración</h2>"
        "<input id=csrf type=hidden name=csrf value=%s>"
        "<label class=mode><input type=radio name=mode value=auto %s> "
        "<b>Automático</b> — usa las mejores redes visibles</label>"
        "<label class=mode><input id=manual type=radio name=mode value=manual %s> "
        "<b>Elegir redes</b> — selecciona hasta 8</label>"
        "<h2>Buscar redes</h2><div class=toolbar>"
        "<button id=scan type=button>Buscar redes</button>"
        "<span id=status aria-live=polite></span></div>"
        "<div id=results></div>"
        "<h2>Redes elegidas <span id=count></span></h2>"
        "<div id=chosen></div><div id=inputs></div>"
        "<p class=hint>Pulsa <b>+ Añadir</b> en cada red de referencia. "
        "No hace falta conectarse a ellas ni conocer su clave.</p>"
        "<h2>Acceso a esta configuración</h2>"
        "<label>Usuario<input type=text name=admin_user maxlength=%u "
        "required value=\"%s\"></label>"
        "<label>Nueva contraseña<input type=password name=admin_pass "
        "minlength=4 maxlength=%u placeholder='Dejar vacía para conservar'>"
        "</label><p class=warn>El valor inicial admin/admin es débil; "
        "conviene cambiarlo.</p><button class=primary>Guardar y revisar</button>"
        "</form><script>let networks=%s,selected=%s;"
        "const R=document.querySelector('#results'),C=document.querySelector("
        "'#chosen'),I=document.querySelector('#inputs'),N=document.querySelector("
        "'#count'),S=document.querySelector('#status'),D=document.querySelector("
        "'#detector'),L=document.querySelector('#motion_label'),M=document."
        "querySelector('#motion_detail'),V=document.querySelector('#motion_chart'),"
        "G=document.querySelector('#chart_note'),CB=document.querySelector("
        "'#calibrate'),CD=document.querySelector('#calibration_delay'),CS="
        "document.querySelector('#calibration_status');"
        "const HISTORY=120000,points=[];let lastScan=0,lastMotion=false,hits=0;"
        "function stateView(d){if(!d.live)return['Sin conexión con el detector',"
        "'bad'];if(d.state==='MOTION')return['MOVIMIENTO DETECTADO','motion'];"
        "if(d.state==='IDLE')return['Sin movimiento','idle'];if(d.state==="
        "'CALIBRATING'||d.state==='WARMUP')return['Calibrando','wait'];return"
        "['Sin cobertura suficiente','bad']}"
        "function renderStatus(d){const v=stateView(d),motion=d.state==='MOTION'"
        "&&d.live;D.className='detector '+v[1];L.textContent=v[0];M.textContent="
        "`Score ${(d.score_x100/100).toFixed(2)} · Umbral ${(d.trigger_x100/100"
        ").toFixed(2)} · Cobertura ${(d.coverage_permille/10).toFixed(0)}"
        "${String.fromCharCode(37)} · ${d.observed_references}/${d.reference_count}"
        " referencias${d.state==='IDLE'&&d.trigger_count?` · Confirmación "
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
        "detecciones desde que abriste la página`:'Esperando muestras…'}"
        "function renderCalibration(d){if(d.calibration_state==='scheduled'){CS."
        "textContent=`Sal ahora: la calibración comienza en "
        "${d.calibration_remaining_seconds} s.`;CB.disabled=false}else if(d."
        "calibration_state==='running'){CS.textContent=`Calibrando ambiente vacío"
        ": ${d.calibration_completed_scans}/${d.calibration_target_scans} "
        "escaneos.`;CB.disabled=true}else{CS.textContent='No hay ninguna "
        "calibración programada.';CB.disabled=false}}"
        "async function poll(){try{const r=await fetch('/status',{cache:'no-store'"
        "});if(!r.ok)throw Error();const d=await r.json();renderStatus(d);"
        "renderCalibration(d)}catch(e){D."
        "className='detector bad';L.textContent='Sin conexión';M.textContent="
        "'No se reciben datos del dispositivo.'}setTimeout(poll,750)}"
        "addEventListener('resize',drawChart);"
        "function row(name,detail,action,label,klass){const d=document.createElement("
        "'div'),s=document.createElement('span'),b=document.createElement('b'),"
        "m=document.createElement('small'),x=document.createElement('button');"
        "d.className='net';b.textContent=name;m.textContent=detail;s.append(b,m);"
        "x.type='button';x.className=klass;x.textContent=label;x.onclick=action;"
        "d.append(s,x);return d}"
        "function draw(){R.replaceChildren();if(!networks.length){const e=document."
        "createElement('p');e.className='empty';e.textContent='Pulsa Buscar redes "
        "para actualizar la lista.';R.append(e)}networks.forEach(n=>{const added="
        "selected.includes(n.ssid),detail=`Canal ${n.channel} · ${n.rssi} dBm · "
        "${n.aps} AP`;const x=row(n.ssid,detail,()=>add(n.ssid),added?'Añadida':"
        "'+ Añadir','add');x.lastChild.disabled=added;R.append(x)});C.replaceChildren"
        "();I.replaceChildren();N.textContent=`(${selected.length}/8)`;"
        "if(!selected.length){const e=document.createElement('p');e.className="
        "'empty';e.textContent='Todavía no has añadido ninguna red.';C.append(e)}"
        "selected.forEach((name,i)=>{C.append(row(name,'Red de referencia',()=>"
        "{selected.splice(i,1);draw()},'Quitar','remove'));const h=document."
        "createElement('input');h.type='hidden';h.name='ssid'+i;h.value=name;"
        "I.append(h)})}"
        "function add(name){if(selected.includes(name))return;if(selected.length"
        ">=8){alert('Máximo 8 redes');return}selected.push(name);document."
        "querySelector('#manual').checked=true;draw()}"
        "document.querySelector('#scan').onclick=async function(){this.disabled="
        "true;S.textContent='Buscando…';try{const r=await fetch('/networks',{"
        "method:'POST',headers:{'Content-Type':'application/x-www-form-urlencoded'"
        "},body:'csrf='+encodeURIComponent(document.querySelector('#csrf').value)"
        "});if(!r.ok)throw Error();networks=await r.json();S.textContent=networks."
        "length?`${networks.length} redes encontradas`:'No se encontraron redes';"
        "draw()}catch(e){S.textContent='No se pudo buscar. Inténtalo otra vez.'}"
        "finally{this.disabled=false}};draw();poll();"
        "CB.onclick=async function(){const delay=Number(CD.value);if(!Number."
        "isInteger(delay)||delay<5||delay>300){CS.textContent='Indica entre 5 y "
        "300 segundos.';return}this.disabled=true;CS.textContent='Programando…';"
        "try{const r=await fetch('/calibrate',{method:'POST',headers:{"
        "'Content-Type':'application/x-www-form-urlencoded'},body:'csrf='+"
        "encodeURIComponent(document.querySelector('#csrf').value)+'&delay_seconds"
        "='+delay});if(!r.ok)throw Error();renderCalibration(await r.json())}"
        "catch(e){CS.textContent='No se pudo programar la calibración.';this."
        "disabled=false}};"
        "</script></body></html>",
        csrf_token,
        manual ? "" : "checked",
        manual ? "checked" : "",
        (unsigned)PROBE_CONFIG_ADMIN_USER_MAX_LENGTH,
        escaped_user,
        (unsigned)PROBE_CONFIG_ADMIN_PASSWORD_MAX_LENGTH,
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
    httpd_resp_set_hdr(request, "Location", "/");
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
    probe_config_blob_t config;
    probe_config_set_defaults(&config);
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
    if (probe_config_save(&config) != PROBE_CONFIG_OK ||
        reference_store_erase() != REFERENCE_STORE_OK) {
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
        "<h1>Configuración guardada</h1>"
        "<p>Al continuar, el dispositivo reiniciará y calibrará las redes "
        "elegidas. La red <b>Motion-C3-Setup</b> volverá a aparecer y esta web "
        "mostrará la detección y la gráfica en directo.</p>"
        "<form method=post action=/apply><input type=hidden name=csrf value=",
        HTTPD_RESP_USE_STRLEN);
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
    if (network_mutex == NULL) {
        atomic_store(&portal_active, false);
        return ESP_ERR_NO_MEM;
    }
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
    http_config.max_uri_handlers = 10U;
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
