#pragma once

#include "esp_err.h"
#include <stdbool.h>

#define CFG_SSID_LEN 33
#define CFG_PASS_LEN 65
#define CFG_USER_LEN 33
#define CFG_EAP_ID_LEN 65
#define CFG_PORT_FWD_MAX 5

typedef struct {
    bool     enabled;
    uint8_t  proto;       // 0=TCP, 1=UDP
    uint16_t ext_port;
    uint32_t int_ip;      // destination IP (network byte order)
    uint16_t int_port;
} port_fwd_rule_t;

typedef struct {
    char sta_ssid[CFG_SSID_LEN];
    char sta_pass[CFG_PASS_LEN];
    char ap_ssid[CFG_SSID_LEN];
    char ap_pass[CFG_PASS_LEN];
    uint8_t ap_channel;
    uint8_t ap_max_conn;
    char web_user[CFG_USER_LEN];
    char web_pass[CFG_PASS_LEN];
    // WPA2-Enterprise (EAP-PEAP/TTLS)
    bool sta_eap_enabled;
    char sta_eap_identity[CFG_EAP_ID_LEN];
    char sta_eap_username[CFG_USER_LEN];
    char sta_eap_password[CFG_PASS_LEN];
    // Port forwarding
    port_fwd_rule_t port_fwd[CFG_PORT_FWD_MAX];
} repeater_config_t;

esp_err_t config_storage_init(void);
esp_err_t config_storage_load(repeater_config_t *config);
esp_err_t config_storage_save(const repeater_config_t *config);
esp_err_t config_storage_erase(void);
void config_storage_set_defaults(repeater_config_t *config);
