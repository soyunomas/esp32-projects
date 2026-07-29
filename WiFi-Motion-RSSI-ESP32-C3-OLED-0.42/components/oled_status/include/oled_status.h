#pragma once

#include <stdbool.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t oled_status_init(void);
bool oled_status_available(void);
void oled_status_show_boot(void);
void oled_status_show_network(const char *ip_address,
                              bool recovery_mode,
                              const char *network_name);
void oled_status_show_sample(const char *ip_address,
                             bool sample_ok,
                             int rssi_dbm,
                             bool calibrated,
                             bool motion,
                             float rssi_score,
                             float csi_score);

#ifdef __cplusplus
}
#endif
