#pragma once

#include "esp_err.h"
#include "esp_wifi_types.h"
#include "esp_netif.h"
#include "config_storage.h"
#include "dns_server.h"

#define WIFI_SCAN_MAX_AP 20

typedef struct {
    bool sta_connected;
    int8_t sta_rssi;
    uint32_t sta_ip;
    uint8_t ap_client_count;
    char sta_ssid[CFG_SSID_LEN];
} wifi_status_t;

esp_err_t wifi_manager_init(repeater_config_t *config);
esp_err_t wifi_manager_start(void);
esp_err_t wifi_manager_reconfigure(repeater_config_t *config);
esp_err_t wifi_manager_scan(wifi_ap_record_t *ap_records, uint16_t *ap_count);
void wifi_manager_get_status(wifi_status_t *status);
void wifi_manager_set_dns_handle(dns_server_handle_t handle);

typedef struct {
    bool success;
    uint32_t elapsed_ms;
    uint32_t addr;
} ping_result_t;

esp_err_t wifi_manager_ping(const char *target, ping_result_t *result);
