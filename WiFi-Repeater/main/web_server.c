#include <string.h>
#include <stdio.h>
#include "web_server.h"
#include "wifi_manager.h"
#include "config_storage.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi_ap_get_sta_list.h"
#include "esp_ota_ops.h"
#include "esp_app_format.h"
#include "lwip/inet.h"
#include "mbedtls/base64.h"

static const char *TAG = "web_server";
static httpd_handle_t s_server = NULL;
static repeater_config_t *s_config = NULL;

extern const uint8_t index_html_start[] asm("_binary_index_html_start");
extern const uint8_t index_html_end[]   asm("_binary_index_html_end");
extern const uint8_t styles_css_start[] asm("_binary_styles_css_start");
extern const uint8_t styles_css_end[]   asm("_binary_styles_css_end");
extern const uint8_t app_js_start[]     asm("_binary_app_js_start");
extern const uint8_t app_js_end[]       asm("_binary_app_js_end");

// --- Utility: escape a string for JSON ---
static int json_escape(char *dst, size_t dst_size, const char *src)
{
    size_t j = 0;
    for (size_t i = 0; src[i] && j < dst_size - 1; i++) {
        if (src[i] == '"' || src[i] == '\\') {
            if (j + 2 >= dst_size) break;
            dst[j++] = '\\';
        }
        dst[j++] = src[i];
    }
    dst[j] = '\0';
    return (int)j;
}

// --- Basic Auth helper ---
static bool require_auth(httpd_req_t *req)
{
    char auth_hdr[256];
    if (httpd_req_get_hdr_value_str(req, "Authorization", auth_hdr, sizeof(auth_hdr)) != ESP_OK) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32 Repeater\"");
        httpd_resp_sendstr(req, "{\"error\":\"Authentication required\"}");
        return false;
    }

    if (strncmp(auth_hdr, "Basic ", 6) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid auth method\"}");
        return false;
    }

    unsigned char decoded[128];
    size_t decoded_len = 0;
    if (mbedtls_base64_decode(decoded, sizeof(decoded) - 1, &decoded_len,
                               (const unsigned char *)(auth_hdr + 6),
                               strlen(auth_hdr + 6)) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid credentials\"}");
        return false;
    }
    decoded[decoded_len] = '\0';

    char *colon = strchr((char *)decoded, ':');
    if (!colon) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid credentials\"}");
        return false;
    }
    *colon = '\0';
    const char *user = (const char *)decoded;
    const char *pass = colon + 1;

    if (strcmp(user, s_config->web_user) != 0 || strcmp(pass, s_config->web_pass) != 0) {
        httpd_resp_set_status(req, "401 Unauthorized");
        httpd_resp_set_hdr(req, "WWW-Authenticate", "Basic realm=\"ESP32 Repeater\"");
        httpd_resp_sendstr(req, "{\"error\":\"Invalid credentials\"}");
        return false;
    }

    return true;
}

// --- Static file handlers ---

static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, (const char *)index_html_start,
                    index_html_end - index_html_start);
    return ESP_OK;
}

static esp_err_t css_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/css");
    httpd_resp_send(req, (const char *)styles_css_start,
                    styles_css_end - styles_css_start);
    return ESP_OK;
}

static esp_err_t js_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "application/javascript");
    httpd_resp_send(req, (const char *)app_js_start,
                    app_js_end - app_js_start);
    return ESP_OK;
}

// --- API handlers ---

static esp_err_t api_status_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    wifi_status_t status;
    wifi_manager_get_status(&status);

    char ip_str[16];
    esp_ip4_addr_t ip = { .addr = status.sta_ip };
    snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));

    esp_netif_ip_info_t ap_ip_info;
    esp_netif_get_ip_info(esp_netif_get_handle_from_ifkey("WIFI_AP_DEF"), &ap_ip_info);
    char ap_ip_str[16];
    snprintf(ap_ip_str, sizeof(ap_ip_str), IPSTR, IP2STR(&ap_ip_info.ip));

    uint32_t free_heap = esp_get_free_heap_size();
    uint32_t uptime_sec = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS / 1000);

    char esc_ssid[68];
    json_escape(esc_ssid, sizeof(esc_ssid), status.sta_ssid);

    char buf[320];
    snprintf(buf, sizeof(buf),
        "{\"sta_connected\":%s,\"sta_ssid\":\"%s\",\"sta_rssi\":%d,"
        "\"sta_ip\":\"%s\",\"ap_clients\":%d,\"ap_ip\":\"%s\","
        "\"free_heap\":%lu,\"uptime\":%lu}",
        status.sta_connected ? "true" : "false",
        esc_ssid, status.sta_rssi, ip_str,
        status.ap_client_count, ap_ip_str,
        (unsigned long)free_heap, (unsigned long)uptime_sec);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t api_scan_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    wifi_ap_record_t ap_records[WIFI_SCAN_MAX_AP];
    uint16_t ap_count = 0;

    esp_err_t ret = wifi_manager_scan(ap_records, &ap_count);
    if (ret != ESP_OK) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Scan failed");
        return ESP_FAIL;
    }

    // Build JSON array manually. Each entry ~100 bytes, max 20 entries
    char *buf = malloc(ap_count * 120 + 16);
    if (!buf) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int pos = 0;
    pos += snprintf(buf + pos, 2, "[");
    for (int i = 0; i < ap_count; i++) {
        char esc_ssid[68];
        json_escape(esc_ssid, sizeof(esc_ssid), (const char *)ap_records[i].ssid);
        pos += snprintf(buf + pos, 120,
            "%s{\"ssid\":\"%s\",\"rssi\":%d,\"channel\":%d,\"auth\":%d}",
            i > 0 ? "," : "",
            esc_ssid, ap_records[i].rssi, ap_records[i].primary, ap_records[i].authmode);
    }
    pos += snprintf(buf + pos, 2, "]");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    free(buf);
    return ESP_OK;
}

static esp_err_t api_config_get_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    char esc_sta_ssid[68], esc_ap_ssid[68];
    json_escape(esc_sta_ssid, sizeof(esc_sta_ssid), s_config->sta_ssid);
    json_escape(esc_ap_ssid, sizeof(esc_ap_ssid), s_config->ap_ssid);

    char buf[256];
    snprintf(buf, sizeof(buf),
        "{\"sta_ssid\":\"%s\",\"sta_pass\":\"\","
        "\"ap_ssid\":\"%s\",\"ap_pass\":\"\","
        "\"ap_channel\":%d,\"ap_max_conn\":%d}",
        esc_sta_ssid, esc_ap_ssid,
        s_config->ap_channel, s_config->ap_max_conn);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

// Simple JSON string value extractor (no full parser needed for our small payloads)
static bool json_get_string(const char *json, const char *key, char *out, size_t out_size)
{
    char search[48];
    snprintf(search, sizeof(search), "\"%s\":\"", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    const char *end = strchr(start, '"');
    if (!end) return false;
    size_t len = end - start;
    if (len >= out_size) len = out_size - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return true;
}

static bool json_get_int(const char *json, const char *key, int *out)
{
    char search[48];
    snprintf(search, sizeof(search), "\"%s\":", key);
    const char *start = strstr(json, search);
    if (!start) return false;
    start += strlen(search);
    *out = atoi(start);
    return true;
}

static esp_err_t api_config_post_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    char buf[512];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char val[65];
    int ival;

    if (json_get_string(buf, "sta_ssid", val, sizeof(val))) {
        strlcpy(s_config->sta_ssid, val, sizeof(s_config->sta_ssid));
    }
    if (json_get_string(buf, "sta_pass", val, sizeof(val)) && strlen(val) > 0) {
        strlcpy(s_config->sta_pass, val, sizeof(s_config->sta_pass));
    }
    if (json_get_string(buf, "ap_ssid", val, sizeof(val)) && strlen(val) > 0) {
        strlcpy(s_config->ap_ssid, val, sizeof(s_config->ap_ssid));
    }
    if (json_get_string(buf, "ap_pass", val, sizeof(val)) && strlen(val) > 0) {
        strlcpy(s_config->ap_pass, val, sizeof(s_config->ap_pass));
    }
    if (json_get_int(buf, "ap_channel", &ival)) {
        s_config->ap_channel = (uint8_t)ival;
    }
    if (json_get_int(buf, "ap_max_conn", &ival)) {
        s_config->ap_max_conn = (uint8_t)ival;
    }

    config_storage_save(s_config);
    wifi_manager_reconfigure(s_config);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Config saved. Reconnecting...\"}");
    return ESP_OK;
}

static esp_err_t api_clients_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    wifi_sta_list_t sta_list;
    wifi_sta_mac_ip_list_t ip_list;

    esp_err_t ret = esp_wifi_ap_get_sta_list(&sta_list);
    if (ret != ESP_OK) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "[]");
        return ESP_OK;
    }

    esp_wifi_ap_get_sta_list_with_ip(&sta_list, &ip_list);

    // Each client entry ~80 bytes, max 10 clients
    char buf[1024];
    int pos = 0;
    pos += snprintf(buf + pos, sizeof(buf) - pos, "[");
    for (int i = 0; i < ip_list.num; i++) {
        char mac_str[18];
        snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(ip_list.sta[i].mac));
        char ip_str[16];
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_list.sta[i].ip));
        pos += snprintf(buf + pos, sizeof(buf) - pos,
            "%s{\"mac\":\"%s\",\"ip\":\"%s\"}",
            i > 0 ? "," : "", mac_str, ip_str);
    }
    snprintf(buf + pos, sizeof(buf) - pos, "]");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, buf);
    return ESP_OK;
}

static esp_err_t api_ping_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    char buf[128];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char target[64];
    if (!json_get_string(buf, "target", target, sizeof(target)) || strlen(target) == 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing target");
        return ESP_FAIL;
    }

    ping_result_t result;
    esp_err_t ret = wifi_manager_ping(target, &result);

    char resp[256];
    if (ret == ESP_OK && result.success) {
        char ip_str[16];
        esp_ip4_addr_t ip = { .addr = result.addr };
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip));
        snprintf(resp, sizeof(resp),
            "{\"success\":true,\"ip\":\"%s\",\"time_ms\":%lu,\"target\":\"%s\"}",
            ip_str, (unsigned long)result.elapsed_ms, target);
    } else {
        const char *reason = "timeout";
        if (ret == ESP_ERR_NOT_FOUND) reason = "dns_failed";
        snprintf(resp, sizeof(resp),
            "{\"success\":false,\"reason\":\"%s\",\"target\":\"%s\"}",
            reason, target);
    }

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, resp);
    return ESP_OK;
}

static esp_err_t api_restart_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Restarting...\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_auth_change_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    char buf[256];
    int received = httpd_req_recv(req, buf, sizeof(buf) - 1);
    if (received <= 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "No data");
        return ESP_FAIL;
    }
    buf[received] = '\0';

    char new_user[CFG_USER_LEN];
    char new_pass[CFG_PASS_LEN];

    if (!json_get_string(buf, "new_user", new_user, sizeof(new_user)) ||
        !json_get_string(buf, "new_pass", new_pass, sizeof(new_pass))) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Missing new_user or new_pass");
        return ESP_FAIL;
    }

    if (strlen(new_user) == 0 || strlen(new_pass) < 4) {
        httpd_resp_set_type(req, "application/json");
        httpd_resp_sendstr(req, "{\"status\":\"error\",\"message\":\"User required, password min 4 chars\"}");
        return ESP_OK;
    }

    strlcpy(s_config->web_user, new_user, sizeof(s_config->web_user));
    strlcpy(s_config->web_pass, new_pass, sizeof(s_config->web_pass));
    config_storage_save(s_config);

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Credentials updated\"}");
    return ESP_OK;
}

static esp_err_t api_auth_check_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"authenticated\":true}");
    return ESP_OK;
}

static esp_err_t api_ota_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
    if (!update_partition) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA: writing to partition '%s' at offset 0x%lx, size %lu",
             update_partition->label, (unsigned long)update_partition->address,
             (unsigned long)update_partition->size);

    esp_ota_handle_t ota_handle;
    esp_err_t ret = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA begin failed");
        return ESP_FAIL;
    }

    char *buf = malloc(4096);
    if (!buf) {
        esp_ota_abort(ota_handle);
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OOM");
        return ESP_FAIL;
    }

    int total_received = 0;
    int content_len = req->content_len;
    ESP_LOGI(TAG, "OTA: receiving %d bytes", content_len);

    while (total_received < content_len) {
        int received = httpd_req_recv(req, buf, 4096);
        if (received <= 0) {
            if (received == HTTPD_SOCK_ERR_TIMEOUT) continue;
            ESP_LOGE(TAG, "OTA: receive error at %d/%d", total_received, content_len);
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Receive failed");
            return ESP_FAIL;
        }

        ret = esp_ota_write(ota_handle, buf, received);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "OTA: write failed: %s", esp_err_to_name(ret));
            free(buf);
            esp_ota_abort(ota_handle);
            httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA write failed");
            return ESP_FAIL;
        }

        total_received += received;
    }

    free(buf);
    ESP_LOGI(TAG, "OTA: received %d bytes total", total_received);

    ret = esp_ota_end(ota_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA: validation failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
        return ESP_FAIL;
    }

    ret = esp_ota_set_boot_partition(update_partition);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "OTA: set boot partition failed: %s", esp_err_to_name(ret));
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Set boot partition failed");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "OTA: success, rebooting...");
    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"OTA successful, rebooting...\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t api_factory_reset_handler(httpd_req_t *req)
{
    if (!require_auth(req)) return ESP_OK;

    ESP_LOGW(TAG, "Factory reset requested!");
    config_storage_erase();

    httpd_resp_set_type(req, "application/json");
    httpd_resp_sendstr(req, "{\"status\":\"ok\",\"message\":\"Factory reset done, rebooting...\"}");

    vTaskDelay(pdMS_TO_TICKS(500));
    esp_restart();
    return ESP_OK;
}

static esp_err_t captive_redirect_handler(httpd_req_t *req)
{
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "http://192.168.4.1/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

esp_err_t web_server_start(repeater_config_t *config)
{
    s_config = config;

    httpd_config_t http_config = HTTPD_DEFAULT_CONFIG();
    http_config.max_uri_handlers = 16;
    http_config.uri_match_fn = httpd_uri_match_wildcard;
    http_config.lru_purge_enable = true;
    http_config.stack_size = 8192;
    http_config.recv_wait_timeout = 30;

    esp_err_t ret = httpd_start(&s_server, &http_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(ret));
        return ret;
    }

    httpd_uri_t uri_index    = { .uri = "/",            .method = HTTP_GET,  .handler = index_handler };
    httpd_uri_t uri_css      = { .uri = "/styles.css",  .method = HTTP_GET,  .handler = css_handler };
    httpd_uri_t uri_js       = { .uri = "/app.js",      .method = HTTP_GET,  .handler = js_handler };
    httpd_uri_t uri_status   = { .uri = "/api/status",  .method = HTTP_GET,  .handler = api_status_handler };
    httpd_uri_t uri_scan     = { .uri = "/api/scan",    .method = HTTP_GET,  .handler = api_scan_handler };
    httpd_uri_t uri_cfg_get  = { .uri = "/api/config",  .method = HTTP_GET,  .handler = api_config_get_handler };
    httpd_uri_t uri_cfg_post = { .uri = "/api/config",  .method = HTTP_POST, .handler = api_config_post_handler };
    httpd_uri_t uri_clients  = { .uri = "/api/clients", .method = HTTP_GET,  .handler = api_clients_handler };
    httpd_uri_t uri_ping     = { .uri = "/api/ping",        .method = HTTP_POST, .handler = api_ping_handler };
    httpd_uri_t uri_restart  = { .uri = "/api/restart",     .method = HTTP_POST, .handler = api_restart_handler };
    httpd_uri_t uri_auth_chg = { .uri = "/api/auth/change", .method = HTTP_POST, .handler = api_auth_change_handler };
    httpd_uri_t uri_auth_chk = { .uri = "/api/auth/check",  .method = HTTP_GET,  .handler = api_auth_check_handler };
    httpd_uri_t uri_ota      = { .uri = "/api/ota",           .method = HTTP_POST, .handler = api_ota_handler };
    httpd_uri_t uri_freset   = { .uri = "/api/factory-reset", .method = HTTP_POST, .handler = api_factory_reset_handler };
    httpd_uri_t uri_catchall = { .uri = "/*",               .method = HTTP_GET,  .handler = captive_redirect_handler };

    httpd_register_uri_handler(s_server, &uri_index);
    httpd_register_uri_handler(s_server, &uri_css);
    httpd_register_uri_handler(s_server, &uri_js);
    httpd_register_uri_handler(s_server, &uri_status);
    httpd_register_uri_handler(s_server, &uri_scan);
    httpd_register_uri_handler(s_server, &uri_cfg_get);
    httpd_register_uri_handler(s_server, &uri_cfg_post);
    httpd_register_uri_handler(s_server, &uri_clients);
    httpd_register_uri_handler(s_server, &uri_ping);
    httpd_register_uri_handler(s_server, &uri_restart);
    httpd_register_uri_handler(s_server, &uri_auth_chg);
    httpd_register_uri_handler(s_server, &uri_auth_chk);
    httpd_register_uri_handler(s_server, &uri_ota);
    httpd_register_uri_handler(s_server, &uri_freset);
    httpd_register_uri_handler(s_server, &uri_catchall);

    ESP_LOGI(TAG, "HTTP server started on port %d", http_config.server_port);
    return ESP_OK;
}

void web_server_stop(void)
{
    if (s_server) {
        httpd_stop(s_server);
        s_server = NULL;
        ESP_LOGI(TAG, "HTTP server stopped");
    }
}
