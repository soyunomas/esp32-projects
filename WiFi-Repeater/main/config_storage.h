#pragma once

#include "esp_err.h"

#define CFG_SSID_LEN 33
#define CFG_PASS_LEN 65

typedef struct {
    char sta_ssid[CFG_SSID_LEN];
    char sta_pass[CFG_PASS_LEN];
    char ap_ssid[CFG_SSID_LEN];
    char ap_pass[CFG_PASS_LEN];
    uint8_t ap_channel;
    uint8_t ap_max_conn;
} repeater_config_t;

esp_err_t config_storage_init(void);
esp_err_t config_storage_load(repeater_config_t *config);
esp_err_t config_storage_save(const repeater_config_t *config);
esp_err_t config_storage_erase(void);
void config_storage_set_defaults(repeater_config_t *config);
