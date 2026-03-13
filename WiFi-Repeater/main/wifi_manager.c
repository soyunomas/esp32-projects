#include <string.h>
#include "wifi_manager.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_mac.h"
#include "lwip/lwip_napt.h"
#include "lwip/inet.h"
#include "lwip/dns.h"
#include "lwip/netdb.h"
#include "ping/ping_sock.h"

static const char *TAG = "wifi_manager";

#define DHCPS_OFFER_DNS 0x02

static esp_netif_t *s_sta_netif = NULL;
static esp_netif_t *s_ap_netif = NULL;
static repeater_config_t *s_config = NULL;
static wifi_status_t s_status = {0};
static bool s_wifi_started = false;
static bool s_napt_enabled = false;
static SemaphoreHandle_t s_scan_done = NULL;
static dns_server_handle_t s_dns_handle = NULL;

void wifi_manager_set_dns_handle(dns_server_handle_t handle)
{
    s_dns_handle = handle;
}

static void set_dhcps_dns(void)
{
    // Copy the upstream DNS server from STA to the AP DHCP server
    // so that AP clients can resolve DNS through the repeater
    esp_netif_dns_info_t dns;
    esp_err_t ret = esp_netif_get_dns_info(s_sta_netif, ESP_NETIF_DNS_MAIN, &dns);
    if (ret != ESP_OK) {
        ESP_LOGW(TAG, "Failed to get STA DNS info");
        return;
    }

    char dns_str[16];
    snprintf(dns_str, sizeof(dns_str), IPSTR, IP2STR(&dns.ip.u_addr.ip4));
    ESP_LOGI(TAG, "Propagating upstream DNS to AP DHCP: %s", dns_str);

    uint8_t dhcps_offer_option = DHCPS_OFFER_DNS;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_stop(s_ap_netif));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_option(s_ap_netif, ESP_NETIF_OP_SET,
        ESP_NETIF_DOMAIN_NAME_SERVER, &dhcps_offer_option, sizeof(dhcps_offer_option)));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_dns_info(s_ap_netif, ESP_NETIF_DNS_MAIN, &dns));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcps_start(s_ap_netif));
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
        case WIFI_EVENT_STA_START:
            if (strlen(s_config->sta_ssid) > 0) {
                ESP_LOGI(TAG, "STA started, connecting to '%s'...", s_config->sta_ssid);
                esp_wifi_connect();
            }
            break;
        case WIFI_EVENT_STA_DISCONNECTED: {
            wifi_event_sta_disconnected_t *event = (wifi_event_sta_disconnected_t *)event_data;
            ESP_LOGW(TAG, "STA disconnected (reason=%d), reconnecting...", event->reason);
            s_status.sta_connected = false;
            s_status.sta_rssi = 0;
            s_status.sta_ip = 0;

            // Disable NAPT when STA disconnects
            if (s_napt_enabled) {
                esp_netif_napt_disable(s_ap_netif);
                s_napt_enabled = false;
                ESP_LOGI(TAG, "NAPT disabled (STA disconnected)");
            }

            // Restart captive portal DNS so users can configure
            if (s_dns_handle == NULL) {
                dns_server_config_t dns_config = DNS_SERVER_CONFIG_SINGLE("*", "WIFI_AP_DEF");
                s_dns_handle = start_dns_server(&dns_config);
                ESP_LOGI(TAG, "Captive portal DNS restarted");
            }

            vTaskDelay(pdMS_TO_TICKS(2000));
            esp_wifi_connect();
            break;
        }
        case WIFI_EVENT_AP_STACONNECTED: {
            wifi_event_ap_staconnected_t *ev = (wifi_event_ap_staconnected_t *)event_data;
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(ev->mac));
            ESP_LOGI(TAG, "AP: station %s joined (AID=%d)", mac_str, ev->aid);
            s_status.ap_client_count++;
            break;
        }
        case WIFI_EVENT_AP_STADISCONNECTED: {
            wifi_event_ap_stadisconnected_t *ev = (wifi_event_ap_stadisconnected_t *)event_data;
            char mac_str[18];
            snprintf(mac_str, sizeof(mac_str), MACSTR, MAC2STR(ev->mac));
            ESP_LOGI(TAG, "AP: station %s left (AID=%d)", mac_str, ev->aid);
            if (s_status.ap_client_count > 0) s_status.ap_client_count--;
            break;
        }
        case WIFI_EVENT_SCAN_DONE:
            if (s_scan_done) xSemaphoreGive(s_scan_done);
            break;
        default:
            break;
        }
    } else if (event_base == IP_EVENT) {
        if (event_id == IP_EVENT_STA_GOT_IP) {
            ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
            s_status.sta_connected = true;
            s_status.sta_ip = event->ip_info.ip.addr;
            char ip_str[16];
            snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&event->ip_info.ip));
            ESP_LOGI(TAG, "STA got IP: %s", ip_str);

            // Stop captive portal DNS — let real DNS work through upstream
            if (s_dns_handle) {
                stop_dns_server(s_dns_handle);
                s_dns_handle = NULL;
                ESP_LOGI(TAG, "Captive portal DNS stopped (STA connected)");
            }

            // Propagate upstream DNS to AP DHCP server
            set_dhcps_dns();

            // Set STA as default netif for routing
            esp_netif_set_default_netif(s_sta_netif);

            // Enable NAPT on the AP netif so AP clients' traffic routes through STA
            if (!s_napt_enabled) {
                if (esp_netif_napt_enable(s_ap_netif) == ESP_OK) {
                    s_napt_enabled = true;
                    ESP_LOGI(TAG, "NAPT enabled on AP interface");
                } else {
                    ESP_LOGE(TAG, "Failed to enable NAPT");
                }
            }
        }
    }
}

esp_err_t wifi_manager_init(repeater_config_t *config)
{
    s_config = config;
    s_scan_done = xSemaphoreCreateBinary();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    s_sta_netif = esp_netif_create_default_wifi_sta();
    s_ap_netif = esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                         &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                         &wifi_event_handler, NULL, NULL));

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));

    return ESP_OK;
}

static void configure_sta(void)
{
    wifi_config_t sta_cfg = {0};
    strlcpy((char *)sta_cfg.sta.ssid, s_config->sta_ssid, sizeof(sta_cfg.sta.ssid));
    strlcpy((char *)sta_cfg.sta.password, s_config->sta_pass, sizeof(sta_cfg.sta.password));
    sta_cfg.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_cfg.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
    strlcpy(s_status.sta_ssid, s_config->sta_ssid, sizeof(s_status.sta_ssid));
}

static void configure_ap(void)
{
    wifi_config_t ap_cfg = {0};
    strlcpy((char *)ap_cfg.ap.ssid, s_config->ap_ssid, sizeof(ap_cfg.ap.ssid));
    ap_cfg.ap.ssid_len = strlen(s_config->ap_ssid);
    strlcpy((char *)ap_cfg.ap.password, s_config->ap_pass, sizeof(ap_cfg.ap.password));
    ap_cfg.ap.channel = s_config->ap_channel;
    ap_cfg.ap.max_connection = s_config->ap_max_conn;
    ap_cfg.ap.authmode = strlen(s_config->ap_pass) > 0 ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
}

esp_err_t wifi_manager_start(void)
{
    configure_sta();
    configure_ap();

    ESP_ERROR_CHECK(esp_wifi_start());
    s_wifi_started = true;

    ESP_LOGI(TAG, "WiFi started: AP='%s' STA='%s'", s_config->ap_ssid, s_config->sta_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_reconfigure(repeater_config_t *config)
{
    s_config = config;
    s_status.sta_connected = false;
    s_status.sta_rssi = 0;
    s_status.sta_ip = 0;

    // Disable NAPT before reconfiguring
    if (s_napt_enabled) {
        esp_netif_napt_disable(s_ap_netif);
        s_napt_enabled = false;
    }

    if (s_wifi_started) {
        esp_wifi_disconnect();
        configure_sta();
        configure_ap();
        if (strlen(s_config->sta_ssid) > 0) {
            esp_wifi_connect();
        }
    }

    ESP_LOGI(TAG, "WiFi reconfigured: AP='%s' STA='%s'", s_config->ap_ssid, s_config->sta_ssid);
    return ESP_OK;
}

esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t *ap_count)
{
    wifi_scan_config_t scan_cfg = {
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300,
    };

    esp_err_t ret = esp_wifi_scan_start(&scan_cfg, false);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        return ret;
    }

    if (xSemaphoreTake(s_scan_done, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "Scan timeout");
        esp_wifi_scan_stop();
        return ESP_ERR_TIMEOUT;
    }

    *ap_count = WIFI_SCAN_MAX_AP;
    ESP_ERROR_CHECK(esp_wifi_scan_get_ap_records(ap_count, ap_records));
    ESP_LOGI(TAG, "Scan found %d APs", *ap_count);
    return ESP_OK;
}

void wifi_manager_get_status(wifi_status_t *status)
{
    if (s_status.sta_connected) {
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            s_status.sta_rssi = ap_info.rssi;
        }
    }
    memcpy(status, &s_status, sizeof(wifi_status_t));
}

// --- Ping implementation ---

static SemaphoreHandle_t s_ping_done = NULL;
static ping_result_t s_ping_result;

static void ping_success_cb(esp_ping_handle_t hdl, void *args)
{
    uint32_t elapsed;
    esp_ping_get_profile(hdl, ESP_PING_PROF_TIMEGAP, &elapsed, sizeof(elapsed));
    ip_addr_t target_addr;
    esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));

    s_ping_result.success = true;
    s_ping_result.elapsed_ms = elapsed;
    s_ping_result.addr = target_addr.u_addr.ip4.addr;
}

static void ping_timeout_cb(esp_ping_handle_t hdl, void *args)
{
    s_ping_result.success = false;
}

static void ping_end_cb(esp_ping_handle_t hdl, void *args)
{
    if (s_ping_done) xSemaphoreGive(s_ping_done);
}

esp_err_t wifi_manager_ping(const char *target, ping_result_t *result)
{
    if (!s_ping_done) {
        s_ping_done = xSemaphoreCreateBinary();
    }

    memset(&s_ping_result, 0, sizeof(s_ping_result));

    // Resolve hostname
    struct addrinfo hint = { .ai_family = AF_INET };
    struct addrinfo *res = NULL;
    if (getaddrinfo(target, NULL, &hint, &res) != 0 || res == NULL) {
        ESP_LOGE(TAG, "DNS lookup failed for '%s'", target);
        result->success = false;
        return ESP_ERR_NOT_FOUND;
    }

    struct in_addr addr = ((struct sockaddr_in *)res->ai_addr)->sin_addr;
    freeaddrinfo(res);

    ip_addr_t target_addr;
    target_addr.type = IPADDR_TYPE_V4;
    target_addr.u_addr.ip4.addr = addr.s_addr;

    esp_ping_config_t ping_config = ESP_PING_DEFAULT_CONFIG();
    ping_config.target_addr = target_addr;
    ping_config.count = 3;
    ping_config.interval_ms = 500;
    ping_config.timeout_ms = 2000;
    ping_config.task_stack_size = 4096;

    esp_ping_callbacks_t cbs = {
        .on_ping_success = ping_success_cb,
        .on_ping_timeout = ping_timeout_cb,
        .on_ping_end = ping_end_cb,
    };

    esp_ping_handle_t hdl;
    esp_err_t ret = esp_ping_new_session(&ping_config, &cbs, &hdl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Ping session creation failed");
        result->success = false;
        return ret;
    }

    esp_ping_start(hdl);

    // Wait for ping to finish (3 pings * 2.5s max each = 7.5s worst case)
    if (xSemaphoreTake(s_ping_done, pdMS_TO_TICKS(10000)) != pdTRUE) {
        ESP_LOGE(TAG, "Ping timeout");
        esp_ping_stop(hdl);
        esp_ping_delete_session(hdl);
        result->success = false;
        return ESP_ERR_TIMEOUT;
    }

    esp_ping_stop(hdl);
    esp_ping_delete_session(hdl);

    memcpy(result, &s_ping_result, sizeof(ping_result_t));
    return ESP_OK;
}
